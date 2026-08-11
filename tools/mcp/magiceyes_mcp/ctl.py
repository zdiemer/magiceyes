"""Client for the engine's debug control channel (host/engine/ctl.{c,h}).

Protocol: newline-delimited JSON, one object per line. A response carrying bulk bytes announces
"bin": N in its header line and follows the newline with exactly N raw bytes.

The engine binds an ephemeral port (ME_CTL=0) and writes it to ME_CTL_PORTFILE, so sessions never
collide on a fixed port and nothing has to be reserved in advance.
"""
from __future__ import annotations

import json
import socket
import time
from pathlib import Path


class CtlError(RuntimeError):
    pass


class Ctl:
    def __init__(self, port: int, timeout: float = 10.0):
        self.port = port
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        self.f = self.sock.makefile("rb")
        self._id = 0

    @classmethod
    def from_portfile(cls, path: Path, wait: float = 15.0, timeout: float = 10.0) -> "Ctl":
        """Wait for the engine to publish its port, then connect."""
        t0 = time.time()
        while time.time() - t0 < wait:
            try:
                txt = Path(path).read_text().strip()
                if txt:
                    return cls(int(txt), timeout=timeout)
            except (OSError, ValueError):
                pass
            time.sleep(0.1)
        raise CtlError(
            f"engine never published a control port at {path}. It may have exited during load, "
            f"or this engine build predates the control channel (rebuild with "
            f"host/engine/build_engine.sh).")

    def call(self, cmd: str, **kw):
        """Send one command; return (header_dict, payload_bytes)."""
        self._id += 1
        req = dict(cmd=cmd, id=self._id, **kw)
        try:
            self.sock.sendall((json.dumps(req) + "\n").encode())
            line = self.f.readline()
        except OSError as e:
            raise CtlError(f"control channel I/O failed ({e}); the engine may have exited") from e
        if not line:
            raise CtlError("control channel closed by the engine (it probably exited)")
        try:
            hdr = json.loads(line)
        except ValueError as e:
            raise CtlError(f"malformed response: {line[:200]!r}") from e
        blob = b""
        n = hdr.get("bin") or 0
        if n:
            blob = self.f.read(n)
            if blob is None or len(blob) != n:
                raise CtlError(f"short binary payload: wanted {n}, got {len(blob or b'')}")
        return hdr, blob

    def ok(self, cmd: str, **kw) -> dict:
        """Call and raise on an error response, so tools surface the engine's own message."""
        hdr, _ = self.call(cmd, **kw)
        if not hdr.get("ok", False):
            raise CtlError(f"{cmd}: {hdr.get('err', 'failed')}"
                           + (f" -- {hdr['detail']}" if hdr.get("detail") else ""))
        return hdr

    def close(self):
        try:
            self.f.close()
            self.sock.close()
        except OSError:
            pass


def hexdump(data: bytes, base: int = 0, width: int = 16, limit: int = 512) -> list[str]:
    """Classic hex+ASCII dump. Reading raw bytes is most of what memory inspection is."""
    out = []
    for off in range(0, min(len(data), limit), width):
        chunk = data[off:off + width]
        hx = " ".join(f"{b:02x}" for b in chunk)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        out.append(f"{base + off:08x}  {hx:<{width * 3}} |{asc}|")
    if len(data) > limit:
        out.append(f"... {len(data) - limit} more bytes")
    return out


PERM_NAMES = {0: "---", 1: "r--", 2: "-w-", 3: "rw-", 4: "--x", 5: "r-x", 6: "-wx", 7: "rwx"}


def label_region(addr: int) -> str:
    """Name well-known parts of the guest address space, from the engine's layout constants."""
    if addr == 0xffff0000:
        return "kuser helper page (get_tls/cmpxchg)"
    if addr == 0x72000000:
        return "shm framebuffer alias (SHMFB_BASE)"
    if addr == 0x71000000:
        return "interpreter / ld.so (INTERP_BASE)"
    if 0x40000000 <= addr < 0x70000000:
        return "mmap arena (shared libs, anon)"
    if 0x78000000 <= addr < 0x80000000:
        return "stack"
    if addr < 0x02000000:
        return "main binary (.text/.data/heap)"
    return ""
