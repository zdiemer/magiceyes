/* magiceyes -- canonical GP2X button bitmap -> a device's own pad word.
 *
 * The viewer always writes the canonical GP2X order from gp2xshm.h; each device's hardware
 * reports something else, and the guest reads the hardware layout. Keeping the mapping here rather
 * than inside devices.c means it can be tested directly: it is pure bit arithmetic, but it lived
 * in a file that makes 41 uc_* calls against live guest state.
 *
 * No engine dependencies (only gp2xshm.h for the canonical bit numbers). */
#ifndef MAGICEYES_PADMAP_H
#define MAGICEYES_PADMAP_H

#include <stdint.h>

/* Wiz hardware button word (the Pollux pad layout, ACTIVE-HIGH pressed bits). Bit positions
   recovered from the unstripped fxi runtime (wizJoystickRead); shared by the MMIO GPIOB/C pad
   model and the Wiz /dev/GPIO read. */
uint32_t wiz_button_word(uint32_t b);

#endif /* MAGICEYES_PADMAP_H */
