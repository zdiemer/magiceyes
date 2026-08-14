# magiceyes compatibility sweep

Every GP2X / Wiz / Caanoo title on the corpus share, booted headlessly through the native engine (`bin/me_unicorn`) and scored from the shm framebuffer + the structured run report. Regenerate with `tools/test/compat_report.py`.


## Summary

| Platform | Titles | Playable | Ingame | Black | Incompatible | Crashed |
|---|--:|--:|--:|--:|--:|--:|
| GP2X | 631 | 476 | 18 | 47 | 90 | 0 |
| Wiz | 147 | 102 | 2 | 28 | 15 | 0 |
| Caanoo | 194 | 108 | 49 | 11 | 26 | 0 |
| **All** | **972** | **686** | **69** | **86** | **131** | **0** |

59 corpus folders held no `.gpe` at all (source dumps, skin packs, data-only add-ons): with nothing to run they are not titles and are excluded from every count above.

### What the tiers mean

| Tier | Meaning |
|---|---|
| `playable` | Held ≥20 fps and the picture survived the visual checks (silence is not held against a title: many simply have no audio; silent ones keep the `no-audio` label) |
| `ingame` | Renders gameplay with a notable gap: slow, a flat fill, or a picture that is visibly wrong |
| `black` | Frames advanced, but every sampled frame was black |
| `incompatible` | Never rendered: died in the loader/ld.so, or no frame at all |
| `crashed` | Host fault after booting (engine exit 70) |

`playable` and `ingame` are the reported grades. The harness's own tier (which only knows frame rate, non-black and audio) is kept per title as `status`, and `baseline.py` still gates on that.

## Failure groups (ranked by titles blocked)

One fix at the top of this table unblocks the whole row.

| Failure group | Titles | Platforms | Most common specifics |
|---|--:|---|---|
| **Renders at speed but no audio** (`no-audio`) | 107 | Caanoo, GP2X, Wiz | n/a |
| **Boots but renders only black** (`black-screen`) | 86 | Caanoo, GP2X, Wiz | n/a |
| **Never rendered a frame (cause unknown)** (`no-frames`) | 67 | Caanoo, GP2X, Wiz | n/a |
| **Not a 32-bit ARM ELF** (`not-arm-elf`) | 45 | GP2X | n/a |
| **Renders but below 20 fps** (`low-fps`) | 40 | Caanoo, GP2X | n/a |
| **Draws only a flat colour** (`flat-fill`) | 17 | Caanoo, GP2X | n/a |
| **Renders, but the picture is wrong** (`garbled-visuals`) | 11 | Caanoo, GP2X, Wiz | n/a |
| **Game data files are missing from the dump** (`missing-game-data`) | 7 | Caanoo, GP2X | n/a |
| **Archive extraction failed** (`archive-failed`) | 5 | Caanoo, GP2X | n/a |
| **Unknown /dev node** (`unknown-device`) | 5 | Caanoo, GP2X, Wiz | `/dev/accel` ×1, `/dev/input/mouse/0` ×1, `/dev/cx25874` ×1, `/dev/graphics/fb0` ×1 |
| **Unimplemented syscall** (`unimplemented-syscall`) | 2 | GP2X | `9437188` ×1, `11711` ×1 |
| **Could not open a display** (`display-init-failed`) | 1 | GP2X | n/a |

## Renders, but the picture is wrong

These 11 titles pass the running checks (frames advancing, frame rate, audio) while the frame itself is visibly broken, so they are graded `ingame` rather than `playable`. The reasons come from measuring the captured frame: a consistent per-row offset means a stride/pitch mismatch, large-scale repetition means the screen holds more than one copy of itself, and noise far above what dithered artwork reaches means corrupt memory.

| Title | Platform | fps | What the frame looks like |
|---|---|--:|---|
| aimcaanoo | Caanoo | 49.9 | the screen holds a second copy of itself, offset by 160px; left and right halves are near-identical; top and bottom halves are near-identical |
| EEEEK! EEEEEK! HOOOOOOK!!! | Caanoo | 24.0 | renders at 640x480 instead of 320x240 |
| Metal Slug Zombies | Caanoo | 30.7 | renders at 640x480 instead of 320x240 |
| mtknights | Caanoo | 31.2 | the screen holds a second copy of itself, offset by 128px |
| 1945_GP2X_0.2b | GP2X | 57.7 | pixel-to-pixel noise of 97, far above what dithered artwork reaches; the frame looks like corrupt memory |
| BunnyTraps-v11 | GP2X | 61.8 | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like corrupt memory |
| GF | GP2X | 61.3 | top and bottom halves are near-identical |
| Life.0.1 | GP2X | 61.7 | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like corrupt memory |
| MoveSweep2X | GP2X | 47.1 | the screen holds a second copy of itself, offset by 96px; left and right halves are near-identical |
| blingo | Wiz | 58.0 | pixel-to-pixel noise of 100, far above what dithered artwork reaches; the frame looks like corrupt memory |
| Ruckman-Wiz | Wiz | 57.2 | pixel-to-pixel noise of 102, far above what dithered artwork reaches; the frame looks like corrupt memory |

## Scored as working, but only painting a flat colour

These 17 titles advanced frames, kept audio running, and held frame rate, so they land in `playable`/`renders`. Their framebuffer never held more than one or two colours, which means the tier overstates them. Worth treating as broken.

| Title | Platform | Status | fps |
|---|---|---|--:|
| Arcadevol3 | Caanoo | `renders` | 58.8 |
| gnp_104 | Caanoo | `playable` | 50.3 |
| meritous | Caanoo | `playable` | 65.1 |
| noiz2sa_caanoo | Caanoo | `playable` | 40.1 |
| rg_ura_103 | Caanoo | `playable` | 54.3 |
| Skull (Caanoo) | Caanoo | `renders` | 15.4 |
| _-The Reversed Preacher 3-_Hack bIld_ | GP2X | `playable` | 56.2 |
| _-the reversed preacher II-_ | GP2X | `playable` | 60.8 |
| ASCIIPong2xV0.4 | GP2X | `playable` | 58.6 |
| dodge | GP2X | `playable` | 45.7 |
| dumbbell2x-01 | GP2X | `renders` | 61.5 |
| fenixGamePack | GP2X | `playable` | 48.0 |
| game bIld 2 | GP2X | `playable` | 61.6 |
| levelEdit | GP2X | `renders` | 61.9 |
| robot-escape | GP2X | `playable` | 84.6 |
| starsystem | GP2X | `playable` | 61.4 |
| the reversed preacher II | GP2X | `playable` | 59.3 |

## Cross-title blockers


### Unimplemented syscalls

| Item | Titles |
|---|--:|
| `9437188` | 1 |
| `9437238` | 1 |
| `9437274` | 1 |
| `9437358` | 1 |
| `11711` | 1 |
| `11713` | 1 |
| `77 (getrusage)` | 1 |

### Missing dynamic symbols

| Item | Titles |
|---|--:|
| `LoadImage -> Could not load image: Failed loading libpng.so.3: /lib/libpng.so.3` | 1 |

### Unknown /dev nodes

| Item | Titles |
|---|--:|
| `/dev/input/mouse/0` | 211 |
| `/dev/psaux` | 188 |
| `/dev/usbmouse` | 188 |
| `/dev/input/mouse0` | 20 |
| `/dev/accel` | 17 |
| `/dev/input/mice` | 8 |
| `/dev/mouse` | 8 |
| `/dev/adbmouse` | 5 |
| `/dev/` | 4 |
| `/dev/pts/` | 4 |
| `/dev/batt` | 3 |
| `/dev/gpmdata` | 3 |
| `/dev/pollux_batt` | 3 |
| `/dev/mmsp2adc` | 2 |
| `/dev/input/mouse` | 2 |
| `/dev/ptmx` | 1 |
| `/dev/ptyp0` | 1 |
| `/dev/cx25874` | 1 |
| `/dev/graphics/fb0` | 1 |

### Quirks (ran, but not fully honoured)

| Item | Titles |
|---|--:|
| `unknown_mmio:0x90a` | 414 |
| `unknown_ioctl:fb` | 239 |
| `unknown_mmio:0x910` | 110 |
| `unknown_mmio:0x1988` | 58 |
| `unknown_mmio:0x19c0` | 58 |
| `unknown_mmio:0x19c4` | 58 |
| `unknown_mmio:0x924` | 53 |
| `unknown_mmio:0x91c` | 51 |
| `unknown_mmio:0x3b46` | 46 |
| `unknown_mmio:0x3802` | 23 |
| `unknown_mmio:0x3804` | 23 |
| `unknown_mmio:0xf16` | 12 |
| `unknown_mmio:0xf58` | 12 |
| `unknown_mmio:0x808` | 12 |
| `unsupported_blit:dst-unmapped` | 11 |
| `unknown_mmio:0xf004` | 10 |
| `unknown_mmio:0x1980` | 6 |
| `unknown_mmio:0x14802` | 5 |
| `unknown_mmio:0x14804` | 5 |
| `unknown_mmio:0x3808` | 5 |
| `unsupported_gles:glEnable` | 4 |
| `unsupported_sdl:IMG_Load_unsupported` | 4 |
| `unknown_mmio:0xf07c` | 4 |
| `unknown_mmio:0x307c` | 4 |
| `unknown_mmio:0x307e` | 4 |

## Per-title results


### GP2X (631 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| 2xquake003 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| 2xquake2 | `incompatible` | 0.0 | 0 | ✓ | missing-game-data |  |
| 2XRally01 | `incompatible` | 0.0 | 0 | – | display-init-failed |  |
| abduction | `incompatible` | 0.0 | 0 | – | no-frames |  |
| airpong4GP2X0.0.4 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/airpong4GP2X0.0.4/airpong022/src/AirPong.gpe' is not an  |
| AlienZ | `incompatible` | 0.0 | 0 | – | no-frames |  |
| animatch_v1.2.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/animatch_v1.2.zip' (exit 32512) |
| AnotherGame2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/AnotherGame2x/AnotherGame2x/anothergame2x.gpe' is not an |
| balluz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/balluz/balluz/balluz.gpe' is not an ARM ELF and no runna |
| BermudaS_gp2x | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/input/mouse/0 |
| blocksGP2X-0 | `incompatible` | 0.0 | 0 | – | unimplemented-syscall | 9437188 |
| Boomshine2x_(java) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Boomshine2x_(java)/Boomshine2x/Boomshine2x.gpe' is not a |
| bunkermaster2x04 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| CloneKeen2X-1.0a | `incompatible` | 0.0 | 0 | – | no-frames |  |
| d1x-rebirth-gp2x_v0.50a | `incompatible` | 0.0 | 0 | – | unimplemented-syscall | 11711 |
| DeathChase4GP2X-V0.1b | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/DeathChase4GP2X-V0.1b/deathchase3d-0.9/deathchase3d/Deat |
| dkbk2x-0.1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| doom | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/doom/doom/10sector.gpe' is not an ARM ELF and no runnabl |
| doom_mod_examples | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/doom_mod_examples/game/interpreters/doom/pwad1/prboom_gm |
| DoomPwadPack | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/DoomPwadPack/AliensTC.gpe' is not an ARM ELF and no runn |
| duckmaze-gp2x-0.1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/duckmaze-gp2x-0.1/duckmaze-gp2x-0.1/duckmaze.gpe' is not |
| FFDoom | `incompatible` | 2.0 | 1 | – | no-frames |  |
| Fire | `incompatible` | 0.0 | 0 | – | no-frames |  |
| garden2x02 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| geoQuiz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/geoQuiz/geoQuiz.gpe' is not an ARM ELF and no runnable b |
| gp2x-rogue-v1.0 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| gp2xninjas-v06 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/gp2xninjas-v06/Ninjas v0.6 Final GP2X/ninjas.gpe' is not |
| GPQuakeDistributable3 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/GPQuakeDistributable3/GPQuakeDistributable3/jzspq2.gpe'  |
| GPQuakeModsDistributable1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/GPQuakeModsDistributable1/alk12.gpe' is not an ARM ELF a |
| GPQuakeModsDistributable2 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/GPQuakeModsDistributable2/flesh.gpe' is not an ARM ELF a |
| gravityforce2x04 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Heretic MOD pack1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Heretic MOD pack1/game/interpreters/heretic/pwad1/Hereti |
| Hexen2X_v0.5 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| hexen_mods1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/hexen_mods1/game/interpreters/hexen/DeathKings.gpe' is n |
| hexen_mods2 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/hexen_mods2/game/interpreters/hexen/pwad2/Hexen2X_gmenu2 |
| HigherOrLower-GP2X-v011 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| kobo_deluxe_beta1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| KQ2X_v3 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Laser2xVers10 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| Liquid Counter.gp2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Liquid Counter.gp2x/LiquidCount/LiquidCount.gpe' is not  |
| Lottys_Lines.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/Lottys_Lines.zip' (exit 32512) |
| Midnight2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Midnight2x/dosbox/midnight/midnight.gpe' is not an ARM E |
| mopesnake-gp2x-0.5 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/mopesnake-gp2x-0.5/mopesnake-gp2x-0.5/mopesnake.gpe' is  |
| nethack-ascii-3.4.3port1 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| nethack-caduhack.r01 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| nethack06 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ohthehumanity-1.0.0 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/ohthehumanity-1.0.0/ohthehumanity/ohthehumanity.gpe' is  |
| onscripter2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| OpenBOR_v2.1933 | `incompatible` | 60.5 | 1081 | ✓ | no-frames |  |
| OpenTTD | `incompatible` | 0.0 | 0 | – | no-frames |  |
| pacmame | `incompatible` | 7.8 | 4 | – | no-frames |  |
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
| roadsmash | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/roadsmash/road.gpe' is not an ARM ELF and no runnable bi |
| rott-v0.2 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| scummvm-alpha-8a_sky | `incompatible` | 0.0 | 0 | – | no-frames |  |
| smw-1.6_gp2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| snakepan | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/snakepan/Snakepan.gpe' is not an ARM ELF and no runnable |
| snowedin6_v1-00_gp2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/graphics/fb0 |
| SpaceRocks2X | `incompatible` | 100.0 | 934 | – | no-frames |  |
| squaregame2xV1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/squaregame2xV1/squaregame2x.gpe' is not an ARM ELF and n |
| Starship Soldier.gp2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Starship Soldier.gp2x/StarshipSoldier/starship_soldier.g |
| stppc2x-v1.1.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/stppc2x-v1.1.zip' (exit 32512) |
| strife | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/strife/dosbox/strife/strife.gpe' is not an ARM ELF and n |
| Supa2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Supa2x/dosbox/supaplex.gpe' is not an ARM ELF and no run |
| testmem2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/testmem2x/testmem2x/testmem2x.gpe' is not an ARM ELF and |
| ttd2x_020108 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| tunar-1.1.0 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/tunar-1.1.0/tunar/tunar.gpe' is not an ARM ELF and no ru |
| uqm-0.5.0-r1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| uqm2x_langpack_v1.2 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/uqm2x_langpack_v1.2/uqm2xfin.gpe' is not an ARM ELF and  |
| uqm2x_remixpack_1.1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/uqm2x_remixpack_1.1/uqm2xrmx.gpe' is not an ARM ELF and  |
| wads1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/wads1/wads1/requiem.gpe' is not an ARM ELF and no runnab |
| wads2 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/wads2/wads2/h2h-xmas.gpe' is not an ARM ELF and no runna |
| Wiztern Demo | `incompatible` | 107.0 | 1858 | ✓ | no-frames |  |
| Wolf4SDL | `incompatible` | 0.0 | 0 | – | no-frames |  |
| worminator302 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Zombiepox2X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| 2xHexen2 v0.05 PB2 | `black` | 24.2 | 25 | – | black-screen |  |
| 2xWargus_PB1.3 | `black` | 0.9 | 15 | ✓ | black-screen |  |
| 2xZdoom_PB1.2 | `black` | 32.7 | 53 | – | black-screen |  |
| 2xZdoom_selector | `black` | 53.8 | 125 | ✓ | black-screen |  |
| A1GP2XV1_1 | `black` | 15.9 | 8 | – | black-screen |  |
| AbusimbelProfanationDeluxe | `black` | 54.6 | 123 | ✓ | black-screen |  |
| albion-v1.0.1-gp2x | `black` | 24.8 | 28 | ✓ | black-screen |  |
| AlienBlaster_1.02 | `black` | 11.3 | 16 | ✓ | black-screen |  |
| angband2x-v2 | `black` | 36.0 | 77 | – | black-screen |  |
| Bombs Panic | `black` | 87.5 | 184 | ✓ | black-screen |  |
| BubbleTrain_GP2X-2006_Entry | `black` | 1.2 | 2 | ✓ | black-screen |  |
| CaptainCrusader_GP2XDemo | `black` | 3.9 | 2 | – | black-screen |  |
| CaptainCrusader_GP2XFull | `black` | 5.8 | 3 | – | black-screen |  |
| d2x-gp2x-0.02 | `black` | 15.0 | 18 | ✓ | black-screen |  |
| Digger | `black` | 26.6 | 60 | ✓ | black-screen |  |
| duke2x004 | `black` | 17.7 | 9 | – | black-screen |  |
| egoboo-cramfs | `black` | 36.8 | 65 | ✓ | black-screen |  |
| Flappynerd_GP2X | `black` | 45.9 | 125 | ✓ | black-screen |  |
| FleshChasmer132c_patch | `black` | 7.3 | 4 | ✓ | black-screen |  |
| FleshChasmer_Dpad | `black` | 10.0 | 5 | ✓ | black-screen |  |
| FlipIR_GP2X | `black` | 117.5 | 2968 | ✓ | black-screen |  |
| flowflowmania-0_6-gp2x | `black` | 28.7 | 70 | – | black-screen |  |
| GP2X_Nat2007 | `black` | 117.2 | 2958 | ✓ | black-screen |  |
| gp2xDoukutsu-1.04 | `black` | 17.0 | 15 | ✓ | black-screen |  |
| gp2xJenkasNightmare | `black` | 19.8 | 16 | ✓ | black-screen |  |
| GPgeneral | `black` | 4.0 | 2 | – | black-screen |  |
| liquidwar2x02 | `black` | 4.0 | 2 | – | black-screen |  |
| openggs | `black` | 61.0 | 1554 | ✓ | black-screen |  |
| openjazz-gp2x | `black` | 15.7 | 14 | ✓ | black-screen |  |
| para3 | `black` | 58.0 | 145 | ✓ | black-screen |  |
| PrBoom PWAD pack | `black` | 2.6 | 6 | – | black-screen |  |
| raw2xv0.3.1 | `black` | 11.8 | 6 | – | black-screen |  |
| scummvm-1.2.0-gp2x | `black` | 17.2 | 21 | ✓ | black-screen |  |
| ShadowWarrior2X | `black` | 11.7 | 6 | – | black-screen |  |
| SmashGp2x02 | `black` | 61.2 | 1552 | ✓ | black-screen |  |
| step2x02 | `black` | 52.1 | 111 | ✓ | black-screen |  |
| tesla-Siren | `black` | 46.5 | 97 | ✓ | black-screen |  |
| Tetrablocks.0.4.GP2X | `black` | 46.9 | 105 | ✓ | black-screen |  |
| uhexen | `black` | 9.7 | 5 | – | black-screen |  |
| ultratumba_exp-20100925.gp2x | `black` | 13.8 | 7 | ✓ | black-screen |  |
| Volleyball | `black` | 52.4 | 111 | ✓ | black-screen |  |
| warcraft-beta3-gp2x | `black` | 30.9 | 27 | ✓ | black-screen |  |
| xbak-0.1.3 | `black` | 29.9 | 15 | – | black-screen |  |
| xcom1-v1.0.2-gp2x | `black` | 43.1 | 1598 | ✓ | black-screen |  |
| xcom2-v1.0.1-gp2x | `black` | 109.3 | 2794 | ✓ | black-screen |  |
| xump2x_beta2 | `black` | 27.3 | 14 | ✓ | black-screen |  |
| zcgp2x_211B18_0.4alpha | `black` | 21.4 | 23 | – | black-screen |  |
| 1945_GP2X_0.2b | `ingame` | 57.7 | 546 | ✓ | garbled-visuals | pixel-to-pixel noise of 97, far above what dithered artwork reaches; the frame looks like  |
| _-The Reversed Preacher 3-_Hack bIld_ | `ingame` | 56.2 | 131 | ✓ | flat-fill |  |
| _-the reversed preacher II-_ | `ingame` | 60.8 | 138 | ✓ | flat-fill |  |
| ASCIIPong2xV0.4 | `ingame` | 58.6 | 1473 | ✓ | flat-fill |  |
| BunnyTraps-v11 | `ingame` | 61.8 | 1557 | ✓ | garbled-visuals | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like |
| Clonk2X_1.0 | `ingame` | 16.1 | 408 | – | not-arm-elf | magiceyes: reload of '/bin/sh' failed |
| CromoZome | `ingame` | 19.1 | 500 | ✓ | low-fps |  |
| dodge | `ingame` | 45.7 | 98 | ✓ | flat-fill |  |
| dumbbell2x-01 | `ingame` | 61.5 | 575 | – | flat-fill |  |
| fenixGamePack | `ingame` | 48.0 | 454 | ✓ | flat-fill |  |
| game bIld 2 | `ingame` | 61.6 | 1553 | ✓ | flat-fill |  |
| GF | `ingame` | 61.3 | 1553 | ✓ | garbled-visuals | top and bottom halves are near-identical |
| levelEdit | `ingame` | 61.9 | 1560 | – | flat-fill |  |
| Life.0.1 | `ingame` | 61.7 | 1551 | – | garbled-visuals | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like |
| MoveSweep2X | `ingame` | 47.1 | 96 | – | garbled-visuals | the screen holds a second copy of itself, offset by 96px; left and right halves are near-i |
| robot-escape | `ingame` | 84.6 | 178 | ✓ | flat-fill |  |
| starsystem | `ingame` | 61.4 | 1553 | ✓ | flat-fill |  |
| the reversed preacher II | `ingame` | 59.3 | 134 | ✓ | flat-fill |  |
| 2xpong_gp2x | `playable` | 61.2 | 1539 | ✓ |  |  |
| 2xtron-v01 | `playable` | 62.1 | 1565 | ✓ |  |  |
| 4WE_GP2x | `playable` | 116.9 | 2018 | ✓ |  |  |
| 9 Lives | `playable` | 70.2 | 1775 | ✓ |  |  |
| a_sn-pong | `playable` | 42.0 | 1555 | – | no-audio |  |
| abe | `playable` | 61.4 | 1546 | ✓ |  |  |
| abuse_1.0 | `playable` | 58.5 | 1553 | ✓ |  |  |
| AdamantArmorAffection2x | `playable` | 60.5 | 1534 | ✓ |  |  |
| ADIC2X | `playable` | 89.2 | 1067 | ✓ |  |  |
| AfterBurner-GP2X | `playable` | 36.6 | 930 | ✓ |  |  |
| Airplyr | `playable` | 61.9 | 1557 | ✓ |  |  |
| airstrike-1.1 | `playable` | 61.1 | 1553 | ✓ |  |  |
| Akd_BB | `playable` | 61.5 | 1555 | ✓ |  |  |
| alex | `playable` | 61.6 | 1551 | ✓ |  |  |
| Alex's Falldown | `playable` | 61.3 | 1553 | ✓ |  |  |
| alex4_gp2x | `playable` | 61.4 | 1547 | ✓ |  |  |
| altitude | `playable` | 60.0 | 1550 | ✓ |  |  |
| AMazing-3D | `playable` | 64.4 | 1630 | – | no-audio |  |
| amoebax-0.2.1-gp2x | `playable` | 58.8 | 1499 | ✓ |  |  |
| armorcity-0_30b | `playable` | 60.7 | 1554 | ✓ |  |  |
| Asteroids | `playable` | 60.6 | 1553 | ✓ |  |  |
| astrochaos | `playable` | 61.2 | 1552 | ✓ |  |  |
| atris-1.0.7 | `playable` | 61.3 | 1582 | ✓ |  |  |
| B'lox! | `playable` | 116.3 | 2969 | ✓ |  |  |
| bang_gp | `playable` | 61.3 | 1555 | ✓ |  |  |
| BareFistFighter | `playable` | 61.2 | 1539 | ✓ |  |  |
| barrage | `playable` | 61.4 | 1553 | ✓ |  |  |
| Batiscafo (versin EXP) | `playable` | 61.1 | 1540 | ✓ |  |  |
| battlejewels-gp2x-062-100 | `playable` | 117.1 | 2956 | ✓ |  |  |
| BearOids | `playable` | 61.8 | 1556 | ✓ |  |  |
| beat2x-0.5-bin | `playable` | 61.1 | 1554 | ✓ |  |  |
| Beatbox_1.2 | `playable` | 58.8 | 1556 | ✓ |  |  |
| BeetleRun | `playable` | 61.8 | 1557 | ✓ |  |  |
| biniax-gp2x_v1.2 | `playable` | 60.8 | 1535 | ✓ |  |  |
| Biniax2_gp2x | `playable` | 42.2 | 1560 | ✓ |  |  |
| Biohazard2 | `playable` | 61.9 | 1557 | ✓ |  |  |
| BioShoot GP2X | `playable` | 60.7 | 1554 | ✓ |  |  |
| Birdshoot | `playable` | 61.9 | 1552 | – | no-audio |  |
| BisfoG | `playable` | 77.6 | 921 | ✓ |  |  |
| blastriot1.21 | `playable` | 59.2 | 1493 | ✓ |  |  |
| blazar_v1-30_gp2x | `playable` | 61.8 | 1556 | ✓ |  |  |
| blingo 1.2 | `playable` | 59.9 | 1552 | ✓ |  |  |
| blipsgp2x | `playable` | 60.1 | 1562 | ✓ |  |  |
| Blix2x | `playable` | 61.9 | 1557 | ✓ |  |  |
| blobbyvolley | `playable` | 61.2 | 1551 | ✓ |  |  |
| blobwars_2x | `playable` | 62.6 | 1586 | ✓ |  |  |
| block | `playable` | 61.4 | 1561 | ✓ |  |  |
| blockdudegp2x | `playable` | 58.6 | 1558 | ✓ |  |  |
| Blocked | `playable` | 113.1 | 2874 | ✓ |  |  |
| blockoid | `playable` | 60.9 | 1557 | ✓ |  |  |
| Blockrage2x | `playable` | 60.9 | 1536 | ✓ |  |  |
| blox | `playable` | 45.1 | 1138 | ✓ |  |  |
| Bloxz_DEMO | `playable` | 61.7 | 1562 | ✓ |  |  |
| bluecube2x | `playable` | 61.7 | 1554 | ✓ |  |  |
| bobtron-gp2x | `playable` | 61.5 | 1551 | ✓ |  |  |
| Boomshine2x_1.12_gp2x | `playable` | 60.5 | 1555 | ✓ |  |  |
| Boulders-0 | `playable` | 61.0 | 1552 | ✓ |  |  |
| brassmunkey_gp2x_1.0 | `playable` | 60.5 | 1532 | ✓ |  |  |
| BubbleX | `playable` | 61.8 | 1549 | ✓ |  |  |
| BubTrain_GP2X-2006_Entry_No-Sound | `playable` | 58.0 | 1554 | ✓ |  |  |
| bugafactorx-v03-beta | `playable` | 61.2 | 1552 | – | no-audio |  |
| BugWarsSE_v1.0 | `playable` | 60.3 | 1551 | ✓ |  |  |
| bumprace-0.2 | `playable` | 60.4 | 1558 | ✓ |  |  |
| BurokkuDemo1 | `playable` | 61.8 | 1564 | ✓ |  |  |
| buscaminas | `playable` | 61.4 | 1551 | – | no-audio |  |
| buttongame | `playable` | 48.7 | 100 | ✓ |  |  |
| BuzzysBadDay-1.0 | `playable` | 61.5 | 1556 | ✓ |  |  |
| cackb2 | `playable` | 35.3 | 76 | ✓ |  |  |
| CamelotWarriors-GP2x_v1.0 | `playable` | 60.9 | 1553 | ✓ |  |  |
| cardm | `playable` | 61.2 | 1553 | – | no-audio |  |
| CascadeBeneath v1.0 for GP2X | `playable` | 62.7 | 1577 | ✓ |  |  |
| cat_trap | `playable` | 112.6 | 2875 | ✓ |  |  |
| cavecopter_gp2x | `playable` | 21.2 | 531 | – | no-audio |  |
| ccrg | `playable` | 54.7 | 239 | ✓ |  |  |
| cdogs2x04 | `playable` | 97.9 | 2482 | ✓ |  |  |
| cgenius-gp2x | `playable` | 58.3 | 1506 | ✓ |  |  |
| chaos2x | `playable` | 61.9 | 1555 | ✓ |  |  |
| checkersgp2x | `playable` | 59.8 | 1559 | ✓ |  |  |
| chess2x05 | `playable` | 60.2 | 1556 | ✓ |  |  |
| chicken-puyopuyo | `playable` | 58.9 | 1490 | – | no-audio |  |
| Chopper | `playable` | 61.2 | 1555 | – | no-audio |  |
| ChopperAttackv1.0.17 | `playable` | 103.3 | 2879 | ✓ |  |  |
| Chroma | `playable` | 117.6 | 2967 | ✓ |  |  |
| chuckiev12 | `playable` | 61.6 | 1553 | ✓ |  |  |
| Codemaster | `playable` | 118.0 | 2967 | ✓ |  |  |
| Comando2gp2xEN | `playable` | 62.1 | 1562 | ✓ |  |  |
| ConnyCarrot | `playable` | 60.7 | 1544 | ✓ |  |  |
| coppergreen | `playable` | 60.0 | 1559 | ✓ |  |  |
| cosmo2x_01 | `playable` | 61.7 | 1563 | – | no-audio |  |
| CowSuckers-1.0 | `playable` | 61.5 | 1554 | ✓ |  |  |
| Crapong | `playable` | 60.9 | 1551 | ✓ |  |  |
| crazeeman | `playable` | 64.6 | 1632 | ✓ |  |  |
| crimsonV1 | `playable` | 59.9 | 1557 | ✓ |  |  |
| crocodingusgp2x | `playable` | 114.7 | 1543 | ✓ |  |  |
| crossroads | `playable` | 61.6 | 1557 | ✓ |  |  |
| CUBES | `playable` | 61.8 | 1563 | ✓ |  |  |
| cyberhockeyV2_6 | `playable` | 61.6 | 1555 | ✓ |  |  |
| DABAKKA-0 | `playable` | 61.6 | 1547 | ✓ |  |  |
| Dance2x Alpha GPE | `playable` | 61.0 | 1557 | ✓ |  |  |
| DangerMouse | `playable` | 117.6 | 2967 | ✓ |  |  |
| Dark_Light_SDL2X | `playable` | 61.1 | 1559 | ✓ |  |  |
| Dastardly dungeon 1.5 | `playable` | 60.7 | 1553 | ✓ |  |  |
| dd2x | `playable` | 108.3 | 489 | ✓ |  |  |
| DealOrNoDeal-v12 | `playable` | 60.9 | 1562 | ✓ |  |  |
| DeathTrap1_1 | `playable` | 59.7 | 1553 | ✓ |  |  |
| debian_vs_pimientos_2x_0.1.2 | `playable` | 58.7 | 554 | ✓ |  |  |
| defeatme-gp2x-1.0.1 | `playable` | 61.0 | 1536 | ✓ |  |  |
| diamant_1_01 | `playable` | 33.0 | 838 | ✓ |  |  |
| DontGetCrushed v1.0 for GP2X | `playable` | 62.3 | 1564 | ✓ |  |  |
| dopewars2x | `playable` | 61.7 | 1549 | – | no-audio |  |
| dosmugen | `playable` | 59.7 | 1555 | ✓ |  |  |
| Dr. Mates v1.0 | `playable` | 42.0 | 99 | ✓ |  |  |
| Drill2x_final | `playable` | 59.7 | 1530 | ✓ |  |  |
| drill2x_xtreme_v1.0.3 | `playable` | 60.8 | 1551 | ✓ |  |  |
| drod-gp2x-1_0 | `playable` | 55.2 | 1435 | – | no-audio |  |
| dstroyGP2X1402 | `playable` | 60.5 | 1555 | – | no-audio |  |
| DubaiRace038a | `playable` | 47.5 | 109 | ✓ |  |  |
| dyc_gp2x | `playable` | 114.3 | 2904 | ✓ |  |  |
| dynamategp2x | `playable` | 60.2 | 1556 | ✓ |  |  |
| E-Fighters2x_FIRST_ALPHA_0_0_5_fixedSound | `playable` | 116.7 | 2967 | ✓ |  |  |
| EasterQuest | `playable` | 117.5 | 2969 | ✓ |  |  |
| eggstreme3_v1-00_gp2x | `playable` | 61.6 | 1552 | ✓ |  |  |
| egoboo2xFeb1207 | `playable` | 98.1 | 3049 | ✓ |  |  |
| Electronia | `playable` | 61.7 | 1558 | ✓ |  |  |
| enigma | `playable` | 49.8 | 1338 | ✓ |  |  |
| entombed2x | `playable` | 61.2 | 1554 | ✓ |  |  |
| EpicFreeFall_GP2X | `playable` | 61.0 | 1554 | ✓ |  |  |
| EpicRocks_GP2X | `playable` | 60.8 | 1552 | ✓ |  |  |
| escapa-v1 | `playable` | 61.8 | 1557 | ✓ |  |  |
| escoba_exp-20101016.gp2x | `playable` | 61.2 | 1555 | – | no-audio |  |
| exi_shoot_gp2x | `playable` | 61.1 | 1541 | ✓ |  |  |
| extraterrestres | `playable` | 98.7 | 2672 | ✓ |  |  |
| extraterrestres-0 | `playable` | 58.6 | 1562 | – | no-audio |  |
| exult_rc3 | `playable` | 41.5 | 1538 | ✓ |  |  |
| Factor-v1.0-final | `playable` | 61.0 | 1554 | ✓ |  |  |
| falldown_gp2x | `playable` | 92.5 | 2326 | ✓ |  |  |
| FCRLG | `playable` | 61.0 | 1535 | – | no-audio |  |
| fenix | `playable` | 57.6 | 1553 | ✓ |  |  |
| fifteen_01 | `playable` | 59.6 | 557 | – | no-audio |  |
| FindMii | `playable` | 117.2 | 2967 | ✓ |  |  |
| Firewhip | `playable` | 110.2 | 2945 | ✓ |  |  |
| Fishball-1.2 | `playable` | 61.2 | 1543 | ✓ |  |  |
| fissionfield2x | `playable` | 61.5 | 1556 | ✓ |  |  |
| Flaschenspiel | `playable` | 61.1 | 1553 | – | no-audio |  |
| FleshChasmer | `playable` | 59.9 | 1528 | ✓ |  |  |
| FleshChasmer Zero | `playable` | 60.6 | 1530 | ✓ |  |  |
| floaters | `playable` | 61.5 | 1558 | ✓ |  |  |
| flobopuyo0.20.1 | `playable` | 59.7 | 1518 | ✓ |  |  |
| flurkies_v1-25_gp2x | `playable` | 61.8 | 1556 | ✓ |  |  |
| fm | `playable` | 109.4 | 2745 | ✓ |  |  |
| Football2X | `playable` | 117.8 | 2967 | ✓ |  |  |
| formula1gp2x | `playable` | 61.1 | 1553 | ✓ |  |  |
| Fragger2x | `playable` | 61.9 | 1558 | ✓ |  |  |
| freec2x | `playable` | 41.6 | 1054 | ✓ |  |  |
| freecell_1 | `playable` | 52.6 | 109 | ✓ |  |  |
| freedroid2x06 | `playable` | 87.7 | 2269 | ✓ |  |  |
| freesci | `playable` | 56.6 | 255 | – | no-audio |  |
| friq-beta-07 | `playable` | 61.4 | 1562 | ✓ |  |  |
| frozen2x-0.1 | `playable` | 78.3 | 736 | ✓ |  |  |
| fruits2x | `playable` | 61.4 | 1553 | – | no-audio |  |
| fruits_gp2x | `playable` | 61.7 | 1558 | ✓ |  |  |
| FullBoard (test ver.) | `playable` | 52.3 | 115 | ✓ |  |  |
| fvc | `playable` | 61.0 | 1536 | ✓ |  |  |
| FyWod_2x | `playable` | 60.9 | 1558 | ✓ |  |  |
| game-watch-mario-bros | `playable` | 61.6 | 1554 | ✓ |  |  |
| gchess-v1.0.1-bin | `playable` | 61.1 | 1552 | – | no-audio |  |
| gchess-v1.1.0-bin | `playable` | 61.6 | 1559 | – | no-audio |  |
| Geek 'em up GP2X | `playable` | 60.9 | 1709 | ✓ |  |  |
| gemdrop2x_v02 | `playable` | 60.9 | 1568 | ✓ |  |  |
| GeneralPromise | `playable` | 116.6 | 2968 | ✓ |  |  |
| Ghostbusters_WIP | `playable` | 60.4 | 565 | ✓ |  |  |
| ghostpix_v10_gp2x | `playable` | 58.4 | 668 | ✓ |  |  |
| glouton | `playable` | 61.6 | 1551 | ✓ |  |  |
| gnp_104 | `playable` | 58.0 | 1579 | ✓ |  |  |
| gnugo2x | `playable` | 61.5 | 1556 | – | no-audio |  |
| gnurobbo_0.66_open2x | `playable` | 55.1 | 1431 | ✓ |  |  |
| godori | `playable` | 61.6 | 1548 | – | no-audio |  |
| GoitGP | `playable` | 60.3 | 1552 | ✓ |  |  |
| gorillaz | `playable` | 53.8 | 1353 | ✓ |  |  |
| gp2hanoi_0.8.1_gp2x | `playable` | 61.7 | 1554 | ✓ |  |  |
| gp2x-blobwars-0.1 | `playable` | 63.2 | 1597 | ✓ |  |  |
| gp2x-bubbletrain-0.1 | `playable` | 60.0 | 1553 | ✓ |  |  |
| gp2x-ceferino-0.1 | `playable` | 58.5 | 1551 | – | no-audio |  |
| gp2x-formido-0.1 | `playable` | 44.7 | 1557 | ✓ |  |  |
| gp2x-invaders-preview-version | `playable` | 61.9 | 1561 | ✓ |  |  |
| gp2x-netrok-0.1 | `playable` | 54.9 | 1557 | ✓ |  |  |
| gp2x-sand-0.3 | `playable` | 57.4 | 118 | – | no-audio |  |
| gp2x-shienso-bin_061021 | `playable` | 61.9 | 1560 | ✓ |  |  |
| gp2x-smc-0.1 | `playable` | 57.6 | 1551 | ✓ |  |  |
| gp2x-tenmado-0.1 | `playable` | 61.1 | 1554 | – | no-audio |  |
| gp2x-tong-v1 | `playable` | 116.5 | 2946 | – | no-audio |  |
| gp2x_2xmas | `playable` | 44.2 | 90 | ✓ |  |  |
| GP2X_BallGame_0.49 | `playable` | 61.0 | 1533 | ✓ |  |  |
| gp2x_drench | `playable` | 59.3 | 1505 | ✓ |  |  |
| GP2X_TLI | `playable` | 31.0 | 783 | ✓ |  |  |
| gp2xbug | `playable` | 115.9 | 2967 | ✓ |  |  |
| gp2xgo-v1.1.0-bin | `playable` | 61.3 | 1553 | – | no-audio |  |
| gp2xjunkie | `playable` | 58.2 | 1498 | ✓ |  |  |
| gp2xmancala-v1.1.1-bin | `playable` | 61.6 | 1555 | – | no-audio |  |
| GP2XOfLife | `playable` | 113.3 | 2863 | – | no-audio |  |
| gp2xpang-v.1.1.1 | `playable` | 100.0 | 2584 | ✓ |  |  |
| gp2xrick 1.0 | `playable` | 60.5 | 1535 | ✓ |  |  |
| GpFrontier v0.1 | `playable` | 61.9 | 1571 | ✓ |  |  |
| gpfrontier v0.4 | `playable` | 59.0 | 1558 | ✓ |  |  |
| gpnoid2x | `playable` | 60.9 | 1553 | ✓ |  |  |
| GPrina-GP2x_v1.0 | `playable` | 60.6 | 1553 | ✓ |  |  |
| GPSquares_GP2X | `playable` | 61.9 | 1555 | – | no-audio |  |
| gr-v1001-gp2x | `playable` | 59.5 | 1554 | ✓ |  |  |
| green | `playable` | 60.4 | 1534 | ✓ |  |  |
| grow | `playable` | 61.1 | 1532 | – | no-audio |  |
| gxeskiv | `playable` | 58.2 | 1470 | – | no-audio |  |
| HamstersEscape (Gp2x F-100 F-200) | `playable` | 31.8 | 300 | ✓ |  |  |
| hanagechu2x_gbax2007 | `playable` | 65.9 | 1664 | ✓ |  |  |
| hanagechu2xalpha | `playable` | 61.7 | 1555 | ✓ |  |  |
| Heretic2X_v0.5 | `playable` | 60.8 | 1529 | ✓ |  |  |
| heroes2x02 | `playable` | 52.2 | 1322 | ✓ |  |  |
| hex-a-hop | `playable` | 61.8 | 1555 | – | no-audio |  |
| hexbattle2x | `playable` | 61.5 | 1555 | ✓ |  |  |
| HumphreyGP2X | `playable` | 60.9 | 1554 | ✓ |  |  |
| Hyperion_GP2X_demo | `playable` | 62.0 | 1567 | ✓ |  |  |
| jump_n_blob_gp2x | `playable` | 58.9 | 1586 | ✓ |  |  |
| jumpnbumpgp2x | `playable` | 60.3 | 1556 | ✓ |  |  |
| Jurlx2 | `playable` | 61.3 | 1552 | ✓ |  |  |
| just4qix | `playable` | 61.5 | 1555 | ✓ |  |  |
| kampfimall-gp2x | `playable` | 61.2 | 1545 | – | no-audio |  |
| kampfimall-gp2x-music | `playable` | 60.2 | 527 | ✓ |  |  |
| ketm_2x_gp2x | `playable` | 52.6 | 1555 | ✓ |  |  |
| KicknPLay_1.1 | `playable` | 61.7 | 1552 | ✓ |  |  |
| Klaur | `playable` | 115.7 | 2968 | ✓ |  |  |
| Knight Lore | `playable` | 61.8 | 1556 | ✓ |  |  |
| koules2x_02 | `playable` | 61.0 | 1554 | ✓ |  |  |
| kuklomenos_gp2x_201209 | `playable` | 60.7 | 1555 | ✓ |  |  |
| kurukuru2x | `playable` | 61.4 | 1556 | ✓ |  |  |
| la | `playable` | 48.2 | 104 | ✓ |  |  |
| LABYRINTH | `playable` | 61.7 | 1555 | – | no-audio |  |
| ladykiller | `playable` | 60.8 | 1555 | ✓ |  |  |
| las-tres-luces-de-glaurung-remake | `playable` | 60.0 | 1543 | ✓ |  |  |
| lbreakoutgp2x | `playable` | 58.8 | 1558 | ✓ |  |  |
| levelshmup | `playable` | 85.9 | 2191 | ✓ |  |  |
| Lexeme | `playable` | 116.7 | 2967 | ✓ |  |  |
| lights-out | `playable` | 59.5 | 1498 | – | no-audio |  |
| LinesXv3 | `playable` | 61.6 | 1547 | ✓ |  |  |
| logicx | `playable` | 61.6 | 1548 | ✓ |  |  |
| Logoball | `playable` | 115.5 | 2966 | ✓ |  |  |
| lumix-beta-01 | `playable` | 64.0 | 1612 | – | no-audio |  |
| mad-mix-game-20b-final | `playable` | 61.1 | 1551 | ✓ |  |  |
| madbomber | `playable` | 60.6 | 1552 | ✓ |  |  |
| malvado2x | `playable` | 60.2 | 1552 | ✓ |  |  |
| MAME-N22_51 | `playable` | 59.5 | 1570 | ✓ |  |  |
| mancala-v1.0.1 | `playable` | 61.4 | 1554 | – | no-audio |  |
| March of the mini tux | `playable` | 62.9 | 1603 | ✓ |  |  |
| Marte Necesita Vacas GP2X | `playable` | 63.5 | 1650 | ✓ |  |  |
| Masteries_Journey_to_the_Center_of_the_earth_GP2X | `playable` | 59.4 | 1552 | ✓ |  |  |
| masterpiece2x | `playable` | 61.7 | 1557 | – | no-audio |  |
| MazeThingie | `playable` | 61.9 | 1561 | ✓ |  |  |
| MazezaMGP2X | `playable` | 97.7 | 2534 | ✓ |  |  |
| memory | `playable` | 62.4 | 1587 | ✓ |  |  |
| MemoryGP2X-v11 | `playable` | 61.8 | 1563 | ✓ |  |  |
| meritous | `playable` | 60.5 | 1552 | ✓ |  |  |
| Merlin2x_beta_021 | `playable` | 58.1 | 550 | ✓ |  |  |
| metaphysik | `playable` | 63.6 | 1619 | ✓ |  |  |
| methaneV1 | `playable` | 61.3 | 1552 | ✓ |  |  |
| minigolf | `playable` | 61.3 | 1551 | – | no-audio |  |
| minos-gp2x | `playable` | 60.2 | 1527 | ✓ |  |  |
| misterhachi | `playable` | 50.6 | 1494 | ✓ |  |  |
| mk13.gpe | `playable` | 61.5 | 1543 | ✓ |  |  |
| mkACE.gpe | `playable` | 61.7 | 1548 | ✓ |  |  |
| mkONE.gpe | `playable` | 61.5 | 1544 | ✓ |  |  |
| MM2X | `playable` | 61.1 | 1539 | ✓ |  |  |
| monacoGP | `playable` | 60.9 | 1566 | ✓ |  |  |
| monochromeworlds-gp2x-1.0.0 | `playable` | 60.7 | 1538 | ✓ |  |  |
| moonlander | `playable` | 59.9 | 1537 | ✓ |  |  |
| MouthTrap | `playable` | 117.6 | 2967 | ✓ |  |  |
| mueppv32 | `playable` | 117.2 | 2967 | ✓ |  |  |
| mush_gp2x | `playable` | 40.9 | 1052 | ✓ |  |  |
| Mutant Tank Knights | `playable` | 57.2 | 316 | ✓ |  |  |
| MyriadUpdated | `playable` | 60.1 | 1555 | ✓ |  |  |
| mzx-2.84c | `playable` | 60.9 | 841 | ✓ |  |  |
| mzx282-gp2x | `playable` | 61.7 | 1554 | ✓ |  |  |
| n-tris_v1.0 | `playable` | 72.5 | 1824 | ✓ |  |  |
| nanobounce-pacc-gp2x | `playable` | 50.3 | 386 | ✓ |  |  |
| nazcarunners-0 | `playable` | 40.4 | 46 | ✓ |  |  |
| nazcasphere | `playable` | 45.3 | 57 | ✓ |  |  |
| ne_deluxe_gp2x | `playable` | 61.4 | 1551 | ✓ |  |  |
| ne_gp2x | `playable` | 59.7 | 1509 | ✓ |  |  |
| Nebulus_gp2x | `playable` | 61.8 | 1552 | – | no-audio |  |
| NecNec2x | `playable` | 61.5 | 1562 | ✓ |  |  |
| Net-Bubble-gp2x_1-21-06_bin | `playable` | 56.0 | 523 | – | no-audio |  |
| newsuperpang | `playable` | 61.2 | 1551 | ✓ |  |  |
| Nifty | `playable` | 60.9 | 1551 | ✓ |  |  |
| noiz2saV3 | `playable` | 68.1 | 1742 | ✓ |  |  |
| Nom | `playable` | 60.9 | 1542 | ✓ |  |  |
| odonata_demo | `playable` | 60.3 | 1525 | ✓ |  |  |
| omok | `playable` | 53.0 | 110 | ✓ |  |  |
| OpenBOR_v3.0_Build_2615_&_2637 | `playable` | 61.0 | 1567 | ✓ |  |  |
| openglad2x | `playable` | 59.7 | 1548 | ✓ |  |  |
| opentyrian2x_0.3_complete | `playable` | 56.6 | 1537 | ✓ |  |  |
| opposite_lock | `playable` | 52.9 | 1495 | ✓ |  |  |
| OrbitalSniper2x_v1.1 | `playable` | 58.4 | 149 | ✓ |  |  |
| othello_v1.0 | `playable` | 61.9 | 1555 | ✓ |  |  |
| oxov06 | `playable` | 44.7 | 91 | – | no-audio |  |
| PAF | `playable` | 61.4 | 1555 | ✓ |  |  |
| PantaVsDragon (Gp2x F-100 F-200) | `playable` | 51.1 | 1317 | ✓ |  |  |
| paraballgp2x | `playable` | 61.1 | 1552 | ✓ |  |  |
| Payback | `playable` | 42.6 | 210 | ✓ |  |  |
| PaybackDemo | `playable` | 32.5 | 849 | ✓ |  |  |
| pc | `playable` | 59.3 | 672 | ✓ |  |  |
| pdcv060b | `playable` | 56.1 | 526 | ✓ |  |  |
| Pentominos | `playable` | 61.9 | 1556 | ✓ |  |  |
| PerfectFit | `playable` | 61.8 | 1554 | – | no-audio |  |
| Peuppy_10_GP2X | `playable` | 34.2 | 861 | ✓ |  |  |
| pez | `playable` | 61.5 | 1553 | – | no-audio |  |
| Phishy-0 | `playable` | 60.8 | 1555 | ✓ |  |  |
| physique | `playable` | 61.3 | 1550 | ✓ |  |  |
| Pika2x | `playable` | 60.6 | 574 | ✓ |  |  |
| pintor2x | `playable` | 96.8 | 2434 | ✓ |  |  |
| pixpang | `playable` | 55.5 | 1552 | ✓ |  |  |
| PocketSnes_SMRPG | `playable` | 118.8 | 2989 | – | no-audio |  |
| Poker2x | `playable` | 111.2 | 2814 | ✓ |  |  |
| Poker_Gp2Xv1.0 | `playable` | 115.5 | 2970 | ✓ |  |  |
| Pond2X | `playable` | 61.4 | 1544 | – | no-audio |  |
| Pong | `playable` | 61.6 | 1549 | – | no-audio |  |
| pong2player | `playable` | 58.2 | 119 | – | no-audio |  |
| pong2v060x | `playable` | 54.5 | 112 | – | no-audio |  |
| Pool Panic | `playable` | 60.9 | 1536 | ✓ |  |  |
| powder2x-112 | `playable` | 61.0 | 1556 | – | no-audio |  |
| powermanga-0.80 | `playable` | 56.7 | 1491 | ✓ |  |  |
| PowerSlide | `playable` | 60.6 | 1540 | ✓ |  |  |
| PPlane | `playable` | 60.4 | 1527 | ✓ |  |  |
| PPlane2.GP2X | `playable` | 60.4 | 1605 | ✓ |  |  |
| prboom-gp2x | `playable` | 60.7 | 1559 | – | no-audio |  |
| proj0-demo_01 | `playable` | 60.6 | 1542 | ✓ |  |  |
| protozoa v1.0 | `playable` | 61.1 | 1554 | ✓ |  |  |
| puckman_gp2x | `playable` | 117.0 | 2969 | ✓ |  |  |
| PulplifeWars | `playable` | 59.4 | 1551 | ✓ |  |  |
| puzzlelandgp2x | `playable` | 56.5 | 1555 | ✓ |  |  |
| qfg3-0 | `playable` | 59.7 | 1559 | ✓ |  |  |
| Quad | `playable` | 117.1 | 2959 | ✓ |  |  |
| quartz2_v1-50_gp2x | `playable` | 61.6 | 1555 | ✓ |  |  |
| Rabbit_vs_Flies_0.9 | `playable` | 61.6 | 1562 | ✓ |  |  |
| ramon atacks | `playable` | 61.6 | 1559 | ✓ |  |  |
| Release GP2X MST_RUNNERS | `playable` | 59.9 | 1551 | ✓ |  |  |
| retrovirus_1_1 | `playable` | 59.8 | 1518 | ✓ |  |  |
| RevoltOfTheBinaryCouriers GP2X | `playable` | 60.8 | 1532 | – | no-audio |  |
| reword_v0.5 | `playable` | 61.3 | 1561 | ✓ |  |  |
| rg_105 | `playable` | 58.2 | 1566 | ✓ |  |  |
| rg_ura_103 | `playable` | 58.6 | 1579 | ✓ |  |  |
| river | `playable` | 61.8 | 1553 | ✓ |  |  |
| RockRain | `playable` | 61.7 | 1557 | ✓ |  |  |
| rockrain2_exp-20100925 | `playable` | 61.1 | 1550 | ✓ |  |  |
| rookiehero_EXP.gp2x.v20120220 | `playable` | 60.6 | 1550 | ✓ |  |  |
| RoundEmUp-alpha3 | `playable` | 61.6 | 1561 | ✓ |  |  |
| rRootage_v1.0 | `playable` | 113.3 | 2928 | ✓ |  |  |
| rubidogp2x | `playable` | 59.6 | 1556 | ✓ |  |  |
| rubik | `playable` | 122.9 | 3107 | – | no-audio |  |
| ruckman_v1.03 | `playable` | 59.2 | 1552 | ✓ |  |  |
| Runner_GP2X | `playable` | 60.8 | 1555 | ✓ |  |  |
| s-tris2_v1-64_gp2x | `playable` | 61.7 | 1554 | ✓ |  |  |
| Sachunsung2_1 | `playable` | 78.7 | 169 | ✓ |  |  |
| sachunsungx | `playable` | 61.4 | 1542 | ✓ |  |  |
| santaMania | `playable` | 60.7 | 1555 | ✓ |  |  |
| ScorchedGPBeta2 | `playable` | 60.0 | 719 | ✓ |  |  |
| scummvm-0.11.1-gp2x | `playable` | 59.7 | 1550 | ✓ |  |  |
| scummvm-kor0.4.2cvs | `playable` | 60.8 | 1543 | – | no-audio |  |
| SdLame | `playable` | 61.2 | 1561 | ✓ |  |  |
| sdlmonkey_0.1 | `playable` | 61.6 | 1551 | – | no-audio |  |
| sdlscav_gp2x_0.2.0 | `playable` | 116.0 | 2923 | ✓ |  |  |
| Shangai v2 | `playable` | 73.0 | 150 | ✓ |  |  |
| ShanghaiX | `playable` | 61.6 | 1543 | ✓ |  |  |
| SheepDash | `playable` | 59.9 | 1562 | ✓ |  |  |
| Shippy84 | `playable` | 60.6 | 1558 | ✓ |  |  |
| Simon2X | `playable` | 60.4 | 1533 | – | no-audio |  |
| SimOniZ | `playable` | 112.1 | 2869 | ✓ |  |  |
| siv050 | `playable` | 58.4 | 1557 | ✓ |  |  |
| sleuth slots 2x | `playable` | 111.7 | 2911 | ✓ |  |  |
| SmallBall_GP | `playable` | 60.7 | 1553 | ✓ |  |  |
| snail runers | `playable` | 60.8 | 1557 | ✓ |  |  |
| snake2x-1.1 | `playable` | 61.0 | 1561 | – | no-audio |  |
| snowedin5_v1-00_gp2x | `playable` | 61.6 | 1552 | ✓ |  |  |
| SOD v1.1 | `playable` | 60.2 | 1554 | ✓ |  |  |
| sokobangp2x | `playable` | 54.4 | 1560 | ✓ |  |  |
| Solitaire2x-v1.4 | `playable` | 82.4 | 770 | – | no-audio |  |
| sopwith_camel_rc3 | `playable` | 44.1 | 96 | ✓ |  |  |
| sources_MEMORY2X | `playable` | 60.8 | 1555 | ✓ |  |  |
| sources_Yahtzee | `playable` | 60.7 | 1556 | ✓ |  |  |
| space squares | `playable` | 59.8 | 1522 | – | no-audio |  |
| space52_gp2x(oficial) | `playable` | 47.4 | 1263 | ✓ |  |  |
| space52_gp2x(open2x) | `playable` | 47.4 | 1267 | ✓ |  |  |
| space_varments_v1.0 | `playable` | 60.4 | 1555 | ✓ |  |  |
| SpaceSnake | `playable` | 116.4 | 2000 | ✓ |  |  |
| spacestorm | `playable` | 56.1 | 1413 | ✓ |  |  |
| spartak-chess_0.0.4_gp2x | `playable` | 61.5 | 1554 | – | no-audio |  |
| Sponge Blob Tennis | `playable` | 42.1 | 1559 | – | no-audio |  |
| spout | `playable` | 61.6 | 1550 | – | no-audio |  |
| sprint_race | `playable` | 61.0 | 1552 | ✓ |  |  |
| Sqcolony | `playable` | 63.7 | 1606 | – | no-audio |  |
| Sqdef 1.4 | `playable` | 60.8 | 1552 | ✓ |  |  |
| Squares-v051 | `playable` | 61.6 | 1563 | ✓ |  |  |
| Squaresliding | `playable` | 61.9 | 1556 | ✓ |  |  |
| StairwayToHeaven | `playable` | 52.7 | 109 | ✓ |  |  |
| starfighter-gp2x-0.01 | `playable` | 65.3 | 1125 | ✓ |  |  |
| StarTrucker | `playable` | 61.3 | 1553 | ✓ |  |  |
| stppc2x-v1.0 | `playable` | 42.1 | 1558 | ✓ |  |  |
| stransball2 | `playable` | 60.2 | 1535 | ✓ |  |  |
| street2x | `playable` | 56.5 | 1558 | ✓ |  |  |
| subhunt | `playable` | 61.3 | 1553 | ✓ |  |  |
| sudoku-v1.0 | `playable` | 61.5 | 1553 | – | no-audio |  |
| sudoku2x-0.5 | `playable` | 60.8 | 1536 | – | no-audio |  |
| SuperChickenFallDemo | `playable` | 61.6 | 1554 | ✓ |  |  |
| SuperPaf_v1.0 | `playable` | 60.4 | 1552 | ✓ |  |  |
| superpang | `playable` | 60.5 | 1552 | ✓ |  |  |
| SuperPixelJumper v1.1 for GP2X | `playable` | 60.9 | 1535 | ✓ |  |  |
| SuperSonicSpeed | `playable` | 61.3 | 1552 | ✓ |  |  |
| supertux-0.1.3-gp2x-v4 | `playable` | 53.9 | 1492 | ✓ |  |  |
| survival | `playable` | 60.7 | 1556 | ✓ |  |  |
| symbolica-0.8 | `playable` | 60.4 | 1543 | ✓ |  |  |
| tail-tale | `playable` | 61.6 | 1554 | ✓ |  |  |
| Tangle | `playable` | 62.1 | 1560 | – | no-audio |  |
| tecnoballz-0.91-gp2x | `playable` | 56.6 | 1473 | ✓ |  |  |
| tetwins | `playable` | 53.2 | 109 | ✓ |  |  |
| ThreeTs_Game | `playable` | 54.7 | 118 | ✓ |  |  |
| Thruster_GP2X | `playable` | 61.5 | 1558 | ✓ |  |  |
| tikka_dungeons_demo_1 | `playable` | 61.0 | 1535 | ✓ |  |  |
| tilematch-0.6 | `playable` | 96.7 | 2448 | ✓ |  |  |
| tileworld2x | `playable` | 57.1 | 1512 | ✓ |  |  |
| tilt | `playable` | 60.8 | 1551 | ✓ |  |  |
| TimeFrack2D for GP2X | `playable` | 46.5 | 96 | – | no-audio |  |
| TouchGames | `playable` | 117.2 | 2970 | ✓ |  |  |
| tower | `playable` | 109.9 | 2775 | – | no-audio |  |
| towertopplergp2x | `playable` | 59.2 | 1552 | ✓ |  |  |
| TRAINS | `playable` | 61.1 | 1551 | ✓ |  |  |
| Trap75 | `playable` | 61.8 | 1555 | ✓ |  |  |
| treev060 | `playable` | 60.4 | 1556 | ✓ |  |  |
| ttxbeta170706b | `playable` | 58.7 | 1552 | – | no-audio |  |
| TUcS.app(V0.7.0 - GP2X) | `playable` | 31.3 | 796 | ✓ |  |  |
| Txishos (Gp2x F-200) | `playable` | 50.7 | 1305 | ✓ |  |  |
| Unicolor | `playable` | 61.0 | 1555 | ✓ |  |  |
| uqm2x_release_1.1 | `playable` | 66.5 | 1700 | ✓ |  |  |
| UQMgp2x-0.5.0_with_content | `playable` | 66.6 | 1691 | ✓ |  |  |
| vectoroids-2x | `playable` | 61.2 | 1553 | ✓ |  |  |
| VekDemo2 | `playable` | 117.4 | 2961 | ✓ |  |  |
| Vektar | `playable` | 117.7 | 2967 | ✓ |  |  |
| vektar-free | `playable` | 29.9 | 758 | ✓ |  |  |
| vektarpack_v1 | `playable` | 90.1 | 2288 | ✓ |  |  |
| Ventifact | `playable` | 60.4 | 1558 | ✓ |  |  |
| vexed-gp2x-10 | `playable` | 60.8 | 1537 | ✓ |  |  |
| vexedb1 | `playable` | 61.7 | 1559 | – | no-audio |  |
| vorton-b4 | `playable` | 59.2 | 1553 | ✓ |  |  |
| vwars | `playable` | 60.1 | 1552 | ✓ |  |  |
| waffle2x | `playable` | 44.7 | 91 | – | no-audio |  |
| war_and_warriorgp2x | `playable` | 61.6 | 1552 | ✓ |  |  |
| warcraft | `playable` | 60.6 | 1556 | ✓ |  |  |
| warehouse_panic_v1.1_gp2x | `playable` | 41.7 | 593 | ✓ |  |  |
| waternetgp2x | `playable` | 57.8 | 1564 | ✓ |  |  |
| wehaveballs | `playable` | 61.1 | 1535 | ✓ |  |  |
| whacky | `playable` | 61.3 | 1548 | ✓ |  |  |
| WindAndWater_teaser_110 | `playable` | 61.1 | 1539 | ✓ |  |  |
| Winter_Jumper | `playable` | 61.4 | 1555 | – | no-audio |  |
| wire3d | `playable` | 58.3 | 1526 | – | no-audio |  |
| Wizznic_2x_07alpha2 | `playable` | 58.7 | 1552 | ✓ |  |  |
| wizznic_gp2x-0.9.9 | `playable` | 59.1 | 1554 | ✓ |  |  |
| wnw | `playable` | 60.7 | 1540 | ✓ |  |  |
| wolfdx | `playable` | 61.7 | 1558 | ✓ |  |  |
| xenitris_demo | `playable` | 62.3 | 1568 | ✓ |  |  |
| xigon-X-gp2x-V1 | `playable` | 61.9 | 1560 | ✓ |  |  |
| Xpired2x 1.0 beta | `playable` | 61.1 | 1553 | ✓ |  |  |
| xRick | `playable` | 61.1 | 1556 | ✓ |  |  |
| yahtzee-v21 | `playable` | 61.8 | 1561 | ✓ |  |  |
| Zelda_roth_US_gp2x | `playable` | 55.0 | 515 | ✓ |  |  |
| znax | `playable` | 59.2 | 1565 | ✓ |  |  |
| Znumbers | `playable` | 79.1 | 164 | ✓ |  |  |
| Zoids Quest2X-0.0.1-2 | `playable` | 61.0 | 1566 | ✓ |  |  |
| zoltan 2x | `playable` | 60.4 | 1554 | ✓ |  |  |
| zombiesorbet_v1.0_gp2x | `playable` | 61.0 | 1568 | ✓ |  |  |
| zooov11 | `playable` | 32.1 | 810 | ✓ |  |  |
| ztunnel-0 | `playable` | 60.5 | 1532 | ✓ |  |  |

### Wiz (147 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| alephone-wiz | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/ |
| chroma 1.01 v1 | `incompatible` | 0.2 | 1 | – | no-frames |  |
| CloneKeen2X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| EpicRocks_Wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| hheretic | `incompatible` | 0.0 | 0 | – | no-frames |  |
| hhexen | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ioquake2 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| pgw | `incompatible` | 9.8 | 19 | ✓ | no-frames |  |
| quake1-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| quake_0.03 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| RailroadRampage_1.2_Wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| rott | `incompatible` | 0.0 | 0 | – | no-frames |  |
| SmallBall_Wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| srb2 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| wolf4sdl_wiz_svn | `incompatible` | 0.0 | 0 | – | no-frames |  |
| albion-v1.0.1-wiz | `black` | 24.5 | 44 | ✓ | black-screen |  |
| Balloonacy_wiz_wip | `black` | 112.4 | 2881 | ✓ | black-screen |  |
| CartoonWiz | `black` | 116.0 | 2923 | ✓ | black-screen |  |
| ColonyConflict_V1.1_B6 | `black` | 107.2 | 2879 | ✓ | black-screen |  |
| DungeonRunner | `black` | 110.3 | 2881 | ✓ | black-screen |  |
| DuoWIZ_Pong | `black` | 111.2 | 2879 | ✓ | black-screen |  |
| eduke32 | `black` | 12.5 | 17 | – | black-screen |  |
| freecell2x | `black` | 107.1 | 2877 | ✓ | black-screen |  |
| March of the mini tux(wiz version) | `black` | 115.2 | 2920 | ✓ | black-screen |  |
| nazcadreams | `black` | 24.6 | 50 | ✓ | black-screen |  |
| nazcarunners | `black` | 28.9 | 63 | ✓ | black-screen |  |
| Nazcasphere | `black` | 32.3 | 77 | ✓ | black-screen |  |
| openjazz-wiz | `black` | 16.3 | 23 | ✓ | black-screen |  |
| opentyrian | `black` | 12.8 | 9 | – | black-screen |  |
| paraballwiz | `black` | 3.9 | 7 | – | black-screen |  |
| PPlane2.WIZ | `black` | 105.9 | 2870 | ✓ | black-screen |  |
| SimOniZ | `black` | 112.4 | 2876 | ✓ | black-screen |  |
| tetwizdownload | `black` | 116.1 | 2922 | ✓ | black-screen |  |
| TUcS.app(V0.7.0 - Wiz) | `black` | 110.7 | 2872 | ✓ | black-screen |  |
| warcraft-beta3-wiz | `black` | 26.8 | 46 | ✓ | black-screen |  |
| wiz-car-binary_090818a | `black` | 60.7 | 1528 | ✓ | black-screen |  |
| Wiz_Blox | `black` | 114.1 | 2919 | ✓ | black-screen |  |
| wiz_drench | `black` | 113.1 | 2875 | ✓ | black-screen |  |
| Wiz_Propis_Demo | `black` | 30.0 | 53 | ✓ | black-screen |  |
| WIZ_S4S | `black` | 113.5 | 2871 | ✓ | black-screen |  |
| WizSticks | `black` | 16.5 | 59 | ✓ | black-screen |  |
| xcom1-v1.0.2-wiz | `black` | 53.2 | 140 | ✓ | black-screen |  |
| xcom2-v1.0.1-wiz | `black` | 116.0 | 2980 | ✓ | black-screen |  |
| blingo | `ingame` | 58.0 | 1504 | ✓ | garbled-visuals | pixel-to-pixel noise of 100, far above what dithered artwork reaches; the frame looks like |
| Ruckman-Wiz | `ingame` | 57.2 | 1507 | ✓ | garbled-visuals | pixel-to-pixel noise of 102, far above what dithered artwork reaches; the frame looks like |
| [DEMO] Wiztern | `playable` | 54.8 | 954 | ✓ |  |  |
| abuse-wiz | `playable` | 56.0 | 1497 | ✓ |  |  |
| AdamantArmorAffectionWiz | `playable` | 60.0 | 1532 | ✓ |  |  |
| airstrike-wiz-1.01 | `playable` | 60.1 | 1526 | ✓ |  |  |
| alexsfalldown | `playable` | 61.6 | 1547 | ✓ |  |  |
| altitude | `playable` | 57.5 | 1500 | ✓ |  |  |
| Animatch Wiz | `playable` | 57.5 | 1510 | ✓ |  |  |
| Art Shot Wiz | `playable` | 54.7 | 1416 | ✓ |  |  |
| Asteroids | `playable` | 59.0 | 1509 | ✓ |  |  |
| battlejewels-wiz-public001demo | `playable` | 60.5 | 1530 | ✓ |  |  |
| beat2x-wiz | `playable` | 60.7 | 1534 | ✓ |  |  |
| Biological Defend | `playable` | 57.9 | 1494 | ✓ |  |  |
| BitDEFENSE | `playable` | 48.4 | 1303 | – | no-audio |  |
| BlastRiot122Wiz | `playable` | 61.1 | 1548 | ✓ |  |  |
| Blix2x | `playable` | 61.6 | 1548 | ✓ |  |  |
| Boomshine2x_1.12_wiz | `playable` | 58.3 | 1503 | ✓ |  |  |
| BubbleTrainWiz_5-20-09 | `playable` | 57.2 | 1511 | ✓ |  |  |
| BugwarsSE | `playable` | 59.2 | 1506 | ✓ |  |  |
| Camelot Warriors | `playable` | 57.4 | 1496 | ✓ |  |  |
| CDogs-wiz | `playable` | 57.9 | 1469 | ✓ |  |  |
| cgenius-wiz | `playable` | 39.6 | 1465 | ✓ |  |  |
| Dastardly_Dungeon | `playable` | 59.0 | 1509 | ✓ |  |  |
| Dd2x | `playable` | 56.6 | 254 | ✓ |  |  |
| deicide3_eng | `playable` | 58.6 | 407 | ✓ |  |  |
| Demons World | `playable` | 61.7 | 1550 | ✓ |  |  |
| EpicFreeFall_Wiz | `playable` | 50.9 | 1315 | ✓ |  |  |
| epiphany | `playable` | 58.7 | 1512 | ✓ |  |  |
| Geca Blaster 2 (Gp2x Wiz) | `playable` | 53.1 | 1365 | ✓ |  |  |
| Ghostpix | `playable` | 55.1 | 628 | ✓ |  |  |
| gnurobbo_0.65_wiz | `playable` | 55.7 | 1428 | ✓ |  |  |
| gobble | `playable` | 59.5 | 1508 | – | no-audio |  |
| gr-v1001-wiz | `playable` | 57.8 | 1499 | ✓ |  |  |
| herknights | `playable` | 58.6 | 1530 | ✓ |  |  |
| hexen2 | `playable` | 56.9 | 1463 | ✓ |  |  |
| kuklomenos | `playable` | 59.1 | 1511 | ✓ |  |  |
| malvado | `playable` | 57.7 | 1499 | ✓ |  |  |
| midway | `playable` | 71.2 | 1811 | ✓ |  |  |
| Minigolf | `playable` | 58.4 | 1496 | – | no-audio |  |
| minos-gp2x-wiz | `playable` | 59.6 | 1509 | ✓ |  |  |
| Monster2-1.0-wiz | `playable` | 52.5 | 1533 | ✓ |  |  |
| mush_gp2x | `playable` | 44.1 | 1135 | ✓ |  |  |
| mush_gp2x-0 | `playable` | 34.4 | 912 | ✓ |  |  |
| Myriad | `playable` | 57.5 | 1512 | ✓ |  |  |
| nethack-wiz | `playable` | 61.5 | 1550 | – | no-audio |  |
| NewSuperPang05 | `playable` | 59.3 | 1504 | ✓ |  |  |
| noiz2sa_wiz | `playable` | 59.7 | 1528 | ✓ |  |  |
| openggs | `playable` | 59.2 | 1513 | ✓ |  |  |
| Out Zone | `playable` | 61.7 | 1549 | ✓ |  |  |
| paf | `playable` | 59.2 | 1503 | ✓ |  |  |
| Pentominos | `playable` | 61.6 | 1547 | ✓ |  |  |
| PEZ | `playable` | 59.4 | 1504 | – | no-audio |  |
| Pharaoh | `playable` | 51.1 | 107 | ✓ |  |  |
| PhishyWiz | `playable` | 58.6 | 1508 | ✓ |  |  |
| Powder2X_wiz_114_v01 | `playable` | 57.3 | 1461 | – | no-audio |  |
| PPlane | `playable` | 52.6 | 1340 | ✓ |  |  |
| prboom-wiz | `playable` | 59.1 | 1512 | – | no-audio |  |
| preggo_Wiz | `playable` | 57.9 | 1507 | ✓ |  |  |
| Propis | `playable` | 50.0 | 1262 | ✓ |  |  |
| protozoa | `playable` | 58.5 | 1505 | ✓ |  |  |
| PuzzleDevilWizDemo | `playable` | 56.1 | 1421 | ✓ |  |  |
| Rezerwar | `playable` | 52.8 | 607 | ✓ |  |  |
| roadfighter | `playable` | 60.3 | 1534 | ✓ |  |  |
| rockrain-gp2x-wiz | `playable` | 61.3 | 1543 | ✓ |  |  |
| Sachunsung2 | `playable` | 49.0 | 103 | ✓ |  |  |
| scummvm-1.2.0-gp2xwiz | `playable` | 54.9 | 1472 | ✓ |  |  |
| Shanghai2 | `playable` | 50.6 | 114 | ✓ |  |  |
| Shock Troopers Base Defense | `playable` | 55.2 | 1495 | ✓ |  |  |
| Skull (Windows, Linux & Gp2x Wiz) | `playable` | 48.1 | 1289 | ✓ |  |  |
| sleuthslots | `playable` | 56.6 | 1497 | ✓ |  |  |
| smw_1.7 | `playable` | 54.1 | 1458 | ✓ |  |  |
| Snow Bros 2 | `playable` | 61.6 | 1547 | ✓ |  |  |
| SOD_Wiz | `playable` | 58.0 | 1507 | ✓ |  |  |
| Sopwith | `playable` | 61.0 | 1539 | ✓ |  |  |
| Space Varments | `playable` | 58.2 | 1506 | ✓ |  |  |
| spout | `playable` | 60.6 | 1534 | – | no-audio |  |
| Sqdef_Wiz_14A | `playable` | 58.0 | 1485 | ✓ |  |  |
| Sudoku2X | `playable` | 60.6 | 1535 | – | no-audio |  |
| SudoQ | `playable` | 27.9 | 263 | ✓ |  |  |
| supertux-wiz | `playable` | 54.3 | 1484 | ✓ |  |  |
| Tail Tale | `playable` | 61.2 | 1541 | ✓ |  |  |
| The Minigame Project | `playable` | 55.5 | 1505 | ✓ |  |  |
| tilt | `playable` | 59.7 | 1512 | ✓ |  |  |
| Trap75 | `playable` | 61.2 | 1538 | ✓ |  |  |
| tricorder | `playable` | 59.2 | 1508 | ✓ |  |  |
| Twin Cobra | `playable` | 61.8 | 1553 | ✓ |  |  |
| uqm2x_release.1.1 | `playable` | 69.4 | 1776 | ✓ |  |  |
| wizchess-v1.1.0-bin | `playable` | 61.0 | 1542 | – | no-audio |  |
| wizchess-v1.2.0-bin | `playable` | 61.2 | 1545 | – | no-audio |  |
| WizFrontier v0.1 | `playable` | 61.0 | 1611 | ✓ |  |  |
| wizgo-v1.1.0-bin | `playable` | 61.0 | 1544 | – | no-audio |  |
| WizGolf | `playable` | 60.3 | 1532 | – | no-audio |  |
| wizmancala-v1.1.2-bin | `playable` | 60.8 | 1543 | – | no-audio |  |
| wizpong | `playable` | 58.9 | 1496 | – | no-audio |  |
| wizznic-0.9.9-wiz | `playable` | 57.8 | 1501 | ✓ |  |  |
| wnw_demo | `playable` | 61.0 | 1539 | ✓ |  |  |
| Worship Vector | `playable` | 60.7 | 1527 | ✓ |  |  |
| WWII | `playable` | 56.7 | 1495 | ✓ |  |  |
| xpiredwiz.eng.101 | `playable` | 60.5 | 1534 | ✓ |  |  |
| xRick | `playable` | 60.8 | 1546 | ✓ |  |  |
| Zero Wing | `playable` | 61.6 | 1547 | ✓ |  |  |
| znumbers | `playable` | 52.2 | 110 | ✓ |  |  |
| Zoltan | `playable` | 58.1 | 1508 | ✓ |  |  |

### Caanoo (194 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| aggressivepong-pre21.1-gph-uni | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ArtShotCaanoo | `incompatible` | 0.2 | 1 | – | no-frames |  |
| audiorace-v1.5-can | `incompatible` | 0.0 | 0 | – | no-frames |  |
| BermudaS_caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| caanoo-tyrian-v1.1-bin | `incompatible` | 0.0 | 0 | ✓ | missing-game-data |  |
| Echo V.1.3.2 (Caanoo) | `incompatible` | 0.0 | 0 | – | no-frames |  |
| freedroid_Caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| fungp.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X Caanoo/fungp.zip' (exit 32512) |
| liar.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X Caanoo/liar.zip' (exit 32512) |
| Liquid Counter.caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| openjazz-caanoo | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| openttd_c | `incompatible` | 0.0 | 0 | – | no-frames |  |
| quake1-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| quake1_build-20111024 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/accel |
| quake2-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| RailroadRampage_1.2_Caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| reminiscence-v0.1.10-bin | `incompatible` | 0.0 | 0 | – | no-frames |  |
| rotate | `incompatible` | 0.6 | 1 | – | no-frames |  |
| runner-Caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| sdllopan_v4-all | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| sdlquake_build-20111113-0 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| supertux | `incompatible` | 1.4 | 1 | ✓ | no-frames |  |
| tmw_v1.0.0-beta-2_caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| warcraft-beta3-caanoo | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| zlocada-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zsxd | `incompatible` | 0.0 | 0 | – | no-frames |  |
| arcadevol1 | `black` | 4.7 | 5 | ✓ | black-screen |  |
| BubbleTrain | `black` | 0.9 | 2 | ✓ | black-screen |  |
| Drench | `black` | 14.5 | 39 | ✓ | black-screen |  |
| Flappynerd_Caanoo | `black` | 15.7 | 40 | ✓ | black-screen |  |
| JUMPNRUN | `black` | 55.5 | 1547 | ✓ | black-screen |  |
| kenlab-caanoo | `black` | 43.5 | 423 | – | black-screen |  |
| MNV_Caanoo_Release1 | `black` | 14.3 | 36 | – | black-screen |  |
| powermanga-0.80 | `black` | 47.0 | 1227 | ✓ | black-screen |  |
| SantaMania | `black` | 18.1 | 50 | ✓ | black-screen |  |
| xcom1-v1.0.2-caanoo | `black` | 32.2 | 34 | ✓ | black-screen |  |
| xcom2-v1.0.1-caanoo | `black` | 58.6 | 1491 | ✓ | black-screen |  |
| aimcaanoo | `ingame` | 49.9 | 1280 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 160px; left and right halves are near- |
| apocalypso Caanoo | `ingame` | 17.3 | 458 | ✓ | low-fps |  |
| Arcadevol3 | `ingame` | 58.8 | 1554 | – | flat-fill |  |
| BubblePop (Caanoo) | `ingame` | 18.0 | 464 | ✓ | low-fps |  |
| caanoo-biniax2-v1.30-bin | `ingame` | 17.0 | 429 | ✓ | low-fps |  |
| can-zomb_3 | `ingame` | 18.1 | 535 | ✓ | low-fps |  |
| chexquest-caanoo | `ingame` | 17.6 | 452 | ✓ | low-fps |  |
| Coral Sea (Caanoo - Bennu) | `ingame` | 17.5 | 455 | ✓ | low-fps |  |
| Deadly Eye (Caanoo) | `ingame` | 18.8 | 484 | ✓ | low-fps |  |
| DefendorX_C | `ingame` | 17.3 | 453 | ✓ | low-fps |  |
| deminor | `ingame` | 11.7 | 27 | – | low-fps |  |
| EEEEK! EEEEEK! HOOOOOOK!!! | `ingame` | 24.0 | 662 | ✓ | garbled-visuals | renders at 640x480 instead of 320x240 |
| EpicFreeFall | `ingame` | 18.5 | 481 | ✓ | low-fps |  |
| EpicFreeFall Caanoo | `ingame` | 18.9 | 489 | ✓ | low-fps |  |
| Geca Blaster 2 (Caanoo) | `ingame` | 13.6 | 350 | ✓ | low-fps |  |
| gnp_104 | `ingame` | 50.3 | 1351 | ✓ | flat-fill |  |
| gnuRobbo | `ingame` | 15.6 | 211 | ✓ | low-fps |  |
| gr-v1001-caanoo | `ingame` | 9.8 | 255 | ✓ | low-fps |  |
| Hamster's Escape 3D (Caanoo) | `ingame` | 7.4 | 69 | ✓ | low-fps |  |
| HamstersEscape (Caanoo) | `ingame` | 8.6 | 80 | ✓ | low-fps |  |
| Hardcore Fight (Caanoo) | `ingame` | 6.6 | 62 | ✓ | low-fps |  |
| jump_n_blob_caanoo | `ingame` | 3.1 | 82 | ✓ | low-fps |  |
| laserchess_c | `ingame` | 19.5 | 40 | – | low-fps |  |
| Liar | `ingame` | 11.9 | 83 | ✓ | low-fps |  |
| llcpcls-caanoo | `ingame` | 16.8 | 35 | ✓ | low-fps |  |
| MasteriesRunners (Caanoo) | `ingame` | 10.3 | 267 | ✓ | low-fps |  |
| meritous | `ingame` | 65.1 | 1656 | ✓ | flat-fill |  |
| Metal Slug Zombies | `ingame` | 30.7 | 837 | ✓ | garbled-visuals | renders at 640x480 instead of 320x240 |
| mtknights | `ingame` | 31.2 | 786 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 128px |
| nlove_0.6.2_(beta)_caanoo | `ingame` | 17.1 | 42 | – | low-fps |  |
| noiz2sa_caanoo | `ingame` | 40.1 | 1024 | ✓ | flat-fill |  |
| OperationFenix (Caanoo) | `ingame` | 8.8 | 242 | ✓ | low-fps |  |
| PantaVsDragon (Caanoo) | `ingame` | 16.3 | 420 | ✓ | low-fps |  |
| Pharaoh | `ingame` | 12.1 | 25 | ✓ | low-fps |  |
| profanation_Caanoo | `ingame` | 19.1 | 42 | ✓ | low-fps |  |
| Protect&rescue | `ingame` | 13.5 | 371 | ✓ | low-fps |  |
| purito_cycling_1.5_Caanoo | `ingame` | 3.4 | 88 | ✓ | low-fps |  |
| pushover-v0.2-bin | `ingame` | 17.4 | 441 | ✓ | low-fps |  |
| rg_ura_103 | `ingame` | 54.3 | 1425 | ✓ | flat-fill |  |
| sbtime_caanoo | `ingame` | 5.8 | 159 | ✓ | low-fps |  |
| Skull (Caanoo) | `ingame` | 15.4 | 397 | ✓ | flat-fill |  |
| smallball | `ingame` | 18.3 | 474 | ✓ | low-fps |  |
| smallball-Caanoo | `ingame` | 18.3 | 471 | ✓ | low-fps |  |
| SnailRace_C | `ingame` | 18.5 | 479 | – | low-fps |  |
| SORRv5_Caanoo | `ingame` | 0.1 | 2 | ✓ | low-fps |  |
| the solitarie | `ingame` | 15.5 | 412 | ✓ | low-fps |  |
| Txishos (Caanoo) | `ingame` | 17.0 | 437 | ✓ | low-fps |  |
| xpiredcan.eng.101 | `ingame` | 0.1 | 3 | ✓ | low-fps |  |
| Zverealm-Caanoo | `ingame` | 8.9 | 275 | ✓ | low-fps |  |
| 20110831 - Bomber Run Redux | `playable` | 35.4 | 952 | – | no-audio |  |
| aaa | `playable` | 59.7 | 1508 | ✓ |  |  |
| aaaa | `playable` | 54.0 | 1369 | ✓ |  |  |
| Abbaye_caanoo | `playable` | 44.2 | 424 | ✓ |  |  |
| Abbaye_caanoo_v3 | `playable` | 46.9 | 447 | ✓ |  |  |
| ADVENTURE | `playable` | 54.8 | 1502 | ✓ |  |  |
| Amoebax | `playable` | 57.3 | 1495 | ✓ |  |  |
| animatch | `playable` | 20.4 | 545 | ✓ |  |  |
| aquaVenture | `playable` | 46.2 | 468 | ✓ |  |  |
| Arcadevol2 | `playable` | 59.5 | 1554 | ✓ |  |  |
| B'lox! | `playable` | 36.2 | 355 | ✓ |  |  |
| Balloonacy | `playable` | 41.4 | 406 | ✓ |  |  |
| balls12_caanoo_bin | `playable` | 36.8 | 75 | – | no-audio |  |
| battlejewels-105-caanoo-beta | `playable` | 53.7 | 1358 | ✓ |  |  |
| Blackjack21v1.1 | `playable` | 60.1 | 1515 | – | no-audio |  |
| Blingo | `playable` | 59.7 | 1554 | ✓ |  |  |
| Blitz | `playable` | 44.2 | 433 | ✓ |  |  |
| Blix2x | `playable` | 61.8 | 1557 | ✓ |  |  |
| caanoo-12swap-v1.0-bin | `playable` | 38.4 | 970 | ✓ |  |  |
| caanoo-chess-v1.1.0-bin | `playable` | 44.7 | 1132 | – | no-audio |  |
| caanoo-gnurobbo-0.68 | `playable` | 31.9 | 428 | ✓ |  |  |
| caanoo-go-v1.1.0-bin | `playable` | 44.7 | 1128 | – | no-audio |  |
| caanoo-mancala-v1.1.0-bin | `playable` | 43.6 | 1101 | – | no-audio |  |
| cat_trap | `playable` | 42.1 | 412 | ✓ |  |  |
| cavestory | `playable` | 58.0 | 1552 | ✓ |  |  |
| ccrg-caanoo | `playable` | 45.8 | 198 | ✓ |  |  |
| cgenius-caanoo | `playable` | 27.2 | 709 | ✓ |  |  |
| cllwrth | `playable` | 24.7 | 624 | ✓ |  |  |
| cooldowncaanoo | `playable` | 764.4 | 19835 | ✓ |  |  |
| daff_s_adventure_2_caanoo | `playable` | 26.2 | 673 | ✓ |  |  |
| deadlyc | `playable` | 55.8 | 1408 | ✓ |  |  |
| DealorNoDeal | `playable` | 60.7 | 1557 | ✓ |  |  |
| demons | `playable` | 118.5 | 2982 | ✓ |  |  |
| dynamate_c | `playable` | 27.8 | 732 | ✓ |  |  |
| echo_caanoo | `playable` | 23.7 | 677 | ✓ |  |  |
| Firewhip-Caanoo | `playable` | 20.0 | 541 | ✓ | no-audio |  |
| fleshchasmer | `playable` | 53.9 | 1373 | ✓ |  |  |
| freeheroes2_c | `playable` | 40.5 | 83 | ✓ |  |  |
| fshark | `playable` | 85.7 | 2152 | ✓ |  |  |
| Fywod_caanoo | `playable` | 54.9 | 1392 | ✓ |  |  |
| Geek_em_up_CAANOO | `playable` | 26.9 | 281 | ✓ |  |  |
| getstar | `playable` | 85.5 | 2150 | ✓ |  |  |
| gravityforcev2 | `playable` | 45.8 | 1159 | ✓ |  |  |
| Guru Logic | `playable` | 44.0 | 433 | ✓ |  |  |
| hellfire | `playable` | 118.6 | 2979 | ✓ |  |  |
| Hero_The_Realm-DEMO | `playable` | 21.3 | 550 | ✓ |  |  |
| HeroTheRealm_DEMOv2 | `playable` | 21.1 | 545 | ✓ |  |  |
| hexahop_1.0 | `playable` | 53.3 | 1342 | – | no-audio |  |
| Humos-Caanoo | `playable` | 24.5 | 685 | ✓ |  |  |
| instead-1.6.1-caanoo | `playable` | 45.1 | 1174 | ✓ |  |  |
| jumpToTheMoon_c | `playable` | 24.6 | 232 | ✓ |  |  |
| ketm | `playable` | 38.2 | 1288 | – | no-audio |  |
| knight | `playable` | 61.5 | 1548 | ✓ |  |  |
| KOF (Ver. 5f) (Caanoo) | `playable` | 27.6 | 811 | ✓ |  |  |
| lmission_0.5 | `playable` | 58.5 | 1469 | ✓ |  |  |
| MISC | `playable` | 59.0 | 1571 | – | no-audio |  |
| Mission_faileD 1.2 [Caanoo] | `playable` | 33.7 | 941 | ✓ |  |  |
| monster | `playable` | 23.4 | 622 | ✓ |  |  |
| next_element | `playable` | 61.6 | 1555 | ✓ |  |  |
| nuclearchess | `playable` | 220.4 | 5562 | – | no-audio |  |
| outzone | `playable` | 85.7 | 2154 | ✓ |  |  |
| pang | `playable` | 59.0 | 1508 | ✓ |  |  |
| pengupop | `playable` | 37.6 | 169 | ✓ |  |  |
| powder | `playable` | 52.9 | 1349 | – | no-audio |  |
| prboom-caanoo | `playable` | 60.3 | 1536 | – | no-audio |  |
| propis | `playable` | 49.4 | 1245 | ✓ |  |  |
| puzsion | `playable` | 27.1 | 765 | ✓ |  |  |
| PUZZLEBOARDS | `playable` | 108.0 | 2967 | ✓ |  |  |
| RACING | `playable` | 59.1 | 1556 | ✓ |  |  |
| rhythmosplay_1.1.12 | `playable` | 51.0 | 1304 | ✓ |  |  |
| Sachunsung2 | `playable` | 26.6 | 54 | ✓ |  |  |
| sbt | `playable` | 31.4 | 863 | ✓ |  |  |
| Shanghai2 | `playable` | 48.3 | 100 | ✓ |  |  |
| SHOOTERS | `playable` | 76.2 | 2096 | ✓ |  |  |
| SimOniZ | `playable` | 55.0 | 1407 | ✓ |  |  |
| Sitwell (Caanoo) | `playable` | 35.9 | 923 | ✓ |  |  |
| Slap | `playable` | 85.8 | 2156 | ✓ |  |  |
| smw_1.7 | `playable` | 32.0 | 857 | ✓ |  |  |
| snowbros | `playable` | 118.6 | 2977 | ✓ |  |  |
| snowbros2 | `playable` | 118.4 | 2973 | ✓ |  |  |
| SOD(r181) | `playable` | 23.4 | 622 | ✓ |  |  |
| space52_caanoo | `playable` | 33.4 | 898 | ✓ |  |  |
| SPORTS | `playable` | 59.5 | 1554 | ✓ |  |  |
| sqrxz-v0996-caanoo | `playable` | 52.9 | 1351 | ✓ |  |  |
| sqrxz2-v0.80-caanoo | `playable` | 58.2 | 1480 | ✓ |  |  |
| stppc-caanoo-29-11-2010 | `playable` | 35.3 | 346 | ✓ |  |  |
| STRATEGY | `playable` | 58.7 | 1552 | ✓ |  |  |
| tailtale4c | `playable` | 61.6 | 1551 | ✓ |  |  |
| Tigerhell | `playable` | 85.6 | 2154 | ✓ |  |  |
| Tile | `playable` | 54.7 | 1527 | ✓ |  |  |
| tlosaf_v12-caanoo | `playable` | 61.3 | 1545 | – | no-audio |  |
| tong-caanoo | `playable` | 57.3 | 1496 | ✓ |  |  |
| Trap75 | `playable` | 53.0 | 1335 | ✓ |  |  |
| Truxton | `playable` | 85.8 | 2154 | ✓ |  |  |
| truxton2 | `playable` | 85.7 | 2150 | ✓ |  |  |
| twincobr | `playable` | 85.6 | 2153 | ✓ |  |  |
| twinhawk | `playable` | 85.7 | 2153 | ✓ |  |  |
| Vigo | `playable` | 55.2 | 1427 | – | no-audio |  |
| Wardner | `playable` | 118.7 | 2985 | ✓ |  |  |
| warehouse_panic_v1.1_caanoo | `playable` | 31.5 | 428 | ✓ |  |  |
| WindandWater | `playable` | 60.3 | 1525 | ✓ |  |  |
| Wizznic 0.9.2- preview | `playable` | 30.1 | 812 | ✓ |  |  |
| wolf4sdl-caanoo | `playable` | 60.1 | 1538 | – | no-audio |  |
| wvector | `playable` | 43.6 | 1096 | ✓ |  |  |
| zelda-roth-olb-3t_caanoo | `playable` | 20.0 | 542 | ✓ |  |  |
| zerowing | `playable` | 118.6 | 2978 | ✓ |  |  |
| zombiesorbet_v1.0_caanoo | `playable` | 23.0 | 590 | ✓ |  |  |
| zomg-Caanoo | `playable` | 21.1 | 564 | ✓ |  |  |
