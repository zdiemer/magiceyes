# magiceyes compatibility sweep

Every GP2X / Wiz / Caanoo title on the corpus share, booted headlessly through the native engine (`bin/me_unicorn`) and scored from the shm framebuffer + the structured run report. Regenerate with `tools/test/compat_report.py`.


## Summary

| Platform | Titles | Playable | Ingame | Black | Incompatible | Crashed |
|---|--:|--:|--:|--:|--:|--:|
| GP2X | 673 | 211 | 90 | 144 | 228 | 0 |
| Wiz | 153 | 26 | 8 | 25 | 94 | 0 |
| Caanoo | 205 | 16 | 25 | 16 | 148 | 0 |
| **All** | **1031** | **253** | **123** | **185** | **470** | **0** |

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
| **Boots but renders only black** (`black-screen`) | 185 | Caanoo, GP2X, Wiz | n/a |
| **Never rendered a frame (cause unknown)** (`no-frames`) | 171 | Caanoo, GP2X, Wiz | n/a |
| **Game data files are missing from the dump** (`missing-game-data`) | 105 | Caanoo, GP2X, Wiz | n/a |
| **Not a 32-bit ARM ELF** (`not-arm-elf`) | 99 | Caanoo, GP2X, Wiz | n/a |
| **Renders at speed but no audio** (`no-audio`) | 69 | Caanoo, GP2X, Wiz | n/a |
| **No .gpe in the dump** (`no-executable`) | 59 | Caanoo, GP2X, Wiz | n/a |
| **Draws only a flat colour** (`flat-fill`) | 28 | Caanoo, GP2X | n/a |
| **Unknown /dev node** (`unknown-device`) | 25 | Caanoo, GP2X, Wiz | `/dev/input/mouse/0` ×14, `/dev/null` ×4, `/dev/` ×2, `/dev/input/mouse0` ×2 |
| **Renders but below 25 fps** (`low-fps`) | 16 | Caanoo, GP2X | n/a |
| **Renders, but the picture is wrong** (`garbled-visuals`) | 9 | Caanoo, GP2X, Wiz | n/a |
| **Archive extraction failed** (`archive-failed`) | 5 | Caanoo, GP2X | n/a |
| **Unimplemented syscall** (`unimplemented-syscall`) | 3 | Caanoo, GP2X | `281 (socket)` ×1, `113` ×1, `117` ×1 |
| **Spins forever polling an MMSP2 register** (`mmio-spin`) | 3 | GP2X, Wiz | `0x90a` ×1, `0x4000` ×1 |
| **Could not open a display** (`display-init-failed`) | 1 | GP2X | n/a |

## Renders, but the picture is wrong

These 9 titles pass the running checks (frames advancing, frame rate, audio) while the frame itself is visibly broken, so they are graded `ingame` rather than `playable`. The reasons come from measuring the captured frame: a consistent per-row offset means a stride/pitch mismatch, large-scale repetition means the screen holds more than one copy of itself, and noise far above what dithered artwork reaches means corrupt memory.

| Title | Platform | fps | What the frame looks like |
|---|---|--:|---|
| nuclearchess | Caanoo | 3429.5 | renders at 26x26 instead of 320x240 |
| 1945_GP2X_0.2b | GP2X | 83.2 | pixel-to-pixel noise of 96, far above what dithered artwork reaches; the frame looks like corrupt memory |
| BunnyTraps-v11 | GP2X | 61.3 | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like corrupt memory |
| FleshChasmer | GP2X | 59.4 | the screen holds a second copy of itself, offset by 160px; left and right halves are near-identical |
| gemdrop2x_v02 | GP2X | 89.4 | the screen holds a second copy of itself, offset by 96px |
| GF | GP2X | 59.8 | top and bottom halves are near-identical |
| Life.0.1 | GP2X | 61.8 | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like corrupt memory |
| MoveSweep2X | GP2X | 49.4 | the screen holds a second copy of itself, offset by 160px; left and right halves are near-identical |
| Worship Vector | Wiz | 60.4 | the screen holds a second copy of itself, offset by 160px; left and right halves are near-identical |

## Scored as working, but only painting a flat colour

These 28 titles advanced frames, kept audio running, and held frame rate, so they land in `playable`/`renders`. Their framebuffer never held more than one or two colours, which means the tier overstates them. Worth treating as broken.

| Title | Platform | Status | fps |
|---|---|---|--:|
| Balloonacy | Caanoo | `renders` | 0.1 |
| Blitz | Caanoo | `renders` | 0.1 |
| cat_trap | Caanoo | `renders` | 0.1 |
| Drench | Caanoo | `renders` | 0.1 |
| Geek_em_up_CAANOO | Caanoo | `renders` | 0.1 |
| gnp_104 | Caanoo | `playable` | 47.2 |
| knight | Caanoo | `renders` | 10.5 |
| MNV_Caanoo_Release1 | Caanoo | `playable` | 46.7 |
| noiz2sa_caanoo | Caanoo | `renders` | 15.8 |
| rg_ura_103 | Caanoo | `playable` | 51.2 |
| SantaMania | Caanoo | `renders` | 0.1 |
| 2xZdoom_selector | GP2X | `renders` | 2.5 |
| alex | GP2X | `renders` | 60.6 |
| alex4_gp2x | GP2X | `renders` | 60.3 |
| ASCIIPong2xV0.4 | GP2X | `playable` | 36.2 |
| BareFistFighter | GP2X | `playable` | 59.5 |
| ConnyCarrot | GP2X | `playable` | 59.5 |
| Digger | GP2X | `renders` | 2.6 |
| dodge | GP2X | `renders` | 14.1 |
| dumbbell2x-01 | GP2X | `renders` | 58.7 |
| HumphreyGP2X | GP2X | `playable` | 59.9 |
| kampfimall-gp2x | GP2X | `renders` | 60.6 |
| kampfimall-gp2x-music | GP2X | `renders` | 60.6 |
| Knight Lore | GP2X | `renders` | 10.7 |
| las-tres-luces-de-glaurung-remake | GP2X | `playable` | 59.9 |
| Pond2X | GP2X | `renders` | 58.9 |
| robot-escape | GP2X | `playable` | 82.6 |
| StairwayToHeaven | GP2X | `playable` | 59.8 |

## Cross-title blockers


### Unimplemented syscalls

| Item | Titles |
|---|--:|
| `163 (mremap)` | 20 |
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
| 2xHexen2 v0.05 PB2 | `incompatible` | 2.0 | 1 | – | no-frames |  |
| 2xHexen2_cheat_patch | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/2xHexen2_cheat_patch' |
| 2xquake003 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| 2xquake2 | `incompatible` | 0.0 | 0 | ✓ | missing-game-data |  |
| 2XRally01 | `incompatible` | 0.0 | 0 | – | display-init-failed |  |
| 2xZdoom_PB1.2 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| 4WE_GP2x | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| 9 Lives | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| A1GP2XV1_1 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/ |
| abduction | `incompatible` | 0.0 | 0 | – | no-frames |  |
| abe | `incompatible` | 60.2 | 1518 | ✓ | no-frames |  |
| abuse_1.0 | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/ |
| ADIC2X | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| airpong4GP2X0.0.4 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/airpong4GP2X0.0.4/airpong022/src/AirPong.gpe' is not an  |
| albion-v1.0.1-gp2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse0 |
| Alex's Falldown | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| AlienZ | `incompatible` | 0.0 | 0 | – | no-frames |  |
| animatch_v1.2 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/animatch_v1.2' |
| animatch_v1.2.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/animatch_v1.2.zip' (exit 32512) |
| AnotherGame2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/AnotherGame2x/AnotherGame2x/anothergame2x.gpe' is not an |
| atris-1.0.7 | `incompatible` | 60.5 | 1572 | ✓ | no-frames |  |
| B'lox! | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| balluz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/balluz/balluz/balluz.gpe' is not an ARM ELF and no runna |
| beat2x-05 source | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-05 source' |
| beat2x-pack-C64 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-C64' |
| beat2x-pack-ccMixter | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-ccMixter' |
| beat2x-pack-magnatune-electronica | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-magnatune-electronica' |
| beat2x-pack-miniMaximum | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-miniMaximum' |
| beat2x-pack-tutorial | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-tutorial' |
| beat2x-pack-urban | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-urban' |
| BermudaS_gp2x | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| BisfoG | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| blastriot1.21 | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Blix2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| blockoid | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| blocksGP2X-0 | `incompatible` | 0.0 | 0 | – | unimplemented-syscall | 113 |
| blox | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Bombs Panic | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Boomshine2x_(java) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Boomshine2x_(java)/Boomshine2x/Boomshine2x.gpe' is not a |
| bunkermaster2x04 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Butterfly | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Butterfly' |
| cackb2 | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| CaptainCrusader_GP2XDemo | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| CaptainCrusader_GP2XFull | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| cdogs2x04 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Chess2xSkins | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Chess2xSkins' |
| chicken-puyopuyo | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Chroma | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Classical | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Classical' |
| CloneKeen2X-1.0a | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Codemaster | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
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
| Dr. Mates v1.0 | `incompatible` | 37.0 | 89 | ✓ | no-frames |  |
| duckmaze-gp2x-0.1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/duckmaze-gp2x-0.1/duckmaze-gp2x-0.1/duckmaze.gpe' is not |
| duke2x004 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| duke3d_cheat_patch | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/duke3d_cheat_patch' |
| dyc_gp2x | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| E-Fighters2x_FIRST_ALPHA_0_0_5_fixedSound | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| EasterQuest | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| exultb4-src | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/exultb4-src' |
| FFDoom | `incompatible` | 0.0 | 0 | – | no-frames |  |
| FindMii | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Fire | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Flappynerd_GP2X | `incompatible` | 0.0 | 1 | – | no-frames |  |
| FleshChasmer Zero (English Patch) | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/FleshChasmer Zero (English Patch)' |
| FlipIR_GP2X | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Football2X | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Fore_1_0 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Fore_1_0' |
| FP_Default_2.0 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/FP_Default_2.0' |
| freec2x | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| freedroid2x06 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/freedroid2x06/Freedroid/FreeDroid.gpe' is not an ARM ELF |
| frotz | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/frotz' |
| FullBoard (test ver.) | `incompatible` | 51.3 | 107 | ✓ | no-frames |  |
| garden2x02 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Geek 'em up GP2X | `incompatible` | 0.0 | 1 | – | no-frames |  |
| GeneralPromise | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| geoQuiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/geoQuiz/geoQuiz.gpe' is not an ARM ELF and no runnable b |
| glouton | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| gnurobbo_0.66_open2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| gorillaz | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| gp2x-abrick-0.1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/gp2x-abrick-0.1' |
| gp2x-rogue-v1.0 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| gp2x-tong-v1 | `incompatible` | 0.0 | 1 | – | no-frames |  |
| gp2x_drench | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| GP2X_Nat2007 | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| GP2X_TLI | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| gp2xbug | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
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
| Klaur | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| kobo_deluxe_beta1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| KQ2X_v3 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Laser2xVers10 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Lexeme | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| lights-out | `incompatible` | 0.0 | 0 | – | mmio-spin |  |
| Liquid Counter.gp2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Liquid Counter.gp2x/LiquidCount/LiquidCount.gpe' is not  |
| Logoball | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Lottys_Lines.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/Lottys_Lines.zip' (exit 32512) |
| lumix-beta-01 | `incompatible` | 60.4 | 1518 | – | no-frames |  |
| March of the mini tux | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| mariodm | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/mariodm' |
| Marte Necesita Vacas GP2X | `incompatible` | 0.0 | 1 | – | no-frames |  |
| MazezaMGP2X | `incompatible` | 0.0 | 1 | – | no-frames |  |
| memory | `incompatible` | 0.0 | 0 | – | mmio-spin | 0x90a |
| Midnight2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Midnight2x/dosbox/midnight/midnight.gpe' is not an ARM E |
| misterhachi | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/misterhachi/misterhachi/misterhachi.gpe' is not an ARM E |
| mopesnake-gp2x-0.5 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/mopesnake-gp2x-0.5/mopesnake-gp2x-0.5/mopesnake.gpe' is  |
| MouthTrap | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
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
| PantaVsDragon (Gp2x F-100 F-200) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/PantaVsDragon (Gp2x F-100 F-200)/PantaVsDragon (Gp2x F-1 |
| para | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/para' |
| Payback_v1_1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Payback_v1_1' |
| PaybackMusicManager | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/PaybackMusicManager' |
| pc | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Pentominos | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| Peuppy_10_GP2X | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Phantomas1.8X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Pipes2_0 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Pipes2_0/Pipes/Pipes.gpe' is not an ARM ELF and no runna |
| Pipes_v2.1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Pipes_v2.1/Pipes/Pipes.gpe' is not an ARM ELF and no run |
| Poker_Gp2Xv1.0 | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| PPlane | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| PPlane2.GP2X | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| PrBoom PWAD pack | `incompatible` | 0.0 | 0 | – | no-frames |  |
| puckman_gp2x | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| pykaraoke-0.6-gp2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| pySlide | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/pySlide/pySlide/pySlide.gpe' is not an ARM ELF and no ru |
| pyTetris | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/pyTetris/pyTetris/pyTetris.gpe' is not an ARM ELF and no |
| Quad | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| Quake Mods 5 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Quake Mods 5/2Fact2NS.gpe' is not an ARM ELF and no runn |
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
| Sachunsung2_1 | `incompatible` | 50.6 | 108 | ✓ | no-frames |  |
| santaMania | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| scummvm-alpha-8a_sky | `incompatible` | 0.0 | 0 | – | no-frames |  |
| scummVMsaves | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/scummVMsaves' |
| sdlscav_gp2x_0.2.0 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Shangai v2 | `incompatible` | 51.8 | 108 | ✓ | no-frames |  |
| shoveit | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/shoveit' |
| Simon2X | `incompatible` | 10.9 | 11 | – | no-frames |  |
| Skin1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Skin1' |
| smw-1.6_gp2x | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/null |
| snakepan | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/snakepan/Snakepan.gpe' is not an ARM ELF and no runnable |
| SnoodForTileMatch | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/SnoodForTileMatch' |
| snowedin6_v1-00_gp2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/graphics/fb0 |
| sopwith_camel_rc3 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/null |
| space52_gp2x(oficial) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/space52_gp2x(oficial)/space_52/space_52_gp2x.gpe' is not |
| space52_gp2x(open2x) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/space52_gp2x(open2x)/space_52/space_52_gp2x.gpe' is not  |
| SpaceSnake | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
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
| wizznic06_NES_30levels | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/wizznic06_NES_30levels' |
| Wolf4SDL | `incompatible` | 0.0 | 0 | – | no-frames |  |
| worminator302 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zcgp2x_211B18_0.4alpha | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/gpmdata |
| Znumbers | `incompatible` | 50.8 | 108 | ✓ | no-frames |  |
| Zombiepox2X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zooov11 | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| 2xWargus_PB1.3 | `black` | 48.6 | 1133 | – | black-screen |  |
| _-The Reversed Preacher 3-_Hack bIld_ | `black` | 59.5 | 1507 | ✓ | black-screen |  |
| _-the reversed preacher II-_ | `black` | 58.8 | 1489 | ✓ | black-screen |  |
| AbusimbelProfanationDeluxe | `black` | 59.7 | 1511 | ✓ | black-screen |  |
| AfterBurner-GP2X | `black` | 60.0 | 1530 | – | black-screen |  |
| Airplyr | `black` | 60.9 | 1534 | – | black-screen |  |
| AlienBlaster_1.02 | `black` | 58.8 | 1527 | ✓ | black-screen |  |
| angband2x-v2 | `black` | 28.5 | 58 | – | black-screen |  |
| bang_gp | `black` | 25.7 | 26 | ✓ | black-screen |  |
| barrage | `black` | 60.7 | 1535 | – | black-screen |  |
| BearOids | `black` | 59.2 | 1498 | ✓ | black-screen |  |
| BeetleRun | `black` | 60.4 | 1520 | – | black-screen |  |
| Biohazard2 | `black` | 60.4 | 1526 | – | black-screen |  |
| blingo 1.2 | `black` | 40.0 | 123 | ✓ | black-screen |  |
| Bloxz_DEMO | `black` | 60.3 | 1530 | – | black-screen |  |
| bobtron-gp2x | `black` | 60.1 | 1516 | ✓ | black-screen |  |
| Boulders-0 | `black` | 16.5 | 10 | ✓ | black-screen |  |
| BubbleTrain_GP2X-2006_Entry | `black` | 1.2 | 2 | ✓ | black-screen |  |
| cat_trap | `black` | 0.2 | 4 | ✓ | black-screen |  |
| cgenius-gp2x | `black` | 57.0 | 1483 | – | black-screen |  |
| ChopperAttackv1.0.17 | `black` | 0.1 | 3 | ✓ | black-screen |  |
| Comando2gp2xEN | `black` | 61.1 | 1538 | – | black-screen |  |
| coppergreen | `black` | 9.4 | 12 | ✓ | black-screen |  |
| cosmo2x_01 | `black` | 60.1 | 1528 | – | black-screen |  |
| crossroads | `black` | 57.6 | 1453 | – | black-screen |  |
| CUBES | `black` | 60.9 | 1540 | – | black-screen |  |
| cyberhockeyV2_6 | `black` | 59.6 | 1498 | ✓ | black-screen |  |
| d2x-gp2x-0.02 | `black` | 6.5 | 4 | – | black-screen |  |
| Dance2x Alpha GPE | `black` | 59.7 | 1520 | – | black-screen |  |
| Dark_Light_SDL2X | `black` | 60.0 | 1532 | – | black-screen |  |
| DealOrNoDeal-v12 | `black` | 59.9 | 1538 | – | black-screen |  |
| DeathTrap1_1 | `black` | 65.4 | 1534 | – | black-screen |  |
| egoboo-cramfs | `black` | 32.9 | 65 | ✓ | black-screen |  |
| escapa-v1 | `black` | 60.6 | 1529 | – | black-screen |  |
| falldown_gp2x | `black` | 60.6 | 1519 | ✓ | black-screen |  |
| fenix | `black` | 11.5 | 26 | ✓ | black-screen |  |
| fenixGamePack | `black` | 14.5 | 47 | ✓ | black-screen |  |
| fifteen_01 | `black` | 59.9 | 1519 | – | black-screen |  |
| FleshChasmer132c_patch | `black` | 9.8 | 5 | ✓ | black-screen |  |
| FleshChasmer_Dpad | `black` | 9.8 | 5 | ✓ | black-screen |  |
| floaters | `black` | 60.4 | 1531 | – | black-screen |  |
| Fragger2x | `black` | 59.1 | 1490 | ✓ | black-screen |  |
| freecell_1 | `black` | 50.4 | 107 | ✓ | black-screen |  |
| frozen2x-0.1 | `black` | 78.5 | 543 | ✓ | black-screen |  |
| fruits_gp2x | `black` | 59.1 | 1495 | – | black-screen |  |
| FyWod_2x | `black` | 60.0 | 1539 | – | black-screen |  |
| game bIld 2 | `black` | 59.3 | 1501 | ✓ | black-screen |  |
| game-watch-mario-bros | `black` | 58.9 | 1486 | ✓ | black-screen |  |
| Ghostbusters_WIP | `black` | 60.2 | 1524 | – | black-screen |  |
| gnp_104 | `black` | 53.9 | 1499 | – | black-screen |  |
| godori | `black` | 7.9 | 4 | – | black-screen |  |
| gp2x-blobwars-0.1 | `black` | 0.1 | 2 | ✓ | black-screen |  |
| gp2x-bubbletrain-0.1 | `black` | 58.5 | 1524 | – | black-screen |  |
| gp2x-formido-0.1 | `black` | 41.4 | 1520 | ✓ | black-screen |  |
| gp2x-netrok-0.1 | `black` | 51.0 | 1511 | – | black-screen |  |
| gp2x-sand-0.3 | `black` | 55.0 | 114 | – | black-screen |  |
| gp2x-tenmado-0.1 | `black` | 0.1 | 2 | – | black-screen |  |
| gp2xDoukutsu-1.04 | `black` | 12.9 | 10 | ✓ | black-screen |  |
| gp2xJenkasNightmare | `black` | 12.9 | 10 | ✓ | black-screen |  |
| GPgeneral | `black` | 4.0 | 2 | – | black-screen |  |
| gpnoid2x | `black` | 22.5 | 20 | ✓ | black-screen |  |
| GPrina-GP2x_v1.0 | `black` | 59.0 | 1513 | ✓ | black-screen |  |
| gr-v1001-gp2x | `black` | 57.9 | 1506 | – | black-screen |  |
| hex-a-hop | `black` | 60.9 | 1535 | – | black-screen |  |
| Jurlx2 | `black` | 60.1 | 1521 | – | black-screen |  |
| just4qix | `black` | 11.1 | 6 | ✓ | black-screen |  |
| koules2x_02 | `black` | 60.1 | 1531 | – | black-screen |  |
| levelEdit | `black` | 60.7 | 1531 | – | black-screen |  |
| liquidwar2x02 | `black` | 3.3 | 2 | – | black-screen |  |
| malvado2x | `black` | 37.6 | 121 | ✓ | black-screen |  |
| MAME-N22_51 | `black` | 57.1 | 1514 | ✓ | black-screen |  |
| mancala-v1.0.1 | `black` | 59.4 | 1508 | – | black-screen |  |
| monacoGP | `black` | 60.2 | 1540 | – | black-screen |  |
| moonlander | `black` | 13.4 | 17 | ✓ | black-screen |  |
| nanobounce-pacc-gp2x | `black` | 57.6 | 1509 | – | black-screen |  |
| nazcarunners-0 | `black` | 24.4 | 26 | ✓ | black-screen |  |
| nazcasphere | `black` | 15.7 | 12 | ✓ | black-screen |  |
| ne_deluxe_gp2x | `black` | 58.5 | 1480 | – | black-screen |  |
| ne_gp2x | `black` | 58.2 | 1469 | – | black-screen |  |
| NecNec2x | `black` | 58.4 | 1488 | ✓ | black-screen |  |
| Nom | `black` | 13.0 | 7 | – | black-screen |  |
| omok | `black` | 52.0 | 110 | ✓ | black-screen |  |
| OpenBOR_v2.1933 | `black` | 58.5 | 1502 | – | black-screen |  |
| OpenBOR_v3.0_Build_2615_&_2637 | `black` | 58.9 | 1511 | – | black-screen |  |
| openggs | `black` | 59.6 | 1532 | ✓ | black-screen |  |
| openjazz-gp2x | `black` | 9.9 | 10 | ✓ | black-screen |  |
| othello_v1.0 | `black` | 59.9 | 1507 | ✓ | black-screen |  |
| pacmame | `black` | 5.2 | 3 | – | black-screen |  |
| para3 | `black` | 59.1 | 1493 | ✓ | black-screen |  |
| pez | `black` | 11.8 | 6 | – | black-screen |  |
| Pong | `black` | 60.6 | 1518 | – | black-screen |  |
| PowerSlide | `black` | 59.3 | 1507 | ✓ | black-screen |  |
| protozoa v1.0 | `black` | 4.1 | 4 | ✓ | black-screen |  |
| Rabbit_vs_Flies_0.9 | `black` | 58.8 | 1491 | ✓ | black-screen |  |
| ramon atacks | `black` | 59.5 | 1501 | ✓ | black-screen |  |
| raw2xv0.3.1 | `black` | 9.8 | 5 | – | black-screen |  |
| rg_105 | `black` | 56.2 | 1518 | – | black-screen |  |
| rg_ura_103 | `black` | 56.5 | 1516 | – | black-screen |  |
| RoundEmUp-alpha3 | `black` | 60.0 | 1521 | – | black-screen |  |
| ruckman_v1.03 | `black` | 29.1 | 78 | ✓ | black-screen |  |
| ShadowWarrior2X | `black` | 5.9 | 3 | – | black-screen |  |
| SimOniZ | `black` | 0.2 | 4 | ✓ | black-screen |  |
| sleuth slots 2x | `black` | 2.1 | 54 | ✓ | black-screen |  |
| SmashGp2x02 | `black` | 55.6 | 1411 | ✓ | black-screen |  |
| snail runers | `black` | 58.7 | 1505 | ✓ | black-screen |  |
| sprint_race | `black` | 7.3 | 5 | – | black-screen |  |
| Squares-v051 | `black` | 59.4 | 1510 | – | black-screen |  |
| starfighter-gp2x-0.01 | `black` | 60.5 | 1523 | ✓ | black-screen |  |
| starsystem | `black` | 16.3 | 10 | ✓ | black-screen |  |
| StarTrucker | `black` | 59.8 | 1515 | – | black-screen |  |
| step2x02 | `black` | 50.8 | 107 | ✓ | black-screen |  |
| SuperChickenFallDemo | `black` | 59.2 | 1495 | ✓ | black-screen |  |
| supertux-0.1.3-gp2x-v4 | `black` | 59.4 | 1472 | ✓ | black-screen |  |
| tesla-Siren | `black` | 19.8 | 12 | ✓ | black-screen |  |
| Tetrablocks.0.4.GP2X | `black` | 47.5 | 110 | ✓ | black-screen |  |
| the reversed preacher II | `black` | 60.0 | 1516 | ✓ | black-screen |  |
| Thruster_GP2X | `black` | 60.8 | 1538 | – | black-screen |  |
| tileworld2x | `black` | 55.8 | 1504 | – | black-screen |  |
| tilt | `black` | 19.2 | 13 | ✓ | black-screen |  |
| tower | `black` | 60.6 | 1536 | – | black-screen |  |
| TRAINS | `black` | 6.9 | 4 | ✓ | black-screen |  |
| ttxbeta170706b | `black` | 56.7 | 1517 | – | black-screen |  |
| uhexen | `black` | 3.7 | 2 | – | black-screen |  |
| ultratumba_exp-20100925.gp2x | `black` | 9.5 | 5 | ✓ | black-screen |  |
| Unicolor | `black` | 60.2 | 1539 | – | black-screen |  |
| Volleyball | `black` | 59.7 | 1508 | – | black-screen |  |
| vorton-b4 | `black` | 57.7 | 1514 | ✓ | black-screen |  |
| vwars | `black` | 59.1 | 1530 | ✓ | black-screen |  |
| war_and_warriorgp2x | `black` | 60.7 | 1529 | – | black-screen |  |
| whacky | `black` | 58.6 | 1482 | – | black-screen |  |
| Winter_Jumper | `black` | 3.9 | 2 | – | black-screen |  |
| wire3d | `black` | 62.0 | 1447 | – | black-screen |  |
| Wizznic_2x_07alpha2 | `black` | 10.0 | 17 | ✓ | black-screen |  |
| wizznic_gp2x-0.9.9 | `black` | 10.8 | 20 | ✓ | black-screen |  |
| wolfdx | `black` | 43.3 | 56 | ✓ | black-screen |  |
| xbak-0.1.3 | `black` | 18.6 | 10 | – | black-screen |  |
| xcom1-v1.0.2-gp2x | `black` | 101.1 | 2587 | ✓ | black-screen |  |
| xcom2-v1.0.1-gp2x | `black` | 100.9 | 2607 | ✓ | black-screen |  |
| xigon-X-gp2x-V1 | `black` | 60.2 | 1518 | ✓ | black-screen |  |
| xump2x_beta2 | `black` | 27.0 | 14 | ✓ | black-screen |  |
| yahtzee-v21 | `black` | 60.3 | 1522 | – | black-screen |  |
| Zelda_roth_US_gp2x | `black` | 19.0 | 40 | ✓ | black-screen |  |
| Zoids Quest2X-0.0.1-2 | `black` | 53.0 | 372 | ✓ | black-screen |  |
| zombiesorbet_v1.0_gp2x | `black` | 38.5 | 78 | ✓ | black-screen |  |
| 1945_GP2X_0.2b | `ingame` | 83.2 | 585 | ✓ | garbled-visuals | pixel-to-pixel noise of 96, far above what dithered artwork reaches; the frame looks like  |
| 2xZdoom_selector | `ingame` | 2.5 | 6 | ✓ | flat-fill |  |
| a_sn-pong | `ingame` | 40.6 | 1503 | – | no-audio |  |
| alex | `ingame` | 60.6 | 1526 | – | flat-fill |  |
| alex4_gp2x | `ingame` | 60.3 | 1521 | – | flat-fill |  |
| AMazing-3D | `ingame` | 63.4 | 1605 | – | no-audio |  |
| ASCIIPong2xV0.4 | `ingame` | 36.2 | 909 | ✓ | flat-fill |  |
| BareFistFighter | `ingame` | 59.5 | 1499 | ✓ | flat-fill |  |
| battlejewels-gp2x-062-100 | `ingame` | 0.1 | 2 | ✓ | low-fps |  |
| Birdshoot | `ingame` | 61.0 | 1531 | – | no-audio |  |
| Blocked | `ingame` | 3.8 | 96 | ✓ | low-fps |  |
| bugafactorx-v03-beta | `ingame` | 59.7 | 1519 | – | no-audio |  |
| BunnyTraps-v11 | `ingame` | 61.3 | 1546 | ✓ | garbled-visuals | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like |
| buscaminas | `ingame` | 60.1 | 1525 | – | no-audio |  |
| cardm | `ingame` | 60.4 | 1532 | – | no-audio |  |
| cavecopter_gp2x | `ingame` | 60.8 | 1526 | – | no-audio |  |
| Chopper | `ingame` | 57.4 | 1460 | – | no-audio |  |
| Clonk2X_1.0 | `ingame` | 11.1 | 280 | – | not-arm-elf | magiceyes: reload of '/bin/sh' failed |
| ConnyCarrot | `ingame` | 59.5 | 1517 | ✓ | flat-fill |  |
| Digger | `ingame` | 2.6 | 6 | ✓ | flat-fill |  |
| dodge | `ingame` | 14.1 | 15 | ✓ | flat-fill |  |
| dopewars2x | `ingame` | 60.4 | 1518 | – | no-audio |  |
| drod-gp2x-1_0 | `ingame` | 51.8 | 1350 | – | no-audio |  |
| dstroyGP2X1402 | `ingame` | 58.7 | 1508 | – | no-audio |  |
| dumbbell2x-01 | `ingame` | 58.7 | 407 | – | flat-fill |  |
| escoba_exp-20101016.gp2x | `ingame` | 60.2 | 1534 | – | no-audio |  |
| extraterrestres-0 | `ingame` | 57.4 | 1554 | – | no-audio |  |
| FCRLG | `ingame` | 59.6 | 1501 | – | no-audio |  |
| Firewhip | `ingame` | 0.6 | 8 | ✓ | low-fps |  |
| FleshChasmer | `ingame` | 59.4 | 1519 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 160px; left and right halves are near- |
| flowflowmania-0_6-gp2x | `ingame` | 42.4 | 313 | – | no-audio |  |
| freesci | `ingame` | 54.7 | 248 | – | no-audio |  |
| fruits2x | `ingame` | 47.3 | 98 | – | no-audio |  |
| gchess-v1.0.1-bin | `ingame` | 60.1 | 1526 | – | no-audio |  |
| gchess-v1.1.0-bin | `ingame` | 59.4 | 1507 | – | no-audio |  |
| gemdrop2x_v02 | `ingame` | 89.4 | 2303 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 96px |
| GF | `ingame` | 59.8 | 1527 | ✓ | garbled-visuals | top and bottom halves are near-identical |
| gnugo2x | `ingame` | 60.7 | 1536 | – | no-audio |  |
| gp2x-ceferino-0.1 | `ingame` | 64.2 | 1534 | – | no-audio |  |
| gp2xgo-v1.1.0-bin | `ingame` | 60.3 | 1528 | – | no-audio |  |
| gp2xmancala-v1.1.1-bin | `ingame` | 60.4 | 1531 | – | no-audio |  |
| GP2XOfLife | `ingame` | 4.4 | 111 | – | low-fps |  |
| GPSquares_GP2X | `ingame` | 59.8 | 1505 | – | no-audio |  |
| grow | `ingame` | 40.5 | 1498 | – | no-audio |  |
| gxeskiv | `ingame` | 57.7 | 1459 | – | no-audio |  |
| HumphreyGP2X | `ingame` | 59.9 | 1527 | ✓ | flat-fill |  |
| kampfimall-gp2x | `ingame` | 60.6 | 1529 | – | flat-fill |  |
| kampfimall-gp2x-music | `ingame` | 60.6 | 1526 | – | flat-fill |  |
| Knight Lore | `ingame` | 10.7 | 269 | ✓ | flat-fill |  |
| LABYRINTH | `ingame` | 59.9 | 1513 | – | no-audio |  |
| las-tres-luces-de-glaurung-remake | `ingame` | 59.9 | 1536 | ✓ | flat-fill |  |
| Life.0.1 | `ingame` | 61.8 | 1557 | – | garbled-visuals | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like |
| masterpiece2x | `ingame` | 59.8 | 1510 | – | no-audio |  |
| minigolf | `ingame` | 59.4 | 1507 | – | no-audio |  |
| MoveSweep2X | `ingame` | 49.4 | 101 | – | garbled-visuals | the screen holds a second copy of itself, offset by 160px; left and right halves are near- |
| Nebulus_gp2x | `ingame` | 60.2 | 1512 | – | no-audio |  |
| Net-Bubble-gp2x_1-21-06_bin | `ingame` | 51.2 | 353 | – | no-audio |  |
| oxov06 | `ingame` | 45.2 | 93 | – | no-audio |  |
| Payback | `ingame` | 0.1 | 2 | ✓ | low-fps |  |
| PerfectFit | `ingame` | 61.3 | 1541 | – | no-audio |  |
| PocketSnes_SMRPG | `ingame` | 110.2 | 2772 | – | no-audio |  |
| Pond2X | `ingame` | 58.9 | 1484 | – | flat-fill |  |
| pong2player | `ingame` | 55.1 | 113 | – | no-audio |  |
| pong2v060x | `ingame` | 53.7 | 110 | – | no-audio |  |
| powder2x-112 | `ingame` | 59.8 | 1526 | – | no-audio |  |
| prboom-gp2x | `ingame` | 59.4 | 1526 | – | no-audio |  |
| RevoltOfTheBinaryCouriers GP2X | `ingame` | 59.6 | 1504 | – | no-audio |  |
| robot-escape | `ingame` | 82.6 | 173 | ✓ | flat-fill |  |
| scummvm-kor0.4.2cvs | `ingame` | 57.6 | 1462 | – | no-audio |  |
| sdlmonkey_0.1 | `ingame` | 60.9 | 1536 | – | no-audio |  |
| snake2x-1.1 | `ingame` | 60.2 | 1542 | – | no-audio |  |
| Solitaire2x-v1.4 | `ingame` | 87.1 | 602 | – | no-audio |  |
| sources_MEMORY2X | `ingame` | 59.6 | 1519 | – | no-audio |  |
| space squares | `ingame` | 60.4 | 1541 | – | no-audio |  |
| SpaceRocks2X | `ingame` | 150.3 | 80 | – | no-audio |  |
| spartak-chess_0.0.4_gp2x | `ingame` | 60.3 | 1532 | – | no-audio |  |
| Sponge Blob Tennis | `ingame` | 41.4 | 1531 | – | no-audio |  |
| spout | `ingame` | 60.4 | 1522 | – | no-audio |  |
| Sqcolony | `ingame` | 62.9 | 1583 | – | no-audio |  |
| StairwayToHeaven | `ingame` | 59.8 | 1510 | ✓ | flat-fill |  |
| sudoku-v1.0 | `ingame` | 59.9 | 1512 | – | no-audio |  |
| sudoku2x-0.5 | `ingame` | 60.4 | 1525 | – | no-audio |  |
| Tangle | `ingame` | 61.1 | 1537 | – | no-audio |  |
| TimeFrack2D for GP2X | `ingame` | 48.2 | 98 | – | no-audio |  |
| VekDemo2 | `ingame` | 0.6 | 15 | ✓ | low-fps |  |
| Vektar | `ingame` | 0.3 | 7 | ✓ | low-fps |  |
| vektar-free | `ingame` | 12.1 | 308 | ✓ | low-fps |  |
| vexedb1 | `ingame` | 59.8 | 1514 | – | no-audio |  |
| waffle2x | `ingame` | 44.7 | 91 | – | no-audio |  |
| Wiztern Demo | `ingame` | 1.0 | 27 | ✓ | low-fps |  |
| 2xpong_gp2x | `playable` | 60.2 | 1516 | ✓ |  |  |
| 2xtron-v01 | `playable` | 60.9 | 1535 | ✓ |  |  |
| AdamantArmorAffection2x | `playable` | 58.7 | 1487 | ✓ |  |  |
| airstrike-1.1 | `playable` | 59.5 | 1514 | ✓ |  |  |
| Akd_BB | `playable` | 60.7 | 1536 | ✓ |  |  |
| altitude | `playable` | 37.9 | 124 | ✓ |  |  |
| amoebax-0.2.1-gp2x | `playable` | 54.9 | 1398 | ✓ |  |  |
| armorcity-0_30b | `playable` | 59.2 | 1520 | ✓ |  |  |
| Asteroids | `playable` | 58.9 | 1511 | ✓ |  |  |
| astrochaos | `playable` | 55.7 | 312 | ✓ |  |  |
| Batiscafo (versin EXP) | `playable` | 60.1 | 1517 | ✓ |  |  |
| beat2x-0.5-bin | `playable` | 58.7 | 1493 | ✓ |  |  |
| Beatbox_1.2 | `playable` | 58.0 | 1529 | ✓ |  |  |
| biniax-gp2x_v1.2 | `playable` | 60.5 | 1524 | ✓ |  |  |
| Biniax2_gp2x | `playable` | 60.4 | 1531 | ✓ |  |  |
| BioShoot GP2X | `playable` | 59.3 | 1526 | ✓ |  |  |
| blazar_v1-30_gp2x | `playable` | 60.6 | 1531 | ✓ |  |  |
| blipsgp2x | `playable` | 59.2 | 1541 | ✓ |  |  |
| blobbyvolley | `playable` | 60.1 | 1529 | ✓ |  |  |
| blobwars_2x | `playable` | 61.6 | 1561 | ✓ |  |  |
| block | `playable` | 60.3 | 1530 | ✓ |  |  |
| blockdudegp2x | `playable` | 56.1 | 1531 | ✓ |  |  |
| Blockrage2x | `playable` | 60.2 | 1524 | ✓ |  |  |
| bluecube2x | `playable` | 60.3 | 1519 | ✓ |  |  |
| Boomshine2x_1.12_gp2x | `playable` | 59.3 | 1522 | ✓ |  |  |
| brassmunkey_gp2x_1.0 | `playable` | 60.6 | 1532 | ✓ |  |  |
| BubbleX | `playable` | 61.3 | 1539 | ✓ |  |  |
| BubTrain_GP2X-2006_Entry_No-Sound | `playable` | 57.3 | 1534 | ✓ |  |  |
| BugWarsSE_v1.0 | `playable` | 59.1 | 1527 | ✓ |  |  |
| bumprace-0.2 | `playable` | 59.1 | 1538 | ✓ |  |  |
| BurokkuDemo1 | `playable` | 60.5 | 1533 | ✓ |  |  |
| buttongame | `playable` | 45.9 | 97 | ✓ |  |  |
| BuzzysBadDay-1.0 | `playable` | 60.6 | 1536 | ✓ |  |  |
| CamelotWarriors-GP2x_v1.0 | `playable` | 59.2 | 1512 | ✓ |  |  |
| CascadeBeneath v1.0 for GP2X | `playable` | 62.8 | 1580 | ✓ |  |  |
| ccrg | `playable` | 53.2 | 233 | ✓ |  |  |
| chaos2x | `playable` | 61.4 | 1544 | ✓ |  |  |
| checkersgp2x | `playable` | 58.5 | 1545 | ✓ |  |  |
| chess2x05 | `playable` | 58.7 | 1524 | ✓ |  |  |
| chuckiev12 | `playable` | 60.7 | 1529 | ✓ |  |  |
| CowSuckers-1.0 | `playable` | 68.3 | 1533 | ✓ |  |  |
| Crapong | `playable` | 59.5 | 1521 | ✓ |  |  |
| crazeeman | `playable` | 63.5 | 1610 | ✓ |  |  |
| crimsonV1 | `playable` | 58.6 | 1527 | ✓ |  |  |
| CromoZome | `playable` | 58.5 | 1527 | ✓ |  |  |
| DABAKKA-0 | `playable` | 61.3 | 1538 | ✓ |  |  |
| Dastardly dungeon 1.5 | `playable` | 41.9 | 103 | ✓ |  |  |
| dd2x | `playable` | 101.2 | 463 | ✓ |  |  |
| debian_vs_pimientos_2x_0.1.2 | `playable` | 60.0 | 1537 | ✓ |  |  |
| defeatme-gp2x-1.0.1 | `playable` | 59.8 | 1510 | ✓ |  |  |
| DontGetCrushed v1.0 for GP2X | `playable` | 60.4 | 1519 | ✓ |  |  |
| dosmugen | `playable` | 59.2 | 1534 | ✓ |  |  |
| Drill2x_final | `playable` | 59.1 | 1517 | ✓ |  |  |
| drill2x_xtreme_v1.0.3 | `playable` | 56.1 | 1429 | ✓ |  |  |
| DubaiRace038a | `playable` | 48.1 | 110 | ✓ |  |  |
| dynamategp2x | `playable` | 57.9 | 1507 | ✓ |  |  |
| eggstreme3_v1-00_gp2x | `playable` | 59.5 | 1498 | ✓ |  |  |
| egoboo2xFeb1207 | `playable` | 106.4 | 3041 | ✓ |  |  |
| Electronia | `playable` | 60.9 | 1537 | ✓ |  |  |
| enigma | `playable` | 56.9 | 736 | ✓ |  |  |
| entombed2x | `playable` | 58.5 | 1489 | ✓ |  |  |
| EpicFreeFall_GP2X | `playable` | 59.4 | 1519 | ✓ |  |  |
| EpicRocks_GP2X | `playable` | 57.0 | 640 | ✓ |  |  |
| exi_shoot_gp2x | `playable` | 60.9 | 1533 | ✓ |  |  |
| extraterrestres | `playable` | 95.6 | 2616 | ✓ |  |  |
| exult_rc3 | `playable` | 40.9 | 1513 | ✓ |  |  |
| Factor-v1.0-final | `playable` | 59.6 | 1525 | ✓ |  |  |
| Fishball-1.2 | `playable` | 60.6 | 1529 | ✓ |  |  |
| fissionfield2x | `playable` | 60.8 | 1537 | ✓ |  |  |
| Flaschenspiel | `playable` | 60.1 | 1533 | ✓ |  |  |
| FleshChasmer Zero | `playable` | 60.0 | 1518 | ✓ |  |  |
| flobopuyo0.20.1 | `playable` | 59.2 | 1507 | ✓ |  |  |
| flurkies_v1-25_gp2x | `playable` | 60.9 | 1537 | ✓ |  |  |
| fm | `playable` | 100.8 | 2526 | ✓ |  |  |
| formula1gp2x | `playable` | 59.9 | 1531 | ✓ |  |  |
| friq-beta-07 | `playable` | 60.4 | 1546 | ✓ |  |  |
| fvc | `playable` | 60.1 | 1514 | ✓ |  |  |
| ghostpix_v10_gp2x | `playable` | 59.0 | 1504 | ✓ |  |  |
| GoitGP | `playable` | 59.0 | 1524 | ✓ |  |  |
| gp2hanoi_0.8.1_gp2x | `playable` | 60.7 | 1535 | ✓ |  |  |
| gp2x-invaders-preview-version | `playable` | 61.2 | 1546 | ✓ |  |  |
| gp2x-shienso-bin_061021 | `playable` | 60.8 | 1539 | ✓ |  |  |
| gp2x-smc-0.1 | `playable` | 55.9 | 1520 | ✓ |  |  |
| gp2x_2xmas | `playable` | 58.0 | 1490 | ✓ |  |  |
| GP2X_BallGame_0.49 | `playable` | 60.9 | 1531 | ✓ |  |  |
| gp2xjunkie | `playable` | 57.9 | 1494 | ✓ |  |  |
| gp2xpang-v.1.1.1 | `playable` | 97.0 | 2514 | ✓ |  |  |
| gp2xrick 1.0 | `playable` | 58.8 | 1491 | ✓ |  |  |
| GpFrontier v0.1 | `playable` | 60.8 | 1546 | ✓ |  |  |
| gpfrontier v0.4 | `playable` | 57.6 | 1525 | ✓ |  |  |
| green | `playable` | 59.1 | 1502 | ✓ |  |  |
| hanagechu2x_gbax2007 | `playable` | 63.0 | 1596 | ✓ |  |  |
| hanagechu2xalpha | `playable` | 61.1 | 1539 | ✓ |  |  |
| Heretic2X_v0.5 | `playable` | 59.6 | 1497 | ✓ |  |  |
| hexbattle2x | `playable` | 57.9 | 1472 | ✓ |  |  |
| Hyperion_GP2X_demo | `playable` | 60.8 | 1542 | ✓ |  |  |
| jumpnbumpgp2x | `playable` | 60.0 | 1540 | ✓ |  |  |
| ketm_2x_gp2x | `playable` | 51.5 | 1525 | ✓ |  |  |
| KicknPLay_1.1 | `playable` | 61.1 | 1536 | ✓ |  |  |
| kuklomenos_gp2x_201209 | `playable` | 41.7 | 1544 | ✓ |  |  |
| kurukuru2x | `playable` | 60.6 | 1541 | ✓ |  |  |
| la | `playable` | 129.7 | 285 | ✓ |  |  |
| ladykiller | `playable` | 60.5 | 1541 | ✓ |  |  |
| lbreakoutgp2x | `playable` | 58.0 | 1549 | ✓ |  |  |
| levelshmup | `playable` | 118.4 | 3028 | ✓ |  |  |
| LinesXv3 | `playable` | 61.0 | 1534 | ✓ |  |  |
| logicx | `playable` | 61.1 | 1537 | ✓ |  |  |
| mad-mix-game-20b-final | `playable` | 58.1 | 1477 | ✓ |  |  |
| madbomber | `playable` | 59.2 | 1522 | ✓ |  |  |
| Masteries_Journey_to_the_Center_of_the_earth_GP2X | `playable` | 57.8 | 1515 | ✓ |  |  |
| MazeThingie | `playable` | 60.7 | 1536 | ✓ |  |  |
| MemoryGP2X-v11 | `playable` | 60.9 | 1542 | ✓ |  |  |
| meritous | `playable` | 53.7 | 371 | ✓ |  |  |
| Merlin2x_beta_021 | `playable` | 53.8 | 374 | ✓ |  |  |
| metaphysik | `playable` | 57.5 | 1462 | ✓ |  |  |
| methaneV1 | `playable` | 67.5 | 1511 | ✓ |  |  |
| minos-gp2x | `playable` | 59.2 | 1501 | ✓ |  |  |
| mk13.gpe | `playable` | 61.2 | 1533 | ✓ |  |  |
| mkACE.gpe | `playable` | 61.1 | 1533 | ✓ |  |  |
| mkONE.gpe | `playable` | 61.2 | 1539 | ✓ |  |  |
| MM2X | `playable` | 60.4 | 1523 | ✓ |  |  |
| monochromeworlds-gp2x-1.0.0 | `playable` | 60.2 | 1530 | ✓ |  |  |
| mush_gp2x | `playable` | 42.1 | 1092 | ✓ |  |  |
| Mutant Tank Knights | `playable` | 53.3 | 193 | ✓ |  |  |
| MyriadUpdated | `playable` | 57.5 | 1509 | ✓ |  |  |
| mzx-2.84c | `playable` | 60.2 | 1522 | ✓ |  |  |
| mzx282-gp2x | `playable` | 58.7 | 1478 | ✓ |  |  |
| n-tris_v1.0 | `playable` | 60.7 | 1524 | ✓ |  |  |
| newsuperpang | `playable` | 60.1 | 1527 | ✓ |  |  |
| Nifty | `playable` | 58.7 | 1502 | ✓ |  |  |
| odonata_demo | `playable` | 58.5 | 1481 | ✓ |  |  |
| openglad2x | `playable` | 58.0 | 1517 | ✓ |  |  |
| opentyrian2x_0.3_complete | `playable` | 54.5 | 1497 | ✓ |  |  |
| OrbitalSniper2x_v1.1 | `playable` | 57.5 | 141 | ✓ |  |  |
| PAF | `playable` | 60.4 | 1523 | ✓ |  |  |
| paraballgp2x | `playable` | 59.9 | 1520 | ✓ |  |  |
| PaybackDemo | `playable` | 26.9 | 708 | ✓ |  |  |
| pdcv060b | `playable` | 49.8 | 347 | ✓ |  |  |
| Phishy-0 | `playable` | 59.7 | 1530 | ✓ |  |  |
| physique | `playable` | 57.1 | 1448 | ✓ |  |  |
| Pika2x | `playable` | 58.7 | 412 | ✓ |  |  |
| pintor2x | `playable` | 77.0 | 1933 | ✓ |  |  |
| pixpang | `playable` | 53.8 | 1516 | ✓ |  |  |
| Poker2x | `playable` | 112.0 | 774 | ✓ |  |  |
| Pool Panic | `playable` | 59.9 | 1512 | ✓ |  |  |
| powermanga-0.80 | `playable` | 53.3 | 1413 | ✓ |  |  |
| proj0-demo_01 | `playable` | 59.3 | 1520 | ✓ |  |  |
| PulplifeWars | `playable` | 58.1 | 1512 | ✓ |  |  |
| puzzlelandgp2x | `playable` | 55.4 | 1534 | ✓ |  |  |
| qfg3-0 | `playable` | 59.4 | 1542 | ✓ |  |  |
| quartz2_v1-50_gp2x | `playable` | 61.1 | 1537 | ✓ |  |  |
| Release GP2X MST_RUNNERS | `playable` | 58.0 | 1509 | ✓ |  |  |
| retrovirus_1_1 | `playable` | 58.4 | 1481 | ✓ |  |  |
| reword_v0.5 | `playable` | 60.5 | 1546 | ✓ |  |  |
| river | `playable` | 60.7 | 1523 | ✓ |  |  |
| RockRain | `playable` | 59.4 | 1500 | ✓ |  |  |
| rockrain2_exp-20100925 | `playable` | 59.9 | 1526 | ✓ |  |  |
| rookiehero_EXP.gp2x.v20120220 | `playable` | 59.8 | 1532 | ✓ |  |  |
| rubidogp2x | `playable` | 58.7 | 1539 | ✓ |  |  |
| Runner_GP2X | `playable` | 58.9 | 1517 | ✓ |  |  |
| s-tris2_v1-64_gp2x | `playable` | 61.0 | 1536 | ✓ |  |  |
| sachunsungx | `playable` | 60.3 | 1517 | ✓ |  |  |
| ScorchedGPBeta2 | `playable` | 58.3 | 1482 | ✓ |  |  |
| scummvm-0.11.1-gp2x | `playable` | 57.5 | 1500 | ✓ |  |  |
| scummvm-1.2.0-gp2x | `playable` | 58.8 | 1531 | ✓ |  |  |
| SdLame | `playable` | 59.2 | 1509 | ✓ |  |  |
| ShanghaiX | `playable` | 61.3 | 1540 | ✓ |  |  |
| SheepDash | `playable` | 58.2 | 1540 | ✓ |  |  |
| Shippy84 | `playable` | 59.5 | 1535 | ✓ |  |  |
| siv050 | `playable` | 57.3 | 1525 | ✓ |  |  |
| SmallBall_GP | `playable` | 58.0 | 1492 | ✓ |  |  |
| snowedin5_v1-00_gp2x | `playable` | 60.0 | 1513 | ✓ |  |  |
| SOD v1.1 | `playable` | 58.7 | 1524 | ✓ |  |  |
| sokobangp2x | `playable` | 53.3 | 1541 | ✓ |  |  |
| sources_Yahtzee | `playable` | 50.0 | 236 | ✓ |  |  |
| space_varments_v1.0 | `playable` | 54.6 | 546 | ✓ |  |  |
| Squaresliding | `playable` | 60.6 | 1525 | ✓ |  |  |
| stppc2x-v1.0 | `playable` | 41.6 | 1539 | ✓ |  |  |
| stransball2 | `playable` | 59.5 | 1508 | ✓ |  |  |
| street2x | `playable` | 54.8 | 1529 | ✓ |  |  |
| subhunt | `playable` | 59.6 | 1511 | ✓ |  |  |
| SuperPaf_v1.0 | `playable` | 58.5 | 1499 | ✓ |  |  |
| superpang | `playable` | 38.6 | 97 | ✓ |  |  |
| SuperPixelJumper v1.1 for GP2X | `playable` | 59.6 | 1500 | ✓ |  |  |
| SuperSonicSpeed | `playable` | 60.5 | 1531 | ✓ |  |  |
| survival | `playable` | 60.5 | 1545 | ✓ |  |  |
| symbolica-0.8 | `playable` | 59.9 | 1521 | ✓ |  |  |
| tail-tale | `playable` | 60.1 | 1519 | ✓ |  |  |
| tecnoballz-0.91-gp2x | `playable` | 56.0 | 1457 | ✓ |  |  |
| tetwins | `playable` | 51.6 | 109 | ✓ |  |  |
| ThreeTs_Game | `playable` | 52.6 | 108 | ✓ |  |  |
| tikka_dungeons_demo_1 | `playable` | 58.4 | 1471 | ✓ |  |  |
| tilematch-0.6 | `playable` | 91.5 | 2311 | ✓ |  |  |
| towertopplergp2x | `playable` | 55.8 | 1470 | ✓ |  |  |
| treev060 | `playable` | 59.3 | 1548 | ✓ |  |  |
| vectoroids-2x | `playable` | 60.0 | 1519 | ✓ |  |  |
| vektarpack_v1 | `playable` | 64.3 | 1639 | ✓ |  |  |
| Ventifact | `playable` | 59.0 | 1531 | ✓ |  |  |
| vexed-gp2x-10 | `playable` | 59.9 | 1515 | ✓ |  |  |
| warcraft | `playable` | 59.5 | 1544 | ✓ |  |  |
| warehouse_panic_v1.1_gp2x | `playable` | 50.9 | 1532 | ✓ |  |  |
| waternetgp2x | `playable` | 97.7 | 2661 | ✓ |  |  |
| wehaveballs | `playable` | 60.2 | 1517 | ✓ |  |  |
| WindAndWater_teaser_110 | `playable` | 59.3 | 1493 | ✓ |  |  |
| wnw | `playable` | 59.9 | 1522 | ✓ |  |  |
| xenitris_demo | `playable` | 61.1 | 1540 | ✓ |  |  |
| Xpired2x 1.0 beta | `playable` | 55.2 | 388 | ✓ |  |  |
| xRick | `playable` | 58.0 | 1481 | ✓ |  |  |
| znax | `playable` | 58.1 | 1535 | ✓ |  |  |
| zoltan 2x | `playable` | 58.4 | 1508 | ✓ |  |  |
| ztunnel-0 | `playable` | 55.7 | 1411 | ✓ |  |  |

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
| albion-v1.0.1-wiz | `black` | 30.5 | 41 | ✓ | black-screen |  |
| Balloonacy_wiz_wip | `black` | 0.2 | 4 | ✓ | black-screen |  |
| CartoonWiz | `black` | 0.2 | 4 | ✓ | black-screen |  |
| chroma 1.01 v1 | `black` | 0.5 | 3 | – | black-screen |  |
| ColonyConflict_V1.1_B6 | `black` | 0.1 | 4 | ✓ | black-screen |  |
| DungeonRunner | `black` | 0.2 | 4 | ✓ | black-screen |  |
| DuoWIZ_Pong | `black` | 0.1 | 3 | ✓ | black-screen |  |
| freecell2x | `black` | 0.1 | 4 | ✓ | black-screen |  |
| herknights | `black` | 57.9 | 1508 | ✓ | black-screen |  |
| March of the mini tux(wiz version) | `black` | 0.2 | 4 | ✓ | black-screen |  |
| opentyrian | `black` | 11.1 | 8 | – | black-screen |  |
| pgw | `black` | 57.3 | 1519 | – | black-screen |  |
| PPlane2.WIZ | `black` | 0.1 | 3 | ✓ | black-screen |  |
| SimOniZ | `black` | 0.1 | 3 | ✓ | black-screen |  |
| tetwizdownload | `black` | 0.1 | 3 | ✓ | black-screen |  |
| Trap75 | `black` | 60.5 | 1523 | ✓ | black-screen |  |
| TUcS.app(V0.7.0 - Wiz) | `black` | 0.2 | 4 | ✓ | black-screen |  |
| warcraft-beta3-wiz | `black` | 40.4 | 46 | ✓ | black-screen |  |
| wiz-car-binary_090818a | `black` | 59.9 | 1508 | ✓ | black-screen |  |
| Wiz_Blox | `black` | 0.2 | 4 | ✓ | black-screen |  |
| wiz_drench | `black` | 0.1 | 3 | ✓ | black-screen |  |
| WIZ_S4S | `black` | 0.2 | 4 | ✓ | black-screen |  |
| WizSticks | `black` | 0.1 | 4 | ✓ | black-screen |  |
| xcom1-v1.0.2-wiz | `black` | 73.1 | 160 | ✓ | black-screen |  |
| xcom2-v1.0.1-wiz | `black` | 119.6 | 3074 | ✓ | black-screen |  |
| spout | `ingame` | 60.7 | 1529 | – | no-audio |  |
| Sudoku2X | `ingame` | 60.6 | 1528 | – | no-audio |  |
| wizchess-v1.1.0-bin | `ingame` | 59.9 | 1517 | – | no-audio |  |
| wizchess-v1.2.0-bin | `ingame` | 60.3 | 1526 | – | no-audio |  |
| wizgo-v1.1.0-bin | `ingame` | 60.0 | 1522 | – | no-audio |  |
| WizGolf | `ingame` | 60.3 | 1526 | – | no-audio |  |
| wizmancala-v1.1.2-bin | `ingame` | 59.8 | 1512 | – | no-audio |  |
| Worship Vector | `ingame` | 60.4 | 1522 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 160px; left and right halves are near- |
| AdamantArmorAffectionWiz | `playable` | 60.2 | 1526 | ✓ |  |  |
| airstrike-wiz-1.01 | `playable` | 60.1 | 1532 | ✓ |  |  |
| alexsfalldown | `playable` | 61.1 | 1533 | ✓ |  |  |
| Blix2x | `playable` | 60.8 | 1528 | ✓ |  |  |
| Dd2x | `playable` | 56.6 | 260 | ✓ |  |  |
| deicide3_eng | `playable` | 60.4 | 1518 | ✓ |  |  |
| gr-v1001-wiz | `playable` | 58.7 | 1514 | ✓ |  |  |
| hexen2 | `playable` | 57.7 | 1487 | ✓ |  |  |
| minos-gp2x-wiz | `playable` | 58.8 | 1490 | ✓ |  |  |
| mush_gp2x | `playable` | 45.6 | 1173 | ✓ |  |  |
| mush_gp2x-0 | `playable` | 36.6 | 972 | ✓ |  |  |
| Pentominos | `playable` | 60.8 | 1528 | ✓ |  |  |
| Pharaoh | `playable` | 51.3 | 109 | ✓ |  |  |
| PuzzleDevilWizDemo | `playable` | 62.7 | 1587 | ✓ |  |  |
| Rezerwar | `playable` | 53.5 | 693 | ✓ |  |  |
| rockrain-gp2x-wiz | `playable` | 60.6 | 1525 | ✓ |  |  |
| Sachunsung2 | `playable` | 49.7 | 102 | ✓ |  |  |
| scummvm-1.2.0-gp2xwiz | `playable` | 56.6 | 1483 | ✓ |  |  |
| Shanghai2 | `playable` | 50.6 | 108 | ✓ |  |  |
| Sopwith | `playable` | 67.8 | 1711 | ✓ |  |  |
| Sqdef_Wiz_14A | `playable` | 59.9 | 1532 | ✓ |  |  |
| Tail Tale | `playable` | 60.6 | 1527 | ✓ |  |  |
| wizznic-0.9.9-wiz | `playable` | 58.1 | 1515 | ✓ |  |  |
| wnw_demo | `playable` | 59.5 | 1505 | ✓ |  |  |
| xRick | `playable` | 60.2 | 1530 | ✓ |  |  |
| znumbers | `playable` | 49.4 | 103 | ✓ |  |  |

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
| PUZZLEBOARDS | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
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
| aquaVenture | `black` | 37.2 | 949 | ✓ | black-screen |  |
| arcadevol1 | `black` | 57.0 | 1478 | – | black-screen |  |
| Arcadevol2 | `black` | 59.9 | 1532 | – | black-screen |  |
| Arcadevol3 | `black` | 58.8 | 1534 | – | black-screen |  |
| B'lox! | `black` | 14.7 | 213 | ✓ | black-screen |  |
| Blingo | `black` | 33.7 | 98 | ✓ | black-screen |  |
| BubbleTrain | `black` | 1.0 | 2 | ✓ | black-screen |  |
| DealorNoDeal | `black` | 59.3 | 1519 | – | black-screen |  |
| Flappynerd_Caanoo | `black` | 13.6 | 197 | ✓ | black-screen |  |
| Guru Logic | `black` | 13.1 | 190 | ✓ | black-screen |  |
| JUMPNRUN | `black` | 54.7 | 1503 | ✓ | black-screen |  |
| next_element | `black` | 60.4 | 1523 | – | black-screen |  |
| RACING | `black` | 57.7 | 1529 | – | black-screen |  |
| SimOniZ | `black` | 4.4 | 39 | ✓ | black-screen |  |
| STRATEGY | `black` | 58.4 | 1526 | – | black-screen |  |
| Tile | `black` | 53.9 | 1506 | – | black-screen |  |
| Balloonacy | `ingame` | 0.1 | 2 | – | flat-fill |  |
| Blitz | `ingame` | 0.1 | 2 | – | flat-fill |  |
| caanoo-biniax2-v1.30-bin | `ingame` | 16.2 | 409 | ✓ | low-fps |  |
| caanoo-chess-v1.1.0-bin | `ingame` | 48.3 | 1223 | – | no-audio |  |
| caanoo-go-v1.1.0-bin | `ingame` | 41.5 | 1050 | – | no-audio |  |
| caanoo-mancala-v1.1.0-bin | `ingame` | 48.5 | 1224 | – | no-audio |  |
| cat_trap | `ingame` | 0.1 | 2 | – | flat-fill |  |
| Drench | `ingame` | 0.1 | 2 | – | flat-fill |  |
| Geek_em_up_CAANOO | `ingame` | 0.1 | 2 | – | flat-fill |  |
| gnp_104 | `ingame` | 47.2 | 1270 | ✓ | flat-fill |  |
| gr-v1001-caanoo | `ingame` | 9.5 | 248 | ✓ | low-fps |  |
| jump_n_blob_caanoo | `ingame` | 10.8 | 287 | ✓ | low-fps |  |
| knight | `ingame` | 10.5 | 265 | ✓ | flat-fill |  |
| Liar | `ingame` | 14.5 | 365 | ✓ | low-fps |  |
| llcpcls-caanoo | `ingame` | 16.2 | 35 | ✓ | low-fps |  |
| MISC | `ingame` | 59.1 | 1566 | – | no-audio |  |
| MNV_Caanoo_Release1 | `ingame` | 46.7 | 1206 | ✓ | flat-fill |  |
| noiz2sa_caanoo | `ingame` | 15.8 | 129 | ✓ | flat-fill |  |
| nuclearchess | `ingame` | 3429.5 | 4164 | – | garbled-visuals | renders at 26x26 instead of 320x240 |
| powder | `ingame` | 56.1 | 1433 | – | no-audio |  |
| rg_ura_103 | `ingame` | 51.2 | 1356 | ✓ | flat-fill |  |
| SantaMania | `ingame` | 0.1 | 2 | – | flat-fill |  |
| tlosaf_v12-caanoo | `ingame` | 58.4 | 1471 | – | no-audio |  |
| warehouse_panic_v1.1_caanoo | `ingame` | 23.7 | 291 | ✓ | low-fps |  |
| zelda-roth-olb-3t_caanoo | `ingame` | 24.9 | 675 | ✓ | low-fps |  |
| ADVENTURE | `playable` | 56.3 | 1497 | ✓ |  |  |
| Amoebax | `playable` | 55.2 | 1401 | ✓ |  |  |
| cavestory | `playable` | 56.0 | 1533 | ✓ |  |  |
| ccrg-caanoo | `playable` | 45.5 | 195 | ✓ |  |  |
| cllwrth | `playable` | 31.8 | 805 | ✓ |  |  |
| Fywod_caanoo | `playable` | 54.7 | 1392 | ✓ |  |  |
| pang | `playable` | 56.9 | 1464 | ✓ |  |  |
| propis | `playable` | 49.0 | 1235 | ✓ |  |  |
| rhythmosplay_1.1.12 | `playable` | 49.9 | 1268 | ✓ |  |  |
| SHOOTERS | `playable` | 110.9 | 3027 | ✓ |  |  |
| smw_1.7 | `playable` | 34.4 | 929 | ✓ |  |  |
| SPORTS | `playable` | 59.6 | 1541 | ✓ |  |  |
| sqrxz-v0996-caanoo | `playable` | 58.0 | 1483 | ✓ |  |  |
| sqrxz2-v0.80-caanoo | `playable` | 58.2 | 1480 | ✓ |  |  |
| tailtale4c | `playable` | 60.3 | 1521 | ✓ |  |  |
| WindandWater | `playable` | 58.9 | 1487 | ✓ |  |  |
