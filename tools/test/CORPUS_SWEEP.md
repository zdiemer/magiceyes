# magiceyes compatibility sweep

Every GP2X / Wiz / Caanoo title on the corpus share, booted headlessly through the native engine (`bin/me_unicorn`) and scored from the shm framebuffer + the structured run report. Regenerate with `tools/test/compat_report.py`.


## Summary

| Platform | Titles | Playable | Ingame | Black | Incompatible | Crashed |
|---|--:|--:|--:|--:|--:|--:|
| GP2X | 673 | 282 | 105 | 80 | 206 | 0 |
| Wiz | 153 | 28 | 8 | 23 | 94 | 0 |
| Caanoo | 205 | 21 | 19 | 17 | 148 | 0 |
| **All** | **1031** | **331** | **132** | **120** | **448** | **0** |

### What the tiers mean

| Tier | Meaning |
|---|---|
| `playable` | Held ≥25 fps with audio, and the picture survived the visual checks |
| `ingame` | Renders gameplay with a notable gap: slow, silent, a flat fill, or a picture that is visibly wrong |
| `black` | Frames advanced, but every sampled frame was black |
| `incompatible` | Never rendered: died in the loader/ld.so, or no frame at all |
| `crashed` | Host fault after booting (engine exit 70) |

`playable` and `ingame` are the reported grades. The harness's own tier (which only knows frame rate, non-black and audio) is kept per title as `status`, and `baseline.py` still gates on that.

## Failure groups (ranked by titles blocked)

One fix at the top of this table unblocks the whole row.

| Failure group | Titles | Platforms | Most common specifics |
|---|--:|---|---|
| **Never rendered a frame (cause unknown)** (`no-frames`) | 127 | Caanoo, GP2X, Wiz | n/a |
| **Boots but renders only black** (`black-screen`) | 119 | Caanoo, GP2X, Wiz | n/a |
| **Game data files are missing from the dump** (`missing-game-data`) | 105 | Caanoo, GP2X, Wiz | n/a |
| **Not a 32-bit ARM ELF** (`not-arm-elf`) | 99 | Caanoo, GP2X, Wiz | n/a |
| **Renders at speed but no audio** (`no-audio`) | 76 | Caanoo, GP2X, Wiz | n/a |
| **No .gpe in the dump** (`no-executable`) | 59 | Caanoo, GP2X, Wiz | n/a |
| **Spins forever polling an MMSP2 register** (`mmio-spin`) | 26 | Caanoo, GP2X, Wiz | `0x90a` ×23, `0x808` ×1, `0x4000` ×1, `0x1988` ×1 |
| **Renders but below 25 fps** (`low-fps`) | 26 | Caanoo, GP2X | n/a |
| **Unknown /dev node** (`unknown-device`) | 25 | Caanoo, GP2X, Wiz | `/dev/input/mouse/0` ×14, `/dev/null` ×4, `/dev/` ×2, `/dev/input/mouse0` ×2 |
| **Draws only a flat colour** (`flat-fill`) | 22 | Caanoo, GP2X | n/a |
| **Renders, but the picture is wrong** (`garbled-visuals`) | 7 | Caanoo, GP2X, Wiz | n/a |
| **Archive extraction failed** (`archive-failed`) | 5 | Caanoo, GP2X | n/a |
| **Unimplemented syscall** (`unimplemented-syscall`) | 3 | Caanoo, GP2X | `281 (socket)` ×1, `113` ×1, `117` ×1 |
| **Could not open a display** (`display-init-failed`) | 1 | GP2X | n/a |

## Renders, but the picture is wrong

These 7 titles pass the running checks (frames advancing, frame rate, audio) while the frame itself is visibly broken, so they are graded `ingame` rather than `playable`. The reasons come from measuring the captured frame: a consistent per-row offset means a stride/pitch mismatch, large-scale repetition means the screen holds more than one copy of itself, and noise far above what dithered artwork reaches means corrupt memory.

| Title | Platform | fps | What the frame looks like |
|---|---|--:|---|
| nuclearchess | Caanoo | 3944.5 | renders at 26x26 instead of 320x240 |
| 1945_GP2X_0.2b | GP2X | 58.7 | pixel-to-pixel noise of 96, far above what dithered artwork reaches; the frame looks like corrupt memory |
| BunnyTraps-v11 | GP2X | 61.8 | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like corrupt memory |
| GF | GP2X | 60.8 | top and bottom halves are near-identical |
| Life.0.1 | GP2X | 61.9 | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like corrupt memory |
| MoveSweep2X | GP2X | 49.9 | the screen holds a second copy of itself, offset by 96px; left and right halves are near-identical |
| Worship Vector | Wiz | 60.7 | the screen holds a second copy of itself, offset by 160px; left and right halves are near-identical |

## Scored as working, but only painting a flat colour

These 22 titles advanced frames, kept audio running, and held frame rate, so they land in `playable`/`renders`. Their framebuffer never held more than one or two colours, which means the tier overstates them. Worth treating as broken.

| Title | Platform | Status | fps |
|---|---|---|--:|
| Arcadevol3 | Caanoo | `renders` | 59.2 |
| gnp_104 | Caanoo | `playable` | 45.9 |
| knight | Caanoo | `renders` | 10.5 |
| noiz2sa_caanoo | Caanoo | `renders` | 16.7 |
| rg_ura_103 | Caanoo | `playable` | 53.4 |
| _-The Reversed Preacher 3-_Hack bIld_ | GP2X | `playable` | 50.8 |
| _-the reversed preacher II-_ | GP2X | `playable` | 51.3 |
| ASCIIPong2xV0.4 | GP2X | `playable` | 39.3 |
| ConnyCarrot | GP2X | `playable` | 60.5 |
| dumbbell2x-01 | GP2X | `renders` | 60.9 |
| HumphreyGP2X | GP2X | `playable` | 60.5 |
| kampfimall-gp2x | GP2X | `renders` | 61.1 |
| kampfimall-gp2x-music | GP2X | `playable` | 59.7 |
| Knight Lore | GP2X | `renders` | 10.5 |
| las-tres-luces-de-glaurung-remake | GP2X | `playable` | 60.3 |
| levelEdit | GP2X | `renders` | 61.4 |
| mk13.gpe | GP2X | `renders` | 0.0 |
| monacoGP | GP2X | `renders` | 0.0 |
| Pond2X | GP2X | `renders` | 62.6 |
| robot-escape | GP2X | `playable` | 87.8 |
| StairwayToHeaven | GP2X | `playable` | 37.0 |
| the reversed preacher II | GP2X | `playable` | 49.0 |

## Cross-title blockers


### Unimplemented syscalls

| Item | Titles |
|---|--:|
| `163 (mremap)` | 21 |
| `43 (times)` | 8 |
| `97 (setpriority)` | 5 |
| `281 (socket)` | 1 |
| `282 (bind)` | 1 |
| `284 (listen)` | 1 |
| `285 (accept)` | 1 |
| `294 (setsockopt)` | 1 |
| `113` | 1 |
| `117` | 1 |
| `150 (mlock)` | 1 |

### Missing dynamic symbols

| Item | Titles |
|---|--:|
| `Unable to Load Image: Failed loading libpng.so.3: /lib/libpng.so.3: undefined s` | 4 |
| `Failed loading libpng.so.3: /lib/libpng.so.3: undefined symbol: inflateReset>Ju` | 1 |
| `Failed loading libpng.so.3: /lib/libpng.so.3: undefined symbol: inflateResetcar` | 1 |
| `Failed loading libpng.so.3: /lib/libpng.so.3: undefined symbol: inflateReseterr` | 1 |
| `storage::Surfaces:  Failed loading libpng.so.3: /lib/libpng.so.3: undefined sym` | 1 |
| `LoadImage -> Could not load image: Failed loading libpng.so.3: /lib/libpng.so.3` | 1 |

### Unknown /dev nodes

| Item | Titles |
|---|--:|
| `/dev/input/mouse/0` | 208 |
| `/dev/usbmouse` | 181 |
| `/dev/psaux` | 180 |
| `/dev/null` | 122 |
| `/dev/touchscreen/wm97xx` | 85 |
| `/dev/sequencer` | 22 |
| `/dev/input/mouse0` | 20 |
| `/dev/accel` | 12 |
| `/dev/input/mice` | 4 |
| `/dev/mouse` | 4 |
| `/dev/gpmdata` | 3 |
| `/dev/batt` | 3 |
| `/dev/` | 2 |
| `/dev/pts/` | 2 |
| `/dev/input/mouse` | 2 |
| `/dev/cx25874` | 1 |
| `/dev/graphics/fb0` | 1 |
| `/dev/ptmx` | 1 |
| `/dev/ptyp0` | 1 |
| `/dev/pollux_batt` | 1 |
| `/dev/mmsp2adc` | 1 |
| `/dev/adbmouse` | 1 |

### Quirks (ran, but not fully honoured)

| Item | Titles |
|---|--:|
| `unknown_mmio:0x90a` | 410 |
| `unknown_ioctl:fb` | 171 |
| `unknown_mmio:0x4058` | 147 |
| `unknown_mmio:0x405c` | 142 |
| `unknown_mmio:0x4060` | 142 |
| `unknown_mmio:0x910` | 104 |
| `unknown_mmio:0x924` | 48 |
| `unknown_mmio:0x3b46` | 46 |
| `unknown_mmio:0x91c` | 46 |
| `unknown_mmio:0x3802` | 21 |
| `unknown_mmio:0x3804` | 21 |
| `unknown_mmio:0x4070` | 16 |
| `unknown_mmio:0x808` | 12 |
| `unknown_mmio:0xf16` | 12 |
| `unknown_mmio:0xf58` | 12 |
| `unsupported_blit:dst-unmapped` | 11 |
| `unknown_mmio:0xfde00910` | 11 |
| `unknown_mmio:0xfde0091c` | 11 |
| `unknown_mmio:0xfde00924` | 11 |
| `unknown_mmio:0x1988` | 10 |
| `unknown_mmio:0x19c0` | 10 |
| `unknown_mmio:0x19c4` | 10 |
| `unknown_mmio:0xfffe2880` | 9 |
| `unknown_mmio:0xfffe2906` | 9 |
| `unknown_mmio:0xfffe2908` | 9 |

## Per-title results


### GP2X (673 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| 2xHexen2 v0.05 PB2 | `incompatible` | 1.4 | 1 | – | no-frames |  |
| 2xHexen2_cheat_patch | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/2xHexen2_cheat_patch' |
| 2xquake003 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| 2xquake2 | `incompatible` | 0.0 | 0 | ✓ | missing-game-data |  |
| 2XRally01 | `incompatible` | 0.0 | 0 | – | display-init-failed |  |
| 2xZdoom_PB1.2 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| 4WE_GP2x | `incompatible` | 0.1 | 1 | ✓ | mmio-spin | 0x90a |
| A1GP2XV1_1 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/ |
| abduction | `incompatible` | 0.0 | 0 | – | no-frames |  |
| abe | `incompatible` | 60.8 | 1534 | ✓ | no-frames |  |
| abuse_1.0 | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/ |
| airpong4GP2X0.0.4 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/airpong4GP2X0.0.4/airpong022/src/AirPong.gpe' is not an  |
| albion-v1.0.1-gp2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse0 |
| Alex's Falldown | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| AlienZ | `incompatible` | 0.0 | 0 | – | no-frames |  |
| animatch_v1.2 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/animatch_v1.2' |
| animatch_v1.2.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/animatch_v1.2.zip' (exit 32512) |
| AnotherGame2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/AnotherGame2x/AnotherGame2x/anothergame2x.gpe' is not an |
| atris-1.0.7 | `incompatible` | 59.8 | 1555 | ✓ | no-frames |  |
| B'lox! | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| balluz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/balluz/balluz/balluz.gpe' is not an ARM ELF and no runna |
| battlejewels-gp2x-062-100 | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x808 |
| beat2x-05 source | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-05 source' |
| beat2x-pack-C64 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-C64' |
| beat2x-pack-ccMixter | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-ccMixter' |
| beat2x-pack-magnatune-electronica | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-magnatune-electronica' |
| beat2x-pack-miniMaximum | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-miniMaximum' |
| beat2x-pack-tutorial | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-tutorial' |
| beat2x-pack-urban | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-urban' |
| BermudaS_gp2x | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| Blix2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| blockoid | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| blocksGP2X-0 | `incompatible` | 0.0 | 0 | – | unimplemented-syscall | 113 |
| Bombs Panic | `incompatible` | 0.5 | 1 | ✓ | no-frames |  |
| Boomshine2x_(java) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Boomshine2x_(java)/Boomshine2x/Boomshine2x.gpe' is not a |
| bunkermaster2x04 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Butterfly | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Butterfly' |
| cackb2 | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| CaptainCrusader_GP2XDemo | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| CaptainCrusader_GP2XFull | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| cdogs2x04 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Chess2xSkins | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Chess2xSkins' |
| chicken-puyopuyo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Chroma | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| Classical | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Classical' |
| CloneKeen2X-1.0a | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Codemaster | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| crocodingusgp2x | `incompatible` | 0.1 | 1 | ✓ | no-frames |  |
| d1x-rebirth-gp2x_v0.50a | `incompatible` | 0.0 | 0 | – | unimplemented-syscall | 117 |
| DangerMouse | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| DeathChase4GP2X-V0.1b | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/DeathChase4GP2X-V0.1b/deathchase3d-0.9/deathchase3d/Deat |
| default | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/default' |
| dkbk2x-0.1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| doom | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/doom/doom/10sector.gpe' is not an ARM ELF and no runnabl |
| doom_mod_examples | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/doom_mod_examples/game/interpreters/doom/pwad1/prboom_gm |
| DoomPwadPack | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/DoomPwadPack/AliensTC.gpe' is not an ARM ELF and no runn |
| dosfsck-gp2x-2.11 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/dosfsck-gp2x-2.11' |
| Dr. Mates v1.0 | `incompatible` | 42.7 | 99 | ✓ | no-frames |  |
| duckmaze-gp2x-0.1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/duckmaze-gp2x-0.1/duckmaze-gp2x-0.1/duckmaze.gpe' is not |
| duke2x004 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| duke3d_cheat_patch | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/duke3d_cheat_patch' |
| E-Fighters2x_FIRST_ALPHA_0_0_5_fixedSound | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| EasterQuest | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| exultb4-src | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/exultb4-src' |
| FFDoom | `incompatible` | 2.0 | 1 | – | no-frames |  |
| FindMii | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| Fire | `incompatible` | 0.0 | 0 | – | no-frames |  |
| FleshChasmer Zero (English Patch) | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/FleshChasmer Zero (English Patch)' |
| FlipIR_GP2X | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| Football2X | `incompatible` | 0.1 | 1 | ✓ | no-frames |  |
| Fore_1_0 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Fore_1_0' |
| FP_Default_2.0 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/FP_Default_2.0' |
| freedroid2x06 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/freedroid2x06/Freedroid/FreeDroid.gpe' is not an ARM ELF |
| frotz | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/frotz' |
| FullBoard (test ver.) | `incompatible` | 52.0 | 108 | ✓ | no-frames |  |
| garden2x02 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| GeneralPromise | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| geoQuiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/geoQuiz/geoQuiz.gpe' is not an ARM ELF and no runnable b |
| glouton | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| gnurobbo_0.66_open2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| gp2x-abrick-0.1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/gp2x-abrick-0.1' |
| gp2x-rogue-v1.0 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| gp2x-tenmado-0.1 | `incompatible` | 0.0 | 1 | – | no-frames |  |
| gp2x-tong-v1 | `incompatible` | 0.0 | 1 | – | no-frames |  |
| gp2xbug | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| gp2xlib | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/gp2xlib' |
| gp2xninjas-v06 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/gp2xninjas-v06/Ninjas v0.6 Final GP2X/ninjas.gpe' is not |
| GPQuakeDistributable3 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/GPQuakeDistributable3/GPQuakeDistributable3/jzspq2.gpe'  |
| GPQuakeModsDistributable1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/GPQuakeModsDistributable1/alk12.gpe' is not an ARM ELF a |
| GPQuakeModsDistributable2 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/GPQuakeModsDistributable2/flesh.gpe' is not an ARM ELF a |
| gravityforce2x04 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| guesstimator | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/guesstimator' |
| Gweled-Tilematch-Theme | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Gweled-Tilematch-Theme' |
| HamstersEscape (Gp2x F-100 F-200) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/HamstersEscape (Gp2x F-100 F-200)/HamstersEscape (Gp2x F |
| Heretic MOD pack1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Heretic MOD pack1/game/interpreters/heretic/pwad1/Hereti |
| heroes2x02 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/heroes2x02/Heroes/Heroes.gpe' is not an ARM ELF and no r |
| Hexen2X_v0.5 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| hexen_mods1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/hexen_mods1/game/interpreters/hexen/DeathKings.gpe' is n |
| hexen_mods2 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/hexen_mods2/game/interpreters/hexen/pwad2/Hexen2X_gmenu2 |
| HigherOrLower-GP2X-v011 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| jump_n_blob_gp2x | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Klaur | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| kobo_deluxe_beta1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| KQ2X_v3 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Laser2xVers10 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Lexeme | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| Liquid Counter.gp2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Liquid Counter.gp2x/LiquidCount/LiquidCount.gpe' is not  |
| Logoball | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| Lottys_Lines.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/Lottys_Lines.zip' (exit 32512) |
| lumix-beta-01 | `incompatible` | 73.8 | 1857 | – | no-frames |  |
| mariodm | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/mariodm' |
| memory | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| Midnight2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Midnight2x/dosbox/midnight/midnight.gpe' is not an ARM E |
| misterhachi | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/misterhachi/misterhachi/misterhachi.gpe' is not an ARM E |
| mopesnake-gp2x-0.5 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/mopesnake-gp2x-0.5/mopesnake-gp2x-0.5/mopesnake.gpe' is  |
| MouthTrap | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| mueppv32 | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| nethack-caduhack.r01 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| nethack06 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| noiz2saV3 | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Odonata | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Odonata' |
| ohthehumanity-1.0.0 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/ohthehumanity-1.0.0/ohthehumanity/ohthehumanity.gpe' is  |
| onscripter2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| OpenTTD | `incompatible` | 0.0 | 0 | – | no-frames |  |
| opposite_lock | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/opposite_lock/opposite_lock/opposite_lock.gpe' is not an |
| ozgur-hanoi-0.2-kelebek | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/ozgur-hanoi-0.2-kelebek' |
| PantaVsDragon (Gp2x F-100 F-200) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/PantaVsDragon (Gp2x F-100 F-200)/PantaVsDragon (Gp2x F-1 |
| para | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/para' |
| Payback | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Payback_v1_1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Payback_v1_1' |
| PaybackMusicManager | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/PaybackMusicManager' |
| pc | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Pentominos | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| Phantomas1.8X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Pipes2_0 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Pipes2_0/Pipes/Pipes.gpe' is not an ARM ELF and no runna |
| Pipes_v2.1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Pipes_v2.1/Pipes/Pipes.gpe' is not an ARM ELF and no run |
| Poker_Gp2Xv1.0 | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| PrBoom PWAD pack | `incompatible` | 0.0 | 0 | – | no-frames |  |
| puckman_gp2x | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| pykaraoke-0.6-gp2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| pySlide | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/pySlide/pySlide/pySlide.gpe' is not an ARM ELF and no ru |
| pyTetris | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/pyTetris/pyTetris/pyTetris.gpe' is not an ARM ELF and no |
| Quad | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| Quake Mods 5 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Quake Mods 5/czg07.gpe' is not an ARM ELF and no runnabl |
| Quake Mods 6 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Quake Mods 6/pcrr.gpe' is not an ARM ELF and no runnable |
| quake2x-wii | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| QuakeMapAbandon | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/QuakeMapAbandon/abandon.gpe' is not an ARM ELF and no ru |
| QuakeMapPack4 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/QuakeMapPack4/alba.gpe' is not an ARM ELF and no runnabl |
| QuakeMods7 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/QuakeMods7/shrak.gpe' is not an ARM ELF and no runnable  |
| ranchr | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/ranchr/ranchr.gpe' is not an ARM ELF and no runnable bin |
| REminiscence-GP2X-v0.4-public | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/cx25874 |
| retrovirusRTS_gp2x_demo1_1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/retrovirusRTS_gp2x_demo1_1/retrovirusRTS/retrovirusRTS.g |
| reword_v0.2_French_Pack | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/reword_v0.2_French_Pack' |
| roadsmash | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/roadsmash/road.gpe' is not an ARM ELF and no runnable bi |
| rott-v0.2 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| rRootage_v1.0 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| rubik | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Sachunsung2_1 | `incompatible` | 51.7 | 107 | ✓ | no-frames |  |
| scummvm-alpha-8a_sky | `incompatible` | 0.0 | 0 | – | no-frames |  |
| scummVMsaves | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/scummVMsaves' |
| Shangai v2 | `incompatible` | 51.2 | 107 | ✓ | no-frames |  |
| shoveit | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/shoveit' |
| Simon2X | `incompatible` | 7.7 | 7 | – | no-frames |  |
| Skin1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Skin1' |
| smw-1.6_gp2x | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/null |
| snakepan | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/snakepan/Snakepan.gpe' is not an ARM ELF and no runnable |
| SnoodForTileMatch | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/SnoodForTileMatch' |
| snowedin6_v1-00_gp2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/graphics/fb0 |
| sopwith_camel_rc3 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/null |
| space52_gp2x(oficial) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/space52_gp2x(oficial)/space_52/space_52_gp2x.gpe' is not |
| space52_gp2x(open2x) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/space52_gp2x(open2x)/space_52/space_52_gp2x.gpe' is not  |
| SpaceSnake | `incompatible` | 0.1 | 1 | ✓ | mmio-spin | 0x90a |
| Sqdef 1.4 | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| squaregame2xV1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/squaregame2xV1/squaregame2x.gpe' is not an ARM ELF and n |
| Starship Soldier.gp2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Starship Soldier.gp2x/StarshipSoldier/starship_soldier.g |
| stppc2x-v1.1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/stppc2x-v1.1' |
| stppc2x-v1.1.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/stppc2x-v1.1.zip' (exit 32512) |
| strife | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/strife/dosbox/strife/strife.gpe' is not an ARM ELF and n |
| Supa2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Supa2x/dosbox/supaplex.gpe' is not an ARM ELF and no run |
| testmem2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/testmem2x/testmem2x/testmem2x.gpe' is not an ARM ELF and |
| TouchGames | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| Trap75 | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| ttd2x_020108 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| tunar-1.1.0 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/tunar-1.1.0/tunar/tunar.gpe' is not an ARM ELF and no ru |
| TurnOn | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/TurnOn' |
| Tux_Strikes_Back | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Tux_Strikes_Back' |
| Txishos (Gp2x F-200) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Txishos (Gp2x F-200)/Gp2x F-200/Txishos/Txishos.gpe' is  |
| UBPGP2x | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/UBPGP2x' |
| uqm-0.4.2-content.uqm | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/uqm-0.4.2-content.uqm' |
| uqm-0.5.0-r1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| uqm2x_langpack_v1.2 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/uqm2x_langpack_v1.2/uqm2xfin.gpe' is not an ARM ELF and  |
| uqm2x_release_1.1 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| uqm2x_remixpack_1.1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/uqm2x_remixpack_1.1/uqm2xrmx.gpe' is not an ARM ELF and  |
| UQMgp2x-0.5.0_with_content | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| WADFEST | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/WADFEST' |
| wads1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/wads1/wads1/requiem.gpe' is not an ARM ELF and no runnab |
| wads2 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/wads2/wads2/h2h-xmas.gpe' is not an ARM ELF and no runna |
| warcraft-beta3-gp2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse0 |
| Winter_Jumper | `incompatible` | 2.0 | 1 | – | no-frames |  |
| wizznic06_NES_30levels | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/wizznic06_NES_30levels' |
| Wolf4SDL | `incompatible` | 0.0 | 0 | – | no-frames |  |
| worminator302 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zcgp2x_211B18_0.4alpha | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/gpmdata |
| Znumbers | `incompatible` | 51.9 | 109 | ✓ | no-frames |  |
| Zombiepox2X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zombiesorbet_v1.0_gp2x | `incompatible` | 57.7 | 435 | ✓ | no-frames |  |
| 2xWargus_PB1.3 | `black` | 1.4 | 11 | ✓ | black-screen |  |
| 2xZdoom_selector | `black` | 1.8 | 4 | ✓ | black-screen |  |
| alex | `black` | 61.1 | 1540 | ✓ | black-screen |  |
| alex4_gp2x | `black` | 61.0 | 1537 | ✓ | black-screen |  |
| AlienBlaster_1.02 | `black` | 6.3 | 9 | ✓ | black-screen |  |
| angband2x-v2 | `black` | 34.5 | 71 | – | black-screen |  |
| bang_gp | `black` | 23.3 | 18 | ✓ | black-screen |  |
| BareFistFighter | `black` | 60.8 | 1530 | ✓ | black-screen |  |
| BeetleRun | `black` | 11.6 | 6 | ✓ | black-screen |  |
| Boulders-0 | `black` | 14.0 | 8 | ✓ | black-screen |  |
| BubbleTrain_GP2X-2006_Entry | `black` | 1.0 | 2 | ✓ | black-screen |  |
| cat_trap | `black` | 0.2 | 4 | ✓ | black-screen |  |
| ChopperAttackv1.0.17 | `black` | 0.1 | 4 | ✓ | black-screen |  |
| Comando2gp2xEN | `black` | 25.8 | 13 | ✓ | black-screen |  |
| coppergreen | `black` | 10.0 | 11 | ✓ | black-screen |  |
| d2x-gp2x-0.02 | `black` | 9.8 | 6 | ✓ | black-screen |  |
| Dark_Light_SDL2X | `black` | 20.0 | 17 | ✓ | black-screen |  |
| DeathTrap1_1 | `black` | 9.0 | 13 | ✓ | black-screen |  |
| dodge | `black` | 15.7 | 14 | ✓ | black-screen |  |
| egoboo-cramfs | `black` | 31.5 | 55 | ✓ | black-screen |  |
| falldown_gp2x | `black` | 61.8 | 1551 | ✓ | black-screen |  |
| fenix | `black` | 10.8 | 23 | ✓ | black-screen |  |
| Flappynerd_GP2X | `black` | 11.6 | 31 | ✓ | black-screen |  |
| FleshChasmer132c_patch | `black` | 7.9 | 4 | ✓ | black-screen |  |
| FleshChasmer_Dpad | `black` | 7.9 | 4 | ✓ | black-screen |  |
| freecell_1 | `black` | 50.8 | 108 | ✓ | black-screen |  |
| godori | `black` | 6.0 | 3 | – | black-screen |  |
| gp2x-blobwars-0.1 | `black` | 0.1 | 2 | ✓ | black-screen |  |
| gp2x-bubbletrain-0.1 | `black` | 0.1 | 3 | ✓ | black-screen |  |
| gp2x-netrok-0.1 | `black` | 105.2 | 3054 | ✓ | black-screen |  |
| gp2x-sand-0.3 | `black` | 54.5 | 112 | – | black-screen |  |
| gp2xDoukutsu-1.04 | `black` | 11.7 | 9 | ✓ | black-screen |  |
| gp2xJenkasNightmare | `black` | 11.8 | 9 | ✓ | black-screen |  |
| GPgeneral | `black` | 3.9 | 2 | – | black-screen |  |
| gpnoid2x | `black` | 20.7 | 15 | ✓ | black-screen |  |
| GPrina-GP2x_v1.0 | `black` | 59.1 | 1516 | ✓ | black-screen |  |
| just4qix | `black` | 11.9 | 6 | ✓ | black-screen |  |
| liquidwar2x02 | `black` | 3.9 | 2 | – | black-screen |  |
| monochromeworlds-gp2x-1.0.0 | `black` | 0.0 | 25 | ✓ | black-screen |  |
| moonlander | `black` | 11.7 | 13 | ✓ | black-screen |  |
| nazcarunners-0 | `black` | 24.1 | 24 | ✓ | black-screen |  |
| nazcasphere | `black` | 15.5 | 11 | ✓ | black-screen |  |
| nethack-ascii-3.4.3port1 | `black` | 4.0 | 2 | – | black-screen |  |
| Nom | `black` | 11.3 | 6 | – | black-screen |  |
| omok | `black` | 52.8 | 110 | ✓ | black-screen |  |
| openggs | `black` | 59.6 | 1533 | ✓ | black-screen |  |
| openjazz-gp2x | `black` | 12.5 | 11 | ✓ | black-screen |  |
| othello_v1.0 | `black` | 61.4 | 1541 | ✓ | black-screen |  |
| pacmame | `black` | 7.8 | 4 | – | black-screen |  |
| para3 | `black` | 49.9 | 127 | ✓ | black-screen |  |
| pez | `black` | 8.0 | 4 | – | black-screen |  |
| Pong | `black` | 61.4 | 1542 | – | black-screen |  |
| PowerSlide | `black` | 60.2 | 1531 | ✓ | black-screen |  |
| protozoa v1.0 | `black` | 4.4 | 4 | ✓ | black-screen |  |
| raw2xv0.3.1 | `black` | 13.9 | 7 | – | black-screen |  |
| ShadowWarrior2X | `black` | 6.0 | 3 | – | black-screen |  |
| SimOniZ | `black` | 0.1 | 3 | ✓ | black-screen |  |
| sleuth slots 2x | `black` | 1.6 | 43 | ✓ | black-screen |  |
| SmashGp2x02 | `black` | 57.0 | 1446 | ✓ | black-screen |  |
| sprint_race | `black` | 7.7 | 6 | – | black-screen |  |
| starsystem | `black` | 19.0 | 13 | ✓ | black-screen |  |
| step2x02 | `black` | 49.5 | 107 | ✓ | black-screen |  |
| superpang | `black` | 37.1 | 96 | ✓ | black-screen |  |
| supertux-0.1.3-gp2x-v4 | `black` | 53.9 | 1489 | ✓ | black-screen |  |
| tesla-Siren | `black` | 17.7 | 11 | ✓ | black-screen |  |
| Tetrablocks.0.4.GP2X | `black` | 47.0 | 108 | ✓ | black-screen |  |
| tileworld2x | `black` | 56.5 | 1527 | ✓ | black-screen |  |
| tilt | `black` | 20.2 | 15 | ✓ | black-screen |  |
| TRAINS | `black` | 7.0 | 4 | ✓ | black-screen |  |
| uhexen | `black` | 7.5 | 4 | – | black-screen |  |
| ultratumba_exp-20100925.gp2x | `black` | 11.2 | 6 | ✓ | black-screen |  |
| Volleyball | `black` | 52.0 | 111 | ✓ | black-screen |  |
| Wizznic_2x_07alpha2 | `black` | 11.0 | 18 | ✓ | black-screen |  |
| wizznic_gp2x-0.9.9 | `black` | 9.9 | 15 | ✓ | black-screen |  |
| wolfdx | `black` | 45.7 | 54 | ✓ | black-screen |  |
| xbak-0.1.3 | `black` | 21.9 | 11 | – | black-screen |  |
| xcom1-v1.0.2-gp2x | `black` | 103.0 | 2638 | ✓ | black-screen |  |
| xcom2-v1.0.1-gp2x | `black` | 106.5 | 2729 | ✓ | black-screen |  |
| xump2x_beta2 | `black` | 27.7 | 14 | ✓ | black-screen |  |
| Zelda_roth_US_gp2x | `black` | 21.0 | 44 | ✓ | black-screen |  |
| 1945_GP2X_0.2b | `ingame` | 58.7 | 554 | ✓ | garbled-visuals | pixel-to-pixel noise of 96, far above what dithered artwork reaches; the frame looks like  |
| _-The Reversed Preacher 3-_Hack bIld_ | `ingame` | 50.8 | 117 | ✓ | flat-fill |  |
| _-the reversed preacher II-_ | `ingame` | 51.3 | 119 | ✓ | flat-fill |  |
| a_sn-pong | `ingame` | 41.5 | 1534 | – | no-audio |  |
| ADIC2X | `ingame` | 20.9 | 249 | ✓ | low-fps |  |
| AMazing-3D | `ingame` | 64.7 | 1638 | – | no-audio |  |
| ASCIIPong2xV0.4 | `ingame` | 39.3 | 988 | ✓ | flat-fill |  |
| Birdshoot | `ingame` | 60.9 | 1529 | – | no-audio |  |
| BisfoG | `ingame` | 8.9 | 106 | ✓ | low-fps |  |
| Blocked | `ingame` | 3.7 | 95 | ✓ | low-fps |  |
| bugafactorx-v03-beta | `ingame` | 60.1 | 1526 | – | no-audio |  |
| BunnyTraps-v11 | `ingame` | 61.8 | 1558 | ✓ | garbled-visuals | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like |
| buscaminas | `ingame` | 59.5 | 1507 | – | no-audio |  |
| cardm | `ingame` | 60.7 | 1544 | – | no-audio |  |
| cavecopter_gp2x | `ingame` | 21.0 | 527 | – | low-fps |  |
| Chopper | `ingame` | 60.8 | 1547 | – | no-audio |  |
| Clonk2X_1.0 | `ingame` | 10.5 | 265 | – | not-arm-elf | magiceyes: reload of '/bin/sh' failed |
| ConnyCarrot | `ingame` | 60.5 | 1540 | ✓ | flat-fill |  |
| cosmo2x_01 | `ingame` | 61.5 | 1557 | – | no-audio |  |
| CromoZome | `ingame` | 18.8 | 491 | ✓ | low-fps |  |
| Digger | `ingame` | 2.2 | 5 | ✓ | low-fps |  |
| dopewars2x | `ingame` | 61.3 | 1538 | – | no-audio |  |
| drod-gp2x-1_0 | `ingame` | 53.2 | 1372 | – | no-audio |  |
| dstroyGP2X1402 | `ingame` | 59.9 | 1545 | – | no-audio |  |
| dumbbell2x-01 | `ingame` | 60.9 | 569 | – | flat-fill |  |
| dyc_gp2x | `ingame` | 0.2 | 5 | ✓ | low-fps |  |
| escoba_exp-20101016.gp2x | `ingame` | 60.9 | 1546 | – | no-audio |  |
| extraterrestres-0 | `ingame` | 57.7 | 1562 | – | no-audio |  |
| FCRLG | `ingame` | 61.1 | 1535 | – | no-audio |  |
| fenixGamePack | `ingame` | 14.6 | 45 | ✓ | low-fps |  |
| fifteen_01 | `ingame` | 59.7 | 559 | – | no-audio |  |
| Firewhip | `ingame` | 0.7 | 10 | ✓ | low-fps |  |
| flowflowmania-0_6-gp2x | `ingame` | 47.9 | 470 | – | no-audio |  |
| freesci | `ingame` | 57.0 | 255 | – | no-audio |  |
| fruits2x | `ingame` | 49.6 | 103 | – | no-audio |  |
| gchess-v1.0.1-bin | `ingame` | 61.1 | 1547 | – | no-audio |  |
| gchess-v1.1.0-bin | `ingame` | 60.9 | 1542 | – | no-audio |  |
| GF | `ingame` | 60.8 | 1546 | ✓ | garbled-visuals | top and bottom halves are near-identical |
| gnugo2x | `ingame` | 61.0 | 1546 | – | no-audio |  |
| gorillaz | `ingame` | 12.7 | 319 | ✓ | low-fps |  |
| gp2x-ceferino-0.1 | `ingame` | 58.4 | 1552 | – | no-audio |  |
| gp2xgo-v1.1.0-bin | `ingame` | 60.4 | 1529 | – | no-audio |  |
| gp2xmancala-v1.1.1-bin | `ingame` | 60.9 | 1541 | – | no-audio |  |
| GP2XOfLife | `ingame` | 4.4 | 110 | – | low-fps |  |
| GPSquares_GP2X | `ingame` | 61.7 | 1550 | – | no-audio |  |
| grow | `ingame` | 41.3 | 1531 | – | no-audio |  |
| gxeskiv | `ingame` | 58.4 | 1474 | – | no-audio |  |
| hex-a-hop | `ingame` | 61.5 | 1548 | – | no-audio |  |
| HumphreyGP2X | `ingame` | 60.5 | 1539 | ✓ | flat-fill |  |
| kampfimall-gp2x | `ingame` | 61.1 | 1538 | – | flat-fill |  |
| kampfimall-gp2x-music | `ingame` | 59.7 | 520 | ✓ | flat-fill |  |
| Knight Lore | `ingame` | 10.5 | 264 | ✓ | flat-fill |  |
| LABYRINTH | `ingame` | 61.5 | 1548 | – | no-audio |  |
| las-tres-luces-de-glaurung-remake | `ingame` | 60.3 | 1540 | ✓ | flat-fill |  |
| levelEdit | `ingame` | 61.4 | 1548 | – | flat-fill |  |
| Life.0.1 | `ingame` | 61.9 | 1556 | – | garbled-visuals | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like |
| lights-out | `ingame` | 59.4 | 1497 | – | no-audio |  |
| mancala-v1.0.1 | `ingame` | 61.1 | 1549 | – | no-audio |  |
| masterpiece2x | `ingame` | 61.6 | 1550 | – | no-audio |  |
| MazezaMGP2X | `ingame` | 7.2 | 187 | ✓ | low-fps |  |
| minigolf | `ingame` | 60.6 | 1532 | – | no-audio |  |
| minos-gp2x | `ingame` | 0.0 | 1397 | ✓ | low-fps |  |
| mk13.gpe | `ingame` | 0.0 | 522 | ✓ | flat-fill |  |
| mkACE.gpe | `ingame` | 0.0 | 248 | ✓ | low-fps |  |
| mkONE.gpe | `ingame` | 0.0 | 188 | ✓ | low-fps |  |
| monacoGP | `ingame` | 0.0 | 132 | ✓ | flat-fill |  |
| MoveSweep2X | `ingame` | 49.9 | 102 | – | garbled-visuals | the screen holds a second copy of itself, offset by 96px; left and right halves are near-i |
| Nebulus_gp2x | `ingame` | 60.7 | 1525 | – | no-audio |  |
| Net-Bubble-gp2x_1-21-06_bin | `ingame` | 55.3 | 517 | – | no-audio |  |
| oxov06 | `ingame` | 46.7 | 96 | – | no-audio |  |
| PerfectFit | `ingame` | 61.5 | 1553 | – | no-audio |  |
| PocketSnes_SMRPG | `ingame` | 116.6 | 2932 | – | no-audio |  |
| Pond2X | `ingame` | 62.6 | 1574 | – | flat-fill |  |
| pong2player | `ingame` | 57.2 | 117 | – | no-audio |  |
| pong2v060x | `ingame` | 53.7 | 110 | – | no-audio |  |
| powder2x-112 | `ingame` | 60.3 | 1538 | – | no-audio |  |
| prboom-gp2x | `ingame` | 60.3 | 1549 | – | no-audio |  |
| RevoltOfTheBinaryCouriers GP2X | `ingame` | 60.2 | 1516 | – | no-audio |  |
| robot-escape | `ingame` | 87.8 | 189 | ✓ | flat-fill |  |
| scummvm-kor0.4.2cvs | `ingame` | 59.7 | 1529 | – | no-audio |  |
| sdlmonkey_0.1 | `ingame` | 60.6 | 1541 | – | no-audio |  |
| snake2x-1.1 | `ingame` | 60.2 | 1548 | – | no-audio |  |
| Solitaire2x-v1.4 | `ingame` | 82.2 | 769 | – | no-audio |  |
| sources_MEMORY2X | `ingame` | 60.5 | 1542 | – | no-audio |  |
| space squares | `ingame` | 60.6 | 1550 | – | no-audio |  |
| SpaceRocks2X | `ingame` | 32.5 | 92 | – | no-audio |  |
| spartak-chess_0.0.4_gp2x | `ingame` | 60.7 | 1543 | – | no-audio |  |
| Sponge Blob Tennis | `ingame` | 41.9 | 1551 | – | no-audio |  |
| spout | `ingame` | 61.2 | 1541 | – | no-audio |  |
| Sqcolony | `ingame` | 63.3 | 1597 | – | no-audio |  |
| StairwayToHeaven | `ingame` | 37.0 | 114 | ✓ | flat-fill |  |
| sudoku-v1.0 | `ingame` | 61.0 | 1539 | – | no-audio |  |
| sudoku2x-0.5 | `ingame` | 60.5 | 1524 | – | no-audio |  |
| Tangle | `ingame` | 62.0 | 1556 | – | no-audio |  |
| the reversed preacher II | `ingame` | 49.0 | 114 | ✓ | flat-fill |  |
| TimeFrack2D for GP2X | `ingame` | 48.8 | 100 | – | no-audio |  |
| tower | `ingame` | 106.8 | 2706 | – | no-audio |  |
| ttxbeta170706b | `ingame` | 57.4 | 1544 | – | no-audio |  |
| TUcS.app(V0.7.0 - GP2X) | `ingame` | 22.7 | 575 | ✓ | low-fps |  |
| VekDemo2 | `ingame` | 0.4 | 10 | ✓ | low-fps |  |
| Vektar | `ingame` | 0.2 | 6 | ✓ | low-fps |  |
| vexedb1 | `ingame` | 61.6 | 1553 | – | no-audio |  |
| waffle2x | `ingame` | 44.3 | 90 | – | no-audio |  |
| wire3d | `ingame` | 57.9 | 1519 | – | no-audio |  |
| Wiztern Demo | `ingame` | 3.5 | 37 | ✓ | low-fps |  |
| 2xpong_gp2x | `playable` | 61.0 | 1534 | ✓ |  |  |
| 2xtron-v01 | `playable` | 61.7 | 1553 | ✓ |  |  |
| 9 Lives | `playable` | 43.7 | 1103 | ✓ |  |  |
| AbusimbelProfanationDeluxe | `playable` | 118.0 | 2991 | ✓ |  |  |
| AdamantArmorAffection2x | `playable` | 60.5 | 1532 | ✓ |  |  |
| AfterBurner-GP2X | `playable` | 26.8 | 681 | ✓ |  |  |
| Airplyr | `playable` | 61.4 | 1546 | ✓ |  |  |
| airstrike-1.1 | `playable` | 60.6 | 1541 | ✓ |  |  |
| Akd_BB | `playable` | 61.4 | 1555 | ✓ |  |  |
| altitude | `playable` | 38.4 | 120 | ✓ |  |  |
| amoebax-0.2.1-gp2x | `playable` | 55.8 | 1420 | ✓ |  |  |
| armorcity-0_30b | `playable` | 60.4 | 1547 | ✓ |  |  |
| Asteroids | `playable` | 60.1 | 1543 | ✓ |  |  |
| astrochaos | `playable` | 56.9 | 312 | ✓ |  |  |
| barrage | `playable` | 61.3 | 1549 | ✓ |  |  |
| Batiscafo (versin EXP) | `playable` | 60.7 | 1528 | ✓ |  |  |
| BearOids | `playable` | 60.6 | 1529 | ✓ |  |  |
| beat2x-0.5-bin | `playable` | 60.9 | 1546 | ✓ |  |  |
| Beatbox_1.2 | `playable` | 58.7 | 1543 | ✓ |  |  |
| biniax-gp2x_v1.2 | `playable` | 61.1 | 1539 | ✓ |  |  |
| Biniax2_gp2x | `playable` | 81.2 | 2060 | ✓ |  |  |
| Biohazard2 | `playable` | 61.6 | 1552 | ✓ |  |  |
| BioShoot GP2X | `playable` | 60.1 | 1543 | ✓ |  |  |
| blastriot1.21 | `playable` | 32.5 | 820 | ✓ |  |  |
| blazar_v1-30_gp2x | `playable` | 61.5 | 1550 | ✓ |  |  |
| blingo 1.2 | `playable` | 42.8 | 126 | ✓ |  |  |
| blipsgp2x | `playable` | 59.6 | 1558 | ✓ |  |  |
| blobbyvolley | `playable` | 60.8 | 1539 | ✓ |  |  |
| blobwars_2x | `playable` | 62.3 | 1578 | ✓ |  |  |
| block | `playable` | 61.3 | 1553 | ✓ |  |  |
| blockdudegp2x | `playable` | 57.5 | 1551 | ✓ |  |  |
| Blockrage2x | `playable` | 60.5 | 1533 | ✓ |  |  |
| blox | `playable` | 36.7 | 925 | ✓ |  |  |
| Bloxz_DEMO | `playable` | 61.6 | 1562 | ✓ |  |  |
| bluecube2x | `playable` | 61.3 | 1549 | ✓ |  |  |
| bobtron-gp2x | `playable` | 61.3 | 1546 | ✓ |  |  |
| Boomshine2x_1.12_gp2x | `playable` | 59.8 | 1541 | ✓ |  |  |
| brassmunkey_gp2x_1.0 | `playable` | 61.0 | 1542 | ✓ |  |  |
| BubbleX | `playable` | 61.4 | 1540 | ✓ |  |  |
| BubTrain_GP2X-2006_Entry_No-Sound | `playable` | 57.6 | 1551 | ✓ |  |  |
| BugWarsSE_v1.0 | `playable` | 59.9 | 1547 | ✓ |  |  |
| bumprace-0.2 | `playable` | 59.9 | 1549 | ✓ |  |  |
| BurokkuDemo1 | `playable` | 61.5 | 1560 | ✓ |  |  |
| buttongame | `playable` | 47.1 | 98 | ✓ |  |  |
| BuzzysBadDay-1.0 | `playable` | 61.1 | 1545 | ✓ |  |  |
| CamelotWarriors-GP2x_v1.0 | `playable` | 59.7 | 1524 | ✓ |  |  |
| CascadeBeneath v1.0 for GP2X | `playable` | 62.4 | 1571 | ✓ |  |  |
| ccrg | `playable` | 55.5 | 246 | ✓ |  |  |
| cgenius-gp2x | `playable` | 57.8 | 1504 | ✓ |  |  |
| chaos2x | `playable` | 61.9 | 1558 | ✓ |  |  |
| checkersgp2x | `playable` | 59.8 | 1555 | ✓ |  |  |
| chess2x05 | `playable` | 60.0 | 1549 | ✓ |  |  |
| chuckiev12 | `playable` | 61.2 | 1541 | ✓ |  |  |
| CowSuckers-1.0 | `playable` | 61.1 | 1545 | ✓ |  |  |
| Crapong | `playable` | 60.4 | 1541 | ✓ |  |  |
| crazeeman | `playable` | 64.3 | 1630 | ✓ |  |  |
| crimsonV1 | `playable` | 59.5 | 1549 | ✓ |  |  |
| crossroads | `playable` | 61.4 | 1550 | ✓ |  |  |
| CUBES | `playable` | 61.3 | 1551 | ✓ |  |  |
| cyberhockeyV2_6 | `playable` | 61.2 | 1543 | ✓ |  |  |
| DABAKKA-0 | `playable` | 61.3 | 1538 | ✓ |  |  |
| Dance2x Alpha GPE | `playable` | 60.9 | 1548 | ✓ |  |  |
| Dastardly dungeon 1.5 | `playable` | 43.2 | 108 | ✓ |  |  |
| dd2x | `playable` | 102.8 | 469 | ✓ |  |  |
| DealOrNoDeal-v12 | `playable` | 60.5 | 1550 | ✓ |  |  |
| debian_vs_pimientos_2x_0.1.2 | `playable` | 58.3 | 554 | ✓ |  |  |
| defeatme-gp2x-1.0.1 | `playable` | 60.5 | 1528 | ✓ |  |  |
| diamant_1_01 | `playable` | 28.4 | 719 | ✓ |  |  |
| DontGetCrushed v1.0 for GP2X | `playable` | 62.4 | 1566 | ✓ |  |  |
| dosmugen | `playable` | 59.7 | 1548 | ✓ |  |  |
| Drill2x_final | `playable` | 60.0 | 1533 | ✓ |  |  |
| drill2x_xtreme_v1.0.3 | `playable` | 60.2 | 1538 | ✓ |  |  |
| DubaiRace038a | `playable` | 49.3 | 115 | ✓ |  |  |
| dynamategp2x | `playable` | 59.9 | 1541 | ✓ |  |  |
| eggstreme3_v1-00_gp2x | `playable` | 61.4 | 1548 | ✓ |  |  |
| egoboo2xFeb1207 | `playable` | 100.0 | 3051 | ✓ |  |  |
| Electronia | `playable` | 61.4 | 1549 | ✓ |  |  |
| enigma | `playable` | 55.0 | 1463 | ✓ |  |  |
| entombed2x | `playable` | 60.8 | 1547 | ✓ |  |  |
| EpicFreeFall_GP2X | `playable` | 60.1 | 1540 | ✓ |  |  |
| EpicRocks_GP2X | `playable` | 57.4 | 638 | ✓ |  |  |
| escapa-v1 | `playable` | 61.6 | 1555 | ✓ |  |  |
| exi_shoot_gp2x | `playable` | 60.5 | 1523 | ✓ |  |  |
| extraterrestres | `playable` | 98.4 | 2689 | ✓ |  |  |
| exult_rc3 | `playable` | 41.3 | 1530 | ✓ |  |  |
| Factor-v1.0-final | `playable` | 60.2 | 1539 | ✓ |  |  |
| Fishball-1.2 | `playable` | 60.6 | 1529 | ✓ |  |  |
| fissionfield2x | `playable` | 61.2 | 1548 | ✓ |  |  |
| Flaschenspiel | `playable` | 60.8 | 1546 | ✓ |  |  |
| FleshChasmer | `playable` | 60.1 | 1534 | ✓ |  |  |
| FleshChasmer Zero | `playable` | 60.6 | 1533 | ✓ |  |  |
| floaters | `playable` | 61.2 | 1547 | ✓ |  |  |
| flobopuyo0.20.1 | `playable` | 59.8 | 1521 | ✓ |  |  |
| flurkies_v1-25_gp2x | `playable` | 61.3 | 1545 | ✓ |  |  |
| fm | `playable` | 111.5 | 2796 | ✓ |  |  |
| formula1gp2x | `playable` | 60.7 | 1546 | ✓ |  |  |
| Fragger2x | `playable` | 61.0 | 1536 | ✓ |  |  |
| freec2x | `playable` | 25.9 | 657 | ✓ |  |  |
| friq-beta-07 | `playable` | 61.3 | 1556 | ✓ |  |  |
| frozen2x-0.1 | `playable` | 76.9 | 725 | ✓ |  |  |
| fruits_gp2x | `playable` | 61.1 | 1543 | ✓ |  |  |
| fvc | `playable` | 61.0 | 1534 | ✓ |  |  |
| FyWod_2x | `playable` | 60.7 | 1553 | ✓ |  |  |
| game bIld 2 | `playable` | 60.7 | 1530 | ✓ |  |  |
| game-watch-mario-bros | `playable` | 61.2 | 1544 | ✓ |  |  |
| Geek 'em up GP2X | `playable` | 44.4 | 1263 | ✓ |  |  |
| gemdrop2x_v02 | `playable` | 60.5 | 1555 | ✓ |  |  |
| Ghostbusters_WIP | `playable` | 62.6 | 590 | ✓ |  |  |
| ghostpix_v10_gp2x | `playable` | 60.4 | 1539 | ✓ |  |  |
| gnp_104 | `playable` | 56.2 | 1546 | ✓ |  |  |
| GoitGP | `playable` | 59.8 | 1545 | ✓ |  |  |
| gp2hanoi_0.8.1_gp2x | `playable` | 61.2 | 1543 | ✓ |  |  |
| gp2x-formido-0.1 | `playable` | 42.0 | 1547 | ✓ |  |  |
| gp2x-invaders-preview-version | `playable` | 61.6 | 1556 | ✓ |  |  |
| gp2x-shienso-bin_061021 | `playable` | 61.6 | 1554 | ✓ |  |  |
| gp2x-smc-0.1 | `playable` | 57.0 | 1540 | ✓ |  |  |
| gp2x_2xmas | `playable` | 59.9 | 1538 | ✓ |  |  |
| GP2X_BallGame_0.49 | `playable` | 61.2 | 1540 | ✓ |  |  |
| gp2x_drench | `playable` | 57.6 | 1459 | ✓ |  |  |
| GP2X_Nat2007 | `playable` | 47.8 | 1206 | ✓ |  |  |
| GP2X_TLI | `playable` | 27.7 | 698 | ✓ |  |  |
| gp2xjunkie | `playable` | 58.6 | 1502 | ✓ |  |  |
| gp2xpang-v.1.1.1 | `playable` | 100.1 | 2588 | ✓ |  |  |
| gp2xrick 1.0 | `playable` | 59.3 | 1509 | ✓ |  |  |
| GpFrontier v0.1 | `playable` | 61.7 | 1564 | ✓ |  |  |
| gpfrontier v0.4 | `playable` | 59.2 | 1559 | ✓ |  |  |
| gr-v1001-gp2x | `playable` | 62.2 | 1548 | ✓ |  |  |
| green | `playable` | 60.3 | 1532 | ✓ |  |  |
| hanagechu2x_gbax2007 | `playable` | 65.5 | 1656 | ✓ |  |  |
| hanagechu2xalpha | `playable` | 61.5 | 1549 | ✓ |  |  |
| Heretic2X_v0.5 | `playable` | 60.3 | 1516 | ✓ |  |  |
| hexbattle2x | `playable` | 60.9 | 1543 | ✓ |  |  |
| Hyperion_GP2X_demo | `playable` | 61.5 | 1556 | ✓ |  |  |
| jumpnbumpgp2x | `playable` | 59.8 | 1545 | ✓ |  |  |
| Jurlx2 | `playable` | 61.0 | 1543 | ✓ |  |  |
| ketm_2x_gp2x | `playable` | 53.5 | 1547 | ✓ |  |  |
| KicknPLay_1.1 | `playable` | 61.5 | 1546 | ✓ |  |  |
| koules2x_02 | `playable` | 60.7 | 1546 | ✓ |  |  |
| kuklomenos_gp2x_201209 | `playable` | 60.3 | 1547 | ✓ |  |  |
| kurukuru2x | `playable` | 61.1 | 1552 | ✓ |  |  |
| la | `playable` | 48.8 | 108 | ✓ |  |  |
| ladykiller | `playable` | 60.8 | 1550 | ✓ |  |  |
| lbreakoutgp2x | `playable` | 58.6 | 1557 | ✓ |  |  |
| levelshmup | `playable` | 85.8 | 2193 | ✓ |  |  |
| LinesXv3 | `playable` | 61.3 | 1539 | ✓ |  |  |
| logicx | `playable` | 61.3 | 1541 | ✓ |  |  |
| mad-mix-game-20b-final | `playable` | 59.7 | 1511 | ✓ |  |  |
| madbomber | `playable` | 60.2 | 1539 | ✓ |  |  |
| malvado2x | `playable` | 45.9 | 148 | ✓ |  |  |
| MAME-N22_51 | `playable` | 58.7 | 1550 | ✓ |  |  |
| March of the mini tux | `playable` | 53.5 | 1357 | ✓ |  |  |
| Marte Necesita Vacas GP2X | `playable` | 42.8 | 1102 | ✓ |  |  |
| Masteries_Journey_to_the_Center_of_the_earth_GP2X | `playable` | 58.9 | 1537 | ✓ |  |  |
| MazeThingie | `playable` | 61.4 | 1548 | ✓ |  |  |
| MemoryGP2X-v11 | `playable` | 61.6 | 1554 | ✓ |  |  |
| meritous | `playable` | 60.1 | 1532 | ✓ |  |  |
| Merlin2x_beta_021 | `playable` | 57.2 | 534 | ✓ |  |  |
| metaphysik | `playable` | 63.2 | 1603 | ✓ |  |  |
| methaneV1 | `playable` | 60.8 | 1538 | ✓ |  |  |
| MM2X | `playable` | 61.0 | 1536 | ✓ |  |  |
| mush_gp2x | `playable` | 44.1 | 1133 | ✓ |  |  |
| Mutant Tank Knights | `playable` | 57.6 | 343 | ✓ |  |  |
| MyriadUpdated | `playable` | 59.0 | 1542 | ✓ |  |  |
| mzx-2.84c | `playable` | 59.9 | 830 | ✓ |  |  |
| mzx282-gp2x | `playable` | 60.8 | 817 | ✓ |  |  |
| n-tris_v1.0 | `playable` | 61.6 | 1546 | ✓ |  |  |
| nanobounce-pacc-gp2x | `playable` | 52.1 | 401 | ✓ |  |  |
| ne_deluxe_gp2x | `playable` | 61.2 | 1543 | ✓ |  |  |
| ne_gp2x | `playable` | 59.3 | 1498 | ✓ |  |  |
| NecNec2x | `playable` | 60.6 | 1537 | ✓ |  |  |
| newsuperpang | `playable` | 60.6 | 1534 | ✓ |  |  |
| Nifty | `playable` | 60.3 | 1538 | ✓ |  |  |
| odonata_demo | `playable` | 59.9 | 1513 | ✓ |  |  |
| OpenBOR_v2.1933 | `playable` | 59.1 | 1056 | ✓ |  |  |
| OpenBOR_v3.0_Build_2615_&_2637 | `playable` | 62.6 | 1550 | ✓ |  |  |
| openglad2x | `playable` | 59.0 | 1535 | ✓ |  |  |
| opentyrian2x_0.3_complete | `playable` | 56.1 | 1534 | ✓ |  |  |
| OrbitalSniper2x_v1.1 | `playable` | 57.4 | 143 | ✓ |  |  |
| PAF | `playable` | 61.1 | 1542 | ✓ |  |  |
| paraballgp2x | `playable` | 60.2 | 1525 | ✓ |  |  |
| PaybackDemo | `playable` | 26.7 | 695 | ✓ |  |  |
| pdcv060b | `playable` | 54.3 | 430 | ✓ |  |  |
| Peuppy_10_GP2X | `playable` | 27.4 | 692 | ✓ |  |  |
| Phishy-0 | `playable` | 59.7 | 1525 | ✓ |  |  |
| physique | `playable` | 61.1 | 1543 | ✓ |  |  |
| Pika2x | `playable` | 59.6 | 561 | ✓ |  |  |
| pintor2x | `playable` | 83.8 | 2106 | ✓ |  |  |
| pixpang | `playable` | 55.4 | 1543 | ✓ |  |  |
| Poker2x | `playable` | 110.6 | 2803 | ✓ |  |  |
| Pool Panic | `playable` | 60.7 | 1530 | ✓ |  |  |
| powermanga-0.80 | `playable` | 56.1 | 1475 | ✓ |  |  |
| PPlane | `playable` | 55.3 | 1395 | ✓ |  |  |
| PPlane2.GP2X | `playable` | 42.4 | 1133 | ✓ |  |  |
| proj0-demo_01 | `playable` | 60.0 | 1528 | ✓ |  |  |
| PulplifeWars | `playable` | 59.0 | 1541 | ✓ |  |  |
| puzzlelandgp2x | `playable` | 56.4 | 1544 | ✓ |  |  |
| qfg3-0 | `playable` | 59.9 | 1551 | ✓ |  |  |
| quartz2_v1-50_gp2x | `playable` | 61.5 | 1548 | ✓ |  |  |
| Rabbit_vs_Flies_0.9 | `playable` | 60.4 | 1529 | ✓ |  |  |
| ramon atacks | `playable` | 60.9 | 1536 | ✓ |  |  |
| Release GP2X MST_RUNNERS | `playable` | 59.5 | 1536 | ✓ |  |  |
| retrovirus_1_1 | `playable` | 59.9 | 1515 | ✓ |  |  |
| reword_v0.5 | `playable` | 61.3 | 1560 | ✓ |  |  |
| rg_105 | `playable` | 58.1 | 1548 | ✓ |  |  |
| rg_ura_103 | `playable` | 58.0 | 1549 | ✓ |  |  |
| river | `playable` | 61.3 | 1540 | ✓ |  |  |
| RockRain | `playable` | 61.3 | 1547 | ✓ |  |  |
| rockrain2_exp-20100925 | `playable` | 60.7 | 1540 | ✓ |  |  |
| rookiehero_EXP.gp2x.v20120220 | `playable` | 60.6 | 1546 | ✓ |  |  |
| RoundEmUp-alpha3 | `playable` | 61.2 | 1554 | ✓ |  |  |
| rubidogp2x | `playable` | 59.7 | 1551 | ✓ |  |  |
| ruckman_v1.03 | `playable` | 32.9 | 88 | ✓ |  |  |
| Runner_GP2X | `playable` | 59.4 | 1517 | ✓ |  |  |
| s-tris2_v1-64_gp2x | `playable` | 61.5 | 1547 | ✓ |  |  |
| sachunsungx | `playable` | 61.5 | 1546 | ✓ |  |  |
| santaMania | `playable` | 56.6 | 1443 | ✓ |  |  |
| ScorchedGPBeta2 | `playable` | 60.6 | 1539 | ✓ |  |  |
| scummvm-0.11.1-gp2x | `playable` | 58.7 | 1529 | ✓ |  |  |
| scummvm-1.2.0-gp2x | `playable` | 57.5 | 1532 | ✓ |  |  |
| SdLame | `playable` | 60.7 | 1545 | ✓ |  |  |
| sdlscav_gp2x_0.2.0 | `playable` | 110.5 | 2812 | ✓ |  |  |
| ShanghaiX | `playable` | 61.3 | 1540 | ✓ |  |  |
| SheepDash | `playable` | 59.5 | 1547 | ✓ |  |  |
| Shippy84 | `playable` | 60.4 | 1550 | ✓ |  |  |
| siv050 | `playable` | 58.2 | 1554 | ✓ |  |  |
| SmallBall_GP | `playable` | 59.7 | 1529 | ✓ |  |  |
| snail runers | `playable` | 59.4 | 1523 | ✓ |  |  |
| snowedin5_v1-00_gp2x | `playable` | 61.3 | 1546 | ✓ |  |  |
| SOD v1.1 | `playable` | 59.6 | 1543 | ✓ |  |  |
| sokobangp2x | `playable` | 53.2 | 1550 | ✓ |  |  |
| sources_Yahtzee | `playable` | 50.3 | 232 | ✓ |  |  |
| space_varments_v1.0 | `playable` | 55.4 | 597 | ✓ |  |  |
| spacestorm | `playable` | 51.4 | 1297 | ✓ |  |  |
| Squares-v051 | `playable` | 61.1 | 1048 | ✓ |  |  |
| Squaresliding | `playable` | 41.8 | 1546 | ✓ |  |  |
| starfighter-gp2x-0.01 | `playable` | 65.3 | 1133 | ✓ |  |  |
| StarTrucker | `playable` | 60.4 | 1531 | ✓ |  |  |
| stppc2x-v1.0 | `playable` | 41.7 | 1544 | ✓ |  |  |
| stransball2 | `playable` | 59.8 | 1519 | ✓ |  |  |
| street2x | `playable` | 54.6 | 1536 | ✓ |  |  |
| subhunt | `playable` | 60.6 | 1535 | ✓ |  |  |
| SuperChickenFallDemo | `playable` | 60.9 | 1533 | ✓ |  |  |
| SuperPaf_v1.0 | `playable` | 62.4 | 1535 | ✓ |  |  |
| SuperPixelJumper v1.1 for GP2X | `playable` | 60.9 | 1533 | ✓ |  |  |
| SuperSonicSpeed | `playable` | 61.1 | 1546 | ✓ |  |  |
| survival | `playable` | 60.9 | 1555 | ✓ |  |  |
| symbolica-0.8 | `playable` | 60.3 | 1530 | ✓ |  |  |
| tail-tale | `playable` | 60.5 | 1526 | ✓ |  |  |
| tecnoballz-0.91-gp2x | `playable` | 57.2 | 1484 | ✓ |  |  |
| tetwins | `playable` | 51.5 | 108 | ✓ |  |  |
| ThreeTs_Game | `playable` | 54.7 | 112 | ✓ |  |  |
| Thruster_GP2X | `playable` | 61.2 | 1547 | ✓ |  |  |
| tikka_dungeons_demo_1 | `playable` | 60.9 | 1534 | ✓ |  |  |
| tilematch-0.6 | `playable` | 90.5 | 2294 | ✓ |  |  |
| towertopplergp2x | `playable` | 58.7 | 1543 | ✓ |  |  |
| treev060 | `playable` | 59.9 | 1555 | ✓ |  |  |
| Unicolor | `playable` | 60.8 | 1550 | ✓ |  |  |
| vectoroids-2x | `playable` | 61.2 | 1548 | ✓ |  |  |
| vektar-free | `playable` | 27.3 | 692 | ✓ |  |  |
| vektarpack_v1 | `playable` | 77.6 | 1977 | ✓ |  |  |
| Ventifact | `playable` | 60.0 | 1550 | ✓ |  |  |
| vexed-gp2x-10 | `playable` | 60.4 | 1523 | ✓ |  |  |
| vorton-b4 | `playable` | 58.8 | 1536 | ✓ |  |  |
| vwars | `playable` | 58.9 | 1533 | ✓ |  |  |
| war_and_warriorgp2x | `playable` | 61.7 | 1551 | ✓ |  |  |
| warcraft | `playable` | 59.6 | 1538 | ✓ |  |  |
| warehouse_panic_v1.1_gp2x | `playable` | 40.8 | 582 | ✓ |  |  |
| waternetgp2x | `playable` | 56.5 | 1547 | ✓ |  |  |
| wehaveballs | `playable` | 60.9 | 1532 | ✓ |  |  |
| whacky | `playable` | 60.8 | 1537 | ✓ |  |  |
| WindAndWater_teaser_110 | `playable` | 61.1 | 1537 | ✓ |  |  |
| wnw | `playable` | 60.4 | 1530 | ✓ |  |  |
| xenitris_demo | `playable` | 61.7 | 1556 | ✓ |  |  |
| xigon-X-gp2x-V1 | `playable` | 60.9 | 1537 | ✓ |  |  |
| Xpired2x 1.0 beta | `playable` | 60.5 | 1535 | ✓ |  |  |
| xRick | `playable` | 59.0 | 1503 | ✓ |  |  |
| yahtzee-v21 | `playable` | 61.5 | 1552 | ✓ |  |  |
| znax | `playable` | 59.3 | 1552 | ✓ |  |  |
| Zoids Quest2X-0.0.1-2 | `playable` | 60.6 | 1551 | ✓ |  |  |
| zoltan 2x | `playable` | 59.6 | 1537 | ✓ |  |  |
| zooov11 | `playable` | 28.7 | 722 | ✓ |  |  |
| ztunnel-0 | `playable` | 59.8 | 1512 | ✓ |  |  |

### Wiz (153 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| [DEMO] Wiztern | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| abuse-wiz | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| alephone-wiz | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| altitude | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/altitude/altitude/altitude.gpe' is not an ARM ELF an |
| Animatch Wiz | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Art Shot Wiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/Art Shot Wiz/ArtShot/ArtShotWiz.gpe' is not an ARM E |
| Asteroids | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| battlejewels-wiz-public001demo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| beat2x-wiz | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Biological Defend | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/Biological Defend/biodef/biodef.gpe' is not an ARM E |
| BitDEFENSE | `incompatible` | 0.0 | 0 | – | no-frames |  |
| BlastRiot122Wiz | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x4000 |
| blingo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Boomshine2x_1.12_wiz | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| BubbleTrainWiz_5-20-09 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| BugwarsSE | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Camelot Warriors | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/Camelot Warriors/cw/cw.gpe' is not an ARM ELF and no |
| CDogs-wiz | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| cgenius-wiz | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| CloneKeen2X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Dastardly_Dungeon | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Demons World | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| Dragons of Rage EX | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Wiz/Dragons of Rage EX' |
| eduke32 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| EpicFreeFall_Wiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/EpicFreeFall_Wiz/EpicFreeFall/freefall.gpe' is not a |
| EpicRocks_Wiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/EpicRocks_Wiz/EpicRocks/EpicRocks.gpe' is not an ARM |
| epiphany | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Geca Blaster 2 (Gp2x Wiz) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/Geca Blaster 2 (Gp2x Wiz)/GecaBlaster2/GecaBlaster2W |
| Ghostpix | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| gnurobbo_0.65_wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| gobble | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| hheretic | `incompatible` | 0.0 | 0 | – | no-frames |  |
| hhexen | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ioquake2 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| kuklomenos | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| ladiesofrage | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Wiz/ladiesofrage' |
| malvado | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/malvado/malvado/malvado.gpe' is not an ARM ELF and n |
| Maplevania | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Wiz/Maplevania' |
| MegaMan The Power War Ep1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Wiz/MegaMan The Power War Ep1' |
| metaphysik | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Wiz/metaphysik' |
| midway | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/midway/midway/midway.gpe' is not an ARM ELF and no r |
| Minigolf | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/Minigolf/minigolf/minigolf.gpe' is not an ARM ELF an |
| Monster2-1.0-wiz | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| Myriad | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| nazcadreams | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| nazcarunners | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Nazcasphere | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| nethack-wiz | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| NewSuperPang05 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| noiz2sa_wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| openggs | `incompatible` | 0.0 | 0 | – | no-frames |  |
| openjazz-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| opentyrian_wiz_source | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Wiz/opentyrian_wiz_source' |
| Out Zone | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| paf | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| paraballwiz | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| PEZ | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| pgw | `incompatible` | 8.1 | 14 | ✓ | no-frames |  |
| PhishyWiz | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Powder2X_wiz_114_v01 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| prboom-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| preggo_Wiz | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Propis | `incompatible` | 0.0 | 0 | – | no-frames |  |
| protozoa | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| quake1-wiz | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| quake_0.03 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| RailroadRampage_1.2_Wiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/RailroadRampage_1.2_Wiz/RailroadRampage_Wiz/Railroad |
| roadfighter | `incompatible` | 0.0 | 0 | – | no-frames |  |
| rott | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/null |
| Ruckman-Wiz | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Shock Troopers Base Defense | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Skull (Windows, Linux & Gp2x Wiz) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/Skull (Windows, Linux & Gp2x Wiz)/Skull Game/Skull/S |
| sleuthslots | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| SmallBall_Wiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/SmallBall_Wiz/SmallBall/SmallBall.gpe' is not an ARM |
| smw_1.7 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Snow Bros 2 | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| SOD_Wiz | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Space Varments | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| srb2 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/null |
| SudoQ | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/SudoQ/SudoQ/sudoq/sudoq.gpe' is not an ARM ELF and n |
| supertux-wiz | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| The Minigame Project | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| tilt | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| tricorder | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Twin Cobra | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| uqm2x_release.1.1 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Wiz_Propis_Demo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| WizFrontier v0.1 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| wizpong | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| wolf4sdl_wiz_svn | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| WWII | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| xpiredwiz.eng.101 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Zero Wing | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| Zoltan | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| albion-v1.0.1-wiz | `black` | 32.1 | 44 | ✓ | black-screen |  |
| Balloonacy_wiz_wip | `black` | 0.1 | 3 | ✓ | black-screen |  |
| CartoonWiz | `black` | 0.2 | 4 | ✓ | black-screen |  |
| chroma 1.01 v1 | `black` | 0.7 | 4 | – | black-screen |  |
| ColonyConflict_V1.1_B6 | `black` | 0.1 | 4 | ✓ | black-screen |  |
| DungeonRunner | `black` | 0.2 | 4 | ✓ | black-screen |  |
| DuoWIZ_Pong | `black` | 0.1 | 3 | ✓ | black-screen |  |
| freecell2x | `black` | 0.1 | 4 | ✓ | black-screen |  |
| March of the mini tux(wiz version) | `black` | 0.2 | 4 | ✓ | black-screen |  |
| opentyrian | `black` | 12.7 | 9 | – | black-screen |  |
| PPlane2.WIZ | `black` | 0.1 | 3 | ✓ | black-screen |  |
| SimOniZ | `black` | 0.2 | 4 | ✓ | black-screen |  |
| tetwizdownload | `black` | 0.2 | 4 | ✓ | black-screen |  |
| Trap75 | `black` | 61.0 | 1533 | ✓ | black-screen |  |
| TUcS.app(V0.7.0 - Wiz) | `black` | 0.1 | 4 | ✓ | black-screen |  |
| warcraft-beta3-wiz | `black` | 40.5 | 46 | ✓ | black-screen |  |
| wiz-car-binary_090818a | `black` | 60.6 | 1526 | ✓ | black-screen |  |
| Wiz_Blox | `black` | 0.2 | 4 | ✓ | black-screen |  |
| wiz_drench | `black` | 0.1 | 3 | ✓ | black-screen |  |
| WIZ_S4S | `black` | 0.2 | 4 | ✓ | black-screen |  |
| WizSticks | `black` | 0.1 | 4 | ✓ | mmio-spin | 0x1988 |
| xcom1-v1.0.2-wiz | `black` | 123.7 | 173 | ✓ | black-screen |  |
| xcom2-v1.0.1-wiz | `black` | 119.8 | 3072 | ✓ | black-screen |  |
| spout | `ingame` | 61.1 | 1538 | – | no-audio |  |
| Sudoku2X | `ingame` | 60.3 | 1521 | – | no-audio |  |
| wizchess-v1.1.0-bin | `ingame` | 60.7 | 1534 | – | no-audio |  |
| wizchess-v1.2.0-bin | `ingame` | 60.9 | 1537 | – | no-audio |  |
| wizgo-v1.1.0-bin | `ingame` | 60.9 | 1539 | – | no-audio |  |
| WizGolf | `ingame` | 60.6 | 1537 | – | no-audio |  |
| wizmancala-v1.1.2-bin | `ingame` | 60.9 | 1539 | – | no-audio |  |
| Worship Vector | `ingame` | 60.7 | 1527 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 160px; left and right halves are near- |
| AdamantArmorAffectionWiz | `playable` | 60.5 | 1532 | ✓ |  |  |
| airstrike-wiz-1.01 | `playable` | 60.8 | 1540 | ✓ |  |  |
| alexsfalldown | `playable` | 61.3 | 1540 | ✓ |  |  |
| Blix2x | `playable` | 61.3 | 1540 | ✓ |  |  |
| Dd2x | `playable` | 57.5 | 259 | ✓ |  |  |
| deicide3_eng | `playable` | 56.5 | 395 | ✓ |  |  |
| gr-v1001-wiz | `playable` | 59.0 | 1533 | ✓ |  |  |
| herknights | `playable` | 58.7 | 1523 | ✓ |  |  |
| hexen2 | `playable` | 58.0 | 1493 | ✓ |  |  |
| minos-gp2x-wiz | `playable` | 59.4 | 1501 | ✓ |  |  |
| mush_gp2x | `playable` | 48.2 | 1233 | ✓ |  |  |
| mush_gp2x-0 | `playable` | 36.8 | 973 | ✓ |  |  |
| Pentominos | `playable` | 61.5 | 1544 | ✓ |  |  |
| Pharaoh | `playable` | 50.7 | 107 | ✓ |  |  |
| PPlane | `playable` | 54.3 | 1377 | ✓ |  |  |
| PuzzleDevilWizDemo | `playable` | 57.7 | 1456 | ✓ |  |  |
| Rezerwar | `playable` | 52.0 | 493 | ✓ |  |  |
| rockrain-gp2x-wiz | `playable` | 61.0 | 1535 | ✓ |  |  |
| Sachunsung2 | `playable` | 50.9 | 109 | ✓ |  |  |
| scummvm-1.2.0-gp2xwiz | `playable` | 56.5 | 1482 | ✓ |  |  |
| Shanghai2 | `playable` | 52.2 | 112 | ✓ |  |  |
| Sopwith | `playable` | 61.1 | 1538 | ✓ |  |  |
| Sqdef_Wiz_14A | `playable` | 60.1 | 1536 | ✓ |  |  |
| Tail Tale | `playable` | 60.9 | 1530 | ✓ |  |  |
| wizznic-0.9.9-wiz | `playable` | 59.0 | 1536 | ✓ |  |  |
| wnw_demo | `playable` | 60.6 | 1532 | ✓ |  |  |
| xRick | `playable` | 60.5 | 1539 | ✓ |  |  |
| znumbers | `playable` | 51.4 | 109 | ✓ |  |  |

### Caanoo (205 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| 20110831 - Bomber Run Redux | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/20110831 - Bomber Run Redux/game/bomber_run_bennu |
| aaa | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| aaaa | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| Abbaye_caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Abbaye_caanoo_v3 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| aggressivepong-pre21.1-gph-uni | `incompatible` | 0.0 | 0 | – | no-frames |  |
| aimcaanoo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| animatch | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Animatch_titlebar | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/Animatch_titlebar' |
| apocalypso Caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ArtShotCaanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/ArtShotCaanoo/ArtShotCaanoo/ArtShot/ArtShotCaanoo |
| audiorace-v1.5-can | `incompatible` | 0.0 | 0 | – | no-frames |  |
| balls12_caanoo_bin | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| battlejewels-105-caanoo-beta | `incompatible` | 0.0 | 0 | – | no-frames |  |
| BermudaS_caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Blackjack21v1.1 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Blix2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| BubblePop (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/BubblePop (Caanoo)/BubblePop/BubblePop.gpe' is no |
| caanmines | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/caanmines' |
| caanoo-12swap-v1.0-bin | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Caanoo-Biniax2x_titlebar | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/Caanoo-Biniax2x_titlebar' |
| caanoo-gnurobbo-0.68 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| caanoo-tyrian-v1.1-bin | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| caanoo_tyrian_titlebar | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/caanoo_tyrian_titlebar' |
| can-zomb_3 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/can-zomb_3/bgd-zomb/zomb/bgd-zomb.gpe' is not an  |
| cgenius-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| chexquest-caanoo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| chexquest-titlebar | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/chexquest-titlebar' |
| cooldowncaanoo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Coral Sea (Caanoo - Bennu) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Coral Sea (Caanoo - Bennu)/coral_sea/coral_sea.gp |
| daff_s_adventure_2_caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Deadly Eye (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Deadly Eye (Caanoo)/DeadlyEye/DeadlyEye.gpe' is n |
| deadlyc | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| DefendorX_C | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/DefendorX_C/defendorx/bin/defendorx.gpe' is not a |
| deminor | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| demons | `incompatible` | 0.0 | 0 | – | no-frames |  |
| dynamate_c | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Echo V.1.3.2 (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Echo V.1.3.2 (Caanoo)/echo_game/echo_caanoo.gpe'  |
| echo_caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| EEEEK! EEEEEK! HOOOOOOK!!! | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/EEEEK! EEEEEK! HOOOOOOK!!!/eek/eek.gpe' is not an |
| EpicFreeFall | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| EpicFreeFall Caanoo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Firewhip-Caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Firewhip-Caanoo/firewhip/firewhip.gpe' is not an  |
| fleshchasmer | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| freedroid_Caanoo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| freeheroes2_c | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| fshark | `incompatible` | 0.0 | 0 | – | no-frames |  |
| fungp.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X Caanoo/fungp.zip' (exit 32512) |
| Geca Blaster 2 (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Geca Blaster 2 (Caanoo)/Geca Blaster 2 (Caanoo)/G |
| getstar | `incompatible` | 0.0 | 0 | – | no-frames |  |
| gnuRobbo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| gravityforcev2 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Hamster's Escape 3D (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Hamster's Escape 3D (Caanoo)/Hamster's Escape 3D  |
| HamstersEscape (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/HamstersEscape (Caanoo)/HamstersEscape (Caanoo)/H |
| Hardcore Fight (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Hardcore Fight (Caanoo)/HardcoreFight/HardcoreFig |
| hellfire | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Hero_The_Realm-DEMO | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| HeroTheRealm_DEMOv2 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| hexahop_1.0 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Humos-Caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ini and icon for wolf3d | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/ini and icon for wolf3d' |
| instead-1.6.1-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| jumpToTheMoon_c | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| kenlab-caanoo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| ketm | `incompatible` | 0.0 | 0 | – | no-frames |  |
| KOF (Ver. 5f) (Caanoo) | `incompatible` | 0.0 | 0 | – | no-frames |  |
| laserchess_c | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| liar.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X Caanoo/liar.zip' (exit 32512) |
| Liquid Counter.caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Liquid Counter.caanoo/liquidcount/liquidcount.gpe |
| lmission_0.5 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| MasteriesRunners (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/MasteriesRunners (Caanoo)/MasteriesRunners (Caano |
| meritous | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Metal Slug Zombies | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Metal Slug Zombies/msz/msz.gpe' is not an ARM ELF |
| Mission_faileD 1.2 [Caanoo] | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| monster | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| MrDrillux | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/MrDrillux' |
| mtknights | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| nlove_0.6.2_(beta)_caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| openjazz-caanoo | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| openttd_c | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| OperationFenix (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/OperationFenix (Caanoo)/OperationFenix/OperationF |
| outzone | `incompatible` | 0.0 | 0 | – | no-frames |  |
| PantaVsDragon (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/PantaVsDragon (Caanoo)/PantaVsDragon (Caanoo)/Pan |
| pengupop | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Pharaoh | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| powermanga-0.80 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| prboom-caanoo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| profanation_Caanoo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Protect&rescue | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| purito_cycling_1.5_Caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/purito_cycling_1.5_Caanoo/game/purito_cycling_1.5 |
| pushover-v0.2-bin | `incompatible` | 0.0 | 0 | – | no-frames |  |
| puzsion | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/puzsion/puzsion/puzsion.gpe' is not an ARM ELF an |
| PUZZLEBOARDS | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| quake1-caanoo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| QUAKE1.INI AND ICON SET | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/QUAKE1.INI AND ICON SET' |
| quake1_addons | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/quake1_addons' |
| quake1_build-20111024 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| quake2-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| RailroadRampage_1.2_Caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/RailroadRampage_1.2_Caanoo/RailroadRampage_Caanoo |
| reminiscence-v0.1.10-bin | `incompatible` | 0.0 | 0 | – | no-frames |  |
| rotate | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| runner-Caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| saaa_ext | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/saaa_ext' |
| Sachunsung2 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| sbt | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| sbtime_caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/sbtime_caanoo/sbtime/sbtime.gpe' is not an ARM EL |
| SDLLopan Banner and Icon | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/SDLLopan Banner and Icon' |
| sdllopan_v4-all | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| sdlquake_build-20111113-0 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Shanghai2 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Sitwell (Caanoo) | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Skull (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Skull (Caanoo)/Skull Game/Skull/Skull.gpe' is not |
| Slap | `incompatible` | 0.0 | 0 | – | no-frames |  |
| smallball | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/smallball/smallball/SmallBall.gpe' is not an ARM  |
| smallball-Caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/smallball-Caanoo/smallball/SmallBall.gpe' is not  |
| SnailRace_C | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/SnailRace_C/snailsrace/snailsrace.gpe' is not an  |
| snowbros | `incompatible` | 0.0 | 0 | – | no-frames |  |
| snowbros2 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| SOD(r181) | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| SORRv5_Caanoo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| space52_caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/space52_caanoo/space_52/space_52_caanoo.gpe' is n |
| stppc-caanoo-29-11-2010 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| supertux | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| the solitarie | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Tigerhell | `incompatible` | 0.0 | 0 | – | no-frames |  |
| tmw_v1.0.0-beta-2_caanoo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| tong-caanoo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Trap75 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Truxton | `incompatible` | 0.0 | 0 | – | no-frames |  |
| truxton2 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| twincobr | `incompatible` | 0.0 | 0 | – | no-frames |  |
| twinhawk | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Txishos (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Txishos (Caanoo)/Caanoo/Txishos/Txishos.gpe' is n |
| Vigo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| warcraft-beta3-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Wardner | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Wizznic 0.9.2- preview | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| wolf4sdl-caanoo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| wvector | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| xcom1-v1.0.2-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| xcom2-v1.0.1-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| xpiredcan.eng.101 | `incompatible` | 2.0 | 1 | ✓ | no-frames |  |
| zerowing | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zlocada-caanoo | `incompatible` | 0.0 | 0 | – | unimplemented-syscall | 281 (socket) |
| zombiesorbet_v1.0_caanoo | `incompatible` | 1.2 | 2 | ✓ | no-frames |  |
| zomg-Caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/zomg-Caanoo/Zomg/zomg.gpe' is not an ARM ELF and  |
| zsxd | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Zverealm-Caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Zverealm-Caanoo/Zverealm/Zverealm.gpe' is not an  |
| aquaVenture | `black` | 12.0 | 32 | – | black-screen |  |
| arcadevol1 | `black` | 5.9 | 7 | ✓ | black-screen |  |
| B'lox! | `black` | 41.2 | 405 | ✓ | black-screen |  |
| Balloonacy | `black` | 43.1 | 423 | ✓ | black-screen |  |
| Blingo | `black` | 35.6 | 99 | ✓ | black-screen |  |
| Blitz | `black` | 12.5 | 33 | – | black-screen |  |
| BubbleTrain | `black` | 0.9 | 2 | ✓ | black-screen |  |
| cat_trap | `black` | 12.7 | 33 | – | black-screen |  |
| Drench | `black` | 13.7 | 35 | ✓ | black-screen |  |
| Flappynerd_Caanoo | `black` | 11.8 | 33 | ✓ | black-screen |  |
| Geek_em_up_CAANOO | `black` | 8.1 | 230 | ✓ | black-screen |  |
| Guru Logic | `black` | 12.4 | 32 | ✓ | black-screen |  |
| JUMPNRUN | `black` | 57.6 | 1523 | ✓ | black-screen |  |
| MNV_Caanoo_Release1 | `black` | 43.1 | 422 | ✓ | black-screen |  |
| SantaMania | `black` | 12.8 | 33 | – | black-screen |  |
| STRATEGY | `black` | 28.9 | 18 | ✓ | black-screen |  |
| Tile | `black` | 54.4 | 1537 | ✓ | black-screen |  |
| Arcadevol3 | `ingame` | 59.2 | 1542 | – | flat-fill |  |
| caanoo-biniax2-v1.30-bin | `ingame` | 16.5 | 415 | ✓ | low-fps |  |
| caanoo-chess-v1.1.0-bin | `ingame` | 45.1 | 1138 | – | no-audio |  |
| caanoo-go-v1.1.0-bin | `ingame` | 45.1 | 1142 | – | no-audio |  |
| caanoo-mancala-v1.1.0-bin | `ingame` | 48.1 | 1214 | – | no-audio |  |
| gnp_104 | `ingame` | 45.9 | 1220 | ✓ | flat-fill |  |
| gr-v1001-caanoo | `ingame` | 10.1 | 264 | ✓ | low-fps |  |
| jump_n_blob_caanoo | `ingame` | 4.4 | 116 | ✓ | low-fps |  |
| knight | `ingame` | 10.5 | 264 | ✓ | flat-fill |  |
| Liar | `ingame` | 12.3 | 85 | ✓ | low-fps |  |
| llcpcls-caanoo | `ingame` | 15.9 | 34 | ✓ | low-fps |  |
| MISC | `ingame` | 59.6 | 1573 | – | no-audio |  |
| noiz2sa_caanoo | `ingame` | 16.7 | 128 | ✓ | flat-fill |  |
| nuclearchess | `ingame` | 3944.5 | 4164 | – | garbled-visuals | renders at 26x26 instead of 320x240 |
| powder | `ingame` | 54.2 | 1381 | – | no-audio |  |
| rg_ura_103 | `ingame` | 53.4 | 1404 | ✓ | flat-fill |  |
| smw_1.7 | `ingame` | 12.2 | 141 | ✓ | low-fps |  |
| tlosaf_v12-caanoo | `ingame` | 60.9 | 1533 | – | no-audio |  |
| zelda-roth-olb-3t_caanoo | `ingame` | 19.7 | 530 | ✓ | low-fps |  |
| ADVENTURE | `playable` | 57.3 | 1508 | ✓ |  |  |
| Amoebax | `playable` | 55.7 | 1418 | ✓ |  |  |
| Arcadevol2 | `playable` | 60.4 | 1550 | ✓ |  |  |
| cavestory | `playable` | 57.3 | 1556 | ✓ |  |  |
| ccrg-caanoo | `playable` | 45.9 | 198 | ✓ |  |  |
| cllwrth | `playable` | 26.9 | 681 | ✓ |  |  |
| DealorNoDeal | `playable` | 60.5 | 1552 | ✓ |  |  |
| Fywod_caanoo | `playable` | 55.4 | 1402 | ✓ |  |  |
| next_element | `playable` | 60.8 | 1534 | ✓ |  |  |
| pang | `playable` | 58.6 | 1502 | ✓ |  |  |
| propis | `playable` | 49.4 | 1245 | ✓ |  |  |
| RACING | `playable` | 59.0 | 1550 | ✓ |  |  |
| rhythmosplay_1.1.12 | `playable` | 51.0 | 1295 | ✓ |  |  |
| SHOOTERS | `playable` | 77.6 | 2098 | ✓ |  |  |
| SimOniZ | `playable` | 53.5 | 1373 | ✓ |  |  |
| SPORTS | `playable` | 59.6 | 1543 | ✓ |  |  |
| sqrxz-v0996-caanoo | `playable` | 55.2 | 1407 | ✓ |  |  |
| sqrxz2-v0.80-caanoo | `playable` | 60.6 | 1540 | ✓ |  |  |
| tailtale4c | `playable` | 61.5 | 1548 | ✓ |  |  |
| warehouse_panic_v1.1_caanoo | `playable` | 34.8 | 461 | ✓ |  |  |
| WindandWater | `playable` | 60.2 | 1524 | ✓ |  |  |
