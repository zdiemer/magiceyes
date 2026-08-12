# magiceyes compatibility sweep

Every GP2X / Wiz / Caanoo title on the corpus share, booted headlessly through the native engine (`bin/me_unicorn`) and scored from the shm framebuffer + the structured run report. Regenerate with `tools/test/compat_report.py`.


## Summary

| Platform | Titles | Playable | Ingame | Black | Incompatible | Crashed |
|---|--:|--:|--:|--:|--:|--:|
| GP2X | 673 | 209 | 91 | 134 | 239 | 0 |
| Wiz | 153 | 25 | 8 | 25 | 95 | 0 |
| Caanoo | 205 | 18 | 24 | 15 | 148 | 0 |
| **All** | **1031** | **252** | **123** | **174** | **482** | **0** |

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
| **Boots but renders only black** (`black-screen`) | 174 | Caanoo, GP2X, Wiz | n/a |
| **Never rendered a frame (cause unknown)** (`no-frames`) | 157 | Caanoo, GP2X, Wiz | n/a |
| **Game data files are missing from the dump** (`missing-game-data`) | 106 | Caanoo, GP2X, Wiz | n/a |
| **Not a 32-bit ARM ELF** (`not-arm-elf`) | 99 | Caanoo, GP2X, Wiz | n/a |
| **Renders at speed but no audio** (`no-audio`) | 66 | Caanoo, GP2X, Wiz | n/a |
| **No .gpe in the dump** (`no-executable`) | 59 | Caanoo, GP2X, Wiz | n/a |
| **Spins forever polling an MMSP2 register** (`mmio-spin`) | 27 | Caanoo, GP2X, Wiz | `0x90a` ×25, `0x4000` ×1 |
| **Unknown /dev node** (`unknown-device`) | 26 | Caanoo, GP2X, Wiz | `/dev/input/mouse/0` ×14, `/dev/null` ×4, `/dev/input/mouse0` ×3, `/dev/` ×2 |
| **Draws only a flat colour** (`flat-fill`) | 24 | Caanoo, GP2X | n/a |
| **Renders but below 25 fps** (`low-fps`) | 17 | Caanoo, GP2X | n/a |
| **Renders, but the picture is wrong** (`garbled-visuals`) | 15 | Caanoo, GP2X, Wiz | n/a |
| **Archive extraction failed** (`archive-failed`) | 5 | Caanoo, GP2X | n/a |
| **Unimplemented syscall** (`unimplemented-syscall`) | 3 | Caanoo, GP2X | `281 (socket)` ×1, `113` ×1, `117` ×1 |
| **Could not open a display** (`display-init-failed`) | 1 | GP2X | n/a |

## Renders, but the picture is wrong

These 15 titles pass the running checks (frames advancing, frame rate, audio) while the frame itself is visibly broken, so they are graded `ingame` rather than `playable`. The reasons come from measuring the captured frame: a consistent per-row offset means a stride/pitch mismatch, large-scale repetition means the screen holds more than one copy of itself, and noise far above what dithered artwork reaches means corrupt memory.

| Title | Platform | fps | What the frame looks like |
|---|---|--:|---|
| nuclearchess | Caanoo | 3735.5 | renders at 26x26 instead of 320x240 |
| 1945_GP2X_0.2b | GP2X | 57.1 | content repeats every 160px across the screen; left and right halves are near-identical; top and bottom halves are near-identical; pixel-to-pixel noise of 96, far above what dithered artwork reaches; the frame looks like corrupt memory |
| 2xtron-v01 | GP2X | 61.1 | content repeats every 84px across the screen |
| Beatbox_1.2 | GP2X | 56.7 | content repeats every 160px across the screen; left and right halves are near-identical |
| BunnyTraps-v11 | GP2X | 60.6 | pixel-to-pixel noise of 89, far above what dithered artwork reaches; the frame looks like corrupt memory |
| CascadeBeneath v1.0 for GP2X | GP2X | 62.2 | content repeats every 84px across the screen |
| DontGetCrushed v1.0 for GP2X | GP2X | 61.3 | content repeats every 84px across the screen |
| FleshChasmer | GP2X | 58.5 | content repeats every 160px across the screen; left and right halves are near-identical |
| fvc | GP2X | 59.7 | content repeats every 100px across the screen; left and right halves are near-identical; top and bottom halves are near-identical |
| gemdrop2x_v02 | GP2X | 63.1 | content repeats every 96px across the screen |
| Life.0.1 | GP2X | 61.5 | pixel-to-pixel noise of 91, far above what dithered artwork reaches; the frame looks like corrupt memory |
| MoveSweep2X | GP2X | 48.0 | content repeats every 96px across the screen; left and right halves are near-identical |
| scummvm-kor0.4.2cvs | GP2X | 59.5 | content repeats every 104px across the screen; left and right halves are near-identical |
| waffle2x | GP2X | 42.3 | content repeats every 160px across the screen; left and right halves are near-identical; top and bottom halves are near-identical |
| Worship Vector | Wiz | 57.9 | content repeats every 160px across the screen |

## Scored as working, but only painting a flat colour

These 24 titles advanced frames, kept audio running, and held frame rate, so they land in `playable`/`renders`. Their framebuffer never held more than one or two colours, which means the tier overstates them. Worth treating as broken.

| Title | Platform | Status | fps |
|---|---|---|--:|
| Balloonacy | Caanoo | `renders` | 0.1 |
| Blitz | Caanoo | `renders` | 0.1 |
| cat_trap | Caanoo | `renders` | 0.1 |
| Drench | Caanoo | `renders` | 0.1 |
| Geek_em_up_CAANOO | Caanoo | `renders` | 0.1 |
| gnp_104 | Caanoo | `playable` | 53.9 |
| knight | Caanoo | `renders` | 10.2 |
| MNV_Caanoo_Release1 | Caanoo | `playable` | 40.1 |
| noiz2sa_caanoo | Caanoo | `renders` | 16.8 |
| rg_ura_103 | Caanoo | `playable` | 51.6 |
| SantaMania | Caanoo | `renders` | 0.1 |
| alex | GP2X | `renders` | 60.3 |
| alex4_gp2x | GP2X | `renders` | 60.1 |
| ASCIIPong2xV0.4 | GP2X | `playable` | 35.6 |
| ConnyCarrot | GP2X | `playable` | 59.5 |
| dumbbell2x-01 | GP2X | `renders` | 58.8 |
| HumphreyGP2X | GP2X | `playable` | 58.2 |
| kampfimall-gp2x | GP2X | `renders` | 59.9 |
| kampfimall-gp2x-music | GP2X | `renders` | 60.8 |
| Knight Lore | GP2X | `renders` | 10.5 |
| las-tres-luces-de-glaurung-remake | GP2X | `playable` | 59.8 |
| Pond2X | GP2X | `renders` | 59.6 |
| robot-escape | GP2X | `playable` | 82.4 |
| StairwayToHeaven | GP2X | `playable` | 59.3 |

## Cross-title blockers


### Unimplemented syscalls

| Item | Titles |
|---|--:|
| `163 (mremap)` | 21 |
| `97 (setpriority)` | 5 |
| `43 (times)` | 4 |
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
| `Failed loading libpng.so.3: /lib/libpng.so.3: undefined symbol: inflateResetcar` | 1 |
| `storage::Surfaces:  Failed loading libpng.so.3: /lib/libpng.so.3: undefined sym` | 1 |

### Unknown /dev nodes

| Item | Titles |
|---|--:|
| `/dev/input/mouse/0` | 208 |
| `/dev/psaux` | 181 |
| `/dev/usbmouse` | 181 |
| `/dev/touchscreen/wm97xx` | 79 |
| `/dev/null` | 69 |
| `/dev/sequencer` | 21 |
| `/dev/input/mouse0` | 18 |
| `/dev/accel` | 6 |
| `/dev/` | 2 |
| `/dev/pts/` | 2 |
| `/dev/input/mice` | 2 |
| `/dev/mouse` | 2 |
| `/dev/batt` | 2 |
| `/dev/ptmx` | 1 |
| `/dev/ptyp0` | 1 |
| `/dev/cx25874` | 1 |
| `/dev/graphics/fb0` | 1 |
| `/dev/gpmdata` | 1 |
| `/dev/pollux_batt` | 1 |
| `/dev/adbmouse` | 1 |

### Quirks (ran, but not fully honoured)

| Item | Titles |
|---|--:|
| `unknown_mmio:0x90a` | 409 |
| `unknown_ioctl:fb` | 172 |
| `unknown_mmio:0x4058` | 148 |
| `unknown_mmio:0x405c` | 143 |
| `unknown_mmio:0x4060` | 143 |
| `unknown_mmio:0x910` | 102 |
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
| `unknown_mmio:0x1988` | 10 |
| `unknown_mmio:0x19c0` | 10 |
| `unknown_mmio:0x19c4` | 10 |
| `unknown_mmio:0xfde00910` | 9 |
| `unknown_mmio:0xfde0091c` | 9 |
| `unknown_mmio:0xfde00924` | 9 |
| `unknown_mmio:0xffaf0910` | 9 |
| `unknown_mmio:0xffaf091c` | 9 |
| `unknown_mmio:0xffaf0924` | 9 |

## Per-title results


### GP2X (673 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| 2xHexen2 v0.05 PB2 | `incompatible` | 1.2 | 1 | – | no-frames |  |
| 2xHexen2_cheat_patch | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/2xHexen2_cheat_patch' |
| 2xquake003 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| 2xquake2 | `incompatible` | 0.0 | 0 | ✓ | missing-game-data |  |
| 2XRally01 | `incompatible` | 0.0 | 0 | – | display-init-failed |  |
| 2xZdoom_PB1.2 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| 4WE_GP2x | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| 9 Lives | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| A1GP2XV1_1 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/ |
| abduction | `incompatible` | 0.0 | 0 | – | no-frames |  |
| abe | `incompatible` | 57.6 | 1453 | ✓ | no-frames |  |
| abuse_1.0 | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/ |
| ADIC2X | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| airpong4GP2X0.0.4 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/airpong4GP2X0.0.4/airpong022/src/AirPong.gpe' is not an  |
| albion-v1.0.1-gp2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse0 |
| Alex's Falldown | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| AlienZ | `incompatible` | 0.0 | 0 | – | no-frames |  |
| animatch_v1.2 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/animatch_v1.2' |
| animatch_v1.2.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/animatch_v1.2.zip' (exit 32512) |
| AnotherGame2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/AnotherGame2x/AnotherGame2x/anothergame2x.gpe' is not an |
| atris-1.0.7 | `incompatible` | 62.0 | 1607 | ✓ | no-frames |  |
| B'lox! | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| balluz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/balluz/balluz/balluz.gpe' is not an ARM ELF and no runna |
| battlejewels-gp2x-062-100 | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| beat2x-05 source | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-05 source' |
| beat2x-pack-C64 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-C64' |
| beat2x-pack-ccMixter | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-ccMixter' |
| beat2x-pack-magnatune-electronica | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-magnatune-electronica' |
| beat2x-pack-miniMaximum | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-miniMaximum' |
| beat2x-pack-tutorial | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-tutorial' |
| beat2x-pack-urban | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-urban' |
| BermudaS_gp2x | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| BisfoG | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| blastriot1.21 | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Blix2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| blockoid | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| blocksGP2X-0 | `incompatible` | 0.0 | 0 | – | unimplemented-syscall | 113 |
| blox | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Bombs Panic | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| Boomshine2x_(java) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Boomshine2x_(java)/Boomshine2x/Boomshine2x.gpe' is not a |
| bunkermaster2x04 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Butterfly | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Butterfly' |
| cackb2 | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| CaptainCrusader_GP2XDemo | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| CaptainCrusader_GP2XFull | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| cdogs2x04 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Chess2xSkins | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Chess2xSkins' |
| chicken-puyopuyo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Chroma | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| Classical | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Classical' |
| CloneKeen2X-1.0a | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Codemaster | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| crocodingusgp2x | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| d1x-rebirth-gp2x_v0.50a | `incompatible` | 0.0 | 0 | – | unimplemented-syscall | 117 |
| DangerMouse | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| DeathChase4GP2X-V0.1b | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/DeathChase4GP2X-V0.1b/deathchase3d-0.9/deathchase3d/Deat |
| default | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/default' |
| diamant_1_01 | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| dkbk2x-0.1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| doom | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/doom/doom/10sector.gpe' is not an ARM ELF and no runnabl |
| doom_mod_examples | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/doom_mod_examples/game/interpreters/doom/pwad1/prboom_gm |
| DoomPwadPack | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/DoomPwadPack/AliensTC.gpe' is not an ARM ELF and no runn |
| dosfsck-gp2x-2.11 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/dosfsck-gp2x-2.11' |
| Dr. Mates v1.0 | `incompatible` | 39.4 | 96 | ✓ | no-frames |  |
| duckmaze-gp2x-0.1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/duckmaze-gp2x-0.1/duckmaze-gp2x-0.1/duckmaze.gpe' is not |
| duke2x004 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| duke3d_cheat_patch | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/duke3d_cheat_patch' |
| dyc_gp2x | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| E-Fighters2x_FIRST_ALPHA_0_0_5_fixedSound | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| EasterQuest | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| exultb4-src | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/exultb4-src' |
| FFDoom | `incompatible` | 0.0 | 0 | – | no-frames |  |
| FindMii | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Fire | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Flappynerd_GP2X | `incompatible` | 0.0 | 1 | – | no-frames |  |
| FleshChasmer Zero (English Patch) | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/FleshChasmer Zero (English Patch)' |
| FlipIR_GP2X | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| Football2X | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Fore_1_0 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Fore_1_0' |
| FP_Default_2.0 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/FP_Default_2.0' |
| freec2x | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| freedroid2x06 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/freedroid2x06/Freedroid/FreeDroid.gpe' is not an ARM ELF |
| frotz | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/frotz' |
| FullBoard (test ver.) | `incompatible` | 49.6 | 112 | ✓ | no-frames |  |
| garden2x02 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Geek 'em up GP2X | `incompatible` | 0.0 | 1 | – | no-frames |  |
| GeneralPromise | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| geoQuiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/geoQuiz/geoQuiz.gpe' is not an ARM ELF and no runnable b |
| glouton | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| gnurobbo_0.66_open2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| gorillaz | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| gp2x-abrick-0.1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/gp2x-abrick-0.1' |
| gp2x-blobwars-0.1 | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| gp2x-rogue-v1.0 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| gp2x-tenmado-0.1 | `incompatible` | 0.0 | 1 | – | no-frames |  |
| gp2x-tong-v1 | `incompatible` | 0.0 | 1 | – | no-frames |  |
| gp2x_drench | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| GP2X_Nat2007 | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| GP2X_TLI | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| gp2xbug | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| gp2xlib | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/gp2xlib' |
| gp2xninjas-v06 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/gp2xninjas-v06/Ninjas v0.6 Final GP2X/ninjas.gpe' is not |
| GPgeneral | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
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
| just4qix | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse0 |
| Klaur | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| kobo_deluxe_beta1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| KQ2X_v3 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Laser2xVers10 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Lexeme | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| lights-out | `incompatible` | 0.0 | 0 | – | mmio-spin |  |
| Liquid Counter.gp2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Liquid Counter.gp2x/LiquidCount/LiquidCount.gpe' is not  |
| liquidwar2x02 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Logoball | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Lottys_Lines.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/Lottys_Lines.zip' (exit 32512) |
| lumix-beta-01 | `incompatible` | 59.7 | 1505 | – | no-frames |  |
| March of the mini tux | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| mariodm | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/mariodm' |
| Marte Necesita Vacas GP2X | `incompatible` | 0.0 | 1 | – | no-frames |  |
| MazezaMGP2X | `incompatible` | 0.0 | 1 | – | no-frames |  |
| memory | `incompatible` | 0.0 | 0 | – | mmio-spin | 0x90a |
| Midnight2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Midnight2x/dosbox/midnight/midnight.gpe' is not an ARM E |
| misterhachi | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/misterhachi/misterhachi/misterhachi.gpe' is not an ARM E |
| mopesnake-gp2x-0.5 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/mopesnake-gp2x-0.5/mopesnake-gp2x-0.5/mopesnake.gpe' is  |
| MouthTrap | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| mueppv32 | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| nethack-ascii-3.4.3port1 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| nethack-caduhack.r01 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| nethack06 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| noiz2saV3 | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Odonata | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Odonata' |
| ohthehumanity-1.0.0 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/ohthehumanity-1.0.0/ohthehumanity/ohthehumanity.gpe' is  |
| onscripter2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| OpenTTD | `incompatible` | 0.0 | 0 | – | no-frames |  |
| opposite_lock | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/opposite_lock/opposite_lock/opposite_lock.gpe' is not an |
| ozgur-hanoi-0.2-kelebek | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/ozgur-hanoi-0.2-kelebek' |
| pacmame | `incompatible` | 0.0 | 0 | – | no-frames |  |
| PantaVsDragon (Gp2x F-100 F-200) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/PantaVsDragon (Gp2x F-100 F-200)/PantaVsDragon (Gp2x F-1 |
| para | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/para' |
| Payback_v1_1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Payback_v1_1' |
| PaybackMusicManager | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/PaybackMusicManager' |
| pc | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Pentominos | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| Peuppy_10_GP2X | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| Phantomas1.8X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Pipes2_0 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Pipes2_0/Pipes/Pipes.gpe' is not an ARM ELF and no runna |
| Pipes_v2.1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Pipes_v2.1/Pipes/Pipes.gpe' is not an ARM ELF and no run |
| Poker_Gp2Xv1.0 | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| PPlane | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| PPlane2.GP2X | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| PrBoom PWAD pack | `incompatible` | 0.0 | 0 | – | no-frames |  |
| puckman_gp2x | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| pykaraoke-0.6-gp2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| pySlide | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/pySlide/pySlide/pySlide.gpe' is not an ARM ELF and no ru |
| pyTetris | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/pyTetris/pyTetris/pyTetris.gpe' is not an ARM ELF and no |
| Quad | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
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
| Sachunsung2_1 | `incompatible` | 50.0 | 107 | ✓ | no-frames |  |
| santaMania | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| scummvm-alpha-8a_sky | `incompatible` | 0.0 | 0 | – | no-frames |  |
| scummVMsaves | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/scummVMsaves' |
| sdlscav_gp2x_0.2.0 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ShadowWarrior2X | `incompatible` | 1.7 | 1 | – | no-frames |  |
| Shangai v2 | `incompatible` | 48.3 | 103 | ✓ | no-frames |  |
| shoveit | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/shoveit' |
| Simon2X | `incompatible` | 5.9 | 6 | – | no-frames |  |
| Skin1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Skin1' |
| smw-1.6_gp2x | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/null |
| snakepan | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/snakepan/Snakepan.gpe' is not an ARM ELF and no runnable |
| SnoodForTileMatch | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/SnoodForTileMatch' |
| snowedin6_v1-00_gp2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/graphics/fb0 |
| sopwith_camel_rc3 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/null |
| space52_gp2x(oficial) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/space52_gp2x(oficial)/space_52/space_52_gp2x.gpe' is not |
| space52_gp2x(open2x) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/space52_gp2x(open2x)/space_52/space_52_gp2x.gpe' is not  |
| SpaceSnake | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
| spacestorm | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Sqdef 1.4 | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| squaregame2xV1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/squaregame2xV1/squaregame2x.gpe' is not an ARM ELF and n |
| Starship Soldier.gp2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Starship Soldier.gp2x/StarshipSoldier/starship_soldier.g |
| stppc2x-v1.1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/stppc2x-v1.1' |
| stppc2x-v1.1.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/stppc2x-v1.1.zip' (exit 32512) |
| strife | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/strife/dosbox/strife/strife.gpe' is not an ARM ELF and n |
| Supa2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Supa2x/dosbox/supaplex.gpe' is not an ARM ELF and no run |
| testmem2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/testmem2x/testmem2x/testmem2x.gpe' is not an ARM ELF and |
| TouchGames | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Trap75 | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| ttd2x_020108 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| TUcS.app(V0.7.0 - GP2X) | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| tunar-1.1.0 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/tunar-1.1.0/tunar/tunar.gpe' is not an ARM ELF and no ru |
| TurnOn | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/TurnOn' |
| Tux_Strikes_Back | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Tux_Strikes_Back' |
| Txishos (Gp2x F-200) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Txishos (Gp2x F-200)/Gp2x F-200/Txishos/Txishos.gpe' is  |
| UBPGP2x | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/UBPGP2x' |
| uhexen | `incompatible` | 0.0 | 0 | – | no-frames |  |
| uqm-0.4.2-content.uqm | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/uqm-0.4.2-content.uqm' |
| uqm-0.5.0-r1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| uqm2x_langpack_v1.2 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/uqm2x_langpack_v1.2/uqm2xfin.gpe' is not an ARM ELF and  |
| uqm2x_release_1.1 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| uqm2x_remixpack_1.1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/uqm2x_remixpack_1.1/uqm2xrmx.gpe' is not an ARM ELF and  |
| UQMgp2x-0.5.0_with_content | `incompatible` | 0.0 | 0 | – | no-frames |  |
| WADFEST | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/WADFEST' |
| wads1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/wads1/wads1/requiem.gpe' is not an ARM ELF and no runnab |
| wads2 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/wads2/wads2/h2h-xmas.gpe' is not an ARM ELF and no runna |
| warcraft-beta3-gp2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse0 |
| Winter_Jumper | `incompatible` | 0.0 | 0 | – | no-frames |  |
| wizznic06_NES_30levels | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/wizznic06_NES_30levels' |
| Wolf4SDL | `incompatible` | 0.0 | 0 | – | no-frames |  |
| worminator302 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zcgp2x_211B18_0.4alpha | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/gpmdata |
| Znumbers | `incompatible` | 49.9 | 107 | ✓ | no-frames |  |
| Zombiepox2X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zombiesorbet_v1.0_gp2x | `incompatible` | 58.5 | 1514 | ✓ | no-frames |  |
| zooov11 | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| 2xWargus_PB1.3 | `black` | 42.8 | 1121 | – | black-screen |  |
| _-The Reversed Preacher 3-_Hack bIld_ | `black` | 58.8 | 1488 | ✓ | black-screen |  |
| _-the reversed preacher II-_ | `black` | 58.0 | 1464 | ✓ | black-screen |  |
| AbusimbelProfanationDeluxe | `black` | 59.6 | 1512 | ✓ | black-screen |  |
| AfterBurner-GP2X | `black` | 59.2 | 1513 | – | black-screen |  |
| Airplyr | `black` | 60.0 | 1516 | – | black-screen |  |
| AlienBlaster_1.02 | `black` | 57.8 | 1508 | ✓ | black-screen |  |
| altitude | `black` | 173.5 | 116 | ✓ | black-screen |  |
| angband2x-v2 | `black` | 34.2 | 73 | – | black-screen |  |
| bang_gp | `black` | 22.0 | 22 | ✓ | black-screen |  |
| BareFistFighter | `black` | 57.8 | 1460 | ✓ | black-screen |  |
| barrage | `black` | 60.7 | 1539 | – | black-screen |  |
| BearOids | `black` | 58.7 | 1474 | ✓ | black-screen |  |
| BeetleRun | `black` | 59.8 | 1505 | – | black-screen |  |
| Biohazard2 | `black` | 60.4 | 1527 | – | black-screen |  |
| blingo 1.2 | `black` | 35.8 | 115 | ✓ | black-screen |  |
| Bloxz_DEMO | `black` | 59.8 | 1518 | – | black-screen |  |
| bobtron-gp2x | `black` | 59.9 | 1516 | ✓ | black-screen |  |
| Boulders-0 | `black` | 11.6 | 7 | ✓ | black-screen |  |
| BubbleTrain_GP2X-2006_Entry | `black` | 1.0 | 2 | ✓ | black-screen |  |
| cat_trap | `black` | 0.2 | 4 | ✓ | black-screen |  |
| cgenius-gp2x | `black` | 57.3 | 1490 | – | black-screen |  |
| ChopperAttackv1.0.17 | `black` | 0.1 | 3 | ✓ | black-screen |  |
| Comando2gp2xEN | `black` | 57.9 | 1459 | – | black-screen |  |
| coppergreen | `black` | 7.0 | 8 | ✓ | black-screen |  |
| cosmo2x_01 | `black` | 60.2 | 1528 | – | black-screen |  |
| crossroads | `black` | 60.5 | 1524 | – | black-screen |  |
| CUBES | `black` | 60.5 | 1528 | – | black-screen |  |
| cyberhockeyV2_6 | `black` | 58.8 | 1482 | ✓ | black-screen |  |
| d2x-gp2x-0.02 | `black` | 8.5 | 6 | ✓ | black-screen |  |
| Dance2x Alpha GPE | `black` | 59.6 | 1516 | – | black-screen |  |
| Dark_Light_SDL2X | `black` | 60.1 | 1529 | – | black-screen |  |
| DealOrNoDeal-v12 | `black` | 59.2 | 1521 | – | black-screen |  |
| DeathTrap1_1 | `black` | 55.6 | 1471 | – | black-screen |  |
| dodge | `black` | 13.0 | 13 | ✓ | black-screen |  |
| egoboo-cramfs | `black` | 29.9 | 64 | ✓ | black-screen |  |
| escapa-v1 | `black` | 60.1 | 1515 | – | black-screen |  |
| falldown_gp2x | `black` | 60.9 | 1529 | ✓ | black-screen |  |
| fenix | `black` | 11.3 | 23 | ✓ | black-screen |  |
| fenixGamePack | `black` | 14.3 | 48 | ✓ | black-screen |  |
| fifteen_01 | `black` | 60.1 | 1527 | – | black-screen |  |
| FleshChasmer132c_patch | `black` | 7.9 | 4 | – | black-screen |  |
| FleshChasmer_Dpad | `black` | 11.9 | 6 | ✓ | black-screen |  |
| floaters | `black` | 60.0 | 1520 | – | black-screen |  |
| Fragger2x | `black` | 59.2 | 1495 | ✓ | black-screen |  |
| freecell_1 | `black` | 51.8 | 110 | ✓ | black-screen |  |
| fruits_gp2x | `black` | 59.2 | 1500 | – | black-screen |  |
| FyWod_2x | `black` | 59.6 | 1531 | – | black-screen |  |
| game bIld 2 | `black` | 58.5 | 1476 | ✓ | black-screen |  |
| game-watch-mario-bros | `black` | 58.7 | 1485 | ✓ | black-screen |  |
| Ghostbusters_WIP | `black` | 58.9 | 1490 | – | black-screen |  |
| gnp_104 | `black` | 54.4 | 1499 | – | black-screen |  |
| godori | `black` | 8.0 | 4 | – | black-screen |  |
| gp2x-bubbletrain-0.1 | `black` | 58.3 | 1523 | – | black-screen |  |
| gp2x-formido-0.1 | `black` | 41.3 | 1519 | ✓ | black-screen |  |
| gp2x-netrok-0.1 | `black` | 51.4 | 1541 | – | black-screen |  |
| gp2x-sand-0.3 | `black` | 53.4 | 114 | – | black-screen |  |
| gp2xDoukutsu-1.04 | `black` | 9.9 | 9 | ✓ | black-screen |  |
| gp2xJenkasNightmare | `black` | 12.4 | 10 | ✓ | black-screen |  |
| gpnoid2x | `black` | 18.8 | 17 | ✓ | black-screen |  |
| GPrina-GP2x_v1.0 | `black` | 57.0 | 1466 | ✓ | black-screen |  |
| gr-v1001-gp2x | `black` | 58.2 | 1512 | – | black-screen |  |
| hex-a-hop | `black` | 60.5 | 1526 | – | black-screen |  |
| Jurlx2 | `black` | 59.6 | 1508 | – | black-screen |  |
| koules2x_02 | `black` | 60.0 | 1533 | – | black-screen |  |
| levelEdit | `black` | 60.7 | 1532 | – | black-screen |  |
| MAME-N22_51 | `black` | 56.7 | 1503 | ✓ | black-screen |  |
| mancala-v1.0.1 | `black` | 59.3 | 1510 | – | black-screen |  |
| monacoGP | `black` | 60.2 | 1542 | – | black-screen |  |
| moonlander | `black` | 13.6 | 18 | ✓ | black-screen |  |
| nanobounce-pacc-gp2x | `black` | 57.2 | 1507 | – | black-screen |  |
| nazcarunners-0 | `black` | 21.5 | 22 | ✓ | black-screen |  |
| nazcasphere | `black` | 13.7 | 11 | ✓ | black-screen |  |
| ne_deluxe_gp2x | `black` | 60.2 | 1525 | – | black-screen |  |
| ne_gp2x | `black` | 58.2 | 1467 | – | black-screen |  |
| NecNec2x | `black` | 58.8 | 1500 | ✓ | black-screen |  |
| Nom | `black` | 4.0 | 2 | – | black-screen |  |
| omok | `black` | 48.7 | 104 | ✓ | black-screen |  |
| OpenBOR_v2.1933 | `black` | 59.1 | 1516 | – | black-screen |  |
| OpenBOR_v3.0_Build_2615_&_2637 | `black` | 58.7 | 1505 | – | black-screen |  |
| openggs | `black` | 58.7 | 1504 | ✓ | black-screen |  |
| openjazz-gp2x | `black` | 9.8 | 11 | ✓ | black-screen |  |
| othello_v1.0 | `black` | 57.2 | 1441 | ✓ | black-screen |  |
| oxov06 | `black` | 42.4 | 92 | – | black-screen |  |
| para3 | `black` | 58.6 | 1483 | ✓ | black-screen |  |
| pez | `black` | 10.0 | 5 | – | black-screen |  |
| Pong | `black` | 59.5 | 1500 | – | black-screen |  |
| PowerSlide | `black` | 59.1 | 1505 | ✓ | black-screen |  |
| protozoa v1.0 | `black` | 4.4 | 4 | ✓ | black-screen |  |
| Rabbit_vs_Flies_0.9 | `black` | 56.6 | 1436 | ✓ | black-screen |  |
| ramon atacks | `black` | 59.2 | 1494 | ✓ | black-screen |  |
| raw2xv0.3.1 | `black` | 9.8 | 5 | – | black-screen |  |
| rg_105 | `black` | 56.6 | 1515 | – | black-screen |  |
| rg_ura_103 | `black` | 52.3 | 1420 | – | black-screen |  |
| RoundEmUp-alpha3 | `black` | 59.3 | 1503 | – | black-screen |  |
| SimOniZ | `black` | 0.2 | 4 | ✓ | black-screen |  |
| SmashGp2x02 | `black` | 55.3 | 1404 | ✓ | black-screen |  |
| snail runers | `black` | 58.3 | 1497 | ✓ | black-screen |  |
| sprint_race | `black` | 8.7 | 7 | – | black-screen |  |
| Squares-v051 | `black` | 59.1 | 1509 | – | black-screen |  |
| starfighter-gp2x-0.01 | `black` | 59.0 | 1486 | ✓ | black-screen |  |
| starsystem | `black` | 9.9 | 6 | ✓ | black-screen |  |
| StarTrucker | `black` | 59.3 | 1505 | – | black-screen |  |
| step2x02 | `black` | 49.3 | 105 | ✓ | black-screen |  |
| SuperChickenFallDemo | `black` | 59.4 | 1500 | ✓ | black-screen |  |
| superpang | `black` | 36.4 | 97 | ✓ | black-screen |  |
| supertux-0.1.3-gp2x-v4 | `black` | 52.3 | 1461 | ✓ | black-screen |  |
| tesla-Siren | `black` | 14.1 | 10 | ✓ | black-screen |  |
| Tetrablocks.0.4.GP2X | `black` | 44.1 | 104 | ✓ | black-screen |  |
| the reversed preacher II | `black` | 59.4 | 1500 | ✓ | black-screen |  |
| Thruster_GP2X | `black` | 60.4 | 1532 | – | black-screen |  |
| tileworld2x | `black` | 54.6 | 1469 | – | black-screen |  |
| tilt | `black` | 18.4 | 13 | ✓ | black-screen |  |
| tower | `black` | 60.1 | 1523 | – | black-screen |  |
| TRAINS | `black` | 6.6 | 4 | ✓ | black-screen |  |
| ttxbeta170706b | `black` | 56.5 | 1518 | – | black-screen |  |
| ultratumba_exp-20100925.gp2x | `black` | 9.8 | 7 | ✓ | black-screen |  |
| Unicolor | `black` | 60.0 | 1534 | – | black-screen |  |
| Volleyball | `black` | 59.9 | 1518 | – | black-screen |  |
| vorton-b4 | `black` | 57.9 | 1509 | ✓ | black-screen |  |
| vwars | `black` | 58.5 | 1517 | ✓ | black-screen |  |
| war_and_warriorgp2x | `black` | 60.4 | 1525 | – | black-screen |  |
| whacky | `black` | 58.1 | 1475 | – | black-screen |  |
| wire3d | `black` | 57.0 | 1502 | – | black-screen |  |
| Wizznic_2x_07alpha2 | `black` | 9.9 | 19 | ✓ | black-screen |  |
| wizznic_gp2x-0.9.9 | `black` | 7.9 | 13 | ✓ | black-screen |  |
| wolfdx | `black` | 42.7 | 53 | ✓ | black-screen |  |
| xbak-0.1.3 | `black` | 18.1 | 11 | – | black-screen |  |
| xcom1-v1.0.2-gp2x | `black` | 42.1 | 1560 | ✓ | black-screen |  |
| xcom2-v1.0.1-gp2x | `black` | 101.4 | 2608 | ✓ | black-screen |  |
| xigon-X-gp2x-V1 | `black` | 59.7 | 1508 | ✓ | black-screen |  |
| xump2x_beta2 | `black` | 12.0 | 6 | – | black-screen |  |
| yahtzee-v21 | `black` | 59.7 | 1510 | – | black-screen |  |
| Zelda_roth_US_gp2x | `black` | 21.0 | 47 | ✓ | black-screen |  |
| 1945_GP2X_0.2b | `ingame` | 57.1 | 400 | ✓ | garbled-visuals | content repeats every 160px across the screen; left and right halves are near-identical; t |
| 2xtron-v01 | `ingame` | 61.1 | 1542 | ✓ | garbled-visuals | content repeats every 84px across the screen |
| 2xZdoom_selector | `ingame` | 2.1 | 5 | ✓ | low-fps |  |
| a_sn-pong | `ingame` | 38.8 | 1437 | – | no-audio |  |
| alex | `ingame` | 60.3 | 1522 | – | flat-fill |  |
| alex4_gp2x | `ingame` | 60.1 | 1518 | – | flat-fill |  |
| AMazing-3D | `ingame` | 63.0 | 1594 | – | no-audio |  |
| ASCIIPong2xV0.4 | `ingame` | 35.6 | 898 | ✓ | flat-fill |  |
| Beatbox_1.2 | `ingame` | 56.7 | 1502 | ✓ | garbled-visuals | content repeats every 160px across the screen; left and right halves are near-identical |
| Birdshoot | `ingame` | 60.2 | 1512 | – | no-audio |  |
| Blocked | `ingame` | 3.9 | 100 | ✓ | low-fps |  |
| bugafactorx-v03-beta | `ingame` | 59.6 | 1517 | – | no-audio |  |
| BunnyTraps-v11 | `ingame` | 60.6 | 1527 | ✓ | garbled-visuals | pixel-to-pixel noise of 89, far above what dithered artwork reaches; the frame looks like  |
| buscaminas | `ingame` | 59.2 | 1507 | – | no-audio |  |
| cardm | `ingame` | 59.4 | 1503 | – | no-audio |  |
| CascadeBeneath v1.0 for GP2X | `ingame` | 62.2 | 1565 | ✓ | garbled-visuals | content repeats every 84px across the screen |
| cavecopter_gp2x | `ingame` | 60.6 | 1524 | – | no-audio |  |
| Chopper | `ingame` | 60.2 | 1526 | – | no-audio |  |
| Clonk2X_1.0 | `ingame` | 16.3 | 412 | – | not-arm-elf | magiceyes: reload of '/bin/sh' failed |
| ConnyCarrot | `ingame` | 59.5 | 1518 | ✓ | flat-fill |  |
| Digger | `ingame` | 1.7 | 4 | ✓ | low-fps |  |
| DontGetCrushed v1.0 for GP2X | `ingame` | 61.3 | 1540 | ✓ | garbled-visuals | content repeats every 84px across the screen |
| dopewars2x | `ingame` | 59.7 | 1503 | – | no-audio |  |
| drod-gp2x-1_0 | `ingame` | 51.5 | 1338 | – | no-audio |  |
| dstroyGP2X1402 | `ingame` | 58.3 | 1507 | – | no-audio |  |
| dumbbell2x-01 | `ingame` | 58.8 | 415 | – | flat-fill |  |
| escoba_exp-20101016.gp2x | `ingame` | 60.3 | 1534 | – | no-audio |  |
| extraterrestres-0 | `ingame` | 57.3 | 1547 | – | no-audio |  |
| FCRLG | `ingame` | 59.5 | 1500 | – | no-audio |  |
| Firewhip | `ingame` | 0.6 | 9 | ✓ | low-fps |  |
| FleshChasmer | `ingame` | 58.5 | 1504 | ✓ | garbled-visuals | content repeats every 160px across the screen; left and right halves are near-identical |
| flowflowmania-0_6-gp2x | `ingame` | 41.8 | 312 | – | no-audio |  |
| freesci | `ingame` | 53.4 | 245 | – | no-audio |  |
| fruits2x | `ingame` | 46.3 | 99 | – | no-audio |  |
| fvc | `ingame` | 59.7 | 1501 | ✓ | garbled-visuals | content repeats every 100px across the screen; left and right halves are near-identical; t |
| gchess-v1.0.1-bin | `ingame` | 59.0 | 1493 | – | no-audio |  |
| gchess-v1.1.0-bin | `ingame` | 58.9 | 1493 | – | no-audio |  |
| gemdrop2x_v02 | `ingame` | 63.1 | 1629 | ✓ | garbled-visuals | content repeats every 96px across the screen |
| gnugo2x | `ingame` | 60.0 | 1521 | – | no-audio |  |
| gp2x-ceferino-0.1 | `ingame` | 61.5 | 1498 | – | no-audio |  |
| gp2xgo-v1.1.0-bin | `ingame` | 58.3 | 1480 | – | no-audio |  |
| gp2xmancala-v1.1.1-bin | `ingame` | 59.8 | 1515 | – | no-audio |  |
| GP2XOfLife | `ingame` | 4.3 | 110 | – | low-fps |  |
| GPSquares_GP2X | `ingame` | 59.1 | 1489 | – | no-audio |  |
| grow | `ingame` | 40.6 | 1501 | – | no-audio |  |
| gxeskiv | `ingame` | 57.3 | 1445 | – | no-audio |  |
| HumphreyGP2X | `ingame` | 58.2 | 1489 | ✓ | flat-fill |  |
| kampfimall-gp2x | `ingame` | 59.9 | 1511 | – | flat-fill |  |
| kampfimall-gp2x-music | `ingame` | 60.8 | 1530 | – | flat-fill |  |
| Knight Lore | `ingame` | 10.5 | 265 | ✓ | flat-fill |  |
| LABYRINTH | `ingame` | 58.6 | 1487 | – | no-audio |  |
| las-tres-luces-de-glaurung-remake | `ingame` | 59.8 | 1529 | ✓ | flat-fill |  |
| Life.0.1 | `ingame` | 61.5 | 1552 | – | garbled-visuals | pixel-to-pixel noise of 91, far above what dithered artwork reaches; the frame looks like  |
| masterpiece2x | `ingame` | 58.4 | 1471 | – | no-audio |  |
| minigolf | `ingame` | 59.2 | 1502 | – | no-audio |  |
| MoveSweep2X | `ingame` | 48.0 | 102 | – | garbled-visuals | content repeats every 96px across the screen; left and right halves are near-identical |
| Nebulus_gp2x | `ingame` | 58.1 | 1460 | – | no-audio |  |
| Net-Bubble-gp2x_1-21-06_bin | `ingame` | 52.6 | 368 | – | no-audio |  |
| Payback | `ingame` | 0.1 | 2 | ✓ | low-fps |  |
| PerfectFit | `ingame` | 61.1 | 1541 | – | no-audio |  |
| PocketSnes_SMRPG | `ingame` | 109.4 | 2771 | – | no-audio |  |
| Pond2X | `ingame` | 59.6 | 1505 | – | flat-fill |  |
| pong2player | `ingame` | 51.4 | 110 | – | no-audio |  |
| pong2v060x | `ingame` | 51.3 | 109 | – | no-audio |  |
| powder2x-112 | `ingame` | 58.8 | 1503 | – | no-audio |  |
| prboom-gp2x | `ingame` | 57.5 | 1484 | – | no-audio |  |
| RevoltOfTheBinaryCouriers GP2X | `ingame` | 58.9 | 1488 | – | no-audio |  |
| robot-escape | `ingame` | 82.4 | 176 | ✓ | flat-fill |  |
| scummvm-kor0.4.2cvs | `ingame` | 59.5 | 1513 | – | garbled-visuals | content repeats every 104px across the screen; left and right halves are near-identical |
| sdlmonkey_0.1 | `ingame` | 60.4 | 1521 | – | no-audio |  |
| sleuth slots 2x | `ingame` | 9.4 | 220 | ✓ | low-fps |  |
| snake2x-1.1 | `ingame` | 59.5 | 1527 | – | no-audio |  |
| Solitaire2x-v1.4 | `ingame` | 84.5 | 600 | – | no-audio |  |
| sources_MEMORY2X | `ingame` | 59.2 | 1508 | – | no-audio |  |
| space squares | `ingame` | 60.1 | 1530 | – | no-audio |  |
| SpaceRocks2X | `ingame` | 28.6 | 87 | – | no-audio |  |
| spartak-chess_0.0.4_gp2x | `ingame` | 59.6 | 1518 | – | no-audio |  |
| Sponge Blob Tennis | `ingame` | 39.4 | 1459 | – | no-audio |  |
| spout | `ingame` | 59.9 | 1508 | – | no-audio |  |
| Sqcolony | `ingame` | 61.0 | 1541 | – | no-audio |  |
| StairwayToHeaven | `ingame` | 59.3 | 1499 | ✓ | flat-fill |  |
| sudoku-v1.0 | `ingame` | 59.7 | 1512 | – | no-audio |  |
| sudoku2x-0.5 | `ingame` | 59.6 | 1506 | – | no-audio |  |
| Tangle | `ingame` | 60.8 | 1535 | – | no-audio |  |
| TimeFrack2D for GP2X | `ingame` | 46.9 | 101 | – | no-audio |  |
| VekDemo2 | `ingame` | 0.5 | 12 | ✓ | low-fps |  |
| Vektar | `ingame` | 0.1 | 3 | ✓ | low-fps |  |
| vektar-free | `ingame` | 11.8 | 300 | ✓ | low-fps |  |
| vexedb1 | `ingame` | 58.8 | 1486 | – | no-audio |  |
| waffle2x | `ingame` | 42.3 | 92 | – | garbled-visuals | content repeats every 160px across the screen; left and right halves are near-identical; t |
| Wiztern Demo | `ingame` | 0.9 | 25 | ✓ | low-fps |  |
| 2xpong_gp2x | `playable` | 60.0 | 1510 | ✓ |  |  |
| AdamantArmorAffection2x | `playable` | 57.8 | 1470 | ✓ |  |  |
| airstrike-1.1 | `playable` | 59.8 | 1522 | ✓ |  |  |
| Akd_BB | `playable` | 60.2 | 1521 | ✓ |  |  |
| amoebax-0.2.1-gp2x | `playable` | 55.0 | 1399 | ✓ |  |  |
| armorcity-0_30b | `playable` | 58.9 | 1510 | ✓ |  |  |
| Asteroids | `playable` | 59.7 | 1527 | ✓ |  |  |
| astrochaos | `playable` | 54.0 | 300 | ✓ |  |  |
| Batiscafo (versin EXP) | `playable` | 59.4 | 1501 | ✓ |  |  |
| beat2x-0.5-bin | `playable` | 60.4 | 1532 | ✓ |  |  |
| biniax-gp2x_v1.2 | `playable` | 60.8 | 1532 | ✓ |  |  |
| Biniax2_gp2x | `playable` | 59.5 | 1507 | ✓ |  |  |
| BioShoot GP2X | `playable` | 57.5 | 1475 | ✓ |  |  |
| blazar_v1-30_gp2x | `playable` | 60.3 | 1522 | ✓ |  |  |
| blipsgp2x | `playable` | 57.9 | 1515 | ✓ |  |  |
| blobbyvolley | `playable` | 58.4 | 1494 | ✓ |  |  |
| blobwars_2x | `playable` | 60.9 | 1543 | ✓ |  |  |
| block | `playable` | 59.9 | 1520 | ✓ |  |  |
| blockdudegp2x | `playable` | 79.2 | 175 | ✓ |  |  |
| Blockrage2x | `playable` | 59.5 | 1502 | ✓ |  |  |
| bluecube2x | `playable` | 58.3 | 1472 | ✓ |  |  |
| Boomshine2x_1.12_gp2x | `playable` | 56.1 | 1446 | ✓ |  |  |
| brassmunkey_gp2x_1.0 | `playable` | 59.2 | 1500 | ✓ |  |  |
| BubbleX | `playable` | 61.2 | 1535 | ✓ |  |  |
| BubTrain_GP2X-2006_Entry_No-Sound | `playable` | 57.2 | 1536 | ✓ |  |  |
| BugWarsSE_v1.0 | `playable` | 59.1 | 1524 | ✓ |  |  |
| bumprace-0.2 | `playable` | 59.1 | 1538 | ✓ |  |  |
| BurokkuDemo1 | `playable` | 60.9 | 1543 | ✓ |  |  |
| buttongame | `playable` | 44.4 | 95 | ✓ |  |  |
| BuzzysBadDay-1.0 | `playable` | 59.9 | 1517 | ✓ |  |  |
| CamelotWarriors-GP2x_v1.0 | `playable` | 59.3 | 1509 | ✓ |  |  |
| ccrg | `playable` | 52.6 | 238 | ✓ |  |  |
| chaos2x | `playable` | 61.0 | 1535 | ✓ |  |  |
| checkersgp2x | `playable` | 58.2 | 1545 | ✓ |  |  |
| chess2x05 | `playable` | 58.9 | 1530 | ✓ |  |  |
| chuckiev12 | `playable` | 60.1 | 1525 | ✓ |  |  |
| CowSuckers-1.0 | `playable` | 59.7 | 1515 | ✓ |  |  |
| Crapong | `playable` | 59.5 | 1526 | ✓ |  |  |
| crazeeman | `playable` | 63.9 | 1614 | ✓ |  |  |
| crimsonV1 | `playable` | 57.3 | 1495 | ✓ |  |  |
| CromoZome | `playable` | 58.3 | 1534 | ✓ |  |  |
| DABAKKA-0 | `playable` | 61.1 | 1541 | ✓ |  |  |
| Dastardly dungeon 1.5 | `playable` | 43.8 | 112 | ✓ |  |  |
| dd2x | `playable` | 101.5 | 467 | ✓ |  |  |
| debian_vs_pimientos_2x_0.1.2 | `playable` | 59.4 | 1519 | ✓ |  |  |
| defeatme-gp2x-1.0.1 | `playable` | 60.0 | 1513 | ✓ |  |  |
| dosmugen | `playable` | 59.1 | 1538 | ✓ |  |  |
| Drill2x_final | `playable` | 57.3 | 1479 | ✓ |  |  |
| drill2x_xtreme_v1.0.3 | `playable` | 58.5 | 1487 | ✓ |  |  |
| DubaiRace038a | `playable` | 46.1 | 107 | ✓ |  |  |
| dynamategp2x | `playable` | 57.6 | 1512 | ✓ |  |  |
| eggstreme3_v1-00_gp2x | `playable` | 60.2 | 1516 | ✓ |  |  |
| egoboo2xFeb1207 | `playable` | 104.0 | 3031 | ✓ |  |  |
| Electronia | `playable` | 67.6 | 1530 | ✓ |  |  |
| enigma | `playable` | 52.3 | 1411 | ✓ |  |  |
| entombed2x | `playable` | 58.8 | 1506 | ✓ |  |  |
| EpicFreeFall_GP2X | `playable` | 59.0 | 1514 | ✓ |  |  |
| EpicRocks_GP2X | `playable` | 53.2 | 595 | ✓ |  |  |
| exi_shoot_gp2x | `playable` | 60.3 | 1518 | ✓ |  |  |
| extraterrestres | `playable` | 94.0 | 2614 | ✓ |  |  |
| exult_rc3 | `playable` | 39.5 | 1465 | ✓ |  |  |
| Factor-v1.0-final | `playable` | 59.4 | 1523 | ✓ |  |  |
| Fishball-1.2 | `playable` | 59.4 | 1499 | ✓ |  |  |
| fissionfield2x | `playable` | 60.8 | 1540 | ✓ |  |  |
| Flaschenspiel | `playable` | 59.5 | 1517 | ✓ |  |  |
| FleshChasmer Zero | `playable` | 59.5 | 1502 | ✓ |  |  |
| flobopuyo0.20.1 | `playable` | 58.8 | 1501 | ✓ |  |  |
| flurkies_v1-25_gp2x | `playable` | 60.3 | 1517 | ✓ |  |  |
| fm | `playable` | 102.6 | 2581 | ✓ |  |  |
| formula1gp2x | `playable` | 60.0 | 1530 | ✓ |  |  |
| friq-beta-07 | `playable` | 59.7 | 1528 | ✓ |  |  |
| frozen2x-0.1 | `playable` | 66.1 | 465 | ✓ |  |  |
| GF | `playable` | 58.9 | 1500 | ✓ |  |  |
| ghostpix_v10_gp2x | `playable` | 64.6 | 1480 | ✓ |  |  |
| GoitGP | `playable` | 56.8 | 1481 | ✓ |  |  |
| gp2hanoi_0.8.1_gp2x | `playable` | 60.3 | 1528 | ✓ |  |  |
| gp2x-invaders-preview-version | `playable` | 60.2 | 1523 | ✓ |  |  |
| gp2x-shienso-bin_061021 | `playable` | 60.8 | 1535 | ✓ |  |  |
| gp2x-smc-0.1 | `playable` | 56.1 | 1508 | ✓ |  |  |
| gp2x_2xmas | `playable` | 59.5 | 1527 | ✓ |  |  |
| GP2X_BallGame_0.49 | `playable` | 59.8 | 1506 | ✓ |  |  |
| gp2xjunkie | `playable` | 57.7 | 1493 | ✓ |  |  |
| gp2xpang-v.1.1.1 | `playable` | 98.1 | 2545 | ✓ |  |  |
| gp2xrick 1.0 | `playable` | 58.6 | 1487 | ✓ |  |  |
| GpFrontier v0.1 | `playable` | 68.4 | 1557 | ✓ |  |  |
| gpfrontier v0.4 | `playable` | 55.0 | 1470 | ✓ |  |  |
| green | `playable` | 58.7 | 1495 | ✓ |  |  |
| hanagechu2x_gbax2007 | `playable` | 64.5 | 1631 | ✓ |  |  |
| hanagechu2xalpha | `playable` | 60.0 | 1515 | ✓ |  |  |
| Heretic2X_v0.5 | `playable` | 58.6 | 1475 | ✓ |  |  |
| hexbattle2x | `playable` | 59.9 | 1523 | ✓ |  |  |
| Hyperion_GP2X_demo | `playable` | 60.4 | 1531 | ✓ |  |  |
| jumpnbumpgp2x | `playable` | 59.1 | 1527 | ✓ |  |  |
| ketm_2x_gp2x | `playable` | 51.9 | 1533 | ✓ |  |  |
| KicknPLay_1.1 | `playable` | 60.0 | 1511 | ✓ |  |  |
| kuklomenos_gp2x_201209 | `playable` | 60.0 | 1541 | ✓ |  |  |
| kurukuru2x | `playable` | 60.2 | 1533 | ✓ |  |  |
| la | `playable` | 46.4 | 104 | ✓ |  |  |
| ladykiller | `playable` | 59.6 | 1524 | ✓ |  |  |
| lbreakoutgp2x | `playable` | 57.6 | 1551 | ✓ |  |  |
| levelshmup | `playable` | 117.9 | 3015 | ✓ |  |  |
| LinesXv3 | `playable` | 61.0 | 1539 | ✓ |  |  |
| logicx | `playable` | 60.9 | 1535 | ✓ |  |  |
| mad-mix-game-20b-final | `playable` | 58.5 | 1483 | ✓ |  |  |
| madbomber | `playable` | 58.9 | 1516 | ✓ |  |  |
| malvado2x | `playable` | 40.1 | 132 | ✓ |  |  |
| Masteries_Journey_to_the_Center_of_the_earth_GP2X | `playable` | 57.4 | 1501 | ✓ |  |  |
| MazeThingie | `playable` | 60.8 | 1535 | ✓ |  |  |
| MemoryGP2X-v11 | `playable` | 60.7 | 1536 | ✓ |  |  |
| meritous | `playable` | 53.8 | 377 | ✓ |  |  |
| Merlin2x_beta_021 | `playable` | 112.8 | 239 | ✓ |  |  |
| metaphysik | `playable` | 60.7 | 1539 | ✓ |  |  |
| methaneV1 | `playable` | 57.8 | 1467 | ✓ |  |  |
| minos-gp2x | `playable` | 58.1 | 1483 | ✓ |  |  |
| mk13.gpe | `playable` | 61.1 | 1532 | ✓ |  |  |
| mkACE.gpe | `playable` | 60.9 | 1536 | ✓ |  |  |
| mkONE.gpe | `playable` | 60.7 | 1530 | ✓ |  |  |
| MM2X | `playable` | 58.5 | 1474 | ✓ |  |  |
| monochromeworlds-gp2x-1.0.0 | `playable` | 60.0 | 1525 | ✓ |  |  |
| mush_gp2x | `playable` | 38.2 | 988 | ✓ |  |  |
| Mutant Tank Knights | `playable` | 56.8 | 342 | ✓ |  |  |
| MyriadUpdated | `playable` | 58.1 | 1521 | ✓ |  |  |
| mzx-2.84c | `playable` | 59.8 | 1515 | ✓ |  |  |
| mzx282-gp2x | `playable` | 59.9 | 1511 | ✓ |  |  |
| n-tris_v1.0 | `playable` | 57.1 | 1441 | ✓ |  |  |
| newsuperpang | `playable` | 58.1 | 1475 | ✓ |  |  |
| Nifty | `playable` | 58.4 | 1513 | ✓ |  |  |
| odonata_demo | `playable` | 57.9 | 1470 | ✓ |  |  |
| openglad2x | `playable` | 58.0 | 1505 | ✓ |  |  |
| opentyrian2x_0.3_complete | `playable` | 54.9 | 1504 | ✓ |  |  |
| OrbitalSniper2x_v1.1 | `playable` | 54.6 | 144 | ✓ |  |  |
| PAF | `playable` | 60.1 | 1523 | ✓ |  |  |
| paraballgp2x | `playable` | 58.6 | 1494 | ✓ |  |  |
| PaybackDemo | `playable` | 26.9 | 705 | ✓ |  |  |
| pdcv060b | `playable` | 49.7 | 349 | ✓ |  |  |
| Phishy-0 | `playable` | 59.4 | 1518 | ✓ |  |  |
| physique | `playable` | 58.7 | 1491 | ✓ |  |  |
| Pika2x | `playable` | 57.1 | 408 | ✓ |  |  |
| pintor2x | `playable` | 77.6 | 1954 | ✓ |  |  |
| pixpang | `playable` | 53.0 | 1483 | ✓ |  |  |
| Poker2x | `playable` | 111.6 | 785 | ✓ |  |  |
| Pool Panic | `playable` | 58.7 | 1482 | ✓ |  |  |
| powermanga-0.80 | `playable` | 54.3 | 1438 | ✓ |  |  |
| proj0-demo_01 | `playable` | 58.7 | 1502 | ✓ |  |  |
| PulplifeWars | `playable` | 57.0 | 1498 | ✓ |  |  |
| puzzlelandgp2x | `playable` | 53.3 | 1505 | ✓ |  |  |
| qfg3-0 | `playable` | 58.6 | 1522 | ✓ |  |  |
| quartz2_v1-50_gp2x | `playable` | 60.4 | 1519 | ✓ |  |  |
| Release GP2X MST_RUNNERS | `playable` | 56.2 | 1473 | ✓ |  |  |
| retrovirus_1_1 | `playable` | 57.0 | 1449 | ✓ |  |  |
| reword_v0.5 | `playable` | 60.2 | 1541 | ✓ |  |  |
| river | `playable` | 59.3 | 1499 | ✓ |  |  |
| RockRain | `playable` | 59.1 | 1492 | ✓ |  |  |
| rockrain2_exp-20100925 | `playable` | 59.7 | 1520 | ✓ |  |  |
| rookiehero_EXP.gp2x.v20120220 | `playable` | 59.4 | 1524 | ✓ |  |  |
| rubidogp2x | `playable` | 57.9 | 1523 | ✓ |  |  |
| ruckman_v1.03 | `playable` | 88.2 | 241 | ✓ |  |  |
| Runner_GP2X | `playable` | 56.8 | 1464 | ✓ |  |  |
| s-tris2_v1-64_gp2x | `playable` | 60.6 | 1530 | ✓ |  |  |
| sachunsungx | `playable` | 60.8 | 1532 | ✓ |  |  |
| ScorchedGPBeta2 | `playable` | 59.8 | 1522 | ✓ |  |  |
| scummvm-0.11.1-gp2x | `playable` | 58.3 | 1521 | ✓ |  |  |
| scummvm-1.2.0-gp2x | `playable` | 58.1 | 1520 | ✓ |  |  |
| SdLame | `playable` | 59.9 | 1526 | ✓ |  |  |
| ShanghaiX | `playable` | 61.3 | 1545 | ✓ |  |  |
| SheepDash | `playable` | 58.2 | 1547 | ✓ |  |  |
| Shippy84 | `playable` | 59.1 | 1531 | ✓ |  |  |
| siv050 | `playable` | 63.2 | 1524 | ✓ |  |  |
| SmallBall_GP | `playable` | 59.0 | 1519 | ✓ |  |  |
| snowedin5_v1-00_gp2x | `playable` | 58.6 | 1481 | ✓ |  |  |
| SOD v1.1 | `playable` | 56.4 | 1469 | ✓ |  |  |
| sokobangp2x | `playable` | 52.2 | 1507 | ✓ |  |  |
| sources_Yahtzee | `playable` | 49.9 | 213 | ✓ |  |  |
| space_varments_v1.0 | `playable` | 51.5 | 538 | ✓ |  |  |
| Squaresliding | `playable` | 58.2 | 1465 | ✓ |  |  |
| stppc2x-v1.0 | `playable` | 40.6 | 1506 | ✓ |  |  |
| stransball2 | `playable` | 59.0 | 1494 | ✓ |  |  |
| street2x | `playable` | 53.7 | 1503 | ✓ |  |  |
| subhunt | `playable` | 59.2 | 1496 | ✓ |  |  |
| SuperPaf_v1.0 | `playable` | 59.2 | 1517 | ✓ |  |  |
| SuperPixelJumper v1.1 for GP2X | `playable` | 60.0 | 1516 | ✓ |  |  |
| SuperSonicSpeed | `playable` | 58.6 | 1483 | ✓ |  |  |
| survival | `playable` | 58.9 | 1503 | ✓ |  |  |
| symbolica-0.8 | `playable` | 59.2 | 1503 | ✓ |  |  |
| tail-tale | `playable` | 59.9 | 1516 | ✓ |  |  |
| tecnoballz-0.91-gp2x | `playable` | 53.4 | 1389 | ✓ |  |  |
| tetwins | `playable` | 51.1 | 109 | ✓ |  |  |
| ThreeTs_Game | `playable` | 53.0 | 113 | ✓ |  |  |
| tikka_dungeons_demo_1 | `playable` | 58.8 | 1482 | ✓ |  |  |
| tilematch-0.6 | `playable` | 93.3 | 2372 | ✓ |  |  |
| towertopplergp2x | `playable` | 57.9 | 1532 | ✓ |  |  |
| treev060 | `playable` | 58.0 | 1504 | ✓ |  |  |
| vectoroids-2x | `playable` | 59.9 | 1521 | ✓ |  |  |
| vektarpack_v1 | `playable` | 63.8 | 1630 | ✓ |  |  |
| Ventifact | `playable` | 58.5 | 1518 | ✓ |  |  |
| vexed-gp2x-10 | `playable` | 59.6 | 1507 | ✓ |  |  |
| warcraft | `playable` | 58.7 | 1527 | ✓ |  |  |
| warehouse_panic_v1.1_gp2x | `playable` | 53.3 | 1509 | ✓ |  |  |
| waternetgp2x | `playable` | 56.2 | 1547 | ✓ |  |  |
| wehaveballs | `playable` | 59.1 | 1487 | ✓ |  |  |
| WindAndWater_teaser_110 | `playable` | 59.5 | 1500 | ✓ |  |  |
| wnw | `playable` | 59.3 | 1509 | ✓ |  |  |
| xenitris_demo | `playable` | 59.1 | 1495 | ✓ |  |  |
| Xpired2x 1.0 beta | `playable` | 52.5 | 369 | ✓ |  |  |
| xRick | `playable` | 57.9 | 1478 | ✓ |  |  |
| znax | `playable` | 57.2 | 1528 | ✓ |  |  |
| Zoids Quest2X-0.0.1-2 | `playable` | 53.6 | 383 | ✓ |  |  |
| zoltan 2x | `playable` | 55.7 | 1475 | ✓ |  |  |
| ztunnel-0 | `playable` | 58.1 | 1475 | ✓ |  |  |

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
| chroma 1.01 v1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
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
| PhishyWiz | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Powder2X_wiz_114_v01 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| PPlane | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
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
| albion-v1.0.1-wiz | `black` | 20.4 | 15 | ✓ | black-screen |  |
| Balloonacy_wiz_wip | `black` | 0.2 | 4 | ✓ | black-screen |  |
| CartoonWiz | `black` | 0.2 | 4 | ✓ | black-screen |  |
| ColonyConflict_V1.1_B6 | `black` | 0.1 | 3 | ✓ | black-screen |  |
| DungeonRunner | `black` | 0.1 | 4 | ✓ | black-screen |  |
| DuoWIZ_Pong | `black` | 0.2 | 4 | ✓ | black-screen |  |
| freecell2x | `black` | 0.1 | 4 | ✓ | black-screen |  |
| herknights | `black` | 57.3 | 1494 | ✓ | black-screen |  |
| March of the mini tux(wiz version) | `black` | 0.2 | 4 | ✓ | black-screen |  |
| opentyrian | `black` | 2.8 | 2 | – | black-screen |  |
| pgw | `black` | 58.2 | 1529 | – | black-screen |  |
| PPlane2.WIZ | `black` | 0.1 | 3 | ✓ | black-screen |  |
| Shanghai2 | `black` | 49.7 | 111 | ✓ | black-screen |  |
| SimOniZ | `black` | 0.2 | 4 | ✓ | black-screen |  |
| tetwizdownload | `black` | 0.2 | 4 | ✓ | black-screen |  |
| Trap75 | `black` | 60.1 | 1515 | ✓ | black-screen |  |
| TUcS.app(V0.7.0 - Wiz) | `black` | 0.2 | 4 | ✓ | black-screen |  |
| warcraft-beta3-wiz | `black` | 31.9 | 39 | ✓ | black-screen |  |
| wiz-car-binary_090818a | `black` | 58.6 | 1476 | ✓ | black-screen |  |
| Wiz_Blox | `black` | 0.2 | 4 | ✓ | black-screen |  |
| wiz_drench | `black` | 0.2 | 4 | ✓ | black-screen |  |
| WIZ_S4S | `black` | 0.2 | 4 | ✓ | black-screen |  |
| WizSticks | `black` | 0.1 | 3 | ✓ | black-screen |  |
| xcom1-v1.0.2-wiz | `black` | 61.2 | 125 | ✓ | black-screen |  |
| xcom2-v1.0.1-wiz | `black` | 118.7 | 3064 | ✓ | black-screen |  |
| spout | `ingame` | 60.0 | 1513 | – | no-audio |  |
| Sudoku2X | `ingame` | 60.6 | 1529 | – | no-audio |  |
| wizchess-v1.1.0-bin | `ingame` | 59.0 | 1492 | – | no-audio |  |
| wizchess-v1.2.0-bin | `ingame` | 59.8 | 1513 | – | no-audio |  |
| wizgo-v1.1.0-bin | `ingame` | 59.4 | 1514 | – | no-audio |  |
| WizGolf | `ingame` | 59.3 | 1504 | – | no-audio |  |
| wizmancala-v1.1.2-bin | `ingame` | 59.3 | 1503 | – | no-audio |  |
| Worship Vector | `ingame` | 57.9 | 1461 | ✓ | garbled-visuals | content repeats every 160px across the screen |
| AdamantArmorAffectionWiz | `playable` | 58.4 | 1480 | ✓ |  |  |
| airstrike-wiz-1.01 | `playable` | 59.6 | 1514 | ✓ |  |  |
| alexsfalldown | `playable` | 59.5 | 1501 | ✓ |  |  |
| Blix2x | `playable` | 61.0 | 1534 | ✓ |  |  |
| Dd2x | `playable` | 55.3 | 252 | ✓ |  |  |
| deicide3_eng | `playable` | 59.3 | 1495 | ✓ |  |  |
| gr-v1001-wiz | `playable` | 57.8 | 1510 | ✓ |  |  |
| hexen2 | `playable` | 56.4 | 1467 | ✓ |  |  |
| minos-gp2x-wiz | `playable` | 58.0 | 1469 | ✓ |  |  |
| mush_gp2x | `playable` | 44.5 | 1137 | ✓ |  |  |
| mush_gp2x-0 | `playable` | 35.4 | 944 | ✓ |  |  |
| Pentominos | `playable` | 60.6 | 1524 | ✓ |  |  |
| Pharaoh | `playable` | 49.1 | 105 | ✓ |  |  |
| PuzzleDevilWizDemo | `playable` | 62.4 | 1582 | ✓ |  |  |
| Rezerwar | `playable` | 52.0 | 680 | ✓ |  |  |
| rockrain-gp2x-wiz | `playable` | 60.6 | 1528 | ✓ |  |  |
| Sachunsung2 | `playable` | 48.9 | 106 | ✓ |  |  |
| scummvm-1.2.0-gp2xwiz | `playable` | 56.9 | 1500 | ✓ |  |  |
| Sopwith | `playable` | 59.9 | 1514 | ✓ |  |  |
| Sqdef_Wiz_14A | `playable` | 59.5 | 1522 | ✓ |  |  |
| Tail Tale | `playable` | 60.3 | 1515 | ✓ |  |  |
| wizznic-0.9.9-wiz | `playable` | 57.9 | 1517 | ✓ |  |  |
| wnw_demo | `playable` | 59.7 | 1515 | ✓ |  |  |
| xRick | `playable` | 59.8 | 1521 | ✓ |  |  |
| znumbers | `playable` | 49.4 | 107 | ✓ |  |  |

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
| PUZZLEBOARDS | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x90a |
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
| stppc-caanoo-29-11-2010 | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
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
| xpiredcan.eng.101 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zerowing | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zlocada-caanoo | `incompatible` | 0.0 | 0 | – | unimplemented-syscall | 281 (socket) |
| zombiesorbet_v1.0_caanoo | `incompatible` | 1.2 | 2 | ✓ | no-frames |  |
| zomg-Caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/zomg-Caanoo/Zomg/zomg.gpe' is not an ARM ELF and  |
| zsxd | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Zverealm-Caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Zverealm-Caanoo/Zverealm/Zverealm.gpe' is not an  |
| aquaVenture | `black` | 37.0 | 942 | ✓ | black-screen |  |
| arcadevol1 | `black` | 57.8 | 1498 | – | black-screen |  |
| Arcadevol2 | `black` | 59.4 | 1522 | – | black-screen |  |
| Arcadevol3 | `black` | 58.0 | 1515 | – | black-screen |  |
| B'lox! | `black` | 15.8 | 188 | ✓ | black-screen |  |
| Blingo | `black` | 31.2 | 92 | ✓ | black-screen |  |
| BubbleTrain | `black` | 1.0 | 2 | ✓ | black-screen |  |
| DealorNoDeal | `black` | 59.3 | 1521 | – | black-screen |  |
| Flappynerd_Caanoo | `black` | 17.6 | 212 | ✓ | black-screen |  |
| Guru Logic | `black` | 14.0 | 205 | ✓ | black-screen |  |
| JUMPNRUN | `black` | 52.2 | 1439 | ✓ | black-screen |  |
| next_element | `black` | 60.2 | 1520 | – | black-screen |  |
| RACING | `black` | 63.4 | 1507 | – | black-screen |  |
| STRATEGY | `black` | 57.8 | 1512 | – | black-screen |  |
| Tile | `black` | 52.5 | 1487 | – | black-screen |  |
| Balloonacy | `ingame` | 0.1 | 2 | – | flat-fill |  |
| Blitz | `ingame` | 0.1 | 2 | – | flat-fill |  |
| caanoo-biniax2-v1.30-bin | `ingame` | 16.4 | 415 | ✓ | low-fps |  |
| caanoo-chess-v1.1.0-bin | `ingame` | 47.1 | 1192 | – | no-audio |  |
| caanoo-go-v1.1.0-bin | `ingame` | 52.4 | 1327 | – | no-audio |  |
| caanoo-mancala-v1.1.0-bin | `ingame` | 40.7 | 1031 | – | no-audio |  |
| cat_trap | `ingame` | 0.1 | 2 | – | flat-fill |  |
| Drench | `ingame` | 0.1 | 2 | – | flat-fill |  |
| Geek_em_up_CAANOO | `ingame` | 0.1 | 2 | – | flat-fill |  |
| gnp_104 | `ingame` | 53.9 | 1447 | ✓ | flat-fill |  |
| gr-v1001-caanoo | `ingame` | 10.8 | 280 | ✓ | low-fps |  |
| jump_n_blob_caanoo | `ingame` | 10.6 | 281 | ✓ | low-fps |  |
| knight | `ingame` | 10.2 | 258 | ✓ | flat-fill |  |
| Liar | `ingame` | 14.5 | 364 | ✓ | low-fps |  |
| llcpcls-caanoo | `ingame` | 15.8 | 34 | ✓ | low-fps |  |
| MISC | `ingame` | 58.6 | 1539 | – | no-audio |  |
| MNV_Caanoo_Release1 | `ingame` | 40.1 | 1042 | ✓ | flat-fill |  |
| noiz2sa_caanoo | `ingame` | 16.8 | 129 | ✓ | flat-fill |  |
| nuclearchess | `ingame` | 3735.5 | 4164 | – | garbled-visuals | renders at 26x26 instead of 320x240 |
| powder | `ingame` | 62.2 | 1418 | – | no-audio |  |
| rg_ura_103 | `ingame` | 51.6 | 1361 | ✓ | flat-fill |  |
| SantaMania | `ingame` | 0.1 | 2 | – | flat-fill |  |
| tlosaf_v12-caanoo | `ingame` | 59.0 | 1490 | – | no-audio |  |
| zelda-roth-olb-3t_caanoo | `ingame` | 24.6 | 666 | ✓ | low-fps |  |
| ADVENTURE | `playable` | 56.2 | 1495 | ✓ |  |  |
| Amoebax | `playable` | 52.9 | 1346 | ✓ |  |  |
| cavestory | `playable` | 55.2 | 1500 | ✓ |  |  |
| ccrg-caanoo | `playable` | 44.8 | 197 | ✓ |  |  |
| cllwrth | `playable` | 31.1 | 788 | ✓ |  |  |
| Fywod_caanoo | `playable` | 53.6 | 1364 | ✓ |  |  |
| pang | `playable` | 64.2 | 1650 | ✓ |  |  |
| propis | `playable` | 49.0 | 1236 | ✓ |  |  |
| rhythmosplay_1.1.12 | `playable` | 49.6 | 1263 | ✓ |  |  |
| SHOOTERS | `playable` | 110.8 | 3029 | ✓ |  |  |
| SimOniZ | `playable` | 41.7 | 1073 | ✓ |  |  |
| smw_1.7 | `playable` | 34.3 | 921 | ✓ |  |  |
| SPORTS | `playable` | 58.7 | 1520 | ✓ |  |  |
| sqrxz-v0996-caanoo | `playable` | 58.0 | 1482 | ✓ |  |  |
| sqrxz2-v0.80-caanoo | `playable` | 58.1 | 1481 | ✓ |  |  |
| tailtale4c | `playable` | 59.3 | 1498 | ✓ |  |  |
| warehouse_panic_v1.1_caanoo | `playable` | 26.0 | 320 | ✓ |  |  |
| WindandWater | `playable` | 58.8 | 1491 | ✓ |  |  |
