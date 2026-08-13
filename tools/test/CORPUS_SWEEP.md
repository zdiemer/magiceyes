# magiceyes compatibility sweep

Every GP2X / Wiz / Caanoo title on the corpus share, booted headlessly through the native engine (`bin/me_unicorn`) and scored from the shm framebuffer + the structured run report. Regenerate with `tools/test/compat_report.py`.


## Summary

| Platform | Titles | Playable | Ingame | Black | Incompatible | Crashed |
|---|--:|--:|--:|--:|--:|--:|
| GP2X | 673 | 351 | 89 | 86 | 147 | 0 |
| Wiz | 153 | 47 | 11 | 39 | 56 | 0 |
| Caanoo | 205 | 59 | 38 | 22 | 86 | 0 |
| **All** | **1031** | **457** | **138** | **147** | **289** | **0** |

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
| **Boots but renders only black** (`black-screen`) | 147 | Caanoo, GP2X, Wiz | n/a |
| **Never rendered a frame (cause unknown)** (`no-frames`) | 110 | Caanoo, GP2X, Wiz | n/a |
| **Not a 32-bit ARM ELF** (`not-arm-elf`) | 99 | Caanoo, GP2X, Wiz | n/a |
| **Renders at speed but no audio** (`no-audio`) | 96 | Caanoo, GP2X, Wiz | n/a |
| **No .gpe in the dump** (`no-executable`) | 59 | Caanoo, GP2X, Wiz | n/a |
| **Renders but below 25 fps** (`low-fps`) | 18 | Caanoo, GP2X | n/a |
| **Draws only a flat colour** (`flat-fill`) | 15 | Caanoo, GP2X | n/a |
| **Renders, but the picture is wrong** (`garbled-visuals`) | 8 | Caanoo, GP2X, Wiz | n/a |
| **Game data files are missing from the dump** (`missing-game-data`) | 7 | Caanoo, GP2X | n/a |
| **Unknown /dev node** (`unknown-device`) | 6 | Caanoo, GP2X | `/dev/input/mouse/0` ×3, `/dev/accel` ×1, `/dev/cx25874` ×1, `/dev/graphics/fb0` ×1 |
| **Archive extraction failed** (`archive-failed`) | 5 | Caanoo, GP2X | n/a |
| **Unimplemented syscall** (`unimplemented-syscall`) | 2 | GP2X | `113` ×1, `117` ×1 |
| **Could not open a display** (`display-init-failed`) | 1 | GP2X | n/a |
| **Spins forever polling an MMSP2 register** (`mmio-spin`) | 1 | Wiz | `0x4000` ×1 |

## Renders, but the picture is wrong

These 8 titles pass the running checks (frames advancing, frame rate, audio) while the frame itself is visibly broken, so they are graded `ingame` rather than `playable`. The reasons come from measuring the captured frame: a consistent per-row offset means a stride/pitch mismatch, large-scale repetition means the screen holds more than one copy of itself, and noise far above what dithered artwork reaches means corrupt memory.

| Title | Platform | fps | What the frame looks like |
|---|---|--:|---|
| aimcaanoo | Caanoo | 49.8 | the screen holds a second copy of itself, offset by 160px; left and right halves are near-identical; top and bottom halves are near-identical |
| mtknights | Caanoo | 31.1 | the screen holds a second copy of itself, offset by 156px; top and bottom halves are near-identical |
| 1945_GP2X_0.2b | GP2X | 60.5 | pixel-to-pixel noise of 97, far above what dithered artwork reaches; the frame looks like corrupt memory |
| BunnyTraps-v11 | GP2X | 61.8 | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like corrupt memory |
| GF | GP2X | 61.4 | top and bottom halves are near-identical |
| Life.0.1 | GP2X | 62.1 | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like corrupt memory |
| MoveSweep2X | GP2X | 50.3 | the screen holds a second copy of itself, offset by 96px; left and right halves are near-identical |
| Worship Vector | Wiz | 61.0 | the screen holds a second copy of itself, offset by 160px; left and right halves are near-identical |

## Scored as working, but only painting a flat colour

These 15 titles advanced frames, kept audio running, and held frame rate, so they land in `playable`/`renders`. Their framebuffer never held more than one or two colours, which means the tier overstates them. Worth treating as broken.

| Title | Platform | Status | fps |
|---|---|---|--:|
| Arcadevol3 | Caanoo | `renders` | 58.7 |
| gnp_104 | Caanoo | `playable` | 50.6 |
| knight | Caanoo | `playable` | 107.7 |
| meritous | Caanoo | `playable` | 87.5 |
| noiz2sa_caanoo | Caanoo | `renders` | 16.9 |
| rg_ura_103 | Caanoo | `playable` | 54.8 |
| _-The Reversed Preacher 3-_Hack bIld_ | GP2X | `playable` | 55.7 |
| _-the reversed preacher II-_ | GP2X | `playable` | 58.0 |
| ASCIIPong2xV0.4 | GP2X | `playable` | 55.4 |
| dumbbell2x-01 | GP2X | `renders` | 61.7 |
| game bIld 2 | GP2X | `playable` | 61.6 |
| Knight Lore | GP2X | `playable` | 108.0 |
| levelEdit | GP2X | `renders` | 61.9 |
| robot-escape | GP2X | `playable` | 89.7 |
| the reversed preacher II | GP2X | `playable` | 58.4 |

## Cross-title blockers


### Unimplemented syscalls

| Item | Titles |
|---|--:|
| `113` | 1 |
| `117` | 1 |
| `241 (sched_setaffinity)` | 1 |

### Missing dynamic symbols

| Item | Titles |
|---|--:|
| `LoadImage -> Could not load image: Failed loading libpng.so.3: /lib/libpng.so.3` | 1 |

### Unknown /dev nodes

| Item | Titles |
|---|--:|
| `/dev/input/mouse/0` | 208 |
| `/dev/psaux` | 181 |
| `/dev/usbmouse` | 181 |
| `/dev/sequencer` | 22 |
| `/dev/input/mouse0` | 20 |
| `/dev/accel` | 17 |
| `/dev/input/mice` | 4 |
| `/dev/mouse` | 4 |
| `/dev/gpmdata` | 3 |
| `/dev/pollux_batt` | 3 |
| `/dev/batt` | 3 |
| `/dev/` | 2 |
| `/dev/pts/` | 2 |
| `/dev/mmsp2adc` | 2 |
| `/dev/input/mouse` | 2 |
| `/dev/ptmx` | 1 |
| `/dev/ptyp0` | 1 |
| `/dev/cx25874` | 1 |
| `/dev/graphics/fb0` | 1 |
| `/dev/adbmouse` | 1 |

### Quirks (ran, but not fully honoured)

| Item | Titles |
|---|--:|
| `unknown_mmio:0x90a` | 410 |
| `unknown_ioctl:fb` | 209 |
| `unknown_mmio:0x4058` | 185 |
| `unknown_mmio:0x405c` | 180 |
| `unknown_mmio:0x4060` | 180 |
| `unknown_mmio:0x910` | 105 |
| `unknown_mmio:0x924` | 48 |
| `unknown_mmio:0x3b46` | 46 |
| `unknown_mmio:0x91c` | 46 |
| `unknown_mmio:0x4070` | 29 |
| `unknown_mmio:0x3802` | 22 |
| `unknown_mmio:0x3804` | 22 |
| `unknown_mmio:0xf16` | 12 |
| `unknown_mmio:0xf58` | 12 |
| `unknown_mmio:0x808` | 12 |
| `unsupported_blit:dst-unmapped` | 11 |
| `unknown_mmio:0x1988` | 11 |
| `unknown_mmio:0x19c0` | 11 |
| `unknown_mmio:0x19c4` | 11 |
| `unknown_mmio:0xfff6f004` | 11 |
| `unknown_mmio:0xfffe2880` | 9 |
| `unknown_mmio:0xfffe2906` | 9 |
| `unknown_mmio:0xfffe2908` | 9 |
| `unknown_mmio:0xfffe290a` | 9 |
| `unknown_mmio:0xfffe2912` | 9 |

## Per-title results


### GP2X (673 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| 2xHexen2_cheat_patch | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/2xHexen2_cheat_patch' |
| 2xquake003 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| 2xquake2 | `incompatible` | 0.0 | 0 | ✓ | missing-game-data |  |
| 2XRally01 | `incompatible` | 0.0 | 0 | – | display-init-failed |  |
| abduction | `incompatible` | 0.0 | 0 | – | no-frames |  |
| abe | `incompatible` | 61.3 | 1543 | ✓ | no-frames |  |
| airpong4GP2X0.0.4 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/airpong4GP2X0.0.4/airpong022/src/AirPong.gpe' is not an  |
| AlienZ | `incompatible` | 0.0 | 0 | – | no-frames |  |
| animatch_v1.2 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/animatch_v1.2' |
| animatch_v1.2.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/animatch_v1.2.zip' (exit 32512) |
| AnotherGame2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/AnotherGame2x/AnotherGame2x/anothergame2x.gpe' is not an |
| atris-1.0.7 | `incompatible` | 60.7 | 1568 | ✓ | no-frames |  |
| balluz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/balluz/balluz/balluz.gpe' is not an ARM ELF and no runna |
| beat2x-05 source | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-05 source' |
| beat2x-pack-C64 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-C64' |
| beat2x-pack-ccMixter | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-ccMixter' |
| beat2x-pack-magnatune-electronica | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-magnatune-electronica' |
| beat2x-pack-miniMaximum | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-miniMaximum' |
| beat2x-pack-tutorial | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-tutorial' |
| beat2x-pack-urban | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/beat2x-pack-urban' |
| BermudaS_gp2x | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| blocksGP2X-0 | `incompatible` | 0.0 | 0 | – | unimplemented-syscall | 113 |
| Boomshine2x_(java) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Boomshine2x_(java)/Boomshine2x/Boomshine2x.gpe' is not a |
| bunkermaster2x04 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Butterfly | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Butterfly' |
| cackb2 | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| Chess2xSkins | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Chess2xSkins' |
| chicken-puyopuyo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Classical | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Classical' |
| CloneKeen2X-1.0a | `incompatible` | 0.0 | 0 | – | no-frames |  |
| d1x-rebirth-gp2x_v0.50a | `incompatible` | 0.0 | 0 | – | unimplemented-syscall | 117 |
| DeathChase4GP2X-V0.1b | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/DeathChase4GP2X-V0.1b/deathchase3d-0.9/deathchase3d/Deat |
| default | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/default' |
| dkbk2x-0.1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| doom | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/doom/doom/10sector.gpe' is not an ARM ELF and no runnabl |
| doom_mod_examples | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/doom_mod_examples/game/interpreters/doom/pwad1/prboom_gm |
| DoomPwadPack | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/DoomPwadPack/AliensTC.gpe' is not an ARM ELF and no runn |
| dosfsck-gp2x-2.11 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/dosfsck-gp2x-2.11' |
| Dr. Mates v1.0 | `incompatible` | 44.3 | 107 | ✓ | no-frames |  |
| duckmaze-gp2x-0.1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/duckmaze-gp2x-0.1/duckmaze-gp2x-0.1/duckmaze.gpe' is not |
| duke3d_cheat_patch | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/duke3d_cheat_patch' |
| exultb4-src | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/exultb4-src' |
| FFDoom | `incompatible` | 2.0 | 1 | – | no-frames |  |
| Fire | `incompatible` | 0.0 | 0 | – | no-frames |  |
| FleshChasmer Zero (English Patch) | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/FleshChasmer Zero (English Patch)' |
| Fore_1_0 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Fore_1_0' |
| FP_Default_2.0 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/FP_Default_2.0' |
| freedroid2x06 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/freedroid2x06/Freedroid/FreeDroid.gpe' is not an ARM ELF |
| frotz | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/frotz' |
| garden2x02 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| geoQuiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/geoQuiz/geoQuiz.gpe' is not an ARM ELF and no runnable b |
| gnurobbo_0.66_open2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| gp2x-abrick-0.1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/gp2x-abrick-0.1' |
| gp2x-rogue-v1.0 | `incompatible` | 0.0 | 0 | – | no-frames |  |
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
| kobo_deluxe_beta1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| KQ2X_v3 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Laser2xVers10 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Liquid Counter.gp2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Liquid Counter.gp2x/LiquidCount/LiquidCount.gpe' is not  |
| Lottys_Lines.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/Lottys_Lines.zip' (exit 32512) |
| mariodm | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/mariodm' |
| Midnight2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Midnight2x/dosbox/midnight/midnight.gpe' is not an ARM E |
| misterhachi | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/misterhachi/misterhachi/misterhachi.gpe' is not an ARM E |
| mopesnake-gp2x-0.5 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/mopesnake-gp2x-0.5/mopesnake-gp2x-0.5/mopesnake.gpe' is  |
| nethack-ascii-3.4.3port1 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| nethack-caduhack.r01 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| nethack06 | `incompatible` | 0.0 | 0 | – | no-frames |  |
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
| Phantomas1.8X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Pipes2_0 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Pipes2_0/Pipes/Pipes.gpe' is not an ARM ELF and no runna |
| Pipes_v2.1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Pipes_v2.1/Pipes/Pipes.gpe' is not an ARM ELF and no run |
| pykaraoke-0.6-gp2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| pySlide | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/pySlide/pySlide/pySlide.gpe' is not an ARM ELF and no ru |
| pyTetris | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/pyTetris/pyTetris/pyTetris.gpe' is not an ARM ELF and no |
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
| scummvm-alpha-8a_sky | `incompatible` | 0.0 | 0 | – | no-frames |  |
| scummVMsaves | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/scummVMsaves' |
| shoveit | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/shoveit' |
| Skin1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Skin1' |
| smw-1.6_gp2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| snakepan | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/snakepan/Snakepan.gpe' is not an ARM ELF and no runnable |
| SnoodForTileMatch | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/SnoodForTileMatch' |
| snowedin6_v1-00_gp2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/graphics/fb0 |
| sopwith_camel_rc3 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| space52_gp2x(oficial) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/space52_gp2x(oficial)/space_52/space_52_gp2x.gpe' is not |
| space52_gp2x(open2x) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/space52_gp2x(open2x)/space_52/space_52_gp2x.gpe' is not  |
| squaregame2xV1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/squaregame2xV1/squaregame2x.gpe' is not an ARM ELF and n |
| Starship Soldier.gp2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Starship Soldier.gp2x/StarshipSoldier/starship_soldier.g |
| stppc2x-v1.1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/stppc2x-v1.1' |
| stppc2x-v1.1.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/stppc2x-v1.1.zip' (exit 32512) |
| strife | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/strife/dosbox/strife/strife.gpe' is not an ARM ELF and n |
| Supa2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Supa2x/dosbox/supaplex.gpe' is not an ARM ELF and no run |
| testmem2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/testmem2x/testmem2x/testmem2x.gpe' is not an ARM ELF and |
| ttd2x_020108 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| tunar-1.1.0 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/tunar-1.1.0/tunar/tunar.gpe' is not an ARM ELF and no ru |
| TurnOn | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/TurnOn' |
| Tux_Strikes_Back | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Tux_Strikes_Back' |
| Txishos (Gp2x F-200) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Txishos (Gp2x F-200)/Gp2x F-200/Txishos/Txishos.gpe' is  |
| UBPGP2x | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/UBPGP2x' |
| uqm-0.4.2-content.uqm | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/uqm-0.4.2-content.uqm' |
| uqm-0.5.0-r1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| uqm2x_langpack_v1.2 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/uqm2x_langpack_v1.2/uqm2xfin.gpe' is not an ARM ELF and  |
| uqm2x_remixpack_1.1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/uqm2x_remixpack_1.1/uqm2xrmx.gpe' is not an ARM ELF and  |
| WADFEST | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/WADFEST' |
| wads1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/wads1/wads1/requiem.gpe' is not an ARM ELF and no runnab |
| wads2 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/wads2/wads2/h2h-xmas.gpe' is not an ARM ELF and no runna |
| wizznic06_NES_30levels | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/wizznic06_NES_30levels' |
| Wolf4SDL | `incompatible` | 0.0 | 0 | – | no-frames |  |
| worminator302 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Zombiepox2X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zombiesorbet_v1.0_gp2x | `incompatible` | 60.8 | 1268 | ✓ | no-frames |  |
| 2xHexen2 v0.05 PB2 | `black` | 35.5 | 25 | – | black-screen |  |
| 2xWargus_PB1.3 | `black` | 0.9 | 15 | ✓ | black-screen |  |
| 2xZdoom_PB1.2 | `black` | 43.9 | 53 | – | black-screen |  |
| A1GP2XV1_1 | `black` | 31.2 | 16 | – | black-screen |  |
| abuse_1.0 | `black` | 4.3 | 6 | ✓ | black-screen |  |
| albion-v1.0.1-gp2x | `black` | 28.7 | 28 | ✓ | black-screen |  |
| AlienBlaster_1.02 | `black` | 10.2 | 16 | ✓ | black-screen |  |
| angband2x-v2 | `black` | 37.9 | 80 | – | black-screen |  |
| bang_gp | `black` | 33.6 | 24 | ✓ | black-screen |  |
| BeetleRun | `black` | 15.5 | 8 | ✓ | black-screen |  |
| blockoid | `black` | 9.1 | 7 | ✓ | black-screen |  |
| Bombs Panic | `black` | 92.0 | 193 | ✓ | black-screen |  |
| Boulders-0 | `black` | 23.7 | 19 | ✓ | black-screen |  |
| BubbleTrain_GP2X-2006_Entry | `black` | 1.2 | 2 | ✓ | black-screen |  |
| CaptainCrusader_GP2XDemo | `black` | 5.9 | 3 | – | black-screen |  |
| CaptainCrusader_GP2XFull | `black` | 5.8 | 3 | – | black-screen |  |
| cat_trap | `black` | 114.4 | 2912 | ✓ | black-screen |  |
| cdogs2x04 | `black` | 11.8 | 6 | – | black-screen |  |
| Comando2gp2xEN | `black` | 30.4 | 16 | ✓ | black-screen |  |
| coppergreen | `black` | 14.7 | 16 | ✓ | black-screen |  |
| d2x-gp2x-0.02 | `black` | 21.3 | 17 | ✓ | black-screen |  |
| Dark_Light_SDL2X | `black` | 22.8 | 16 | ✓ | black-screen |  |
| DeathTrap1_1 | `black` | 12.9 | 17 | ✓ | black-screen |  |
| Digger | `black` | 36.9 | 84 | ✓ | black-screen |  |
| dodge | `black` | 21.7 | 17 | ✓ | black-screen |  |
| duke2x004 | `black` | 17.9 | 9 | – | black-screen |  |
| egoboo-cramfs | `black` | 37.4 | 68 | ✓ | black-screen |  |
| fenix | `black` | 12.7 | 30 | ✓ | black-screen |  |
| fenixGamePack | `black` | 17.1 | 49 | ✓ | black-screen |  |
| Flappynerd_GP2X | `black` | 45.4 | 120 | ✓ | black-screen |  |
| FleshChasmer132c_patch | `black` | 12.0 | 6 | ✓ | black-screen |  |
| FleshChasmer_Dpad | `black` | 10.0 | 5 | ✓ | black-screen |  |
| FlipIR_GP2X | `black` | 117.4 | 2967 | ✓ | black-screen |  |
| godori | `black` | 8.0 | 4 | – | black-screen |  |
| gp2x-blobwars-0.1 | `black` | 117.2 | 2967 | ✓ | black-screen |  |
| gp2x-bubbletrain-0.1 | `black` | 113.7 | 2953 | ✓ | black-screen |  |
| gp2x-netrok-0.1 | `black` | 106.5 | 3050 | ✓ | black-screen |  |
| gp2x-sand-0.3 | `black` | 58.5 | 120 | – | black-screen |  |
| gp2x-tenmado-0.1 | `black` | 116.4 | 2953 | – | black-screen |  |
| GP2X_Nat2007 | `black` | 117.2 | 2958 | ✓ | black-screen |  |
| gp2xDoukutsu-1.04 | `black` | 19.5 | 15 | ✓ | black-screen |  |
| gp2xJenkasNightmare | `black` | 20.3 | 18 | ✓ | black-screen |  |
| GPgeneral | `black` | 3.9 | 2 | – | black-screen |  |
| gpnoid2x | `black` | 25.6 | 19 | ✓ | black-screen |  |
| GPrina-GP2x_v1.0 | `black` | 61.1 | 1549 | ✓ | black-screen |  |
| just4qix | `black` | 13.7 | 7 | ✓ | black-screen |  |
| liquidwar2x02 | `black` | 3.9 | 2 | – | black-screen |  |
| nazcarunners-0 | `black` | 37.9 | 36 | ✓ | black-screen |  |
| nazcasphere | `black` | 31.3 | 22 | ✓ | black-screen |  |
| openggs | `black` | 60.7 | 1552 | ✓ | black-screen |  |
| openjazz-gp2x | `black` | 17.2 | 15 | ✓ | black-screen |  |
| pacmame | `black` | 11.2 | 6 | – | black-screen |  |
| para3 | `black` | 55.9 | 148 | ✓ | black-screen |  |
| pez | `black` | 17.9 | 9 | – | black-screen |  |
| PrBoom PWAD pack | `black` | 2.5 | 5 | – | black-screen |  |
| protozoa v1.0 | `black` | 33.6 | 27 | ✓ | black-screen |  |
| raw2xv0.3.1 | `black` | 13.6 | 7 | – | black-screen |  |
| ruckman_v1.03 | `black` | 36.5 | 98 | ✓ | black-screen |  |
| ShadowWarrior2X | `black` | 15.9 | 8 | – | black-screen |  |
| SimOniZ | `black` | 113.6 | 2910 | ✓ | black-screen |  |
| SmashGp2x02 | `black` | 61.3 | 1551 | ✓ | black-screen |  |
| sprint_race | `black` | 13.2 | 8 | – | black-screen |  |
| starsystem | `black` | 26.1 | 14 | ✓ | black-screen |  |
| step2x02 | `black` | 52.7 | 110 | ✓ | black-screen |  |
| superpang | `black` | 43.9 | 118 | ✓ | black-screen |  |
| tesla-Siren | `black` | 25.1 | 16 | ✓ | black-screen |  |
| Tetrablocks.0.4.GP2X | `black` | 46.9 | 105 | ✓ | black-screen |  |
| tilt | `black` | 25.1 | 17 | ✓ | black-screen |  |
| TRAINS | `black` | 18.5 | 10 | ✓ | black-screen |  |
| uhexen | `black` | 5.0 | 3 | – | black-screen |  |
| ultratumba_exp-20100925.gp2x | `black` | 13.3 | 8 | ✓ | black-screen |  |
| uqm2x_release_1.1 | `black` | 85.5 | 362 | – | black-screen |  |
| UQMgp2x-0.5.0_with_content | `black` | 3.9 | 2 | – | black-screen |  |
| Volleyball | `black` | 51.8 | 109 | ✓ | black-screen |  |
| vwars | `black` | 60.2 | 1551 | ✓ | black-screen |  |
| warcraft-beta3-gp2x | `black` | 31.7 | 30 | ✓ | black-screen |  |
| Winter_Jumper | `black` | 5.8 | 3 | – | black-screen |  |
| Wizznic_2x_07alpha2 | `black` | 10.8 | 20 | ✓ | black-screen |  |
| wizznic_gp2x-0.9.9 | `black` | 9.3 | 16 | ✓ | black-screen |  |
| wolfdx | `black` | 49.4 | 53 | ✓ | black-screen |  |
| xbak-0.1.3 | `black` | 27.6 | 14 | – | black-screen |  |
| xcom1-v1.0.2-gp2x | `black` | 42.3 | 1566 | ✓ | black-screen |  |
| xcom2-v1.0.1-gp2x | `black` | 109.6 | 2800 | ✓ | black-screen |  |
| xump2x_beta2 | `black` | 25.9 | 13 | ✓ | black-screen |  |
| zcgp2x_211B18_0.4alpha | `black` | 24.2 | 23 | – | black-screen |  |
| Zelda_roth_US_gp2x | `black` | 19.8 | 42 | ✓ | black-screen |  |
| 1945_GP2X_0.2b | `ingame` | 60.5 | 570 | ✓ | garbled-visuals | pixel-to-pixel noise of 97, far above what dithered artwork reaches; the frame looks like  |
| _-The Reversed Preacher 3-_Hack bIld_ | `ingame` | 55.7 | 132 | ✓ | flat-fill |  |
| _-the reversed preacher II-_ | `ingame` | 58.0 | 136 | ✓ | flat-fill |  |
| a_sn-pong | `ingame` | 42.0 | 1554 | – | no-audio |  |
| AMazing-3D | `ingame` | 65.0 | 1638 | – | no-audio |  |
| ASCIIPong2xV0.4 | `ingame` | 55.4 | 1399 | ✓ | flat-fill |  |
| Birdshoot | `ingame` | 62.0 | 1554 | – | no-audio |  |
| bugafactorx-v03-beta | `ingame` | 61.2 | 1552 | – | no-audio |  |
| BunnyTraps-v11 | `ingame` | 61.8 | 1560 | ✓ | garbled-visuals | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like |
| buscaminas | `ingame` | 61.4 | 1552 | – | no-audio |  |
| cardm | `ingame` | 61.2 | 1552 | – | no-audio |  |
| cavecopter_gp2x | `ingame` | 21.0 | 527 | – | low-fps |  |
| Chopper | `ingame` | 61.1 | 1554 | – | no-audio |  |
| Clonk2X_1.0 | `ingame` | 16.2 | 409 | – | not-arm-elf | magiceyes: reload of '/bin/sh' failed |
| cosmo2x_01 | `ingame` | 61.7 | 1560 | – | no-audio |  |
| CromoZome | `ingame` | 19.6 | 511 | ✓ | low-fps |  |
| dopewars2x | `ingame` | 61.4 | 1542 | – | no-audio |  |
| drod-gp2x-1_0 | `ingame` | 52.1 | 1350 | – | no-audio |  |
| dstroyGP2X1402 | `ingame` | 60.5 | 1552 | – | no-audio |  |
| dumbbell2x-01 | `ingame` | 61.7 | 575 | – | flat-fill |  |
| escoba_exp-20101016.gp2x | `ingame` | 61.3 | 1556 | – | no-audio |  |
| extraterrestres-0 | `ingame` | 58.8 | 1561 | – | no-audio |  |
| FCRLG | `ingame` | 61.4 | 1543 | – | no-audio |  |
| fifteen_01 | `ingame` | 59.8 | 564 | – | no-audio |  |
| flowflowmania-0_6-gp2x | `ingame` | 45.0 | 441 | – | no-audio |  |
| freesci | `ingame` | 57.8 | 260 | – | no-audio |  |
| fruits2x | `ingame` | 51.5 | 107 | – | no-audio |  |
| game bIld 2 | `ingame` | 61.6 | 1551 | ✓ | flat-fill |  |
| gchess-v1.0.1-bin | `ingame` | 61.3 | 1557 | – | no-audio |  |
| gchess-v1.1.0-bin | `ingame` | 61.5 | 1556 | – | no-audio |  |
| GF | `ingame` | 61.4 | 1555 | ✓ | garbled-visuals | top and bottom halves are near-identical |
| gnugo2x | `ingame` | 61.5 | 1555 | – | no-audio |  |
| gp2x-ceferino-0.1 | `ingame` | 58.8 | 1554 | – | no-audio |  |
| gp2x-tong-v1 | `ingame` | 116.3 | 2941 | – | no-audio |  |
| gp2xgo-v1.1.0-bin | `ingame` | 61.4 | 1554 | – | no-audio |  |
| gp2xmancala-v1.1.1-bin | `ingame` | 61.6 | 1555 | – | no-audio |  |
| GP2XOfLife | `ingame` | 113.4 | 2863 | – | no-audio |  |
| GPSquares_GP2X | `ingame` | 62.1 | 1558 | – | no-audio |  |
| grow | `ingame` | 61.2 | 1536 | – | no-audio |  |
| gxeskiv | `ingame` | 58.3 | 1472 | – | no-audio |  |
| hex-a-hop | `ingame` | 61.5 | 1551 | – | no-audio |  |
| kampfimall-gp2x | `ingame` | 61.2 | 1543 | – | no-audio |  |
| Knight Lore | `ingame` | 108.0 | 2715 | ✓ | flat-fill |  |
| LABYRINTH | `ingame` | 61.8 | 1556 | – | no-audio |  |
| levelEdit | `ingame` | 61.9 | 1558 | – | flat-fill |  |
| Life.0.1 | `ingame` | 62.1 | 1562 | – | garbled-visuals | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like |
| lights-out | `ingame` | 59.4 | 1498 | – | no-audio |  |
| lumix-beta-01 | `ingame` | 64.9 | 1636 | – | no-audio |  |
| mancala-v1.0.1 | `ingame` | 61.5 | 1560 | – | no-audio |  |
| masterpiece2x | `ingame` | 61.8 | 1558 | – | no-audio |  |
| minigolf | `ingame` | 61.2 | 1552 | – | no-audio |  |
| MoveSweep2X | `ingame` | 50.3 | 104 | – | garbled-visuals | the screen holds a second copy of itself, offset by 96px; left and right halves are near-i |
| Nebulus_gp2x | `ingame` | 61.9 | 1554 | – | no-audio |  |
| Net-Bubble-gp2x_1-21-06_bin | `ingame` | 56.3 | 526 | – | no-audio |  |
| openglad2x | `ingame` | 59.6 | 1548 | – | no-audio |  |
| oxov06 | `ingame` | 44.7 | 92 | – | no-audio |  |
| PerfectFit | `ingame` | 61.8 | 1556 | – | no-audio |  |
| PocketSnes_SMRPG | `ingame` | 118.6 | 2982 | – | no-audio |  |
| Pond2X | `ingame` | 61.4 | 1542 | – | no-audio |  |
| Pong | `ingame` | 61.6 | 1545 | – | no-audio |  |
| pong2player | `ingame` | 58.0 | 118 | – | no-audio |  |
| pong2v060x | `ingame` | 55.6 | 114 | – | no-audio |  |
| powder2x-112 | `ingame` | 61.0 | 1557 | – | no-audio |  |
| prboom-gp2x | `ingame` | 61.0 | 1557 | – | no-audio |  |
| RevoltOfTheBinaryCouriers GP2X | `ingame` | 61.1 | 1538 | – | no-audio |  |
| robot-escape | `ingame` | 89.7 | 184 | ✓ | flat-fill |  |
| rubik | `ingame` | 122.6 | 3104 | – | no-audio |  |
| scummvm-kor0.4.2cvs | `ingame` | 60.5 | 1536 | – | no-audio |  |
| sdlmonkey_0.1 | `ingame` | 61.5 | 1552 | – | no-audio |  |
| Simon2X | `ingame` | 60.5 | 1536 | – | no-audio |  |
| snake2x-1.1 | `ingame` | 60.7 | 1557 | – | no-audio |  |
| Solitaire2x-v1.4 | `ingame` | 80.9 | 752 | – | no-audio |  |
| sources_MEMORY2X | `ingame` | 60.8 | 1554 | – | no-audio |  |
| space squares | `ingame` | 60.9 | 1549 | – | no-audio |  |
| SpaceRocks2X | `ingame` | 54.6 | 157 | – | no-audio |  |
| spartak-chess_0.0.4_gp2x | `ingame` | 61.3 | 1553 | – | no-audio |  |
| Sponge Blob Tennis | `ingame` | 42.0 | 1555 | – | no-audio |  |
| spout | `ingame` | 61.6 | 1550 | – | no-audio |  |
| Sqcolony | `ingame` | 63.7 | 1605 | – | no-audio |  |
| sudoku-v1.0 | `ingame` | 61.6 | 1554 | – | no-audio |  |
| sudoku2x-0.5 | `ingame` | 61.1 | 1539 | – | no-audio |  |
| Tangle | `ingame` | 62.1 | 1558 | – | no-audio |  |
| the reversed preacher II | `ingame` | 58.4 | 135 | ✓ | flat-fill |  |
| TimeFrack2D for GP2X | `ingame` | 48.8 | 100 | – | no-audio |  |
| tower | `ingame` | 109.5 | 2774 | – | no-audio |  |
| ttxbeta170706b | `ingame` | 57.9 | 1553 | – | no-audio |  |
| vexedb1 | `ingame` | 61.7 | 1556 | – | no-audio |  |
| waffle2x | `ingame` | 42.9 | 91 | – | no-audio |  |
| wire3d | `ingame` | 58.9 | 1530 | – | no-audio |  |
| 2xpong_gp2x | `playable` | 61.4 | 1543 | ✓ |  |  |
| 2xtron-v01 | `playable` | 61.9 | 1557 | ✓ |  |  |
| 2xZdoom_selector | `playable` | 73.6 | 172 | ✓ |  |  |
| 4WE_GP2x | `playable` | 116.9 | 2015 | ✓ |  |  |
| 9 Lives | `playable` | 70.7 | 1779 | ✓ |  |  |
| AbusimbelProfanationDeluxe | `playable` | 118.6 | 3003 | ✓ |  |  |
| AdamantArmorAffection2x | `playable` | 60.8 | 1538 | ✓ |  |  |
| ADIC2X | `playable` | 89.5 | 1062 | ✓ |  |  |
| AfterBurner-GP2X | `playable` | 35.9 | 910 | ✓ |  |  |
| Airplyr | `playable` | 61.8 | 1554 | ✓ |  |  |
| airstrike-1.1 | `playable` | 61.1 | 1553 | ✓ |  |  |
| Akd_BB | `playable` | 61.6 | 1557 | ✓ |  |  |
| alex | `playable` | 61.5 | 1549 | ✓ |  |  |
| Alex's Falldown | `playable` | 61.4 | 1554 | ✓ |  |  |
| alex4_gp2x | `playable` | 61.5 | 1552 | ✓ |  |  |
| altitude | `playable` | 40.9 | 126 | ✓ |  |  |
| amoebax-0.2.1-gp2x | `playable` | 54.9 | 1396 | ✓ |  |  |
| armorcity-0_30b | `playable` | 60.9 | 1554 | ✓ |  |  |
| Asteroids | `playable` | 60.7 | 1555 | ✓ |  |  |
| astrochaos | `playable` | 58.5 | 321 | ✓ |  |  |
| B'lox! | `playable` | 116.4 | 2966 | ✓ |  |  |
| BareFistFighter | `playable` | 61.4 | 1542 | ✓ |  |  |
| barrage | `playable` | 61.8 | 1557 | ✓ |  |  |
| Batiscafo (versin EXP) | `playable` | 61.2 | 1544 | ✓ |  |  |
| battlejewels-gp2x-062-100 | `playable` | 117.2 | 2957 | ✓ |  |  |
| BearOids | `playable` | 61.6 | 1555 | ✓ |  |  |
| beat2x-0.5-bin | `playable` | 61.2 | 1553 | ✓ |  |  |
| Beatbox_1.2 | `playable` | 59.2 | 1555 | ✓ |  |  |
| biniax-gp2x_v1.2 | `playable` | 61.3 | 1544 | ✓ |  |  |
| Biniax2_gp2x | `playable` | 81.4 | 2069 | ✓ |  |  |
| Biohazard2 | `playable` | 62.0 | 1561 | ✓ |  |  |
| BioShoot GP2X | `playable` | 60.7 | 1552 | ✓ |  |  |
| BisfoG | `playable` | 77.8 | 924 | ✓ |  |  |
| blastriot1.21 | `playable` | 59.2 | 1494 | ✓ |  |  |
| blazar_v1-30_gp2x | `playable` | 61.7 | 1553 | ✓ |  |  |
| blingo 1.2 | `playable` | 46.0 | 139 | ✓ |  |  |
| blipsgp2x | `playable` | 60.5 | 1568 | ✓ |  |  |
| Blix2x | `playable` | 61.7 | 1556 | ✓ |  |  |
| blobbyvolley | `playable` | 61.3 | 1551 | ✓ |  |  |
| blobwars_2x | `playable` | 62.5 | 1583 | ✓ |  |  |
| block | `playable` | 61.4 | 1560 | ✓ |  |  |
| blockdudegp2x | `playable` | 58.4 | 1561 | ✓ |  |  |
| Blocked | `playable` | 113.1 | 2874 | ✓ |  |  |
| Blockrage2x | `playable` | 60.8 | 1536 | ✓ |  |  |
| blox | `playable` | 44.6 | 1123 | ✓ |  |  |
| Bloxz_DEMO | `playable` | 61.8 | 1562 | ✓ |  |  |
| bluecube2x | `playable` | 61.8 | 1553 | ✓ |  |  |
| bobtron-gp2x | `playable` | 61.6 | 1554 | ✓ |  |  |
| Boomshine2x_1.12_gp2x | `playable` | 60.6 | 1554 | ✓ |  |  |
| brassmunkey_gp2x_1.0 | `playable` | 61.1 | 1545 | ✓ |  |  |
| BubbleX | `playable` | 61.6 | 1544 | ✓ |  |  |
| BubTrain_GP2X-2006_Entry_No-Sound | `playable` | 57.7 | 1554 | ✓ |  |  |
| BugWarsSE_v1.0 | `playable` | 60.3 | 1551 | ✓ |  |  |
| bumprace-0.2 | `playable` | 60.1 | 1555 | ✓ |  |  |
| BurokkuDemo1 | `playable` | 61.7 | 1563 | ✓ |  |  |
| buttongame | `playable` | 48.0 | 100 | ✓ |  |  |
| BuzzysBadDay-1.0 | `playable` | 61.5 | 1556 | ✓ |  |  |
| CamelotWarriors-GP2x_v1.0 | `playable` | 60.9 | 1551 | ✓ |  |  |
| CascadeBeneath v1.0 for GP2X | `playable` | 62.3 | 1566 | ✓ |  |  |
| ccrg | `playable` | 55.5 | 247 | ✓ |  |  |
| cgenius-gp2x | `playable` | 58.0 | 1507 | ✓ |  |  |
| chaos2x | `playable` | 61.8 | 1553 | ✓ |  |  |
| checkersgp2x | `playable` | 59.4 | 1557 | ✓ |  |  |
| chess2x05 | `playable` | 60.4 | 1557 | ✓ |  |  |
| ChopperAttackv1.0.17 | `playable` | 105.2 | 2909 | ✓ |  |  |
| Chroma | `playable` | 117.8 | 2969 | ✓ |  |  |
| chuckiev12 | `playable` | 61.6 | 1550 | ✓ |  |  |
| Codemaster | `playable` | 118.0 | 2969 | ✓ |  |  |
| ConnyCarrot | `playable` | 61.0 | 1550 | ✓ |  |  |
| CowSuckers-1.0 | `playable` | 61.5 | 1553 | ✓ |  |  |
| Crapong | `playable` | 61.0 | 1553 | ✓ |  |  |
| crazeeman | `playable` | 64.7 | 1640 | ✓ |  |  |
| crimsonV1 | `playable` | 60.3 | 1558 | ✓ |  |  |
| crocodingusgp2x | `playable` | 114.5 | 1541 | ✓ |  |  |
| crossroads | `playable` | 61.7 | 1559 | ✓ |  |  |
| CUBES | `playable` | 61.8 | 1563 | ✓ |  |  |
| cyberhockeyV2_6 | `playable` | 61.5 | 1550 | ✓ |  |  |
| DABAKKA-0 | `playable` | 61.7 | 1548 | ✓ |  |  |
| Dance2x Alpha GPE | `playable` | 61.0 | 1556 | ✓ |  |  |
| DangerMouse | `playable` | 118.0 | 2968 | ✓ |  |  |
| Dastardly dungeon 1.5 | `playable` | 43.7 | 110 | ✓ |  |  |
| dd2x | `playable` | 107.1 | 492 | ✓ |  |  |
| DealOrNoDeal-v12 | `playable` | 60.9 | 1562 | ✓ |  |  |
| debian_vs_pimientos_2x_0.1.2 | `playable` | 58.6 | 548 | ✓ |  |  |
| defeatme-gp2x-1.0.1 | `playable` | 61.0 | 1535 | ✓ |  |  |
| diamant_1_01 | `playable` | 32.5 | 824 | ✓ |  |  |
| DontGetCrushed v1.0 for GP2X | `playable` | 61.9 | 1554 | ✓ |  |  |
| dosmugen | `playable` | 59.6 | 1557 | ✓ |  |  |
| Drill2x_final | `playable` | 60.1 | 1537 | ✓ |  |  |
| drill2x_xtreme_v1.0.3 | `playable` | 60.6 | 1551 | ✓ |  |  |
| DubaiRace038a | `playable` | 49.1 | 113 | ✓ |  |  |
| dyc_gp2x | `playable` | 114.4 | 2902 | ✓ |  |  |
| dynamategp2x | `playable` | 60.6 | 1558 | ✓ |  |  |
| E-Fighters2x_FIRST_ALPHA_0_0_5_fixedSound | `playable` | 116.7 | 2968 | ✓ |  |  |
| EasterQuest | `playable` | 117.3 | 2967 | ✓ |  |  |
| eggstreme3_v1-00_gp2x | `playable` | 61.6 | 1552 | ✓ |  |  |
| egoboo2xFeb1207 | `playable` | 98.6 | 3050 | ✓ |  |  |
| Electronia | `playable` | 61.7 | 1558 | ✓ |  |  |
| enigma | `playable` | 50.3 | 1351 | ✓ |  |  |
| entombed2x | `playable` | 61.2 | 1555 | ✓ |  |  |
| EpicFreeFall_GP2X | `playable` | 60.9 | 1555 | ✓ |  |  |
| EpicRocks_GP2X | `playable` | 58.0 | 649 | ✓ |  |  |
| escapa-v1 | `playable` | 62.0 | 1561 | ✓ |  |  |
| exi_shoot_gp2x | `playable` | 61.3 | 1542 | ✓ |  |  |
| extraterrestres | `playable` | 98.2 | 2644 | ✓ |  |  |
| exult_rc3 | `playable` | 41.6 | 1541 | ✓ |  |  |
| Factor-v1.0-final | `playable` | 61.0 | 1552 | ✓ |  |  |
| falldown_gp2x | `playable` | 93.0 | 2337 | ✓ |  |  |
| FindMii | `playable` | 116.3 | 2966 | ✓ |  |  |
| Firewhip | `playable` | 101.7 | 1447 | ✓ |  |  |
| Fishball-1.2 | `playable` | 60.6 | 1539 | ✓ |  |  |
| fissionfield2x | `playable` | 61.8 | 1556 | ✓ |  |  |
| Flaschenspiel | `playable` | 61.1 | 1554 | ✓ |  |  |
| FleshChasmer | `playable` | 60.3 | 1537 | ✓ |  |  |
| FleshChasmer Zero | `playable` | 61.0 | 1538 | ✓ |  |  |
| floaters | `playable` | 61.5 | 1556 | ✓ |  |  |
| flobopuyo0.20.1 | `playable` | 60.1 | 1529 | ✓ |  |  |
| flurkies_v1-25_gp2x | `playable` | 61.8 | 1557 | ✓ |  |  |
| fm | `playable` | 111.8 | 2803 | ✓ |  |  |
| Football2X | `playable` | 117.9 | 2968 | ✓ |  |  |
| formula1gp2x | `playable` | 61.0 | 1558 | ✓ |  |  |
| Fragger2x | `playable` | 61.8 | 1556 | ✓ |  |  |
| freec2x | `playable` | 40.9 | 1036 | ✓ |  |  |
| freecell_1 | `playable` | 52.4 | 108 | ✓ |  |  |
| friq-beta-07 | `playable` | 61.2 | 1555 | ✓ |  |  |
| frozen2x-0.1 | `playable` | 79.0 | 738 | ✓ |  |  |
| fruits_gp2x | `playable` | 61.6 | 1556 | ✓ |  |  |
| FullBoard (test ver.) | `playable` | 50.9 | 107 | ✓ |  |  |
| fvc | `playable` | 61.0 | 1533 | ✓ |  |  |
| FyWod_2x | `playable` | 60.9 | 1557 | ✓ |  |  |
| game-watch-mario-bros | `playable` | 61.5 | 1553 | ✓ |  |  |
| Geek 'em up GP2X | `playable` | 61.0 | 1713 | ✓ |  |  |
| gemdrop2x_v02 | `playable` | 61.0 | 1568 | ✓ |  |  |
| GeneralPromise | `playable` | 116.9 | 2970 | ✓ |  |  |
| Ghostbusters_WIP | `playable` | 61.6 | 581 | ✓ |  |  |
| ghostpix_v10_gp2x | `playable` | 61.1 | 1555 | ✓ |  |  |
| glouton | `playable` | 61.7 | 1553 | ✓ |  |  |
| gnp_104 | `playable` | 57.7 | 1567 | ✓ |  |  |
| GoitGP | `playable` | 60.3 | 1552 | ✓ |  |  |
| gorillaz | `playable` | 53.6 | 1349 | ✓ |  |  |
| gp2hanoi_0.8.1_gp2x | `playable` | 61.6 | 1555 | ✓ |  |  |
| gp2x-formido-0.1 | `playable` | 45.2 | 1555 | ✓ |  |  |
| gp2x-invaders-preview-version | `playable` | 61.6 | 1558 | ✓ |  |  |
| gp2x-shienso-bin_061021 | `playable` | 61.7 | 1557 | ✓ |  |  |
| gp2x-smc-0.1 | `playable` | 57.7 | 1551 | ✓ |  |  |
| gp2x_2xmas | `playable` | 44.3 | 91 | ✓ |  |  |
| GP2X_BallGame_0.49 | `playable` | 61.3 | 1540 | ✓ |  |  |
| gp2x_drench | `playable` | 59.2 | 1504 | ✓ |  |  |
| GP2X_TLI | `playable` | 31.3 | 790 | ✓ |  |  |
| gp2xbug | `playable` | 116.2 | 2967 | ✓ |  |  |
| gp2xjunkie | `playable` | 58.7 | 1508 | ✓ |  |  |
| gp2xpang-v.1.1.1 | `playable` | 99.9 | 2581 | ✓ |  |  |
| gp2xrick 1.0 | `playable` | 60.4 | 1531 | ✓ |  |  |
| GpFrontier v0.1 | `playable` | 62.3 | 1576 | ✓ |  |  |
| gpfrontier v0.4 | `playable` | 58.7 | 1557 | ✓ |  |  |
| gr-v1001-gp2x | `playable` | 60.1 | 1554 | ✓ |  |  |
| green | `playable` | 60.4 | 1535 | ✓ |  |  |
| hanagechu2x_gbax2007 | `playable` | 66.1 | 1667 | ✓ |  |  |
| hanagechu2xalpha | `playable` | 61.8 | 1557 | ✓ |  |  |
| Heretic2X_v0.5 | `playable` | 60.8 | 1531 | ✓ |  |  |
| hexbattle2x | `playable` | 61.4 | 1553 | ✓ |  |  |
| HumphreyGP2X | `playable` | 60.8 | 1554 | ✓ |  |  |
| Hyperion_GP2X_demo | `playable` | 61.7 | 1565 | ✓ |  |  |
| jump_n_blob_gp2x | `playable` | 58.6 | 1560 | ✓ |  |  |
| jumpnbumpgp2x | `playable` | 59.9 | 1553 | ✓ |  |  |
| Jurlx2 | `playable` | 61.3 | 1552 | ✓ |  |  |
| kampfimall-gp2x-music | `playable` | 60.0 | 525 | ✓ |  |  |
| ketm_2x_gp2x | `playable` | 52.3 | 1552 | ✓ |  |  |
| KicknPLay_1.1 | `playable` | 61.9 | 1556 | ✓ |  |  |
| Klaur | `playable` | 115.5 | 2969 | ✓ |  |  |
| koules2x_02 | `playable` | 60.7 | 1555 | ✓ |  |  |
| kuklomenos_gp2x_201209 | `playable` | 60.5 | 1554 | ✓ |  |  |
| kurukuru2x | `playable` | 61.2 | 1557 | ✓ |  |  |
| la | `playable` | 49.8 | 109 | ✓ |  |  |
| ladykiller | `playable` | 60.8 | 1552 | ✓ |  |  |
| las-tres-luces-de-glaurung-remake | `playable` | 60.2 | 1548 | ✓ |  |  |
| lbreakoutgp2x | `playable` | 59.0 | 1563 | ✓ |  |  |
| levelshmup | `playable` | 85.9 | 2195 | ✓ |  |  |
| Lexeme | `playable` | 117.1 | 2966 | ✓ |  |  |
| LinesXv3 | `playable` | 61.5 | 1543 | ✓ |  |  |
| logicx | `playable` | 61.5 | 1546 | ✓ |  |  |
| Logoball | `playable` | 115.9 | 2969 | ✓ |  |  |
| mad-mix-game-20b-final | `playable` | 61.1 | 1550 | ✓ |  |  |
| madbomber | `playable` | 60.7 | 1558 | ✓ |  |  |
| malvado2x | `playable` | 46.7 | 151 | ✓ |  |  |
| MAME-N22_51 | `playable` | 59.3 | 1560 | ✓ |  |  |
| March of the mini tux | `playable` | 63.6 | 1614 | ✓ |  |  |
| Marte Necesita Vacas GP2X | `playable` | 64.2 | 1649 | ✓ |  |  |
| Masteries_Journey_to_the_Center_of_the_earth_GP2X | `playable` | 59.6 | 1552 | ✓ |  |  |
| MazeThingie | `playable` | 61.9 | 1561 | ✓ |  |  |
| MazezaMGP2X | `playable` | 97.9 | 2534 | ✓ |  |  |
| memory | `playable` | 61.8 | 1575 | ✓ |  |  |
| MemoryGP2X-v11 | `playable` | 61.5 | 1554 | ✓ |  |  |
| meritous | `playable` | 42.0 | 1554 | ✓ |  |  |
| Merlin2x_beta_021 | `playable` | 58.0 | 544 | ✓ |  |  |
| metaphysik | `playable` | 64.1 | 1621 | ✓ |  |  |
| methaneV1 | `playable` | 61.3 | 1551 | ✓ |  |  |
| minos-gp2x | `playable` | 60.2 | 1527 | ✓ |  |  |
| mk13.gpe | `playable` | 61.5 | 1543 | ✓ |  |  |
| mkACE.gpe | `playable` | 61.5 | 1543 | ✓ |  |  |
| mkONE.gpe | `playable` | 61.5 | 1543 | ✓ |  |  |
| MM2X | `playable` | 61.1 | 1541 | ✓ |  |  |
| monacoGP | `playable` | 61.0 | 1564 | ✓ |  |  |
| monochromeworlds-gp2x-1.0.0 | `playable` | 60.7 | 1533 | ✓ |  |  |
| moonlander | `playable` | 59.9 | 1533 | ✓ |  |  |
| MouthTrap | `playable` | 117.7 | 2970 | ✓ |  |  |
| mueppv32 | `playable` | 117.2 | 2967 | ✓ |  |  |
| mush_gp2x | `playable` | 41.8 | 1075 | ✓ |  |  |
| Mutant Tank Knights | `playable` | 57.5 | 314 | ✓ |  |  |
| MyriadUpdated | `playable` | 59.8 | 1556 | ✓ |  |  |
| mzx-2.84c | `playable` | 61.0 | 846 | ✓ |  |  |
| mzx282-gp2x | `playable` | 61.3 | 820 | ✓ |  |  |
| n-tris_v1.0 | `playable` | 73.2 | 1838 | ✓ |  |  |
| nanobounce-pacc-gp2x | `playable` | 52.4 | 397 | ✓ |  |  |
| ne_deluxe_gp2x | `playable` | 61.4 | 1552 | ✓ |  |  |
| ne_gp2x | `playable` | 59.9 | 1508 | ✓ |  |  |
| NecNec2x | `playable` | 61.7 | 1563 | ✓ |  |  |
| newsuperpang | `playable` | 61.1 | 1552 | ✓ |  |  |
| Nifty | `playable` | 60.9 | 1551 | ✓ |  |  |
| noiz2saV3 | `playable` | 68.4 | 1745 | ✓ |  |  |
| Nom | `playable` | 61.2 | 1544 | ✓ |  |  |
| odonata_demo | `playable` | 60.2 | 1523 | ✓ |  |  |
| omok | `playable` | 52.0 | 108 | ✓ |  |  |
| OpenBOR_v2.1933 | `playable` | 60.1 | 1075 | ✓ |  |  |
| OpenBOR_v3.0_Build_2615_&_2637 | `playable` | 61.0 | 1564 | ✓ |  |  |
| opentyrian2x_0.3_complete | `playable` | 57.4 | 1539 | ✓ |  |  |
| OrbitalSniper2x_v1.1 | `playable` | 57.7 | 144 | ✓ |  |  |
| othello_v1.0 | `playable` | 62.2 | 1561 | ✓ |  |  |
| PAF | `playable` | 61.3 | 1550 | ✓ |  |  |
| paraballgp2x | `playable` | 61.1 | 1549 | ✓ |  |  |
| Payback | `playable` | 75.3 | 2060 | ✓ |  |  |
| PaybackDemo | `playable` | 32.4 | 846 | ✓ |  |  |
| pdcv060b | `playable` | 55.1 | 451 | ✓ |  |  |
| Pentominos | `playable` | 61.8 | 1554 | ✓ |  |  |
| Peuppy_10_GP2X | `playable` | 33.9 | 856 | ✓ |  |  |
| Phishy-0 | `playable` | 60.9 | 1554 | ✓ |  |  |
| physique | `playable` | 61.3 | 1551 | ✓ |  |  |
| Pika2x | `playable` | 60.4 | 571 | ✓ |  |  |
| pintor2x | `playable` | 98.2 | 2464 | ✓ |  |  |
| pixpang | `playable` | 56.2 | 1551 | ✓ |  |  |
| Poker2x | `playable` | 109.0 | 2776 | ✓ |  |  |
| Poker_Gp2Xv1.0 | `playable` | 116.4 | 2970 | ✓ |  |  |
| Pool Panic | `playable` | 61.0 | 1539 | ✓ |  |  |
| powermanga-0.80 | `playable` | 56.2 | 1479 | ✓ |  |  |
| PowerSlide | `playable` | 60.9 | 1542 | ✓ |  |  |
| PPlane | `playable` | 60.3 | 1525 | ✓ |  |  |
| PPlane2.GP2X | `playable` | 59.3 | 1574 | ✓ |  |  |
| proj0-demo_01 | `playable` | 60.5 | 1541 | ✓ |  |  |
| puckman_gp2x | `playable` | 117.3 | 2967 | ✓ |  |  |
| PulplifeWars | `playable` | 59.6 | 1551 | ✓ |  |  |
| puzzlelandgp2x | `playable` | 56.3 | 1554 | ✓ |  |  |
| qfg3-0 | `playable` | 60.1 | 1554 | ✓ |  |  |
| Quad | `playable` | 117.0 | 2961 | ✓ |  |  |
| quartz2_v1-50_gp2x | `playable` | 61.7 | 1554 | ✓ |  |  |
| Rabbit_vs_Flies_0.9 | `playable` | 61.3 | 1553 | ✓ |  |  |
| ramon atacks | `playable` | 61.7 | 1555 | ✓ |  |  |
| Release GP2X MST_RUNNERS | `playable` | 60.2 | 1550 | ✓ |  |  |
| retrovirus_1_1 | `playable` | 60.2 | 1527 | ✓ |  |  |
| reword_v0.5 | `playable` | 61.2 | 1561 | ✓ |  |  |
| rg_105 | `playable` | 58.5 | 1567 | ✓ |  |  |
| rg_ura_103 | `playable` | 58.5 | 1565 | ✓ |  |  |
| river | `playable` | 62.0 | 1554 | ✓ |  |  |
| RockRain | `playable` | 61.6 | 1553 | ✓ |  |  |
| rockrain2_exp-20100925 | `playable` | 61.1 | 1550 | ✓ |  |  |
| rookiehero_EXP.gp2x.v20120220 | `playable` | 60.6 | 1548 | ✓ |  |  |
| RoundEmUp-alpha3 | `playable` | 61.6 | 1561 | ✓ |  |  |
| rubidogp2x | `playable` | 60.4 | 1556 | ✓ |  |  |
| Runner_GP2X | `playable` | 60.8 | 1556 | ✓ |  |  |
| s-tris2_v1-64_gp2x | `playable` | 61.8 | 1554 | ✓ |  |  |
| Sachunsung2_1 | `playable` | 51.8 | 108 | ✓ |  |  |
| sachunsungx | `playable` | 61.6 | 1547 | ✓ |  |  |
| santaMania | `playable` | 60.8 | 1556 | ✓ |  |  |
| ScorchedGPBeta2 | `playable` | 61.2 | 1552 | ✓ |  |  |
| scummvm-0.11.1-gp2x | `playable` | 59.8 | 1549 | ✓ |  |  |
| scummvm-1.2.0-gp2x | `playable` | 60.4 | 1561 | ✓ |  |  |
| SdLame | `playable` | 61.3 | 1559 | ✓ |  |  |
| sdlscav_gp2x_0.2.0 | `playable` | 116.0 | 2924 | ✓ |  |  |
| Shangai v2 | `playable` | 51.8 | 108 | ✓ |  |  |
| ShanghaiX | `playable` | 61.4 | 1540 | ✓ |  |  |
| SheepDash | `playable` | 60.1 | 1563 | ✓ |  |  |
| Shippy84 | `playable` | 60.5 | 1555 | ✓ |  |  |
| siv050 | `playable` | 58.5 | 1558 | ✓ |  |  |
| sleuth slots 2x | `playable` | 112.0 | 2903 | ✓ |  |  |
| SmallBall_GP | `playable` | 60.8 | 1553 | ✓ |  |  |
| snail runers | `playable` | 61.0 | 1556 | ✓ |  |  |
| snowedin5_v1-00_gp2x | `playable` | 61.6 | 1552 | ✓ |  |  |
| SOD v1.1 | `playable` | 60.2 | 1553 | ✓ |  |  |
| sokobangp2x | `playable` | 54.6 | 1561 | ✓ |  |  |
| sources_Yahtzee | `playable` | 52.2 | 226 | ✓ |  |  |
| space_varments_v1.0 | `playable` | 56.7 | 565 | ✓ |  |  |
| SpaceSnake | `playable` | 115.9 | 1996 | ✓ |  |  |
| spacestorm | `playable` | 56.0 | 1410 | ✓ |  |  |
| Sqdef 1.4 | `playable` | 61.1 | 1553 | ✓ |  |  |
| Squares-v051 | `playable` | 61.6 | 1565 | ✓ |  |  |
| Squaresliding | `playable` | 61.8 | 1554 | ✓ |  |  |
| StairwayToHeaven | `playable` | 53.6 | 110 | ✓ |  |  |
| starfighter-gp2x-0.01 | `playable` | 65.6 | 1132 | ✓ |  |  |
| StarTrucker | `playable` | 61.2 | 1552 | ✓ |  |  |
| stppc2x-v1.0 | `playable` | 42.2 | 1562 | ✓ |  |  |
| stransball2 | `playable` | 60.2 | 1532 | ✓ |  |  |
| street2x | `playable` | 58.0 | 1558 | ✓ |  |  |
| subhunt | `playable` | 61.4 | 1553 | ✓ |  |  |
| SuperChickenFallDemo | `playable` | 61.7 | 1555 | ✓ |  |  |
| SuperPaf_v1.0 | `playable` | 61.1 | 1552 | ✓ |  |  |
| SuperPixelJumper v1.1 for GP2X | `playable` | 61.2 | 1539 | ✓ |  |  |
| SuperSonicSpeed | `playable` | 61.4 | 1553 | ✓ |  |  |
| supertux-0.1.3-gp2x-v4 | `playable` | 55.7 | 1520 | ✓ |  |  |
| survival | `playable` | 60.9 | 1561 | ✓ |  |  |
| symbolica-0.8 | `playable` | 60.7 | 1539 | ✓ |  |  |
| tail-tale | `playable` | 61.5 | 1552 | ✓ |  |  |
| tecnoballz-0.91-gp2x | `playable` | 56.8 | 1470 | ✓ |  |  |
| tetwins | `playable` | 52.6 | 109 | ✓ |  |  |
| ThreeTs_Game | `playable` | 56.1 | 120 | ✓ |  |  |
| Thruster_GP2X | `playable` | 61.6 | 1555 | ✓ |  |  |
| tikka_dungeons_demo_1 | `playable` | 61.3 | 1541 | ✓ |  |  |
| tilematch-0.6 | `playable` | 97.3 | 2466 | ✓ |  |  |
| tileworld2x | `playable` | 56.6 | 1513 | ✓ |  |  |
| TouchGames | `playable` | 117.3 | 2967 | ✓ |  |  |
| towertopplergp2x | `playable` | 59.5 | 1554 | ✓ |  |  |
| Trap75 | `playable` | 61.7 | 1552 | ✓ |  |  |
| treev060 | `playable` | 60.5 | 1556 | ✓ |  |  |
| TUcS.app(V0.7.0 - GP2X) | `playable` | 31.7 | 802 | ✓ |  |  |
| Unicolor | `playable` | 60.7 | 1553 | ✓ |  |  |
| vectoroids-2x | `playable` | 61.4 | 1553 | ✓ |  |  |
| VekDemo2 | `playable` | 117.3 | 2959 | ✓ |  |  |
| Vektar | `playable` | 117.9 | 2967 | ✓ |  |  |
| vektar-free | `playable` | 72.4 | 1834 | ✓ |  |  |
| vektarpack_v1 | `playable` | 88.9 | 2261 | ✓ |  |  |
| Ventifact | `playable` | 60.3 | 1562 | ✓ |  |  |
| vexed-gp2x-10 | `playable` | 60.8 | 1534 | ✓ |  |  |
| vorton-b4 | `playable` | 59.4 | 1554 | ✓ |  |  |
| war_and_warriorgp2x | `playable` | 61.7 | 1554 | ✓ |  |  |
| warcraft | `playable` | 60.6 | 1557 | ✓ |  |  |
| warehouse_panic_v1.1_gp2x | `playable` | 44.1 | 587 | ✓ |  |  |
| waternetgp2x | `playable` | 58.3 | 1568 | ✓ |  |  |
| wehaveballs | `playable` | 61.0 | 1536 | ✓ |  |  |
| whacky | `playable` | 61.1 | 1544 | ✓ |  |  |
| WindAndWater_teaser_110 | `playable` | 61.3 | 1542 | ✓ |  |  |
| Wiztern Demo | `playable` | 107.2 | 1867 | ✓ |  |  |
| wnw | `playable` | 60.7 | 1541 | ✓ |  |  |
| xenitris_demo | `playable` | 62.2 | 1571 | ✓ |  |  |
| xigon-X-gp2x-V1 | `playable` | 61.6 | 1554 | ✓ |  |  |
| Xpired2x 1.0 beta | `playable` | 61.0 | 1553 | ✓ |  |  |
| xRick | `playable` | 61.3 | 1554 | ✓ |  |  |
| yahtzee-v21 | `playable` | 61.8 | 1563 | ✓ |  |  |
| znax | `playable` | 59.4 | 1560 | ✓ |  |  |
| Znumbers | `playable` | 51.3 | 108 | ✓ |  |  |
| Zoids Quest2X-0.0.1-2 | `playable` | 61.1 | 1566 | ✓ |  |  |
| zoltan 2x | `playable` | 60.4 | 1553 | ✓ |  |  |
| zooov11 | `playable` | 32.1 | 808 | ✓ |  |  |
| ztunnel-0 | `playable` | 60.8 | 1537 | ✓ |  |  |

### Wiz (153 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| abuse-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| alephone-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| altitude | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/altitude/altitude/altitude.gpe' is not an ARM ELF an |
| Art Shot Wiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/Art Shot Wiz/ArtShot/ArtShotWiz.gpe' is not an ARM E |
| Biological Defend | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/Biological Defend/biodef/biodef.gpe' is not an ARM E |
| BitDEFENSE | `incompatible` | 0.0 | 0 | – | no-frames |  |
| BlastRiot122Wiz | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x4000 |
| Camelot Warriors | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/Camelot Warriors/cw/cw.gpe' is not an ARM ELF and no |
| cgenius-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| chroma 1.01 v1 | `incompatible` | 0.2 | 1 | – | no-frames |  |
| CloneKeen2X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Demons World | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| Dragons of Rage EX | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Wiz/Dragons of Rage EX' |
| EpicFreeFall_Wiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/EpicFreeFall_Wiz/EpicFreeFall/freefall.gpe' is not a |
| EpicRocks_Wiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/EpicRocks_Wiz/EpicRocks/EpicRocks.gpe' is not an ARM |
| epiphany | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Geca Blaster 2 (Gp2x Wiz) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/Geca Blaster 2 (Gp2x Wiz)/GecaBlaster2/GecaBlaster2W |
| gnurobbo_0.65_wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| hheretic | `incompatible` | 0.0 | 0 | – | no-frames |  |
| hhexen | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ioquake2 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| kuklomenos | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ladiesofrage | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Wiz/ladiesofrage' |
| malvado | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/malvado/malvado/malvado.gpe' is not an ARM ELF and n |
| Maplevania | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Wiz/Maplevania' |
| MegaMan The Power War Ep1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Wiz/MegaMan The Power War Ep1' |
| metaphysik | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Wiz/metaphysik' |
| midway | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/midway/midway/midway.gpe' is not an ARM ELF and no r |
| Minigolf | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/Minigolf/minigolf/minigolf.gpe' is not an ARM ELF an |
| Monster2-1.0-wiz | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| nethack-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| noiz2sa_wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| openggs | `incompatible` | 0.0 | 0 | – | no-frames |  |
| openjazz-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| opentyrian_wiz_source | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Wiz/opentyrian_wiz_source' |
| Out Zone | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| pgw | `incompatible` | 13.3 | 19 | ✓ | no-frames |  |
| prboom-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Propis | `incompatible` | 0.0 | 0 | – | no-frames |  |
| quake1-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| quake_0.03 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| RailroadRampage_1.2_Wiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/RailroadRampage_1.2_Wiz/RailroadRampage_Wiz/Railroad |
| roadfighter | `incompatible` | 0.0 | 0 | – | no-frames |  |
| rott | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Shock Troopers Base Defense | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Skull (Windows, Linux & Gp2x Wiz) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/Skull (Windows, Linux & Gp2x Wiz)/Skull Game/Skull/S |
| SmallBall_Wiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/SmallBall_Wiz/SmallBall/SmallBall.gpe' is not an ARM |
| smw_1.7 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Snow Bros 2 | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| srb2 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| SudoQ | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/SudoQ/SudoQ/sudoq/sudoq.gpe' is not an ARM ELF and n |
| supertux-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Twin Cobra | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| Wiz_Propis_Demo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| wolf4sdl_wiz_svn | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Zero Wing | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| albion-v1.0.1-wiz | `black` | 33.0 | 43 | ✓ | black-screen |  |
| Balloonacy_wiz_wip | `black` | 113.5 | 2910 | ✓ | black-screen |  |
| blingo | `black` | 30.9 | 74 | ✓ | black-screen |  |
| BubbleTrainWiz_5-20-09 | `black` | 44.3 | 1201 | ✓ | black-screen |  |
| CartoonWiz | `black` | 115.1 | 2906 | ✓ | black-screen |  |
| CDogs-wiz | `black` | 3.1 | 9 | – | black-screen |  |
| ColonyConflict_V1.1_B6 | `black` | 107.2 | 2911 | ✓ | black-screen |  |
| Dastardly_Dungeon | `black` | 20.4 | 32 | ✓ | black-screen |  |
| DungeonRunner | `black` | 111.4 | 2908 | ✓ | black-screen |  |
| DuoWIZ_Pong | `black` | 112.3 | 2911 | ✓ | black-screen |  |
| eduke32 | `black` | 12.8 | 18 | – | black-screen |  |
| freecell2x | `black` | 109.1 | 2906 | ✓ | black-screen |  |
| March of the mini tux(wiz version) | `black` | 114.7 | 2907 | ✓ | black-screen |  |
| nazcadreams | `black` | 28.5 | 52 | ✓ | black-screen |  |
| nazcarunners | `black` | 34.8 | 67 | ✓ | black-screen |  |
| Nazcasphere | `black` | 38.2 | 79 | ✓ | black-screen |  |
| opentyrian | `black` | 12.2 | 9 | – | black-screen |  |
| paraballwiz | `black` | 6.9 | 7 | – | black-screen |  |
| PEZ | `black` | 11.6 | 11 | – | black-screen |  |
| PPlane2.WIZ | `black` | 110.1 | 2912 | ✓ | black-screen |  |
| protozoa | `black` | 29.6 | 49 | ✓ | black-screen |  |
| Ruckman-Wiz | `black` | 26.1 | 66 | ✓ | black-screen |  |
| Shanghai2 | `black` | 48.5 | 102 | ✓ | black-screen |  |
| SimOniZ | `black` | 114.0 | 2908 | ✓ | black-screen |  |
| tetwizdownload | `black` | 115.3 | 2905 | ✓ | black-screen |  |
| The Minigame Project | `black` | 56.9 | 1529 | ✓ | black-screen |  |
| tilt | `black` | 26.8 | 34 | ✓ | black-screen |  |
| Trap75 | `black` | 61.2 | 1539 | ✓ | black-screen |  |
| tricorder | `black` | 33.2 | 69 | ✓ | black-screen |  |
| TUcS.app(V0.7.0 - Wiz) | `black` | 112.8 | 2908 | ✓ | black-screen |  |
| uqm2x_release.1.1 | `black` | 75.3 | 369 | – | black-screen |  |
| warcraft-beta3-wiz | `black` | 44.4 | 48 | ✓ | black-screen |  |
| wiz-car-binary_090818a | `black` | 60.7 | 1527 | ✓ | black-screen |  |
| Wiz_Blox | `black` | 113.9 | 2911 | ✓ | black-screen |  |
| wiz_drench | `black` | 114.2 | 2907 | ✓ | black-screen |  |
| WIZ_S4S | `black` | 114.9 | 2906 | ✓ | black-screen |  |
| WizSticks | `black` | 107.2 | 2905 | ✓ | black-screen |  |
| xcom1-v1.0.2-wiz | `black` | 72.2 | 146 | ✓ | black-screen |  |
| xcom2-v1.0.1-wiz | `black` | 120.6 | 3071 | ✓ | black-screen |  |
| gobble | `ingame` | 45.5 | 141 | – | no-audio |  |
| Powder2X_wiz_114_v01 | `ingame` | 58.7 | 1495 | – | no-audio |  |
| spout | `ingame` | 61.1 | 1539 | – | no-audio |  |
| Sudoku2X | `ingame` | 60.8 | 1533 | – | no-audio |  |
| wizchess-v1.1.0-bin | `ingame` | 60.7 | 1536 | – | no-audio |  |
| wizchess-v1.2.0-bin | `ingame` | 60.9 | 1541 | – | no-audio |  |
| wizgo-v1.1.0-bin | `ingame` | 60.7 | 1541 | – | no-audio |  |
| WizGolf | `ingame` | 60.9 | 1539 | – | no-audio |  |
| wizmancala-v1.1.2-bin | `ingame` | 61.0 | 1542 | – | no-audio |  |
| wizpong | `ingame` | 60.1 | 1526 | – | no-audio |  |
| Worship Vector | `ingame` | 61.0 | 1536 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 160px; left and right halves are near- |
| [DEMO] Wiztern | `playable` | 53.9 | 567 | ✓ |  |  |
| AdamantArmorAffectionWiz | `playable` | 60.1 | 1536 | ✓ |  |  |
| airstrike-wiz-1.01 | `playable` | 105.1 | 2674 | ✓ |  |  |
| alexsfalldown | `playable` | 61.6 | 1548 | ✓ |  |  |
| Animatch Wiz | `playable` | 58.1 | 1532 | ✓ |  |  |
| Asteroids | `playable` | 59.7 | 1526 | ✓ |  |  |
| battlejewels-wiz-public001demo | `playable` | 60.5 | 1531 | ✓ |  |  |
| beat2x-wiz | `playable` | 60.1 | 1521 | ✓ |  |  |
| Blix2x | `playable` | 61.7 | 1549 | ✓ |  |  |
| Boomshine2x_1.12_wiz | `playable` | 58.6 | 1525 | ✓ |  |  |
| BugwarsSE | `playable` | 59.7 | 1522 | ✓ |  |  |
| Dd2x | `playable` | 57.5 | 258 | ✓ |  |  |
| deicide3_eng | `playable` | 58.0 | 413 | ✓ |  |  |
| Ghostpix | `playable` | 59.9 | 1527 | ✓ |  |  |
| gr-v1001-wiz | `playable` | 59.6 | 1538 | ✓ |  |  |
| herknights | `playable` | 58.4 | 1529 | ✓ |  |  |
| hexen2 | `playable` | 58.1 | 1488 | ✓ |  |  |
| minos-gp2x-wiz | `playable` | 59.4 | 1508 | ✓ |  |  |
| mush_gp2x | `playable` | 42.5 | 1090 | ✓ |  |  |
| mush_gp2x-0 | `playable` | 33.1 | 884 | ✓ |  |  |
| Myriad | `playable` | 58.9 | 1531 | ✓ |  |  |
| NewSuperPang05 | `playable` | 60.2 | 1527 | ✓ |  |  |
| paf | `playable` | 60.3 | 1527 | ✓ |  |  |
| Pentominos | `playable` | 61.5 | 1547 | ✓ |  |  |
| Pharaoh | `playable` | 51.7 | 109 | ✓ |  |  |
| PhishyWiz | `playable` | 59.7 | 1528 | ✓ |  |  |
| PPlane | `playable` | 60.3 | 1527 | ✓ |  |  |
| preggo_Wiz | `playable` | 52.7 | 355 | ✓ |  |  |
| PuzzleDevilWizDemo | `playable` | 56.0 | 1414 | ✓ |  |  |
| Rezerwar | `playable` | 54.3 | 633 | ✓ |  |  |
| rockrain-gp2x-wiz | `playable` | 61.4 | 1547 | ✓ |  |  |
| Sachunsung2 | `playable` | 51.4 | 109 | ✓ |  |  |
| scummvm-1.2.0-gp2xwiz | `playable` | 57.5 | 1502 | ✓ |  |  |
| sleuthslots | `playable` | 58.4 | 1524 | ✓ |  |  |
| SOD_Wiz | `playable` | 58.9 | 1527 | ✓ |  |  |
| Sopwith | `playable` | 61.3 | 1544 | ✓ |  |  |
| Space Varments | `playable` | 55.6 | 593 | ✓ |  |  |
| Sqdef_Wiz_14A | `playable` | 60.1 | 1539 | ✓ |  |  |
| Tail Tale | `playable` | 61.0 | 1534 | ✓ |  |  |
| WizFrontier v0.1 | `playable` | 60.8 | 1606 | ✓ |  |  |
| wizznic-0.9.9-wiz | `playable` | 58.7 | 1533 | ✓ |  |  |
| wnw_demo | `playable` | 60.9 | 1538 | ✓ |  |  |
| WWII | `playable` | 57.9 | 1525 | ✓ |  |  |
| xpiredwiz.eng.101 | `playable` | 59.2 | 1503 | ✓ |  |  |
| xRick | `playable` | 61.1 | 1549 | ✓ |  |  |
| znumbers | `playable` | 50.8 | 109 | ✓ |  |  |
| Zoltan | `playable` | 59.1 | 1530 | ✓ |  |  |

### Caanoo (205 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| 20110831 - Bomber Run Redux | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/20110831 - Bomber Run Redux/game/bomber_run_bennu |
| Abbaye_caanoo | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| Abbaye_caanoo_v3 | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| aggressivepong-pre21.1-gph-uni | `incompatible` | 0.0 | 0 | – | no-frames |  |
| animatch | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Animatch_titlebar | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/Animatch_titlebar' |
| apocalypso Caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ArtShotCaanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/ArtShotCaanoo/ArtShotCaanoo/ArtShot/ArtShotCaanoo |
| audiorace-v1.5-can | `incompatible` | 0.0 | 0 | – | no-frames |  |
| BermudaS_caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Blix2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| BubblePop (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/BubblePop (Caanoo)/BubblePop/BubblePop.gpe' is no |
| caanmines | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/caanmines' |
| Caanoo-Biniax2x_titlebar | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/Caanoo-Biniax2x_titlebar' |
| caanoo-tyrian-v1.1-bin | `incompatible` | 0.0 | 0 | ✓ | missing-game-data |  |
| caanoo_tyrian_titlebar | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/caanoo_tyrian_titlebar' |
| can-zomb_3 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/can-zomb_3/bgd-zomb/zomb/bgd-zomb.gpe' is not an  |
| chexquest-titlebar | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/chexquest-titlebar' |
| Coral Sea (Caanoo - Bennu) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Coral Sea (Caanoo - Bennu)/coral_sea/coral_sea.gp |
| Deadly Eye (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Deadly Eye (Caanoo)/DeadlyEye/DeadlyEye.gpe' is n |
| DefendorX_C | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/DefendorX_C/defendorx/bin/defendorx.gpe' is not a |
| Echo V.1.3.2 (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Echo V.1.3.2 (Caanoo)/echo_game/echo_caanoo.gpe'  |
| echo_caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| EEEEK! EEEEEK! HOOOOOOK!!! | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/EEEEK! EEEEEK! HOOOOOOK!!!/eek/eek.gpe' is not an |
| EpicFreeFall | `incompatible` | 0.0 | 0 | – | no-frames |  |
| EpicFreeFall Caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Firewhip-Caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Firewhip-Caanoo/firewhip/firewhip.gpe' is not an  |
| freedroid_Caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| fungp.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X Caanoo/fungp.zip' (exit 32512) |
| Geca Blaster 2 (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Geca Blaster 2 (Caanoo)/Geca Blaster 2 (Caanoo)/G |
| Hamster's Escape 3D (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Hamster's Escape 3D (Caanoo)/Hamster's Escape 3D  |
| HamstersEscape (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/HamstersEscape (Caanoo)/HamstersEscape (Caanoo)/H |
| Hardcore Fight (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Hardcore Fight (Caanoo)/HardcoreFight/HardcoreFig |
| Hero_The_Realm-DEMO | `incompatible` | 0.0 | 0 | – | no-frames |  |
| HeroTheRealm_DEMOv2 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Humos-Caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ini and icon for wolf3d | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/ini and icon for wolf3d' |
| instead-1.6.1-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| KOF (Ver. 5f) (Caanoo) | `incompatible` | 0.0 | 0 | – | no-frames |  |
| liar.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X Caanoo/liar.zip' (exit 32512) |
| Liquid Counter.caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Liquid Counter.caanoo/liquidcount/liquidcount.gpe |
| MasteriesRunners (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/MasteriesRunners (Caanoo)/MasteriesRunners (Caano |
| Metal Slug Zombies | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Metal Slug Zombies/msz/msz.gpe' is not an ARM ELF |
| Mission_faileD 1.2 [Caanoo] | `incompatible` | 0.0 | 0 | – | no-frames |  |
| monster | `incompatible` | 0.0 | 0 | – | no-frames |  |
| MrDrillux | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/MrDrillux' |
| openjazz-caanoo | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| openttd_c | `incompatible` | 0.0 | 0 | – | no-frames |  |
| OperationFenix (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/OperationFenix (Caanoo)/OperationFenix/OperationF |
| PantaVsDragon (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/PantaVsDragon (Caanoo)/PantaVsDragon (Caanoo)/Pan |
| Protect&rescue | `incompatible` | 0.0 | 0 | – | no-frames |  |
| purito_cycling_1.5_Caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/purito_cycling_1.5_Caanoo/game/purito_cycling_1.5 |
| puzsion | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/puzsion/puzsion/puzsion.gpe' is not an ARM ELF an |
| quake1-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| QUAKE1.INI AND ICON SET | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/QUAKE1.INI AND ICON SET' |
| quake1_addons | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/quake1_addons' |
| quake1_build-20111024 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/accel |
| quake2-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| RailroadRampage_1.2_Caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/RailroadRampage_1.2_Caanoo/RailroadRampage_Caanoo |
| reminiscence-v0.1.10-bin | `incompatible` | 0.0 | 0 | – | no-frames |  |
| rotate | `incompatible` | 0.5 | 1 | – | no-frames |  |
| runner-Caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| saaa_ext | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/saaa_ext' |
| sbt | `incompatible` | 0.0 | 0 | – | no-frames |  |
| sbtime_caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/sbtime_caanoo/sbtime/sbtime.gpe' is not an ARM EL |
| SDLLopan Banner and Icon | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/SDLLopan Banner and Icon' |
| sdllopan_v4-all | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| sdlquake_build-20111113-0 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Sitwell (Caanoo) | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Skull (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Skull (Caanoo)/Skull Game/Skull/Skull.gpe' is not |
| smallball | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/smallball/smallball/SmallBall.gpe' is not an ARM  |
| smallball-Caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/smallball-Caanoo/smallball/SmallBall.gpe' is not  |
| SnailRace_C | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/SnailRace_C/snailsrace/snailsrace.gpe' is not an  |
| SOD(r181) | `incompatible` | 0.0 | 0 | – | no-frames |  |
| SORRv5_Caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| space52_caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/space52_caanoo/space_52/space_52_caanoo.gpe' is n |
| supertux | `incompatible` | 1.8 | 1 | – | no-frames |  |
| the solitarie | `incompatible` | 0.0 | 0 | – | no-frames |  |
| tmw_v1.0.0-beta-2_caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Txishos (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Txishos (Caanoo)/Caanoo/Txishos/Txishos.gpe' is n |
| warcraft-beta3-caanoo | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| zlocada-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zombiesorbet_v1.0_caanoo | `incompatible` | 23.1 | 593 | ✓ | no-frames |  |
| zomg-Caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/zomg-Caanoo/Zomg/zomg.gpe' is not an ARM ELF and  |
| zsxd | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Zverealm-Caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Zverealm-Caanoo/Zverealm/Zverealm.gpe' is not an  |
| aquaVenture | `black` | 12.2 | 34 | – | black-screen |  |
| arcadevol1 | `black` | 5.6 | 6 | ✓ | black-screen |  |
| B'lox! | `black` | 32.8 | 324 | ✓ | black-screen |  |
| Balloonacy | `black` | 34.1 | 335 | ✓ | black-screen |  |
| Blingo | `black` | 40.0 | 113 | ✓ | black-screen |  |
| Blitz | `black` | 39.3 | 387 | ✓ | black-screen |  |
| BubbleTrain | `black` | 1.0 | 2 | ✓ | black-screen |  |
| cat_trap | `black` | 37.5 | 367 | ✓ | black-screen |  |
| Drench | `black` | 12.3 | 33 | – | black-screen |  |
| Flappynerd_Caanoo | `black` | 24.6 | 242 | ✓ | black-screen |  |
| Geek_em_up_CAANOO | `black` | 21.3 | 232 | ✓ | black-screen |  |
| Guru Logic | `black` | 37.4 | 368 | ✓ | black-screen |  |
| JUMPNRUN | `black` | 55.7 | 1551 | ✓ | black-screen |  |
| kenlab-caanoo | `black` | 44.5 | 419 | – | black-screen |  |
| laserchess_c | `black` | 18.0 | 37 | – | black-screen |  |
| MNV_Caanoo_Release1 | `black` | 41.9 | 412 | ✓ | black-screen |  |
| powermanga-0.80 | `black` | 47.0 | 1227 | ✓ | black-screen |  |
| SantaMania | `black` | 53.0 | 1346 | ✓ | black-screen |  |
| STRATEGY | `black` | 6.9 | 13 | ✓ | black-screen |  |
| Trap75 | `black` | 47.7 | 1201 | ✓ | black-screen |  |
| xcom1-v1.0.2-caanoo | `black` | 15.7 | 10 | ✓ | black-screen |  |
| xcom2-v1.0.1-caanoo | `black` | 64.2 | 1633 | ✓ | black-screen |  |
| aimcaanoo | `ingame` | 49.8 | 1281 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 160px; left and right halves are near- |
| Arcadevol3 | `ingame` | 58.7 | 1547 | – | flat-fill |  |
| balls12_caanoo_bin | `ingame` | 37.1 | 76 | – | no-audio |  |
| Blackjack21v1.1 | `ingame` | 59.8 | 1509 | – | no-audio |  |
| caanoo-biniax2-v1.30-bin | `ingame` | 17.2 | 433 | ✓ | low-fps |  |
| caanoo-chess-v1.1.0-bin | `ingame` | 44.7 | 1130 | – | no-audio |  |
| caanoo-go-v1.1.0-bin | `ingame` | 45.3 | 1144 | – | no-audio |  |
| caanoo-mancala-v1.1.0-bin | `ingame` | 43.7 | 1103 | – | no-audio |  |
| chexquest-caanoo | `ingame` | 17.6 | 453 | ✓ | low-fps |  |
| cllwrth | `ingame` | 24.7 | 624 | ✓ | low-fps |  |
| deminor | `ingame` | 11.8 | 27 | – | low-fps |  |
| gnp_104 | `ingame` | 50.6 | 1354 | ✓ | flat-fill |  |
| gnuRobbo | `ingame` | 16.3 | 220 | ✓ | low-fps |  |
| gr-v1001-caanoo | `ingame` | 9.8 | 255 | ✓ | low-fps |  |
| hexahop_1.0 | `ingame` | 53.6 | 1348 | – | no-audio |  |
| jump_n_blob_caanoo | `ingame` | 3.4 | 91 | ✓ | low-fps |  |
| ketm | `ingame` | 39.1 | 1292 | – | no-audio |  |
| knight | `ingame` | 107.7 | 2712 | ✓ | flat-fill |  |
| Liar | `ingame` | 12.4 | 85 | ✓ | low-fps |  |
| llcpcls-caanoo | `ingame` | 16.8 | 36 | ✓ | low-fps |  |
| meritous | `ingame` | 87.5 | 2227 | ✓ | flat-fill |  |
| MISC | `ingame` | 59.3 | 1578 | – | no-audio |  |
| mtknights | `ingame` | 31.1 | 784 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 156px; top and bottom halves are near- |
| nlove_0.6.2_(beta)_caanoo | `ingame` | 16.7 | 41 | – | low-fps |  |
| noiz2sa_caanoo | `ingame` | 16.9 | 128 | ✓ | flat-fill |  |
| nuclearchess | `ingame` | 220.8 | 5566 | – | no-audio |  |
| Pharaoh | `ingame` | 12.1 | 25 | ✓ | low-fps |  |
| powder | `ingame` | 52.8 | 1342 | – | no-audio |  |
| prboom-caanoo | `ingame` | 60.4 | 1536 | – | no-audio |  |
| profanation_Caanoo | `ingame` | 18.0 | 39 | ✓ | low-fps |  |
| pushover-v0.2-bin | `ingame` | 18.1 | 459 | ✓ | low-fps |  |
| rg_ura_103 | `ingame` | 54.8 | 1435 | ✓ | flat-fill |  |
| Sachunsung2 | `ingame` | 22.1 | 45 | ✓ | low-fps |  |
| tlosaf_v12-caanoo | `ingame` | 61.4 | 1547 | – | no-audio |  |
| Vigo | `ingame` | 53.0 | 909 | – | no-audio |  |
| wolf4sdl-caanoo | `ingame` | 60.3 | 1537 | – | no-audio |  |
| xpiredcan.eng.101 | `ingame` | 0.1 | 3 | ✓ | low-fps |  |
| zelda-roth-olb-3t_caanoo | `ingame` | 20.5 | 551 | ✓ | low-fps |  |
| aaa | `playable` | 59.7 | 1508 | ✓ |  |  |
| aaaa | `playable` | 54.4 | 1380 | ✓ |  |  |
| ADVENTURE | `playable` | 55.6 | 1512 | ✓ |  |  |
| Amoebax | `playable` | 53.8 | 1392 | ✓ |  |  |
| Arcadevol2 | `playable` | 60.1 | 1551 | ✓ |  |  |
| battlejewels-105-caanoo-beta | `playable` | 53.4 | 1350 | ✓ |  |  |
| caanoo-12swap-v1.0-bin | `playable` | 39.5 | 999 | ✓ |  |  |
| caanoo-gnurobbo-0.68 | `playable` | 32.2 | 432 | ✓ |  |  |
| cavestory | `playable` | 57.7 | 1551 | ✓ |  |  |
| ccrg-caanoo | `playable` | 46.2 | 198 | ✓ |  |  |
| cgenius-caanoo | `playable` | 27.9 | 725 | ✓ |  |  |
| cooldowncaanoo | `playable` | 762.3 | 19756 | ✓ |  |  |
| daff_s_adventure_2_caanoo | `playable` | 26.0 | 668 | ✓ |  |  |
| deadlyc | `playable` | 56.1 | 1416 | ✓ |  |  |
| DealorNoDeal | `playable` | 60.4 | 1554 | ✓ |  |  |
| demons | `playable` | 118.6 | 2981 | ✓ |  |  |
| dynamate_c | `playable` | 28.5 | 744 | ✓ |  |  |
| fleshchasmer | `playable` | 54.9 | 1398 | ✓ |  |  |
| freeheroes2_c | `playable` | 39.3 | 80 | ✓ |  |  |
| fshark | `playable` | 85.7 | 2153 | ✓ |  |  |
| Fywod_caanoo | `playable` | 55.0 | 1395 | ✓ |  |  |
| getstar | `playable` | 85.6 | 2152 | ✓ |  |  |
| gravityforcev2 | `playable` | 46.7 | 1181 | ✓ |  |  |
| hellfire | `playable` | 118.5 | 2975 | ✓ |  |  |
| jumpToTheMoon_c | `playable` | 25.0 | 234 | ✓ |  |  |
| lmission_0.5 | `playable` | 58.4 | 1467 | ✓ |  |  |
| next_element | `playable` | 61.3 | 1548 | ✓ |  |  |
| outzone | `playable` | 85.5 | 2148 | ✓ |  |  |
| pang | `playable` | 58.9 | 1507 | ✓ |  |  |
| pengupop | `playable` | 37.8 | 168 | ✓ |  |  |
| propis | `playable` | 49.3 | 1247 | ✓ |  |  |
| PUZZLEBOARDS | `playable` | 108.6 | 2968 | ✓ |  |  |
| RACING | `playable` | 59.1 | 1560 | ✓ |  |  |
| rhythmosplay_1.1.12 | `playable` | 51.4 | 1304 | ✓ |  |  |
| Shanghai2 | `playable` | 45.4 | 93 | ✓ |  |  |
| SHOOTERS | `playable` | 77.0 | 2099 | ✓ |  |  |
| SimOniZ | `playable` | 52.7 | 1345 | ✓ |  |  |
| Slap | `playable` | 85.6 | 2151 | ✓ |  |  |
| smw_1.7 | `playable` | 29.7 | 797 | ✓ |  |  |
| snowbros | `playable` | 118.5 | 2973 | ✓ |  |  |
| snowbros2 | `playable` | 100.6 | 2529 | ✓ |  |  |
| SPORTS | `playable` | 59.3 | 1551 | ✓ |  |  |
| sqrxz-v0996-caanoo | `playable` | 52.8 | 1348 | ✓ |  |  |
| sqrxz2-v0.80-caanoo | `playable` | 58.2 | 1478 | ✓ |  |  |
| stppc-caanoo-29-11-2010 | `playable` | 35.4 | 342 | ✓ |  |  |
| tailtale4c | `playable` | 61.8 | 1557 | ✓ |  |  |
| Tigerhell | `playable` | 85.6 | 2152 | ✓ |  |  |
| Tile | `playable` | 55.7 | 1546 | ✓ |  |  |
| tong-caanoo | `playable` | 57.5 | 1507 | ✓ |  |  |
| Truxton | `playable` | 85.4 | 2147 | ✓ |  |  |
| truxton2 | `playable` | 85.6 | 2149 | ✓ |  |  |
| twincobr | `playable` | 85.7 | 2154 | ✓ |  |  |
| twinhawk | `playable` | 85.7 | 2150 | ✓ |  |  |
| Wardner | `playable` | 118.5 | 2983 | ✓ |  |  |
| warehouse_panic_v1.1_caanoo | `playable` | 34.4 | 453 | ✓ |  |  |
| WindandWater | `playable` | 60.6 | 1535 | ✓ |  |  |
| Wizznic 0.9.2- preview | `playable` | 29.2 | 795 | ✓ |  |  |
| wvector | `playable` | 44.0 | 1105 | ✓ |  |  |
| zerowing | `playable` | 118.4 | 2977 | ✓ |  |  |
