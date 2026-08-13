# magiceyes compatibility sweep

Every GP2X / Wiz / Caanoo title on the corpus share, booted headlessly through the native engine (`bin/me_unicorn`) and scored from the shm framebuffer + the structured run report. Regenerate with `tools/test/compat_report.py`.


## Summary

| Platform | Titles | Playable | Ingame | Black | Incompatible | Crashed |
|---|--:|--:|--:|--:|--:|--:|
| GP2X | 673 | 301 | 103 | 90 | 179 | 0 |
| Wiz | 153 | 46 | 13 | 38 | 56 | 0 |
| Caanoo | 205 | 57 | 38 | 24 | 86 | 0 |
| **All** | **1031** | **404** | **154** | **152** | **321** | **0** |

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
| **Boots but renders only black** (`black-screen`) | 150 | Caanoo, GP2X, Wiz | n/a |
| **Never rendered a frame (cause unknown)** (`no-frames`) | 112 | Caanoo, GP2X, Wiz | n/a |
| **Not a 32-bit ARM ELF** (`not-arm-elf`) | 99 | Caanoo, GP2X, Wiz | n/a |
| **Renders at speed but no audio** (`no-audio`) | 89 | Caanoo, GP2X, Wiz | n/a |
| **No .gpe in the dump** (`no-executable`) | 59 | Caanoo, GP2X, Wiz | n/a |
| **Renders but below 25 fps** (`low-fps`) | 40 | Caanoo, GP2X, Wiz | n/a |
| **Spins forever polling an MMSP2 register** (`mmio-spin`) | 25 | Caanoo, GP2X, Wiz | `0x90a` ×22, `0x4000` ×1, `0x808` ×1, `0x1988` ×1 |
| **Draws only a flat colour** (`flat-fill`) | 16 | Caanoo, GP2X | n/a |
| **Unknown /dev node** (`unknown-device`) | 11 | Caanoo, GP2X, Wiz | `/dev/null` ×5, `/dev/input/mouse/0` ×3, `/dev/accel` ×1, `/dev/cx25874` ×1 |
| **Game data files are missing from the dump** (`missing-game-data`) | 9 | Caanoo, GP2X | n/a |
| **Renders, but the picture is wrong** (`garbled-visuals`) | 8 | Caanoo, GP2X, Wiz | n/a |
| **Archive extraction failed** (`archive-failed`) | 5 | Caanoo, GP2X | n/a |
| **Unimplemented syscall** (`unimplemented-syscall`) | 3 | Caanoo, GP2X | `220 (madvise)` ×1, `113` ×1, `117` ×1 |
| **Could not open a display** (`display-init-failed`) | 1 | GP2X | n/a |

## Renders, but the picture is wrong

These 8 titles pass the running checks (frames advancing, frame rate, audio) while the frame itself is visibly broken, so they are graded `ingame` rather than `playable`. The reasons come from measuring the captured frame: a consistent per-row offset means a stride/pitch mismatch, large-scale repetition means the screen holds more than one copy of itself, and noise far above what dithered artwork reaches means corrupt memory.

| Title | Platform | fps | What the frame looks like |
|---|---|--:|---|
| aimcaanoo | Caanoo | 49.5 | the screen holds a second copy of itself, offset by 160px; left and right halves are near-identical; top and bottom halves are near-identical |
| 1945_GP2X_0.2b | GP2X | 59.7 | pixel-to-pixel noise of 96, far above what dithered artwork reaches; the frame looks like corrupt memory |
| BunnyTraps-v11 | GP2X | 61.8 | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like corrupt memory |
| FleshChasmer | GP2X | 59.9 | the screen holds a second copy of itself, offset by 160px; left and right halves are near-identical |
| GF | GP2X | 60.9 | top and bottom halves are near-identical |
| Life.0.1 | GP2X | 61.7 | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like corrupt memory |
| MoveSweep2X | GP2X | 49.7 | the screen holds a second copy of itself, offset by 96px; left and right halves are near-identical |
| Worship Vector | Wiz | 61.0 | the screen holds a second copy of itself, offset by 160px; left and right halves are near-identical |

## Scored as working, but only painting a flat colour

These 16 titles advanced frames, kept audio running, and held frame rate, so they land in `playable`/`renders`. Their framebuffer never held more than one or two colours, which means the tier overstates them. Worth treating as broken.

| Title | Platform | Status | fps |
|---|---|---|--:|
| Arcadevol3 | Caanoo | `renders` | 59.6 |
| gnp_104 | Caanoo | `playable` | 51.6 |
| knight | Caanoo | `renders` | 10.5 |
| meritous | Caanoo | `playable` | 57.5 |
| mtknights | Caanoo | `playable` | 31.4 |
| noiz2sa_caanoo | Caanoo | `renders` | 17.0 |
| rg_ura_103 | Caanoo | `playable` | 54.7 |
| _-The Reversed Preacher 3-_Hack bIld_ | GP2X | `playable` | 56.6 |
| ASCIIPong2xV0.4 | GP2X | `playable` | 38.8 |
| dumbbell2x-01 | GP2X | `renders` | 69.7 |
| game bIld 2 | GP2X | `playable` | 61.4 |
| Knight Lore | GP2X | `renders` | 10.8 |
| levelEdit | GP2X | `renders` | 61.8 |
| Pond2X | GP2X | `renders` | 61.0 |
| robot-escape | GP2X | `playable` | 85.5 |
| superpang | GP2X | `playable` | 46.3 |

## Cross-title blockers


### Unimplemented syscalls

| Item | Titles |
|---|--:|
| `163 (mremap)` | 24 |
| `43 (times)` | 9 |
| `220 (madvise)` | 6 |
| `97 (setpriority)` | 5 |
| `27 (alarm)` | 2 |
| `113` | 1 |
| `117` | 1 |
| `242 (sched_getaffinity)` | 1 |
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
| `/dev/psaux` | 181 |
| `/dev/usbmouse` | 181 |
| `/dev/null` | 126 |
| `/dev/touchscreen/wm97xx` | 86 |
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
| `unknown_mmio:0xfde00910` | 11 |
| `unknown_mmio:0xfde0091c` | 11 |
| `unknown_mmio:0xfde00924` | 11 |
| `unknown_mmio:0x1988` | 11 |
| `unknown_mmio:0x19c0` | 11 |
| `unknown_mmio:0x19c4` | 11 |
| `unknown_mmio:0xfff6f004` | 11 |
| `unknown_mmio:0xfffe2880` | 9 |
| `unknown_mmio:0xfffe2906` | 9 |

## Per-title results


### GP2X (673 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| 2xHexen2_cheat_patch | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/2xHexen2_cheat_patch' |
| 2xquake003 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| 2xquake2 | `incompatible` | 0.0 | 0 | ✓ | missing-game-data |  |
| 2XRally01 | `incompatible` | 0.0 | 0 | – | display-init-failed |  |
| 4WE_GP2x | `incompatible` | 0.1 | 1 | ✓ | mmio-spin | 0x90a |
| abduction | `incompatible` | 0.0 | 0 | – | no-frames |  |
| abe | `incompatible` | 60.7 | 1529 | ✓ | no-frames |  |
| airpong4GP2X0.0.4 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/airpong4GP2X0.0.4/airpong022/src/AirPong.gpe' is not an  |
| AlienZ | `incompatible` | 0.0 | 0 | – | no-frames |  |
| animatch_v1.2 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/animatch_v1.2' |
| animatch_v1.2.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/animatch_v1.2.zip' (exit 32512) |
| AnotherGame2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/AnotherGame2x/AnotherGame2x/anothergame2x.gpe' is not an |
| atris-1.0.7 | `incompatible` | 61.0 | 1573 | ✓ | no-frames |  |
| B'lox! | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
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
| Bombs Panic | `incompatible` | 0.5 | 1 | ✓ | no-frames |  |
| Boomshine2x_(java) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Boomshine2x_(java)/Boomshine2x/Boomshine2x.gpe' is not a |
| bunkermaster2x04 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Butterfly | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Butterfly' |
| cackb2 | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| Chess2xSkins | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Chess2xSkins' |
| chicken-puyopuyo | `incompatible` | 0.0 | 0 | – | no-frames |  |
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
| Dr. Mates v1.0 | `incompatible` | 43.0 | 100 | ✓ | no-frames |  |
| duckmaze-gp2x-0.1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/duckmaze-gp2x-0.1/duckmaze-gp2x-0.1/duckmaze.gpe' is not |
| duke3d_cheat_patch | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/duke3d_cheat_patch' |
| E-Fighters2x_FIRST_ALPHA_0_0_5_fixedSound | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| EasterQuest | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| exultb4-src | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/exultb4-src' |
| FFDoom | `incompatible` | 1.9 | 1 | – | no-frames |  |
| FindMii | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| Fire | `incompatible` | 0.0 | 0 | – | no-frames |  |
| FleshChasmer Zero (English Patch) | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/FleshChasmer Zero (English Patch)' |
| FlipIR_GP2X | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| Football2X | `incompatible` | 0.1 | 1 | ✓ | no-frames |  |
| Fore_1_0 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Fore_1_0' |
| FP_Default_2.0 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/FP_Default_2.0' |
| freedroid2x06 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/freedroid2x06/Freedroid/FreeDroid.gpe' is not an ARM ELF |
| frotz | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/frotz' |
| FullBoard (test ver.) | `incompatible` | 51.9 | 108 | ✓ | no-frames |  |
| garden2x02 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| GeneralPromise | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| geoQuiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/geoQuiz/geoQuiz.gpe' is not an ARM ELF and no runnable b |
| gnurobbo_0.66_open2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| gp2x-abrick-0.1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/gp2x-abrick-0.1' |
| gp2x-rogue-v1.0 | `incompatible` | 0.0 | 0 | – | no-frames |  |
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
| Klaur | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| kobo_deluxe_beta1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| KQ2X_v3 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Laser2xVers10 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Lexeme | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| Liquid Counter.gp2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Liquid Counter.gp2x/LiquidCount/LiquidCount.gpe' is not  |
| Logoball | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| Lottys_Lines.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/Lottys_Lines.zip' (exit 32512) |
| lumix-beta-01 | `incompatible` | 76.5 | 1926 | – | no-frames |  |
| mariodm | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/mariodm' |
| Midnight2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Midnight2x/dosbox/midnight/midnight.gpe' is not an ARM E |
| misterhachi | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/misterhachi/misterhachi/misterhachi.gpe' is not an ARM E |
| mopesnake-gp2x-0.5 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/mopesnake-gp2x-0.5/mopesnake-gp2x-0.5/mopesnake.gpe' is  |
| MouthTrap | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| mueppv32 | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
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
| Poker_Gp2Xv1.0 | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| PrBoom PWAD pack | `incompatible` | 0.0 | 0 | – | no-frames |  |
| puckman_gp2x | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| pykaraoke-0.6-gp2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| pySlide | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/pySlide/pySlide/pySlide.gpe' is not an ARM ELF and no ru |
| pyTetris | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/pyTetris/pyTetris/pyTetris.gpe' is not an ARM ELF and no |
| Quad | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
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
| Sachunsung2_1 | `incompatible` | 50.7 | 106 | ✓ | no-frames |  |
| scummvm-alpha-8a_sky | `incompatible` | 0.0 | 0 | – | no-frames |  |
| scummVMsaves | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/scummVMsaves' |
| Shangai v2 | `incompatible` | 51.2 | 108 | ✓ | no-frames |  |
| shoveit | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/shoveit' |
| Simon2X | `incompatible` | 8.1 | 7 | – | no-frames |  |
| Skin1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/Skin1' |
| smw-1.6_gp2x | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/null |
| snakepan | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/snakepan/Snakepan.gpe' is not an ARM ELF and no runnable |
| SnoodForTileMatch | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/SnoodForTileMatch' |
| snowedin6_v1-00_gp2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/graphics/fb0 |
| sopwith_camel_rc3 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/null |
| space52_gp2x(oficial) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/space52_gp2x(oficial)/space_52/space_52_gp2x.gpe' is not |
| space52_gp2x(open2x) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/space52_gp2x(open2x)/space_52/space_52_gp2x.gpe' is not  |
| SpaceSnake | `incompatible` | 0.1 | 1 | ✓ | mmio-spin | 0x90a |
| squaregame2xV1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/squaregame2xV1/squaregame2x.gpe' is not an ARM ELF and n |
| Starship Soldier.gp2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Starship Soldier.gp2x/StarshipSoldier/starship_soldier.g |
| stppc2x-v1.1 | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/stppc2x-v1.1' |
| stppc2x-v1.1.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/stppc2x-v1.1.zip' (exit 32512) |
| strife | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/strife/dosbox/strife/strife.gpe' is not an ARM ELF and n |
| Supa2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Supa2x/dosbox/supaplex.gpe' is not an ARM ELF and no run |
| testmem2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/testmem2x/testmem2x/testmem2x.gpe' is not an ARM ELF and |
| TouchGames | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
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
| UQMgp2x-0.5.0_with_content | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| WADFEST | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/WADFEST' |
| wads1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/wads1/wads1/requiem.gpe' is not an ARM ELF and no runnab |
| wads2 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/wads2/wads2/h2h-xmas.gpe' is not an ARM ELF and no runna |
| wizznic06_NES_30levels | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X/wizznic06_NES_30levels' |
| Wolf4SDL | `incompatible` | 0.0 | 0 | – | no-frames |  |
| worminator302 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Znumbers | `incompatible` | 52.7 | 111 | ✓ | no-frames |  |
| Zombiepox2X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zombiesorbet_v1.0_gp2x | `incompatible` | 61.1 | 1567 | ✓ | no-frames |  |
| 2xHexen2 v0.05 PB2 | `black` | 17.6 | 13 | – | black-screen |  |
| 2xWargus_PB1.3 | `black` | 1.6 | 12 | ✓ | black-screen |  |
| 2xZdoom_PB1.2 | `black` | 35.3 | 38 | – | black-screen |  |
| A1GP2XV1_1 | `black` | 19.3 | 10 | – | black-screen |  |
| abuse_1.0 | `black` | 25.4 | 674 | ✓ | black-screen |  |
| albion-v1.0.1-gp2x | `black` | 28.2 | 28 | ✓ | black-screen |  |
| AlienBlaster_1.02 | `black` | 12.0 | 16 | ✓ | black-screen |  |
| altitude | `black` | 42.2 | 124 | ✓ | black-screen |  |
| bang_gp | `black` | 29.7 | 24 | ✓ | black-screen |  |
| BareFistFighter | `black` | 60.8 | 1528 | ✓ | black-screen |  |
| battlejewels-gp2x-062-100 | `black` | 0.5 | 13 | ✓ | mmio-spin | 0x808 |
| BeetleRun | `black` | 17.7 | 9 | ✓ | black-screen |  |
| blockdudegp2x | `black` | 51.6 | 116 | ✓ | black-screen |  |
| blockoid | `black` | 11.6 | 7 | ✓ | black-screen |  |
| Boulders-0 | `black` | 24.5 | 16 | ✓ | black-screen |  |
| BubbleTrain_GP2X-2006_Entry | `black` | 3.8 | 2 | ✓ | black-screen |  |
| CaptainCrusader_GP2XDemo | `black` | 5.7 | 3 | – | black-screen |  |
| CaptainCrusader_GP2XFull | `black` | 5.8 | 3 | – | black-screen |  |
| cat_trap | `black` | 0.1 | 3 | ✓ | black-screen |  |
| cdogs2x04 | `black` | 11.6 | 6 | – | black-screen |  |
| ChopperAttackv1.0.17 | `black` | 0.1 | 3 | ✓ | black-screen |  |
| Comando2gp2xEN | `black` | 31.2 | 16 | ✓ | black-screen |  |
| coppergreen | `black` | 14.2 | 16 | ✓ | black-screen |  |
| d2x-gp2x-0.02 | `black` | 11.9 | 6 | ✓ | black-screen |  |
| Dark_Light_SDL2X | `black` | 25.8 | 21 | ✓ | black-screen |  |
| DeathTrap1_1 | `black` | 11.7 | 16 | ✓ | black-screen |  |
| Digger | `black` | 7.7 | 18 | ✓ | black-screen |  |
| dodge | `black` | 21.4 | 16 | ✓ | black-screen |  |
| duke2x004 | `black` | 17.7 | 9 | – | black-screen |  |
| egoboo-cramfs | `black` | 5.5 | 71 | ✓ | black-screen |  |
| fenix | `black` | 15.1 | 27 | ✓ | black-screen |  |
| fenixGamePack | `black` | 16.8 | 47 | ✓ | black-screen |  |
| Flappynerd_GP2X | `black` | 13.4 | 36 | ✓ | black-screen |  |
| FleshChasmer132c_patch | `black` | 7.8 | 4 | ✓ | black-screen |  |
| FleshChasmer_Dpad | `black` | 8.0 | 4 | ✓ | black-screen |  |
| freecell_1 | `black` | 51.9 | 107 | ✓ | black-screen |  |
| godori | `black` | 8.0 | 4 | – | black-screen |  |
| gp2x-blobwars-0.1 | `black` | 0.1 | 2 | ✓ | black-screen |  |
| gp2x-bubbletrain-0.1 | `black` | 0.2 | 5 | ✓ | black-screen |  |
| gp2x-netrok-0.1 | `black` | 105.0 | 3052 | ✓ | black-screen |  |
| gp2x-tenmado-0.1 | `black` | 0.1 | 2 | – | black-screen |  |
| gp2x-tong-v1 | `black` | 1.2 | 31 | – | black-screen |  |
| gp2xDoukutsu-1.04 | `black` | 16.6 | 13 | ✓ | black-screen |  |
| gp2xJenkasNightmare | `black` | 14.8 | 13 | ✓ | black-screen |  |
| GPgeneral | `black` | 4.0 | 2 | – | black-screen |  |
| gpnoid2x | `black` | 23.4 | 15 | ✓ | black-screen |  |
| GPrina-GP2x_v1.0 | `black` | 60.4 | 1538 | ✓ | black-screen |  |
| just4qix | `black` | 17.9 | 9 | ✓ | black-screen |  |
| liquidwar2x02 | `black` | 3.7 | 2 | – | black-screen |  |
| moonlander | `black` | 15.5 | 18 | ✓ | black-screen |  |
| nazcarunners-0 | `black` | 38.2 | 32 | ✓ | black-screen |  |
| nazcasphere | `black` | 28.7 | 19 | ✓ | black-screen |  |
| Nom | `black` | 13.8 | 7 | – | black-screen |  |
| omok | `black` | 52.3 | 108 | ✓ | black-screen |  |
| openggs | `black` | 60.6 | 1551 | ✓ | black-screen |  |
| openjazz-gp2x | `black` | 14.2 | 13 | ✓ | black-screen |  |
| pacmame | `black` | 7.8 | 4 | – | black-screen |  |
| para3 | `black` | 58.6 | 145 | ✓ | black-screen |  |
| pez | `black` | 11.6 | 6 | – | black-screen |  |
| Pong | `black` | 61.2 | 1539 | – | black-screen |  |
| PowerSlide | `black` | 60.1 | 1523 | ✓ | black-screen |  |
| protozoa v1.0 | `black` | 28.3 | 26 | ✓ | black-screen |  |
| raw2xv0.3.1 | `black` | 12.0 | 6 | – | black-screen |  |
| ruckman_v1.03 | `black` | 36.6 | 98 | ✓ | black-screen |  |
| ShadowWarrior2X | `black` | 10.2 | 6 | – | black-screen |  |
| SimOniZ | `black` | 0.1 | 3 | ✓ | black-screen |  |
| sleuth slots 2x | `black` | 2.5 | 65 | ✓ | black-screen |  |
| SmashGp2x02 | `black` | 59.0 | 1494 | ✓ | black-screen |  |
| sprint_race | `black` | 11.7 | 6 | – | black-screen |  |
| starsystem | `black` | 23.8 | 12 | ✓ | black-screen |  |
| step2x02 | `black` | 52.4 | 112 | ✓ | black-screen |  |
| tesla-Siren | `black` | 24.5 | 14 | ✓ | black-screen |  |
| Tetrablocks.0.4.GP2X | `black` | 47.4 | 110 | ✓ | black-screen |  |
| tilt | `black` | 23.8 | 13 | ✓ | black-screen |  |
| TRAINS | `black` | 13.9 | 7 | ✓ | black-screen |  |
| uhexen | `black` | 4.0 | 2 | – | black-screen |  |
| ultratumba_exp-20100925.gp2x | `black` | 15.9 | 8 | ✓ | black-screen |  |
| uqm2x_release_1.1 | `black` | 88.1 | 358 | – | black-screen |  |
| Volleyball | `black` | 54.5 | 114 | ✓ | black-screen |  |
| warcraft-beta3-gp2x | `black` | 33.0 | 28 | ✓ | black-screen |  |
| Winter_Jumper | `black` | 5.7 | 3 | – | black-screen |  |
| Wizznic_2x_07alpha2 | `black` | 13.4 | 19 | ✓ | black-screen |  |
| wizznic_gp2x-0.9.9 | `black` | 10.1 | 14 | ✓ | black-screen |  |
| wolfdx | `black` | 50.0 | 51 | ✓ | black-screen |  |
| xbak-0.1.3 | `black` | 29.2 | 15 | – | black-screen |  |
| xcom1-v1.0.2-gp2x | `black` | 42.7 | 1581 | ✓ | black-screen |  |
| xcom2-v1.0.1-gp2x | `black` | 109.3 | 2800 | ✓ | black-screen |  |
| xump2x_beta2 | `black` | 27.8 | 14 | ✓ | black-screen |  |
| zcgp2x_211B18_0.4alpha | `black` | 22.4 | 22 | – | black-screen |  |
| Zelda_roth_US_gp2x | `black` | 26.7 | 56 | ✓ | black-screen |  |
| 1945_GP2X_0.2b | `ingame` | 59.7 | 566 | ✓ | garbled-visuals | pixel-to-pixel noise of 96, far above what dithered artwork reaches; the frame looks like  |
| 2xZdoom_selector | `ingame` | 7.9 | 18 | ✓ | low-fps |  |
| _-The Reversed Preacher 3-_Hack bIld_ | `ingame` | 56.6 | 136 | ✓ | flat-fill |  |
| _-the reversed preacher II-_ | `ingame` | 3.5 | 130 | ✓ | low-fps |  |
| a_sn-pong | `ingame` | 41.5 | 1538 | – | no-audio |  |
| ADIC2X | `ingame` | 21.0 | 251 | ✓ | low-fps |  |
| AMazing-3D | `ingame` | 64.0 | 1615 | – | no-audio |  |
| angband2x-v2 | `ingame` | 40.4 | 83 | – | no-audio |  |
| ASCIIPong2xV0.4 | `ingame` | 38.8 | 976 | ✓ | flat-fill |  |
| Birdshoot | `ingame` | 61.9 | 1553 | – | no-audio |  |
| BisfoG | `ingame` | 8.9 | 106 | ✓ | low-fps |  |
| Blocked | `ingame` | 3.9 | 100 | ✓ | low-fps |  |
| bugafactorx-v03-beta | `ingame` | 61.1 | 1548 | – | no-audio |  |
| BunnyTraps-v11 | `ingame` | 61.8 | 1557 | ✓ | garbled-visuals | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like |
| buscaminas | `ingame` | 59.4 | 1506 | – | no-audio |  |
| cardm | `ingame` | 60.8 | 1543 | – | no-audio |  |
| cavecopter_gp2x | `ingame` | 21.2 | 532 | – | low-fps |  |
| Chopper | `ingame` | 60.7 | 1541 | – | no-audio |  |
| Clonk2X_1.0 | `ingame` | 10.6 | 267 | – | not-arm-elf | magiceyes: reload of '/bin/sh' failed |
| cosmo2x_01 | `ingame` | 61.7 | 1563 | – | no-audio |  |
| CromoZome | `ingame` | 19.0 | 493 | ✓ | low-fps |  |
| dopewars2x | `ingame` | 61.4 | 1541 | – | no-audio |  |
| drod-gp2x-1_0 | `ingame` | 22.5 | 1091 | – | low-fps |  |
| dstroyGP2X1402 | `ingame` | 24.6 | 679 | – | low-fps |  |
| dumbbell2x-01 | `ingame` | 69.7 | 654 | – | flat-fill |  |
| dyc_gp2x | `ingame` | 0.2 | 5 | ✓ | low-fps |  |
| dynamategp2x | `ingame` | 18.7 | 552 | ✓ | low-fps |  |
| eggstreme3_v1-00_gp2x | `ingame` | 20.1 | 552 | ✓ | low-fps |  |
| escoba_exp-20101016.gp2x | `ingame` | 61.0 | 1550 | – | no-audio |  |
| extraterrestres-0 | `ingame` | 59.1 | 1564 | – | no-audio |  |
| FCRLG | `ingame` | 60.8 | 1527 | – | no-audio |  |
| fifteen_01 | `ingame` | 60.3 | 563 | – | no-audio |  |
| Firewhip | `ingame` | 0.9 | 13 | ✓ | low-fps |  |
| FleshChasmer | `ingame` | 59.9 | 1527 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 160px; left and right halves are near- |
| flowflowmania-0_6-gp2x | `ingame` | 47.9 | 469 | – | no-audio |  |
| freesci | `ingame` | 58.1 | 261 | – | no-audio |  |
| fruits2x | `ingame` | 51.2 | 106 | – | no-audio |  |
| game bIld 2 | `ingame` | 61.4 | 1549 | ✓ | flat-fill |  |
| gchess-v1.0.1-bin | `ingame` | 61.3 | 1553 | – | no-audio |  |
| gchess-v1.1.0-bin | `ingame` | 61.1 | 1546 | – | no-audio |  |
| GF | `ingame` | 60.9 | 1548 | ✓ | garbled-visuals | top and bottom halves are near-identical |
| gnugo2x | `ingame` | 61.3 | 1551 | – | no-audio |  |
| gorillaz | `ingame` | 13.2 | 331 | ✓ | low-fps |  |
| gp2x-ceferino-0.1 | `ingame` | 59.0 | 1550 | – | no-audio |  |
| gp2x-sand-0.3 | `ingame` | 57.2 | 120 | – | no-audio |  |
| gp2xgo-v1.1.0-bin | `ingame` | 61.6 | 1555 | – | no-audio |  |
| gp2xmancala-v1.1.1-bin | `ingame` | 61.5 | 1554 | – | no-audio |  |
| GP2XOfLife | `ingame` | 4.6 | 115 | – | low-fps |  |
| GPSquares_GP2X | `ingame` | 61.7 | 1551 | – | no-audio |  |
| grow | `ingame` | 61.2 | 1536 | – | no-audio |  |
| gxeskiv | `ingame` | 58.8 | 1484 | – | no-audio |  |
| hex-a-hop | `ingame` | 61.8 | 1554 | – | no-audio |  |
| kampfimall-gp2x | `ingame` | 61.3 | 1543 | – | no-audio |  |
| Knight Lore | `ingame` | 10.8 | 271 | ✓ | flat-fill |  |
| LABYRINTH | `ingame` | 61.4 | 1545 | – | no-audio |  |
| levelEdit | `ingame` | 61.8 | 1557 | – | flat-fill |  |
| Life.0.1 | `ingame` | 61.7 | 1553 | – | garbled-visuals | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like |
| lights-out | `ingame` | 59.6 | 1499 | – | no-audio |  |
| mancala-v1.0.1 | `ingame` | 61.5 | 1556 | – | no-audio |  |
| masterpiece2x | `ingame` | 61.9 | 1558 | – | no-audio |  |
| MazezaMGP2X | `ingame` | 7.5 | 194 | ✓ | low-fps |  |
| minigolf | `ingame` | 61.3 | 1551 | – | no-audio |  |
| MoveSweep2X | `ingame` | 49.7 | 102 | – | garbled-visuals | the screen holds a second copy of itself, offset by 96px; left and right halves are near-i |
| Nebulus_gp2x | `ingame` | 61.1 | 1533 | – | no-audio |  |
| Net-Bubble-gp2x_1-21-06_bin | `ingame` | 56.3 | 526 | – | no-audio |  |
| oxov06 | `ingame` | 47.4 | 97 | – | no-audio |  |
| Payback | `ingame` | 0.5 | 14 | ✓ | low-fps |  |
| PerfectFit | `ingame` | 61.6 | 1554 | – | no-audio |  |
| PocketSnes_SMRPG | `ingame` | 114.8 | 2886 | – | no-audio |  |
| Pond2X | `ingame` | 61.0 | 1534 | – | flat-fill |  |
| pong2player | `ingame` | 57.7 | 118 | – | no-audio |  |
| pong2v060x | `ingame` | 54.9 | 112 | – | no-audio |  |
| powder2x-112 | `ingame` | 61.1 | 1555 | – | no-audio |  |
| prboom-gp2x | `ingame` | 60.9 | 1557 | – | no-audio |  |
| RevoltOfTheBinaryCouriers GP2X | `ingame` | 60.4 | 1522 | – | no-audio |  |
| robot-escape | `ingame` | 85.5 | 174 | ✓ | flat-fill |  |
| rubik | `ingame` | 122.7 | 3105 | – | no-audio |  |
| scummvm-kor0.4.2cvs | `ingame` | 60.6 | 1534 | – | no-audio |  |
| sdlmonkey_0.1 | `ingame` | 61.7 | 1552 | – | no-audio |  |
| snake2x-1.1 | `ingame` | 61.0 | 1558 | – | no-audio |  |
| Solitaire2x-v1.4 | `ingame` | 82.0 | 766 | – | no-audio |  |
| sources_MEMORY2X | `ingame` | 61.2 | 1557 | – | no-audio |  |
| space squares | `ingame` | 61.2 | 1554 | – | no-audio |  |
| SpaceRocks2X | `ingame` | 33.0 | 95 | – | no-audio |  |
| spartak-chess_0.0.4_gp2x | `ingame` | 61.4 | 1554 | – | no-audio |  |
| Sponge Blob Tennis | `ingame` | 41.8 | 1547 | – | no-audio |  |
| spout | `ingame` | 61.6 | 1550 | – | no-audio |  |
| Sqcolony | `ingame` | 63.6 | 1602 | – | no-audio |  |
| sudoku-v1.0 | `ingame` | 61.7 | 1554 | – | no-audio |  |
| sudoku2x-0.5 | `ingame` | 60.8 | 1534 | – | no-audio |  |
| superpang | `ingame` | 46.3 | 113 | ✓ | flat-fill |  |
| Tangle | `ingame` | 61.2 | 1538 | – | no-audio |  |
| the reversed preacher II | `ingame` | 3.6 | 135 | ✓ | low-fps |  |
| TimeFrack2D for GP2X | `ingame` | 49.5 | 102 | – | no-audio |  |
| tower | `ingame` | 110.1 | 2783 | – | no-audio |  |
| ttxbeta170706b | `ingame` | 58.5 | 1553 | – | no-audio |  |
| TUcS.app(V0.7.0 - GP2X) | `ingame` | 22.6 | 570 | ✓ | low-fps |  |
| VekDemo2 | `ingame` | 0.5 | 13 | ✓ | low-fps |  |
| Vektar | `ingame` | 0.3 | 7 | ✓ | low-fps |  |
| vexedb1 | `ingame` | 61.6 | 1554 | – | no-audio |  |
| waffle2x | `ingame` | 45.7 | 96 | – | no-audio |  |
| wire3d | `ingame` | 58.8 | 1532 | – | no-audio |  |
| Wiztern Demo | `ingame` | 4.1 | 71 | ✓ | low-fps |  |
| 2xpong_gp2x | `playable` | 60.3 | 1518 | ✓ |  |  |
| 2xtron-v01 | `playable` | 61.8 | 1556 | ✓ |  |  |
| 9 Lives | `playable` | 43.6 | 1101 | ✓ |  |  |
| AbusimbelProfanationDeluxe | `playable` | 118.7 | 3002 | ✓ |  |  |
| AdamantArmorAffection2x | `playable` | 60.4 | 1529 | ✓ |  |  |
| AfterBurner-GP2X | `playable` | 28.2 | 715 | ✓ |  |  |
| Airplyr | `playable` | 61.4 | 1545 | ✓ |  |  |
| airstrike-1.1 | `playable` | 60.9 | 1544 | ✓ |  |  |
| Akd_BB | `playable` | 61.4 | 1548 | ✓ |  |  |
| alex | `playable` | 61.3 | 1544 | ✓ |  |  |
| Alex's Falldown | `playable` | 61.1 | 1544 | ✓ |  |  |
| alex4_gp2x | `playable` | 61.3 | 1543 | ✓ |  |  |
| amoebax-0.2.1-gp2x | `playable` | 56.1 | 1424 | ✓ |  |  |
| armorcity-0_30b | `playable` | 60.5 | 1547 | ✓ |  |  |
| Asteroids | `playable` | 60.2 | 1541 | ✓ |  |  |
| astrochaos | `playable` | 58.0 | 316 | ✓ |  |  |
| barrage | `playable` | 61.6 | 1552 | ✓ |  |  |
| Batiscafo (versin EXP) | `playable` | 60.9 | 1534 | ✓ |  |  |
| BearOids | `playable` | 61.1 | 1539 | ✓ |  |  |
| beat2x-0.5-bin | `playable` | 61.0 | 1546 | ✓ |  |  |
| Beatbox_1.2 | `playable` | 57.0 | 1493 | ✓ |  |  |
| biniax-gp2x_v1.2 | `playable` | 60.4 | 1518 | ✓ |  |  |
| Biniax2_gp2x | `playable` | 81.1 | 2049 | ✓ |  |  |
| Biohazard2 | `playable` | 61.7 | 1553 | ✓ |  |  |
| BioShoot GP2X | `playable` | 59.4 | 1527 | ✓ |  |  |
| blastriot1.21 | `playable` | 32.9 | 829 | ✓ |  |  |
| blazar_v1-30_gp2x | `playable` | 61.2 | 1543 | ✓ |  |  |
| blingo 1.2 | `playable` | 46.2 | 137 | ✓ |  |  |
| blipsgp2x | `playable` | 59.7 | 1555 | ✓ |  |  |
| Blix2x | `playable` | 61.6 | 1549 | ✓ |  |  |
| blobbyvolley | `playable` | 61.1 | 1546 | ✓ |  |  |
| blobwars_2x | `playable` | 62.3 | 1575 | ✓ |  |  |
| block | `playable` | 61.4 | 1551 | ✓ |  |  |
| Blockrage2x | `playable` | 60.6 | 1530 | ✓ |  |  |
| blox | `playable` | 37.2 | 935 | ✓ |  |  |
| Bloxz_DEMO | `playable` | 61.4 | 1554 | ✓ |  |  |
| bluecube2x | `playable` | 61.4 | 1546 | ✓ |  |  |
| bobtron-gp2x | `playable` | 61.2 | 1543 | ✓ |  |  |
| Boomshine2x_1.12_gp2x | `playable` | 60.2 | 1547 | ✓ |  |  |
| brassmunkey_gp2x_1.0 | `playable` | 60.1 | 1516 | ✓ |  |  |
| BubbleX | `playable` | 61.6 | 1547 | ✓ |  |  |
| BubTrain_GP2X-2006_Entry_No-Sound | `playable` | 58.0 | 1548 | ✓ |  |  |
| BugWarsSE_v1.0 | `playable` | 60.2 | 1545 | ✓ |  |  |
| bumprace-0.2 | `playable` | 60.0 | 1551 | ✓ |  |  |
| BurokkuDemo1 | `playable` | 61.5 | 1556 | ✓ |  |  |
| buttongame | `playable` | 48.8 | 105 | ✓ |  |  |
| BuzzysBadDay-1.0 | `playable` | 61.2 | 1549 | ✓ |  |  |
| CamelotWarriors-GP2x_v1.0 | `playable` | 60.7 | 1544 | ✓ |  |  |
| CascadeBeneath v1.0 for GP2X | `playable` | 62.3 | 1566 | ✓ |  |  |
| ccrg | `playable` | 55.5 | 245 | ✓ |  |  |
| cgenius-gp2x | `playable` | 58.3 | 1509 | ✓ |  |  |
| chaos2x | `playable` | 61.8 | 1553 | ✓ |  |  |
| checkersgp2x | `playable` | 59.6 | 1558 | ✓ |  |  |
| chess2x05 | `playable` | 59.9 | 1548 | ✓ |  |  |
| chuckiev12 | `playable` | 61.1 | 1540 | ✓ |  |  |
| ConnyCarrot | `playable` | 60.8 | 1543 | ✓ |  |  |
| CowSuckers-1.0 | `playable` | 61.3 | 1548 | ✓ |  |  |
| Crapong | `playable` | 60.1 | 1536 | ✓ |  |  |
| crazeeman | `playable` | 64.4 | 1631 | ✓ |  |  |
| crimsonV1 | `playable` | 59.7 | 1552 | ✓ |  |  |
| crossroads | `playable` | 61.4 | 1551 | ✓ |  |  |
| CUBES | `playable` | 61.5 | 1554 | ✓ |  |  |
| cyberhockeyV2_6 | `playable` | 61.2 | 1543 | ✓ |  |  |
| DABAKKA-0 | `playable` | 61.3 | 1538 | ✓ |  |  |
| Dance2x Alpha GPE | `playable` | 60.6 | 1549 | ✓ |  |  |
| Dastardly dungeon 1.5 | `playable` | 43.3 | 107 | ✓ |  |  |
| dd2x | `playable` | 109.0 | 494 | ✓ |  |  |
| DealOrNoDeal-v12 | `playable` | 60.9 | 1557 | ✓ |  |  |
| debian_vs_pimientos_2x_0.1.2 | `playable` | 59.0 | 555 | ✓ |  |  |
| defeatme-gp2x-1.0.1 | `playable` | 60.8 | 1532 | ✓ |  |  |
| diamant_1_01 | `playable` | 28.5 | 720 | ✓ |  |  |
| DontGetCrushed v1.0 for GP2X | `playable` | 62.3 | 1562 | ✓ |  |  |
| dosmugen | `playable` | 60.1 | 1554 | ✓ |  |  |
| Drill2x_final | `playable` | 59.4 | 1516 | ✓ |  |  |
| drill2x_xtreme_v1.0.3 | `playable` | 36.7 | 1484 | ✓ |  |  |
| DubaiRace038a | `playable` | 49.3 | 112 | ✓ |  |  |
| egoboo2xFeb1207 | `playable` | 68.4 | 2531 | ✓ |  |  |
| Electronia | `playable` | 61.5 | 1549 | ✓ |  |  |
| enigma | `playable` | 54.5 | 1458 | ✓ |  |  |
| entombed2x | `playable` | 60.6 | 1548 | ✓ |  |  |
| EpicFreeFall_GP2X | `playable` | 60.4 | 1542 | ✓ |  |  |
| EpicRocks_GP2X | `playable` | 58.4 | 644 | ✓ |  |  |
| escapa-v1 | `playable` | 61.2 | 1548 | ✓ |  |  |
| exi_shoot_gp2x | `playable` | 60.8 | 1532 | ✓ |  |  |
| extraterrestres | `playable` | 101.3 | 2688 | ✓ |  |  |
| exult_rc3 | `playable` | 40.8 | 1512 | ✓ |  |  |
| Factor-v1.0-final | `playable` | 60.1 | 1535 | ✓ |  |  |
| falldown_gp2x | `playable` | 89.7 | 2250 | ✓ |  |  |
| Fishball-1.2 | `playable` | 60.0 | 1515 | ✓ |  |  |
| fissionfield2x | `playable` | 61.6 | 1554 | ✓ |  |  |
| Flaschenspiel | `playable` | 60.4 | 1536 | ✓ |  |  |
| FleshChasmer Zero | `playable` | 60.3 | 1524 | ✓ |  |  |
| floaters | `playable` | 61.3 | 1550 | ✓ |  |  |
| flobopuyo0.20.1 | `playable` | 60.1 | 1525 | ✓ |  |  |
| flurkies_v1-25_gp2x | `playable` | 61.6 | 1550 | ✓ |  |  |
| fm | `playable` | 103.5 | 2595 | ✓ |  |  |
| formula1gp2x | `playable` | 61.0 | 1554 | ✓ |  |  |
| Fragger2x | `playable` | 61.5 | 1549 | ✓ |  |  |
| freec2x | `playable` | 26.5 | 671 | ✓ |  |  |
| friq-beta-07 | `playable` | 61.2 | 1560 | ✓ |  |  |
| frozen2x-0.1 | `playable` | 78.3 | 738 | ✓ |  |  |
| fruits_gp2x | `playable` | 61.4 | 1549 | ✓ |  |  |
| fvc | `playable` | 61.4 | 1542 | ✓ |  |  |
| FyWod_2x | `playable` | 60.5 | 1546 | ✓ |  |  |
| game-watch-mario-bros | `playable` | 61.2 | 1544 | ✓ |  |  |
| Geek 'em up GP2X | `playable` | 44.5 | 1259 | ✓ |  |  |
| gemdrop2x_v02 | `playable` | 60.7 | 1558 | ✓ |  |  |
| Ghostbusters_WIP | `playable` | 61.5 | 576 | ✓ |  |  |
| ghostpix_v10_gp2x | `playable` | 60.7 | 1544 | ✓ |  |  |
| glouton | `playable` | 61.3 | 1540 | ✓ |  |  |
| gnp_104 | `playable` | 57.7 | 1564 | ✓ |  |  |
| GoitGP | `playable` | 60.2 | 1546 | ✓ |  |  |
| gp2hanoi_0.8.1_gp2x | `playable` | 61.6 | 1552 | ✓ |  |  |
| gp2x-formido-0.1 | `playable` | 44.0 | 1554 | ✓ |  |  |
| gp2x-invaders-preview-version | `playable` | 61.5 | 1551 | ✓ |  |  |
| gp2x-shienso-bin_061021 | `playable` | 61.3 | 1552 | ✓ |  |  |
| gp2x-smc-0.1 | `playable` | 57.8 | 1547 | ✓ |  |  |
| gp2x_2xmas | `playable` | 44.6 | 91 | ✓ |  |  |
| GP2X_BallGame_0.49 | `playable` | 60.9 | 1530 | ✓ |  |  |
| gp2x_drench | `playable` | 57.7 | 1459 | ✓ |  |  |
| GP2X_Nat2007 | `playable` | 47.5 | 1198 | ✓ |  |  |
| GP2X_TLI | `playable` | 27.8 | 700 | ✓ |  |  |
| gp2xjunkie | `playable` | 58.5 | 1501 | ✓ |  |  |
| gp2xpang-v.1.1.1 | `playable` | 100.7 | 2587 | ✓ |  |  |
| gp2xrick 1.0 | `playable` | 60.3 | 1528 | ✓ |  |  |
| GpFrontier v0.1 | `playable` | 62.0 | 1571 | ✓ |  |  |
| gpfrontier v0.4 | `playable` | 59.6 | 1561 | ✓ |  |  |
| gr-v1001-gp2x | `playable` | 60.2 | 1551 | ✓ |  |  |
| green | `playable` | 60.4 | 1533 | ✓ |  |  |
| hanagechu2x_gbax2007 | `playable` | 66.0 | 1662 | ✓ |  |  |
| hanagechu2xalpha | `playable` | 61.8 | 1556 | ✓ |  |  |
| Heretic2X_v0.5 | `playable` | 60.6 | 1522 | ✓ |  |  |
| hexbattle2x | `playable` | 61.3 | 1555 | ✓ |  |  |
| HumphreyGP2X | `playable` | 60.7 | 1544 | ✓ |  |  |
| Hyperion_GP2X_demo | `playable` | 61.3 | 1551 | ✓ |  |  |
| jump_n_blob_gp2x | `playable` | 60.6 | 1605 | ✓ |  |  |
| jumpnbumpgp2x | `playable` | 60.7 | 1555 | ✓ |  |  |
| Jurlx2 | `playable` | 61.2 | 1546 | ✓ |  |  |
| kampfimall-gp2x-music | `playable` | 60.1 | 522 | ✓ |  |  |
| ketm_2x_gp2x | `playable` | 53.1 | 1551 | ✓ |  |  |
| KicknPLay_1.1 | `playable` | 61.5 | 1544 | ✓ |  |  |
| koules2x_02 | `playable` | 61.0 | 1554 | ✓ |  |  |
| kuklomenos_gp2x_201209 | `playable` | 60.7 | 1552 | ✓ |  |  |
| kurukuru2x | `playable` | 61.1 | 1548 | ✓ |  |  |
| la | `playable` | 49.3 | 108 | ✓ |  |  |
| ladykiller | `playable` | 61.0 | 1552 | ✓ |  |  |
| las-tres-luces-de-glaurung-remake | `playable` | 60.5 | 1548 | ✓ |  |  |
| lbreakoutgp2x | `playable` | 58.6 | 1556 | ✓ |  |  |
| levelshmup | `playable` | 86.1 | 2197 | ✓ |  |  |
| LinesXv3 | `playable` | 61.4 | 1540 | ✓ |  |  |
| logicx | `playable` | 61.6 | 1550 | ✓ |  |  |
| mad-mix-game-20b-final | `playable` | 61.0 | 1551 | ✓ |  |  |
| madbomber | `playable` | 60.9 | 1553 | ✓ |  |  |
| malvado2x | `playable` | 48.3 | 156 | ✓ |  |  |
| MAME-N22_51 | `playable` | 86.9 | 2293 | ✓ |  |  |
| March of the mini tux | `playable` | 54.3 | 1375 | ✓ |  |  |
| Marte Necesita Vacas GP2X | `playable` | 43.2 | 1110 | ✓ |  |  |
| Masteries_Journey_to_the_Center_of_the_earth_GP2X | `playable` | 59.4 | 1546 | ✓ |  |  |
| MazeThingie | `playable` | 62.0 | 1563 | ✓ |  |  |
| memory | `playable` | 62.3 | 1585 | ✓ |  |  |
| MemoryGP2X-v11 | `playable` | 61.5 | 1553 | ✓ |  |  |
| meritous | `playable` | 60.8 | 1551 | ✓ |  |  |
| Merlin2x_beta_021 | `playable` | 58.4 | 546 | ✓ |  |  |
| metaphysik | `playable` | 64.0 | 1619 | ✓ |  |  |
| methaneV1 | `playable` | 61.5 | 1552 | ✓ |  |  |
| minos-gp2x | `playable` | 60.3 | 1529 | ✓ |  |  |
| mk13.gpe | `playable` | 61.5 | 1541 | ✓ |  |  |
| mkACE.gpe | `playable` | 61.7 | 1548 | ✓ |  |  |
| mkONE.gpe | `playable` | 61.5 | 1544 | ✓ |  |  |
| MM2X | `playable` | 60.2 | 1515 | ✓ |  |  |
| monacoGP | `playable` | 61.2 | 1562 | ✓ |  |  |
| monochromeworlds-gp2x-1.0.0 | `playable` | 60.8 | 1534 | ✓ |  |  |
| mush_gp2x | `playable` | 46.0 | 1182 | ✓ |  |  |
| Mutant Tank Knights | `playable` | 55.7 | 230 | ✓ |  |  |
| MyriadUpdated | `playable` | 59.4 | 1543 | ✓ |  |  |
| mzx-2.84c | `playable` | 60.8 | 847 | ✓ |  |  |
| mzx282-gp2x | `playable` | 61.1 | 824 | ✓ |  |  |
| n-tris_v1.0 | `playable` | 73.7 | 1850 | ✓ |  |  |
| nanobounce-pacc-gp2x | `playable` | 52.4 | 398 | ✓ |  |  |
| ne_deluxe_gp2x | `playable` | 61.5 | 1551 | ✓ |  |  |
| ne_gp2x | `playable` | 60.0 | 1514 | ✓ |  |  |
| NecNec2x | `playable` | 60.7 | 1537 | ✓ |  |  |
| newsuperpang | `playable` | 61.1 | 1552 | ✓ |  |  |
| Nifty | `playable` | 60.2 | 1534 | ✓ |  |  |
| noiz2saV3 | `playable` | 49.1 | 1248 | ✓ |  |  |
| odonata_demo | `playable` | 60.4 | 1525 | ✓ |  |  |
| OpenBOR_v2.1933 | `playable` | 60.3 | 994 | ✓ |  |  |
| OpenBOR_v3.0_Build_2615_&_2637 | `playable` | 61.0 | 1563 | ✓ |  |  |
| openglad2x | `playable` | 59.9 | 1552 | ✓ |  |  |
| opentyrian2x_0.3_complete | `playable` | 56.6 | 1540 | ✓ |  |  |
| OrbitalSniper2x_v1.1 | `playable` | 57.1 | 142 | ✓ |  |  |
| othello_v1.0 | `playable` | 62.0 | 1554 | ✓ |  |  |
| PAF | `playable` | 61.1 | 1541 | ✓ |  |  |
| paraballgp2x | `playable` | 61.2 | 1551 | ✓ |  |  |
| PaybackDemo | `playable` | 27.1 | 704 | ✓ |  |  |
| pdcv060b | `playable` | 55.3 | 458 | ✓ |  |  |
| Pentominos | `playable` | 61.7 | 1552 | ✓ |  |  |
| Peuppy_10_GP2X | `playable` | 27.5 | 693 | ✓ |  |  |
| Phishy-0 | `playable` | 60.5 | 1548 | ✓ |  |  |
| physique | `playable` | 61.4 | 1552 | ✓ |  |  |
| Pika2x | `playable` | 59.9 | 568 | ✓ |  |  |
| pintor2x | `playable` | 88.0 | 2210 | ✓ |  |  |
| pixpang | `playable` | 55.6 | 1552 | ✓ |  |  |
| Poker2x | `playable` | 110.6 | 2802 | ✓ |  |  |
| Pool Panic | `playable` | 60.8 | 1530 | ✓ |  |  |
| powermanga-0.80 | `playable` | 57.4 | 1503 | ✓ |  |  |
| PPlane | `playable` | 55.6 | 1406 | ✓ |  |  |
| PPlane2.GP2X | `playable` | 42.2 | 1129 | ✓ |  |  |
| proj0-demo_01 | `playable` | 60.5 | 1541 | ✓ |  |  |
| PulplifeWars | `playable` | 59.0 | 1535 | ✓ |  |  |
| puzzlelandgp2x | `playable` | 57.3 | 1556 | ✓ |  |  |
| qfg3-0 | `playable` | 60.0 | 1555 | ✓ |  |  |
| quartz2_v1-50_gp2x | `playable` | 61.8 | 1555 | ✓ |  |  |
| Rabbit_vs_Flies_0.9 | `playable` | 61.0 | 1544 | ✓ |  |  |
| ramon atacks | `playable` | 61.7 | 1558 | ✓ |  |  |
| Release GP2X MST_RUNNERS | `playable` | 59.0 | 1526 | ✓ |  |  |
| retrovirus_1_1 | `playable` | 60.1 | 1522 | ✓ |  |  |
| reword_v0.5 | `playable` | 61.4 | 1561 | ✓ |  |  |
| rg_105 | `playable` | 58.9 | 1569 | ✓ |  |  |
| rg_ura_103 | `playable` | 58.8 | 1564 | ✓ |  |  |
| river | `playable` | 62.0 | 1555 | ✓ |  |  |
| RockRain | `playable` | 60.5 | 1524 | ✓ |  |  |
| rockrain2_exp-20100925 | `playable` | 61.2 | 1549 | ✓ |  |  |
| rookiehero_EXP.gp2x.v20120220 | `playable` | 60.8 | 1553 | ✓ |  |  |
| RoundEmUp-alpha3 | `playable` | 61.4 | 1553 | ✓ |  |  |
| rubidogp2x | `playable` | 60.0 | 1555 | ✓ |  |  |
| Runner_GP2X | `playable` | 60.4 | 1544 | ✓ |  |  |
| s-tris2_v1-64_gp2x | `playable` | 61.8 | 1554 | ✓ |  |  |
| sachunsungx | `playable` | 61.4 | 1543 | ✓ |  |  |
| santaMania | `playable` | 56.7 | 1445 | ✓ |  |  |
| ScorchedGPBeta2 | `playable` | 61.0 | 1544 | ✓ |  |  |
| scummvm-0.11.1-gp2x | `playable` | 59.7 | 1548 | ✓ |  |  |
| scummvm-1.2.0-gp2x | `playable` | 59.8 | 1557 | ✓ |  |  |
| SdLame | `playable` | 61.0 | 1553 | ✓ |  |  |
| sdlscav_gp2x_0.2.0 | `playable` | 116.6 | 2942 | ✓ |  |  |
| ShanghaiX | `playable` | 61.4 | 1541 | ✓ |  |  |
| SheepDash | `playable` | 60.1 | 1559 | ✓ |  |  |
| Shippy84 | `playable` | 60.6 | 1550 | ✓ |  |  |
| siv050 | `playable` | 59.2 | 1562 | ✓ |  |  |
| SmallBall_GP | `playable` | 58.8 | 1502 | ✓ |  |  |
| snail runers | `playable` | 60.8 | 1553 | ✓ |  |  |
| snowedin5_v1-00_gp2x | `playable` | 61.7 | 1552 | ✓ |  |  |
| SOD v1.1 | `playable` | 60.0 | 1546 | ✓ |  |  |
| sokobangp2x | `playable` | 55.5 | 1561 | ✓ |  |  |
| sources_Yahtzee | `playable` | 52.1 | 223 | ✓ |  |  |
| space_varments_v1.0 | `playable` | 57.7 | 560 | ✓ |  |  |
| spacestorm | `playable` | 53.7 | 1350 | ✓ |  |  |
| Sqdef 1.4 | `playable` | 60.7 | 1549 | ✓ |  |  |
| Squares-v051 | `playable` | 61.6 | 1560 | ✓ |  |  |
| Squaresliding | `playable` | 61.8 | 1552 | ✓ |  |  |
| StairwayToHeaven | `playable` | 54.9 | 116 | ✓ |  |  |
| starfighter-gp2x-0.01 | `playable` | 65.5 | 1133 | ✓ |  |  |
| StarTrucker | `playable` | 61.0 | 1544 | ✓ |  |  |
| stppc2x-v1.0 | `playable` | 42.2 | 1562 | ✓ |  |  |
| stransball2 | `playable` | 60.5 | 1530 | ✓ |  |  |
| street2x | `playable` | 57.2 | 1558 | ✓ |  |  |
| subhunt | `playable` | 61.5 | 1554 | ✓ |  |  |
| SuperChickenFallDemo | `playable` | 61.3 | 1548 | ✓ |  |  |
| SuperPaf_v1.0 | `playable` | 60.4 | 1542 | ✓ |  |  |
| SuperPixelJumper v1.1 for GP2X | `playable` | 60.8 | 1531 | ✓ |  |  |
| SuperSonicSpeed | `playable` | 61.2 | 1544 | ✓ |  |  |
| supertux-0.1.3-gp2x-v4 | `playable` | 55.5 | 1508 | ✓ |  |  |
| survival | `playable` | 61.1 | 1554 | ✓ |  |  |
| symbolica-0.8 | `playable` | 60.8 | 1537 | ✓ |  |  |
| tail-tale | `playable` | 61.6 | 1553 | ✓ |  |  |
| tecnoballz-0.91-gp2x | `playable` | 57.9 | 1495 | ✓ |  |  |
| tetwins | `playable` | 52.1 | 109 | ✓ |  |  |
| ThreeTs_Game | `playable` | 55.4 | 118 | ✓ |  |  |
| Thruster_GP2X | `playable` | 61.5 | 1553 | ✓ |  |  |
| tikka_dungeons_demo_1 | `playable` | 61.1 | 1543 | ✓ |  |  |
| tilematch-0.6 | `playable` | 94.9 | 2396 | ✓ |  |  |
| tileworld2x | `playable` | 58.6 | 1543 | ✓ |  |  |
| towertopplergp2x | `playable` | 59.4 | 1554 | ✓ |  |  |
| Trap75 | `playable` | 61.7 | 1552 | ✓ |  |  |
| treev060 | `playable` | 60.4 | 1562 | ✓ |  |  |
| Unicolor | `playable` | 61.0 | 1549 | ✓ |  |  |
| vectoroids-2x | `playable` | 61.5 | 1553 | ✓ |  |  |
| vektar-free | `playable` | 69.1 | 1748 | ✓ |  |  |
| vektarpack_v1 | `playable` | 83.7 | 2123 | ✓ |  |  |
| Ventifact | `playable` | 60.6 | 1555 | ✓ |  |  |
| vexed-gp2x-10 | `playable` | 61.0 | 1538 | ✓ |  |  |
| vorton-b4 | `playable` | 59.8 | 1552 | ✓ |  |  |
| vwars | `playable` | 60.4 | 1552 | ✓ |  |  |
| war_and_warriorgp2x | `playable` | 61.8 | 1554 | ✓ |  |  |
| warcraft | `playable` | 60.6 | 1557 | ✓ |  |  |
| warehouse_panic_v1.1_gp2x | `playable` | 42.4 | 586 | ✓ |  |  |
| waternetgp2x | `playable` | 57.8 | 1565 | ✓ |  |  |
| wehaveballs | `playable` | 61.2 | 1539 | ✓ |  |  |
| whacky | `playable` | 61.2 | 1550 | ✓ |  |  |
| WindAndWater_teaser_110 | `playable` | 60.8 | 1529 | ✓ |  |  |
| wnw | `playable` | 60.9 | 1539 | ✓ |  |  |
| xenitris_demo | `playable` | 62.3 | 1569 | ✓ |  |  |
| xigon-X-gp2x-V1 | `playable` | 61.8 | 1557 | ✓ |  |  |
| Xpired2x 1.0 beta | `playable` | 60.6 | 1534 | ✓ |  |  |
| xRick | `playable` | 61.2 | 1556 | ✓ |  |  |
| yahtzee-v21 | `playable` | 61.7 | 1558 | ✓ |  |  |
| znax | `playable` | 59.3 | 1559 | ✓ |  |  |
| Zoids Quest2X-0.0.1-2 | `playable` | 60.6 | 1553 | ✓ |  |  |
| zoltan 2x | `playable` | 60.4 | 1552 | ✓ |  |  |
| zooov11 | `playable` | 28.6 | 720 | ✓ |  |  |
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
| chroma 1.01 v1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
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
| nethack-wiz | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/null |
| noiz2sa_wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| openggs | `incompatible` | 0.0 | 0 | – | no-frames |  |
| openjazz-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| opentyrian_wiz_source | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Wiz/opentyrian_wiz_source' |
| Out Zone | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| pgw | `incompatible` | 10.0 | 13 | ✓ | no-frames |  |
| prboom-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Propis | `incompatible` | 0.0 | 0 | – | no-frames |  |
| quake1-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| quake_0.03 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| RailroadRampage_1.2_Wiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/RailroadRampage_1.2_Wiz/RailroadRampage_Wiz/Railroad |
| roadfighter | `incompatible` | 0.0 | 0 | – | no-frames |  |
| rott | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/null |
| Shock Troopers Base Defense | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Skull (Windows, Linux & Gp2x Wiz) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/Skull (Windows, Linux & Gp2x Wiz)/Skull Game/Skull/S |
| SmallBall_Wiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/SmallBall_Wiz/SmallBall/SmallBall.gpe' is not an ARM |
| smw_1.7 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Snow Bros 2 | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| srb2 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/null |
| SudoQ | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Wiz/SudoQ/SudoQ/sudoq/sudoq.gpe' is not an ARM ELF and n |
| supertux-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Twin Cobra | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| Wiz_Propis_Demo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| wolf4sdl_wiz_svn | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Zero Wing | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| albion-v1.0.1-wiz | `black` | 32.5 | 43 | ✓ | black-screen |  |
| Balloonacy_wiz_wip | `black` | 0.2 | 4 | ✓ | black-screen |  |
| blingo | `black` | 27.7 | 64 | ✓ | black-screen |  |
| BubbleTrainWiz_5-20-09 | `black` | 45.6 | 1202 | ✓ | black-screen |  |
| CartoonWiz | `black` | 0.2 | 4 | ✓ | black-screen |  |
| CDogs-wiz | `black` | 3.6 | 8 | – | black-screen |  |
| ColonyConflict_V1.1_B6 | `black` | 0.1 | 4 | ✓ | black-screen |  |
| Dastardly_Dungeon | `black` | 18.3 | 23 | ✓ | black-screen |  |
| DungeonRunner | `black` | 0.1 | 4 | ✓ | black-screen |  |
| DuoWIZ_Pong | `black` | 0.1 | 4 | ✓ | black-screen |  |
| eduke32 | `black` | 6.1 | 7 | – | black-screen |  |
| freecell2x | `black` | 0.1 | 3 | ✓ | black-screen |  |
| March of the mini tux(wiz version) | `black` | 0.1 | 3 | ✓ | black-screen |  |
| nazcadreams | `black` | 31.2 | 55 | ✓ | black-screen |  |
| nazcarunners | `black` | 35.1 | 69 | ✓ | black-screen |  |
| Nazcasphere | `black` | 6.2 | 73 | ✓ | black-screen |  |
| opentyrian | `black` | 11.4 | 8 | – | black-screen |  |
| paraballwiz | `black` | 5.1 | 5 | – | black-screen |  |
| PEZ | `black` | 11.5 | 10 | – | black-screen |  |
| PPlane2.WIZ | `black` | 0.1 | 4 | ✓ | black-screen |  |
| protozoa | `black` | 23.9 | 38 | ✓ | black-screen |  |
| Ruckman-Wiz | `black` | 23.1 | 56 | ✓ | black-screen |  |
| SimOniZ | `black` | 0.2 | 4 | ✓ | black-screen |  |
| tetwizdownload | `black` | 0.1 | 3 | ✓ | black-screen |  |
| The Minigame Project | `black` | 56.6 | 1533 | ✓ | black-screen |  |
| tilt | `black` | 23.4 | 28 | ✓ | black-screen |  |
| Trap75 | `black` | 61.1 | 1538 | ✓ | black-screen |  |
| tricorder | `black` | 34.1 | 68 | ✓ | black-screen |  |
| TUcS.app(V0.7.0 - Wiz) | `black` | 0.1 | 3 | ✓ | black-screen |  |
| uqm2x_release.1.1 | `black` | 79.4 | 356 | – | black-screen |  |
| warcraft-beta3-wiz | `black` | 42.3 | 45 | ✓ | black-screen |  |
| wiz-car-binary_090818a | `black` | 60.6 | 1526 | ✓ | black-screen |  |
| Wiz_Blox | `black` | 0.1 | 3 | ✓ | black-screen |  |
| wiz_drench | `black` | 0.2 | 4 | ✓ | black-screen |  |
| WIZ_S4S | `black` | 0.1 | 3 | ✓ | black-screen |  |
| WizSticks | `black` | 0.1 | 4 | ✓ | mmio-spin | 0x1988 |
| xcom1-v1.0.2-wiz | `black` | 52.0 | 80 | ✓ | black-screen |  |
| xcom2-v1.0.1-wiz | `black` | 119.9 | 3075 | ✓ | black-screen |  |
| Ghostpix | `ingame` | 18.1 | 642 | ✓ | low-fps |  |
| gobble | `ingame` | 46.4 | 139 | – | no-audio |  |
| Myriad | `ingame` | 22.4 | 644 | ✓ | low-fps |  |
| Powder2X_wiz_114_v01 | `ingame` | 59.2 | 1507 | – | no-audio |  |
| spout | `ingame` | 61.1 | 1538 | – | no-audio |  |
| Sudoku2X | `ingame` | 60.9 | 1536 | – | no-audio |  |
| wizchess-v1.1.0-bin | `ingame` | 60.8 | 1537 | – | no-audio |  |
| wizchess-v1.2.0-bin | `ingame` | 60.9 | 1540 | – | no-audio |  |
| wizgo-v1.1.0-bin | `ingame` | 60.8 | 1540 | – | no-audio |  |
| WizGolf | `ingame` | 60.9 | 1541 | – | no-audio |  |
| wizmancala-v1.1.2-bin | `ingame` | 61.0 | 1540 | – | no-audio |  |
| wizpong | `ingame` | 60.2 | 1527 | – | no-audio |  |
| Worship Vector | `ingame` | 61.0 | 1535 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 160px; left and right halves are near- |
| [DEMO] Wiztern | `playable` | 53.9 | 574 | ✓ |  |  |
| AdamantArmorAffectionWiz | `playable` | 60.8 | 1538 | ✓ |  |  |
| airstrike-wiz-1.01 | `playable` | 60.9 | 1548 | ✓ |  |  |
| alexsfalldown | `playable` | 61.7 | 1549 | ✓ |  |  |
| Animatch Wiz | `playable` | 58.3 | 1533 | ✓ |  |  |
| Asteroids | `playable` | 60.3 | 1529 | ✓ |  |  |
| battlejewels-wiz-public001demo | `playable` | 60.6 | 1533 | ✓ |  |  |
| beat2x-wiz | `playable` | 60.2 | 1522 | ✓ |  |  |
| Blix2x | `playable` | 61.6 | 1547 | ✓ |  |  |
| Boomshine2x_1.12_wiz | `playable` | 59.8 | 1530 | ✓ |  |  |
| BugwarsSE | `playable` | 60.3 | 1528 | ✓ |  |  |
| Dd2x | `playable` | 58.1 | 263 | ✓ |  |  |
| deicide3_eng | `playable` | 58.3 | 407 | ✓ |  |  |
| gr-v1001-wiz | `playable` | 59.2 | 1535 | ✓ |  |  |
| herknights | `playable` | 58.8 | 1527 | ✓ |  |  |
| hexen2 | `playable` | 58.6 | 1507 | ✓ |  |  |
| minos-gp2x-wiz | `playable` | 60.1 | 1520 | ✓ |  |  |
| mush_gp2x | `playable` | 46.4 | 1190 | ✓ |  |  |
| mush_gp2x-0 | `playable` | 38.7 | 1030 | ✓ |  |  |
| NewSuperPang05 | `playable` | 43.8 | 1530 | ✓ |  |  |
| paf | `playable` | 60.2 | 1526 | ✓ |  |  |
| Pentominos | `playable` | 61.8 | 1551 | ✓ |  |  |
| Pharaoh | `playable` | 51.6 | 109 | ✓ |  |  |
| PhishyWiz | `playable` | 59.7 | 1533 | ✓ |  |  |
| PPlane | `playable` | 54.8 | 1390 | ✓ |  |  |
| preggo_Wiz | `playable` | 51.8 | 352 | ✓ |  |  |
| PuzzleDevilWizDemo | `playable` | 56.0 | 1415 | ✓ |  |  |
| Rezerwar | `playable` | 53.7 | 507 | ✓ |  |  |
| rockrain-gp2x-wiz | `playable` | 61.4 | 1546 | ✓ |  |  |
| Sachunsung2 | `playable` | 49.7 | 103 | ✓ |  |  |
| scummvm-1.2.0-gp2xwiz | `playable` | 57.1 | 1497 | ✓ |  |  |
| Shanghai2 | `playable` | 50.5 | 103 | ✓ |  |  |
| sleuthslots | `playable` | 58.5 | 1528 | ✓ |  |  |
| SOD_Wiz | `playable` | 59.2 | 1530 | ✓ |  |  |
| Sopwith | `playable` | 61.1 | 1538 | ✓ |  |  |
| Space Varments | `playable` | 54.6 | 569 | ✓ |  |  |
| Sqdef_Wiz_14A | `playable` | 60.2 | 1534 | ✓ |  |  |
| Tail Tale | `playable` | 61.1 | 1537 | ✓ |  |  |
| WizFrontier v0.1 | `playable` | 58.1 | 1528 | ✓ |  |  |
| wizznic-0.9.9-wiz | `playable` | 59.0 | 1532 | ✓ |  |  |
| wnw_demo | `playable` | 61.1 | 1540 | ✓ |  |  |
| WWII | `playable` | 58.0 | 1530 | ✓ |  |  |
| xpiredwiz.eng.101 | `playable` | 59.2 | 1504 | ✓ |  |  |
| xRick | `playable` | 61.0 | 1550 | ✓ |  |  |
| znumbers | `playable` | 51.3 | 109 | ✓ |  |  |
| Zoltan | `playable` | 59.3 | 1531 | ✓ |  |  |

### Caanoo (205 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| 20110831 - Bomber Run Redux | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/20110831 - Bomber Run Redux/game/bomber_run_bennu |
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
| PUZZLEBOARDS | `incompatible` | 0.0 | 1 | ✓ | mmio-spin | 0x90a |
| quake1-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| QUAKE1.INI AND ICON SET | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/QUAKE1.INI AND ICON SET' |
| quake1_addons | `incompatible` | 0.0 | 0 | – | no-executable | magiceyes: no .gpe found under '/mnt/s/GP2X Caanoo/quake1_addons' |
| quake1_build-20111024 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/accel |
| quake2-caanoo | `incompatible` | 0.0 | 0 | ✓ | missing-game-data |  |
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
| supertux | `incompatible` | 0.0 | 0 | – | no-frames |  |
| the solitarie | `incompatible` | 0.0 | 0 | – | no-frames |  |
| tmw_v1.0.0-beta-2_caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Txishos (Caanoo) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Txishos (Caanoo)/Caanoo/Txishos/Txishos.gpe' is n |
| warcraft-beta3-caanoo | `incompatible` | 0.0 | 0 | ✓ | unimplemented-syscall | 220 (madvise) |
| zlocada-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zombiesorbet_v1.0_caanoo | `incompatible` | 24.2 | 622 | ✓ | no-frames |  |
| zomg-Caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/zomg-Caanoo/Zomg/zomg.gpe' is not an ARM ELF and  |
| zsxd | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Zverealm-Caanoo | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X Caanoo/Zverealm-Caanoo/Zverealm/Zverealm.gpe' is not an  |
| Abbaye_caanoo | `black` | 1.3 | 2 | ✓ | black-screen |  |
| aquaVenture | `black` | 12.6 | 33 | – | black-screen |  |
| arcadevol1 | `black` | 6.3 | 7 | ✓ | black-screen |  |
| B'lox! | `black` | 35.2 | 346 | ✓ | black-screen |  |
| Balloonacy | `black` | 12.5 | 33 | – | black-screen |  |
| Blingo | `black` | 37.0 | 105 | ✓ | black-screen |  |
| Blitz | `black` | 12.9 | 34 | – | black-screen |  |
| BubbleTrain | `black` | 1.1 | 2 | ✓ | black-screen |  |
| cat_trap | `black` | 13.0 | 34 | – | black-screen |  |
| Drench | `black` | 14.1 | 36 | ✓ | black-screen |  |
| Flappynerd_Caanoo | `black` | 12.2 | 34 | ✓ | black-screen |  |
| Geek_em_up_CAANOO | `black` | 26.5 | 274 | ✓ | black-screen |  |
| Guru Logic | `black` | 13.8 | 35 | ✓ | black-screen |  |
| JUMPNRUN | `black` | 56.5 | 1547 | ✓ | black-screen |  |
| kenlab-caanoo | `black` | 46.9 | 437 | – | black-screen |  |
| laserchess_c | `black` | 15.1 | 31 | – | black-screen |  |
| MNV_Caanoo_Release1 | `black` | 44.5 | 438 | ✓ | black-screen |  |
| powermanga-0.80 | `black` | 53.4 | 1400 | ✓ | black-screen |  |
| SantaMania | `black` | 12.6 | 33 | – | black-screen |  |
| SimOniZ | `black` | 11.3 | 31 | – | black-screen |  |
| STRATEGY | `black` | 10.6 | 17 | ✓ | black-screen |  |
| Trap75 | `black` | 47.7 | 1199 | ✓ | black-screen |  |
| xcom1-v1.0.2-caanoo | `black` | 20.4 | 21 | ✓ | black-screen |  |
| xcom2-v1.0.1-caanoo | `black` | 61.7 | 1575 | ✓ | black-screen |  |
| aimcaanoo | `ingame` | 49.5 | 1279 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 160px; left and right halves are near- |
| Arcadevol3 | `ingame` | 59.6 | 1548 | – | flat-fill |  |
| balls12_caanoo_bin | `ingame` | 34.4 | 71 | – | no-audio |  |
| Blackjack21v1.1 | `ingame` | 60.0 | 1513 | – | no-audio |  |
| caanoo-biniax2-v1.30-bin | `ingame` | 17.4 | 438 | ✓ | low-fps |  |
| caanoo-chess-v1.1.0-bin | `ingame` | 45.6 | 1150 | – | no-audio |  |
| caanoo-go-v1.1.0-bin | `ingame` | 44.9 | 1140 | – | no-audio |  |
| caanoo-mancala-v1.1.0-bin | `ingame` | 43.4 | 1095 | – | no-audio |  |
| chexquest-caanoo | `ingame` | 18.0 | 461 | ✓ | low-fps |  |
| cllwrth | `ingame` | 24.9 | 628 | ✓ | low-fps |  |
| deminor | `ingame` | 11.7 | 27 | – | low-fps |  |
| gnp_104 | `ingame` | 51.6 | 1379 | ✓ | flat-fill |  |
| gnuRobbo | `ingame` | 16.6 | 223 | ✓ | low-fps |  |
| gr-v1001-caanoo | `ingame` | 9.9 | 260 | ✓ | low-fps |  |
| hexahop_1.0 | `ingame` | 53.2 | 1339 | – | no-audio |  |
| jump_n_blob_caanoo | `ingame` | 5.1 | 135 | ✓ | low-fps |  |
| ketm | `ingame` | 38.6 | 1315 | – | no-audio |  |
| knight | `ingame` | 10.5 | 263 | ✓ | flat-fill |  |
| Liar | `ingame` | 12.7 | 88 | ✓ | low-fps |  |
| llcpcls-caanoo | `ingame` | 16.4 | 35 | ✓ | low-fps |  |
| meritous | `ingame` | 57.5 | 1466 | ✓ | flat-fill |  |
| MISC | `ingame` | 59.6 | 1570 | – | no-audio |  |
| mtknights | `ingame` | 31.4 | 790 | ✓ | flat-fill |  |
| nlove_0.6.2_(beta)_caanoo | `ingame` | 19.0 | 47 | – | low-fps |  |
| noiz2sa_caanoo | `ingame` | 17.0 | 128 | ✓ | flat-fill |  |
| nuclearchess | `ingame` | 221.1 | 5585 | – | no-audio |  |
| Pharaoh | `ingame` | 12.6 | 26 | ✓ | low-fps |  |
| powder | `ingame` | 53.9 | 1373 | – | no-audio |  |
| prboom-caanoo | `ingame` | 60.3 | 1533 | – | no-audio |  |
| pushover-v0.2-bin | `ingame` | 18.4 | 468 | ✓ | low-fps |  |
| rg_ura_103 | `ingame` | 54.7 | 1439 | ✓ | flat-fill |  |
| Sachunsung2 | `ingame` | 24.5 | 50 | ✓ | low-fps |  |
| smw_1.7 | `ingame` | 22.2 | 382 | ✓ | low-fps |  |
| tlosaf_v12-caanoo | `ingame` | 60.6 | 1528 | – | no-audio |  |
| Vigo | `ingame` | 53.5 | 921 | – | no-audio |  |
| wolf4sdl-caanoo | `ingame` | 59.8 | 1535 | – | no-audio |  |
| xpiredcan.eng.101 | `ingame` | 0.1 | 3 | ✓ | low-fps |  |
| zelda-roth-olb-3t_caanoo | `ingame` | 21.1 | 576 | ✓ | low-fps |  |
| aaa | `playable` | 59.3 | 1501 | ✓ |  |  |
| aaaa | `playable` | 55.5 | 1408 | ✓ |  |  |
| ADVENTURE | `playable` | 57.9 | 1524 | ✓ |  |  |
| Amoebax | `playable` | 56.1 | 1423 | ✓ |  |  |
| Arcadevol2 | `playable` | 61.0 | 1553 | ✓ |  |  |
| battlejewels-105-caanoo-beta | `playable` | 55.6 | 1405 | ✓ |  |  |
| caanoo-12swap-v1.0-bin | `playable` | 39.8 | 1008 | ✓ |  |  |
| caanoo-gnurobbo-0.68 | `playable` | 33.5 | 448 | ✓ |  |  |
| cavestory | `playable` | 58.1 | 1555 | ✓ |  |  |
| ccrg-caanoo | `playable` | 45.8 | 198 | ✓ |  |  |
| cgenius-caanoo | `playable` | 49.2 | 1267 | ✓ |  |  |
| cooldowncaanoo | `playable` | 793.8 | 20589 | ✓ |  |  |
| daff_s_adventure_2_caanoo | `playable` | 26.4 | 678 | ✓ |  |  |
| deadlyc | `playable` | 55.2 | 1394 | ✓ |  |  |
| DealorNoDeal | `playable` | 60.8 | 1556 | ✓ |  |  |
| demons | `playable` | 118.5 | 2978 | ✓ |  |  |
| dynamate_c | `playable` | 28.6 | 752 | ✓ |  |  |
| fleshchasmer | `playable` | 54.7 | 1399 | ✓ |  |  |
| freeheroes2_c | `playable` | 39.2 | 80 | ✓ |  |  |
| fshark | `playable` | 85.6 | 2150 | ✓ |  |  |
| Fywod_caanoo | `playable` | 56.0 | 1420 | ✓ |  |  |
| getstar | `playable` | 85.5 | 2145 | ✓ |  |  |
| gravityforcev2 | `playable` | 47.5 | 1203 | ✓ |  |  |
| hellfire | `playable` | 118.5 | 2978 | ✓ |  |  |
| jumpToTheMoon_c | `playable` | 25.1 | 234 | ✓ |  |  |
| lmission_0.5 | `playable` | 58.5 | 1469 | ✓ |  |  |
| next_element | `playable` | 61.0 | 1543 | ✓ |  |  |
| outzone | `playable` | 85.5 | 2148 | ✓ |  |  |
| pang | `playable` | 60.1 | 1542 | ✓ |  |  |
| pengupop | `playable` | 37.5 | 168 | ✓ |  |  |
| profanation_Caanoo | `playable` | 33.1 | 71 | ✓ |  |  |
| propis | `playable` | 49.5 | 1245 | ✓ |  |  |
| RACING | `playable` | 59.5 | 1557 | ✓ |  |  |
| rhythmosplay_1.1.12 | `playable` | 51.1 | 1300 | ✓ |  |  |
| Shanghai2 | `playable` | 45.5 | 93 | ✓ |  |  |
| SHOOTERS | `playable` | 77.4 | 2099 | ✓ |  |  |
| Slap | `playable` | 85.8 | 2154 | ✓ |  |  |
| snowbros | `playable` | 118.5 | 2975 | ✓ |  |  |
| snowbros2 | `playable` | 118.1 | 2967 | ✓ |  |  |
| SPORTS | `playable` | 60.3 | 1555 | ✓ |  |  |
| sqrxz-v0996-caanoo | `playable` | 52.6 | 1344 | ✓ |  |  |
| sqrxz2-v0.80-caanoo | `playable` | 58.3 | 1482 | ✓ |  |  |
| stppc-caanoo-29-11-2010 | `playable` | 32.8 | 342 | ✓ |  |  |
| tailtale4c | `playable` | 61.2 | 1542 | ✓ |  |  |
| Tigerhell | `playable` | 85.7 | 2153 | ✓ |  |  |
| Tile | `playable` | 55.4 | 1548 | ✓ |  |  |
| tong-caanoo | `playable` | 56.9 | 1500 | ✓ |  |  |
| Truxton | `playable` | 85.5 | 2150 | ✓ |  |  |
| truxton2 | `playable` | 85.4 | 2147 | ✓ |  |  |
| twincobr | `playable` | 85.5 | 2147 | ✓ |  |  |
| twinhawk | `playable` | 85.3 | 2145 | ✓ |  |  |
| Wardner | `playable` | 118.9 | 2987 | ✓ |  |  |
| warehouse_panic_v1.1_caanoo | `playable` | 30.7 | 452 | ✓ |  |  |
| WindandWater | `playable` | 60.7 | 1537 | ✓ |  |  |
| Wizznic 0.9.2- preview | `playable` | 36.8 | 984 | ✓ |  |  |
| wvector | `playable` | 43.5 | 1094 | ✓ |  |  |
| zerowing | `playable` | 118.5 | 2977 | ✓ |  |  |
