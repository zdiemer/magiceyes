/* magiceyes -- canonical GP2X button bitmap -> a device's own pad word. See padmap.h. */
#include "padmap.h"
#include "gp2xshm.h"

uint32_t wiz_button_word(uint32_t b) {
    uint32_t w = 0;
    if (b & 0x0006) w |= 1u << 16;                    /* LEFT | UPLEFT */
    if (b & 0x0008) w |= (1u << 16) | (1u << 19);     /* DOWNLEFT */
    if (b & 0x0040) w |= 1u << 17;                    /* RIGHT */
    if (b & 0x0080) w |= (1u << 17) | (1u << 18);     /* UPRIGHT */
    if (b & 0x0020) w |= (1u << 17) | (1u << 19);     /* DOWNRIGHT */
    if (b & 0x0003) w |= 1u << 18;                    /* UP | UPLEFT */
    if (b & 0x0010) w |= 1u << 19;                    /* DOWN */
    if (b & (1u << GP2X_A))       w |= 1u << 20;
    if (b & (1u << GP2X_B))       w |= 1u << 21;
    if (b & (1u << GP2X_X))       w |= 1u << 22;
    if (b & (1u << GP2X_Y))       w |= 1u << 23;
    if (b & (1u << GP2X_SELECT))  w |= 1u << 8;
    if (b & (1u << GP2X_START))   w |= 1u << 9;       /* Wiz MENU */
    if (b & (1u << GP2X_L))       w |= 1u << 7;
    if (b & (1u << GP2X_R))       w |= 1u << 6;
    if (b & (1u << GP2X_VOLUP))   w |= 1u << 10;
    if (b & (1u << GP2X_VOLDOWN)) w |= 1u << 11;
    if (b & (1u << GP2X_CLICK))   w |= 1u << 27;
    return w;
}
