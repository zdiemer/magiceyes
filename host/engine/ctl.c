/* Control-channel transport: loopback listener, NDJSON framing, dispatch. Handlers live in
 * ctl_cmd.c. See ctl.h for the protocol and the threading contract.
 *
 * The whole module compiles to nothing on the Windows bundle unless ME_DEV is defined: a shipping
 * consumer binary must not open a listening socket (firewall prompts on a -mwindows app with no
 * console, AV heuristics, and 127.0.0.1 is not a security boundary on Windows). bin/me_unicorn and
 * bin/magiceyes-dev.exe get it; bin/magiceyes.exe does not. */
#include "engine.h"

#if defined(ME_BUNDLED) && !defined(ME_DEV)
void ctl_init(void) {}          /* release bundle: no control channel, by design */
void ctl_shutdown(void) {}
#else

#ifdef _WIN32
/* winsock2.h must precede anything that drags in windows.h. */
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define CLOSESOCK(s) closesocket(s)
#define SOCKERR()    WSAGetLastError()
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define CLOSESOCK(s) close(s)
#define SOCKERR()    errno
#endif

#include "ctl.h"
#include "ctl_json.h"

/* ctl_cmd.c */
int ctl_dispatch(const struct jp *req, struct jw *resp, const uint8_t **bin, size_t *binlen,
                 void **binown, const uint8_t *payload, size_t paylen);

#define CTL_MAXLINE (64 * 1024)

static SOCKET g_lsock = INVALID_SOCKET;
static int g_ctl_running = 0;
static char g_token[128];
static volatile int g_nconn = 0;   /* live connections; the last one leaving releases a pause */

static int send_all(SOCKET s, const void *buf, size_t n) {
    const char *p = buf;
    while (n) {
        int w = (int)send(s, p, (int)n, 0);
        if (w <= 0) return -1;
        p += w; n -= (size_t)w;
    }
    return 0;
}

/* One line of JSON, then (optionally) the raw payload it announced. */
static int send_msg(SOCKET s, struct jw *w, const uint8_t *bin, size_t binlen) {
    jw_raw(w, "\n");
    if (w->err || send_all(s, w->buf, w->len) < 0) return -1;
    if (bin && binlen && send_all(s, bin, binlen) < 0) return -1;
    return 0;
}

static void send_err(SOCKET s, const char *err, const char *detail) {
    struct jw w; jw_init(&w);
    jw_raw(&w, "{");
    jw_kv_bool(&w, "ok", 0);
    jw_kv_str(&w, "err", err);
    if (detail) jw_kv_str(&w, "detail", detail);
    jw_raw(&w, "}");
    send_msg(s, &w, NULL, 0);
    jw_free(&w);
}

static void *conn_thread(void *arg) {
    SOCKET s = (SOCKET)(intptr_t)arg;
    char *line = malloc(CTL_MAXLINE);
    size_t len = 0;
    int authed = (g_token[0] == 0);

    if (!line) { CLOSESOCK(s); return NULL; }

    for (;;) {
        char c;
        int r = (int)recv(s, &c, 1, 0);
        if (r <= 0) break;
        if (c == '\n') {
            if (len == 0) continue;
            struct jp req;
            if (jp_parse(&req, line, len) != 0) {
                send_err(s, "bad_json", "expected one flat JSON object per line");
                len = 0;
                continue;
            }
            const char *cmd = jp_get(&req, "cmd");
            if (!cmd) {
                send_err(s, "no_cmd", "every request needs a \"cmd\" key");
                len = 0;
                continue;
            }
            if (!authed) {
                const char *tok = jp_get(&req, "token");
                if (strcmp(cmd, "hello") != 0 || !tok || strcmp(tok, g_token) != 0) {
                    send_err(s, "auth", "send {\"cmd\":\"hello\",\"token\":\"...\"} first");
                    len = 0;
                    continue;
                }
                authed = 1;
            }

            /* A request may carry a binary payload too (mem.write): "bin":N in the header line,
               then exactly N raw bytes after the newline -- symmetric with responses. */
            uint8_t *payload = NULL; size_t paylen = 0;
            long long want = jp_int(&req, "bin", 0);
            if (want > 0 && want <= 16 * 1024 * 1024) {
                payload = malloc((size_t)want);
                size_t got = 0;
                while (payload && got < (size_t)want) {
                    int k = (int)recv(s, (char *)payload + got, (int)((size_t)want - got), 0);
                    if (k <= 0) break;
                    got += (size_t)k;
                }
                if (!payload || got != (size_t)want) {
                    free(payload); payload = NULL;
                    send_err(s, "short_payload", "declared bin bytes never arrived");
                    len = 0;
                    continue;
                }
                paylen = (size_t)want;
            }

            struct jw resp; jw_init(&resp);
            const uint8_t *bin = NULL; size_t binlen = 0; void *binown = NULL;
            int rc = ctl_dispatch(&req, &resp, &bin, &binlen, &binown, payload, paylen);
            if (rc != 0 && resp.len == 0) {
                send_err(s, "unknown_cmd", cmd);
            } else if (send_msg(s, &resp, bin, binlen) < 0) {
                jw_free(&resp); free(binown); free(payload);
                break;
            }
            jw_free(&resp);
            free(binown);
            free(payload);
            len = 0;
            continue;
        }
        if (len + 1 < CTL_MAXLINE) line[len++] = c;
        else len = 0;   /* overlong line: drop it rather than growing without bound */
    }
    free(line);
    CLOSESOCK(s);
    /* Safety valve: a debugger that crashes or is killed mid-pause would otherwise leave every
       guest thread parked forever. When the last client goes away, release the world. */
    if (--g_nconn <= 0) {
        g_nconn = 0;
        if (dbg_is_paused()) {
            fprintf(DIAG, "[ctl] last client disconnected while paused -- resuming\n");
            dbg_force_resume();
        }
    }
    return NULL;
}

static void *accept_thread(void *arg) {
    (void)arg;
    while (g_ctl_running) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof ca;
        SOCKET c = accept(g_lsock, (struct sockaddr *)&ca, &cl);
        if (c == INVALID_SOCKET) {
            if (!g_ctl_running) break;
            me_usleep(20000);
            continue;
        }
        pthread_t th;
        g_nconn++;
        if (pthread_create(&th, NULL, conn_thread, (void *)(intptr_t)c) == 0)
            pthread_detach(th);
        else { g_nconn--; CLOSESOCK(c); }
    }
    return NULL;
}

void ctl_init(void) {
    const char *p = getenv("ME_CTL");
    if (!p || !*p || g_ctl_running) return;

#ifdef _WIN32
    { WSADATA wsa; if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(DIAG, "[ctl] WSAStartup failed\n"); return; } }
#endif

    int port = atoi(p);
    g_lsock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_lsock == INVALID_SOCKET) { fprintf(DIAG, "[ctl] socket() failed\n"); return; }

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* never configurable: loopback only */
    a.sin_port = htons((unsigned short)port);
    /* No SO_REUSEADDR: a port collision should fail loudly rather than silently stealing it. */
    if (bind(g_lsock, (struct sockaddr *)&a, sizeof a) != 0 || listen(g_lsock, 4) != 0) {
        fprintf(DIAG, "[ctl] bind/listen on 127.0.0.1:%d failed (err %d)\n", port, SOCKERR());
        CLOSESOCK(g_lsock);
        g_lsock = INVALID_SOCKET;
        return;
    }
    socklen_t al = sizeof a;
    if (getsockname(g_lsock, (struct sockaddr *)&a, &al) == 0) port = ntohs(a.sin_port);

    const char *tok = getenv("ME_CTL_TOKEN");
    snprintf(g_token, sizeof g_token, "%s", tok ? tok : "");

    const char *pf = getenv("ME_CTL_PORTFILE");
    if (pf && *pf) {   /* the only way to discover an ephemeral port from the console-less bundle */
        FILE *f = fopen(pf, "w");
        if (f) { fprintf(f, "%d\n", port); fclose(f); }
    }
    fprintf(DIAG, "[ctl] listening on 127.0.0.1:%d%s\n", port, g_token[0] ? " (token required)" : "");

    g_ctl_running = 1;
    pthread_t th;
    if (pthread_create(&th, NULL, accept_thread, NULL) == 0) pthread_detach(th);
    else { g_ctl_running = 0; CLOSESOCK(g_lsock); g_lsock = INVALID_SOCKET; }
}

void ctl_shutdown(void) {
    g_ctl_running = 0;
    if (g_lsock != INVALID_SOCKET) { CLOSESOCK(g_lsock); g_lsock = INVALID_SOCKET; }
}

#endif /* release-bundle guard */
