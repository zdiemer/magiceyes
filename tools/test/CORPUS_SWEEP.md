# magiceyes compatibility sweep

Every GP2X / Wiz / Caanoo title on the corpus share, booted headlessly through the native engine (`bin/me_unicorn`) and scored from the shm framebuffer + the structured run report. Regenerate with `tools/test/compat_report.py`.


## Summary

| Platform | Titles | Playable | Ingame | Black | Incompatible | Crashed |
|---|--:|--:|--:|--:|--:|--:|
| GP2X | 631 | 459 | 24 | 59 | 88 | 1 |
| Wiz | 147 | 94 | 0 | 38 | 15 | 0 |
| Caanoo | 194 | 99 | 48 | 19 | 28 | 0 |
| **All** | **972** | **652** | **72** | **116** | **131** | **1** |

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
| **Boots but renders only black** (`black-screen`) | 116 | Caanoo, GP2X, Wiz | n/a |
| **Renders at speed but no audio** (`no-audio`) | 104 | Caanoo, GP2X, Wiz | n/a |
| **Never rendered a frame (cause unknown)** (`no-frames`) | 69 | Caanoo, GP2X, Wiz | n/a |
| **Not a 32-bit ARM ELF** (`not-arm-elf`) | 45 | GP2X | n/a |
| **Renders but below 20 fps** (`low-fps`) | 45 | Caanoo, GP2X | n/a |
| **Draws only a flat colour** (`flat-fill`) | 18 | Caanoo, GP2X | n/a |
| **Renders, but the picture is wrong** (`garbled-visuals`) | 8 | Caanoo, GP2X | n/a |
| **Game data files are missing from the dump** (`missing-game-data`) | 6 | Caanoo, GP2X | n/a |
| **Archive extraction failed** (`archive-failed`) | 5 | Caanoo, GP2X | n/a |
| **Unknown /dev node** (`unknown-device`) | 4 | Caanoo, GP2X, Wiz | `/dev/accel` ×1, `/dev/cx25874` ×1, `/dev/graphics/fb0` ×1, `/dev/` ×1 |
| **Unimplemented syscall** (`unimplemented-syscall`) | 2 | GP2X | `9437188` ×1, `11711` ×1 |
| **Host fault (guest crashed the engine)** (`host-fault`) | 1 | GP2X | n/a |
| **Could not open a display** (`display-init-failed`) | 1 | GP2X | n/a |

## Renders, but the picture is wrong

These 8 titles pass the running checks (frames advancing, frame rate, audio) while the frame itself is visibly broken, so they are graded `ingame` rather than `playable`. The reasons come from measuring the captured frame: a consistent per-row offset means a stride/pitch mismatch, large-scale repetition means the screen holds more than one copy of itself, and noise far above what dithered artwork reaches means corrupt memory.

| Title | Platform | fps | What the frame looks like |
|---|---|--:|---|
| aimcaanoo | Caanoo | 46.2 | the screen holds a second copy of itself, offset by 160px; left and right halves are near-identical; top and bottom halves are near-identical |
| EEEEK! EEEEEK! HOOOOOOK!!! | Caanoo | 4.9 | renders at 640x480 instead of 320x240 |
| Metal Slug Zombies | Caanoo | 30.3 | renders at 640x480 instead of 320x240 |
| 1945_GP2X_0.2b | GP2X | 60.0 | pixel-to-pixel noise of 96, far above what dithered artwork reaches; the frame looks like corrupt memory |
| BunnyTraps-v11 | GP2X | 61.9 | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like corrupt memory |
| GF | GP2X | 61.1 | top and bottom halves are near-identical |
| Life.0.1 | GP2X | 61.9 | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like corrupt memory |
| MoveSweep2X | GP2X | 46.8 | the screen holds a second copy of itself, offset by 96px; left and right halves are near-identical |

## Scored as working, but only painting a flat colour

These 18 titles advanced frames, kept audio running, and held frame rate, so they land in `playable`/`renders`. Their framebuffer never held more than one or two colours, which means the tier overstates them. Worth treating as broken.

| Title | Platform | Status | fps |
|---|---|---|--:|
| Arcadevol3 | Caanoo | `renders` | 57.8 |
| gnp_104 | Caanoo | `playable` | 50.0 |
| knight | Caanoo | `playable` | 106.5 |
| mtknights | Caanoo | `playable` | 30.9 |
| noiz2sa_caanoo | Caanoo | `playable` | 39.4 |
| rg_ura_103 | Caanoo | `playable` | 54.6 |
| Skull (Caanoo) | Caanoo | `renders` | 15.7 |
| _-The Reversed Preacher 3-_Hack bIld_ | GP2X | `playable` | 60.0 |
| _-the reversed preacher II-_ | GP2X | `playable` | 60.7 |
| ASCIIPong2xV0.4 | GP2X | `playable` | 56.7 |
| cackb2 | GP2X | `playable` | 38.8 |
| dodge | GP2X | `playable` | 44.6 |
| dumbbell2x-01 | GP2X | `renders` | 61.9 |
| game bIld 2 | GP2X | `playable` | 58.7 |
| Knight Lore | GP2X | `playable` | 107.9 |
| levelEdit | GP2X | `renders` | 59.3 |
| robot-escape | GP2X | `playable` | 91.4 |
| starsystem | GP2X | `playable` | 60.1 |

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

### Missing dynamic symbols

| Item | Titles |
|---|--:|
| `(load) unable to load compiled module - libSDL_gfx.so.13: cannot open shared ob` | 1 |
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
| `/dev/gpmdata` | 3 |
| `/dev/pollux_batt` | 3 |
| `/dev/batt` | 3 |
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
| `unknown_ioctl:fb` | 237 |
| `unknown_mmio:0x910` | 110 |
| `unknown_mmio:0x1988` | 58 |
| `unknown_mmio:0x19c0` | 58 |
| `unknown_mmio:0x19c4` | 58 |
| `unknown_mmio:0x924` | 53 |
| `unknown_mmio:0x91c` | 51 |
| `unknown_mmio:0x3b46` | 46 |
| `unknown_mmio:0x3802` | 23 |
| `unknown_mmio:0x3804` | 23 |
| `unknown_mmio:0xf004` | 22 |
| `unknown_mmio:0xf16` | 12 |
| `unknown_mmio:0xf58` | 12 |
| `unknown_mmio:0x808` | 12 |
| `unsupported_blit:dst-unmapped` | 11 |
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
| chicken-puyopuyo | `crashed` | 0.0 | 0 | – | host-fault |  |
| 2xquake003 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| 2xquake2 | `incompatible` | 0.0 | 0 | ✓ | missing-game-data |  |
| 2XRally01 | `incompatible` | 0.0 | 0 | – | display-init-failed |  |
| abduction | `incompatible` | 0.0 | 0 | – | no-frames |  |
| airpong4GP2X0.0.4 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/airpong4GP2X0.0.4/airpong022/src/AirPong.gpe' is not an  |
| AlienZ | `incompatible` | 0.0 | 0 | – | no-frames |  |
| animatch_v1.2.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X/animatch_v1.2.zip' (exit 32512) |
| AnotherGame2x | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/AnotherGame2x/AnotherGame2x/anothergame2x.gpe' is not an |
| balluz | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/balluz/balluz/balluz.gpe' is not an ARM ELF and no runna |
| BermudaS_gp2x | `incompatible` | 1.9 | 1 | – | no-frames |  |
| blocksGP2X-0 | `incompatible` | 0.0 | 0 | – | unimplemented-syscall | 9437188 |
| Boomshine2x_(java) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Boomshine2x_(java)/Boomshine2x/Boomshine2x.gpe' is not a |
| bunkermaster2x04 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| CloneKeen2X-1.0a | `incompatible` | 0.0 | 0 | – | no-frames |  |
| d1x-rebirth-gp2x_v0.50a | `incompatible` | 0.0 | 0 | – | unimplemented-syscall | 11711 |
| DeathChase4GP2X-V0.1b | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/DeathChase4GP2X-V0.1b/deathchase3d-0.9/deathchase3d/Deat |
| dkbk2x-0.1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| doom | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/doom/doom/10sector.gpe' is not an ARM ELF and no runnabl |
| doom_mod_examples | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/doom_mod_examples/game/interpreters/doom/pwad1/prboom_gm |
| DoomPwadPack | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/DoomPwadPack/CounterStrike.gpe' is not an ARM ELF and no |
| duckmaze-gp2x-0.1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/duckmaze-gp2x-0.1/duckmaze-gp2x-0.1/duckmaze.gpe' is not |
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
| nethack-ascii-3.4.3port1 | `incompatible` | 2.0 | 1 | – | no-frames |  |
| nethack-caduhack.r01 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| nethack06 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ohthehumanity-1.0.0 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/ohthehumanity-1.0.0/ohthehumanity/ohthehumanity.gpe' is  |
| onscripter2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| OpenBOR_v2.1933 | `incompatible` | 60.0 | 1071 | ✓ | no-frames |  |
| OpenTTD | `incompatible` | 0.0 | 0 | – | no-frames |  |
| pacmame | `incompatible` | 12.0 | 6 | – | no-frames |  |
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
| Wiztern Demo | `incompatible` | 103.3 | 1880 | ✓ | no-frames |  |
| Wolf4SDL | `incompatible` | 0.0 | 0 | – | no-frames |  |
| worminator302 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Zombiepox2X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| 2xHexen2 v0.05 PB2 | `black` | 34.8 | 28 | – | black-screen |  |
| 2xWargus_PB1.3 | `black` | 0.9 | 17 | ✓ | black-screen |  |
| 2xZdoom_PB1.2 | `black` | 39.2 | 54 | – | black-screen |  |
| A1GP2XV1_1 | `black` | 19.3 | 11 | – | black-screen |  |
| abuse_1.0 | `black` | 25.3 | 670 | ✓ | black-screen |  |
| albion-v1.0.1-gp2x | `black` | 29.9 | 28 | ✓ | black-screen |  |
| AlienBlaster_1.02 | `black` | 14.4 | 21 | ✓ | black-screen |  |
| angband2x-v2 | `black` | 40.9 | 87 | – | black-screen |  |
| BeetleRun | `black` | 19.9 | 10 | ✓ | black-screen |  |
| blockoid | `black` | 12.6 | 8 | ✓ | black-screen |  |
| Bombs Panic | `black` | 88.0 | 182 | ✓ | black-screen |  |
| BubbleTrain_GP2X-2006_Entry | `black` | 1.2 | 2 | ✓ | black-screen |  |
| CaptainCrusader_GP2XDemo | `black` | 6.0 | 3 | – | black-screen |  |
| CaptainCrusader_GP2XFull | `black` | 6.0 | 3 | – | black-screen |  |
| Comando2gp2xEN | `black` | 31.8 | 16 | ✓ | black-screen |  |
| coppergreen | `black` | 15.2 | 16 | ✓ | black-screen |  |
| d2x-gp2x-0.02 | `black` | 10.3 | 11 | ✓ | black-screen |  |
| Dark_Light_SDL2X | `black` | 25.4 | 21 | ✓ | black-screen |  |
| DeathTrap1_1 | `black` | 9.9 | 16 | ✓ | black-screen |  |
| Digger | `black` | 8.0 | 19 | ✓ | black-screen |  |
| duke2x004 | `black` | 18.0 | 9 | – | black-screen |  |
| egoboo-cramfs | `black` | 35.7 | 65 | ✓ | black-screen |  |
| fenixGamePack | `black` | 14.7 | 36 | ✓ | black-screen |  |
| FFDoom | `black` | 4.0 | 2 | – | black-screen |  |
| Flappynerd_GP2X | `black` | 47.3 | 130 | ✓ | black-screen |  |
| FleshChasmer132c_patch | `black` | 6.8 | 4 | ✓ | black-screen |  |
| FleshChasmer_Dpad | `black` | 6.0 | 3 | – | black-screen |  |
| FlipIR_GP2X | `black` | 117.5 | 2966 | ✓ | black-screen |  |
| flowflowmania-0_6-gp2x | `black` | 32.0 | 78 | – | black-screen |  |
| gp2x-sand-0.3 | `black` | 58.2 | 120 | – | black-screen |  |
| GP2X_Nat2007 | `black` | 117.1 | 2957 | ✓ | black-screen |  |
| gp2xDoukutsu-1.04 | `black` | 19.9 | 18 | ✓ | black-screen |  |
| gp2xJenkasNightmare | `black` | 21.1 | 18 | ✓ | black-screen |  |
| GPgeneral | `black` | 3.8 | 2 | – | black-screen |  |
| just4qix | `black` | 21.5 | 11 | ✓ | black-screen |  |
| liquidwar2x02 | `black` | 5.9 | 3 | – | black-screen |  |
| openggs | `black` | 58.1 | 1489 | ✓ | black-screen |  |
| openjazz-gp2x | `black` | 18.6 | 17 | ✓ | black-screen |  |
| para3 | `black` | 60.0 | 155 | ✓ | black-screen |  |
| PrBoom PWAD pack | `black` | 3.3 | 6 | – | black-screen |  |
| raw2xv0.3.1 | `black` | 13.0 | 7 | – | black-screen |  |
| ShadowWarrior2X | `black` | 15.6 | 8 | – | black-screen |  |
| SmashGp2x02 | `black` | 13.1 | 397 | ✓ | black-screen |  |
| step2x02 | `black` | 53.1 | 115 | ✓ | black-screen |  |
| tesla-Siren | `black` | 24.9 | 15 | ✓ | black-screen |  |
| Tetrablocks.0.4.GP2X | `black` | 50.1 | 115 | ✓ | black-screen |  |
| uhexen | `black` | 8.0 | 4 | – | black-screen |  |
| ultratumba_exp-20100925.gp2x | `black` | 15.7 | 8 | ✓ | black-screen |  |
| Volleyball | `black` | 53.6 | 113 | ✓ | black-screen |  |
| warcraft-beta3-gp2x | `black` | 34.4 | 29 | ✓ | black-screen |  |
| Wizznic_2x_07alpha2 | `black` | 7.0 | 17 | ✓ | black-screen |  |
| wizznic_gp2x-0.9.9 | `black` | 10.3 | 16 | ✓ | black-screen |  |
| wolfdx | `black` | 54.0 | 55 | ✓ | black-screen |  |
| xbak-0.1.3 | `black` | 31.9 | 16 | – | black-screen |  |
| xcom1-v1.0.2-gp2x | `black` | 42.4 | 1568 | ✓ | black-screen |  |
| xcom2-v1.0.1-gp2x | `black` | 109.9 | 2806 | ✓ | black-screen |  |
| xump2x_beta2 | `black` | 27.2 | 14 | ✓ | black-screen |  |
| zcgp2x_211B18_0.4alpha | `black` | 22.3 | 21 | – | black-screen |  |
| Zelda_roth_US_gp2x | `black` | 28.1 | 60 | ✓ | black-screen |  |
| 1945_GP2X_0.2b | `ingame` | 60.0 | 568 | ✓ | garbled-visuals | pixel-to-pixel noise of 96, far above what dithered artwork reaches; the frame looks like  |
| _-The Reversed Preacher 3-_Hack bIld_ | `ingame` | 60.0 | 140 | ✓ | flat-fill |  |
| _-the reversed preacher II-_ | `ingame` | 60.7 | 144 | ✓ | flat-fill |  |
| ASCIIPong2xV0.4 | `ingame` | 56.7 | 1427 | ✓ | flat-fill |  |
| BunnyTraps-v11 | `ingame` | 61.9 | 1560 | ✓ | garbled-visuals | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like |
| cackb2 | `ingame` | 38.8 | 84 | ✓ | flat-fill |  |
| Clonk2X_1.0 | `ingame` | 16.2 | 408 | – | not-arm-elf | magiceyes: reload of '/bin/sh' failed |
| dodge | `ingame` | 44.6 | 97 | ✓ | flat-fill |  |
| dumbbell2x-01 | `ingame` | 61.9 | 578 | – | flat-fill |  |
| game bIld 2 | `ingame` | 58.7 | 1481 | ✓ | flat-fill |  |
| GF | `ingame` | 61.1 | 1554 | ✓ | garbled-visuals | top and bottom halves are near-identical |
| Knight Lore | `ingame` | 107.9 | 2717 | ✓ | flat-fill |  |
| levelEdit | `ingame` | 59.3 | 1494 | – | flat-fill |  |
| Life.0.1 | `ingame` | 61.9 | 1557 | – | garbled-visuals | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like |
| MoveSweep2X | `ingame` | 46.8 | 96 | – | garbled-visuals | the screen holds a second copy of itself, offset by 96px; left and right halves are near-i |
| pintor2x | `ingame` | 19.3 | 485 | ✓ | low-fps |  |
| protozoa v1.0 | `ingame` | 19.2 | 1024 | ✓ | low-fps |  |
| puzzlelandgp2x | `ingame` | 11.2 | 521 | ✓ | low-fps |  |
| robot-escape | `ingame` | 91.4 | 193 | ✓ | flat-fill |  |
| SmallBall_GP | `ingame` | 18.2 | 906 | ✓ | low-fps |  |
| Solitaire2x-v1.4 | `ingame` | 11.9 | 462 | – | low-fps |  |
| SpaceRocks2X | `ingame` | 8.6 | 331 | – | low-fps |  |
| SpaceSnake | `ingame` | 14.2 | 387 | ✓ | low-fps |  |
| starsystem | `ingame` | 60.1 | 1520 | ✓ | flat-fill |  |
| 2xpong_gp2x | `playable` | 61.1 | 1537 | ✓ |  |  |
| 2xtron-v01 | `playable` | 61.9 | 1559 | ✓ |  |  |
| 2xZdoom_selector | `playable` | 56.8 | 135 | ✓ |  |  |
| 4WE_GP2x | `playable` | 116.8 | 2004 | ✓ |  |  |
| 9 Lives | `playable` | 70.0 | 1769 | ✓ |  |  |
| a_sn-pong | `playable` | 42.0 | 1555 | – | no-audio |  |
| abe | `playable` | 61.6 | 1547 | ✓ |  |  |
| AbusimbelProfanationDeluxe | `playable` | 118.6 | 3003 | ✓ |  |  |
| AdamantArmorAffection2x | `playable` | 60.4 | 1530 | ✓ |  |  |
| ADIC2X | `playable` | 88.5 | 1057 | ✓ |  |  |
| AfterBurner-GP2X | `playable` | 36.5 | 926 | ✓ |  |  |
| Airplyr | `playable` | 61.9 | 1556 | ✓ |  |  |
| airstrike-1.1 | `playable` | 58.5 | 1484 | ✓ |  |  |
| Akd_BB | `playable` | 61.5 | 1556 | ✓ |  |  |
| alex | `playable` | 58.7 | 1478 | ✓ |  |  |
| Alex's Falldown | `playable` | 61.5 | 1555 | ✓ |  |  |
| alex4_gp2x | `playable` | 58.7 | 1479 | ✓ |  |  |
| altitude | `playable` | 57.5 | 1484 | ✓ |  |  |
| AMazing-3D | `playable` | 64.2 | 1624 | – | no-audio |  |
| amoebax-0.2.1-gp2x | `playable` | 56.4 | 1450 | ✓ |  |  |
| armorcity-0_30b | `playable` | 58.2 | 1485 | ✓ |  |  |
| Asteroids | `playable` | 60.5 | 1554 | ✓ |  |  |
| astrochaos | `playable` | 58.6 | 1482 | ✓ |  |  |
| atris-1.0.7 | `playable` | 57.9 | 1492 | ✓ |  |  |
| B'lox! | `playable` | 115.5 | 2962 | ✓ |  |  |
| bang_gp | `playable` | 58.7 | 1488 | ✓ |  |  |
| BareFistFighter | `playable` | 61.2 | 1539 | ✓ |  |  |
| barrage | `playable` | 58.8 | 1485 | ✓ |  |  |
| Batiscafo (versin EXP) | `playable` | 61.2 | 1542 | ✓ |  |  |
| battlejewels-gp2x-062-100 | `playable` | 118.0 | 2982 | ✓ |  |  |
| BearOids | `playable` | 61.5 | 1553 | ✓ |  |  |
| beat2x-0.5-bin | `playable` | 61.5 | 1557 | ✓ |  |  |
| Beatbox_1.2 | `playable` | 59.3 | 1554 | ✓ |  |  |
| biniax-gp2x_v1.2 | `playable` | 58.3 | 1466 | ✓ |  |  |
| Biniax2_gp2x | `playable` | 81.4 | 2061 | ✓ |  |  |
| Biohazard2 | `playable` | 61.8 | 1560 | ✓ |  |  |
| BioShoot GP2X | `playable` | 60.8 | 1554 | ✓ |  |  |
| Birdshoot | `playable` | 62.0 | 1559 | – | no-audio |  |
| BisfoG | `playable` | 77.7 | 929 | ✓ |  |  |
| blastriot1.21 | `playable` | 57.9 | 1459 | ✓ |  |  |
| blazar_v1-30_gp2x | `playable` | 59.0 | 1483 | ✓ |  |  |
| blingo 1.2 | `playable` | 57.7 | 1481 | ✓ |  |  |
| blipsgp2x | `playable` | 57.4 | 1488 | ✓ |  |  |
| Blix2x | `playable` | 61.8 | 1557 | ✓ |  |  |
| blobbyvolley | `playable` | 58.4 | 1477 | ✓ |  |  |
| blobwars_2x | `playable` | 62.8 | 1588 | ✓ |  |  |
| block | `playable` | 61.7 | 1559 | ✓ |  |  |
| blockdudegp2x | `playable` | 56.4 | 1521 | ✓ |  |  |
| Blocked | `playable` | 112.9 | 2879 | ✓ |  |  |
| Blockrage2x | `playable` | 61.0 | 1541 | ✓ |  |  |
| blox | `playable` | 42.3 | 1066 | ✓ |  |  |
| Bloxz_DEMO | `playable` | 61.9 | 1565 | ✓ |  |  |
| bluecube2x | `playable` | 59.2 | 1490 | ✓ |  |  |
| bobtron-gp2x | `playable` | 59.1 | 1488 | ✓ |  |  |
| Boomshine2x_1.12_gp2x | `playable` | 60.5 | 1554 | ✓ |  |  |
| Boulders-0 | `playable` | 61.3 | 1550 | ✓ |  |  |
| brassmunkey_gp2x_1.0 | `playable` | 58.2 | 1470 | ✓ |  |  |
| BubbleX | `playable` | 61.6 | 1546 | ✓ |  |  |
| BubTrain_GP2X-2006_Entry_No-Sound | `playable` | 57.8 | 1554 | ✓ |  |  |
| bugafactorx-v03-beta | `playable` | 58.7 | 1489 | – | no-audio |  |
| BugWarsSE_v1.0 | `playable` | 60.7 | 1554 | ✓ |  |  |
| bumprace-0.2 | `playable` | 60.6 | 1557 | ✓ |  |  |
| BurokkuDemo1 | `playable` | 62.0 | 1571 | ✓ |  |  |
| buscaminas | `playable` | 61.6 | 1557 | – | no-audio |  |
| buttongame | `playable` | 49.5 | 104 | ✓ |  |  |
| BuzzysBadDay-1.0 | `playable` | 61.5 | 1553 | ✓ |  |  |
| CamelotWarriors-GP2x_v1.0 | `playable` | 60.9 | 1551 | ✓ |  |  |
| cardm | `playable` | 58.5 | 1487 | – | no-audio |  |
| CascadeBeneath v1.0 for GP2X | `playable` | 62.5 | 1573 | ✓ |  |  |
| cat_trap | `playable` | 108.2 | 2757 | ✓ |  |  |
| cavecopter_gp2x | `playable` | 21.8 | 548 | – | no-audio |  |
| ccrg | `playable` | 55.5 | 246 | ✓ |  |  |
| cdogs2x04 | `playable` | 93.6 | 2372 | ✓ |  |  |
| cgenius-gp2x | `playable` | 55.8 | 1439 | ✓ |  |  |
| chaos2x | `playable` | 59.1 | 1486 | ✓ |  |  |
| checkersgp2x | `playable` | 59.8 | 1559 | ✓ |  |  |
| chess2x05 | `playable` | 60.5 | 1559 | ✓ |  |  |
| Chopper | `playable` | 61.1 | 1555 | – | no-audio |  |
| ChopperAttackv1.0.17 | `playable` | 102.8 | 2869 | ✓ |  |  |
| Chroma | `playable` | 117.4 | 2966 | ✓ |  |  |
| chuckiev12 | `playable` | 58.6 | 1479 | ✓ |  |  |
| Codemaster | `playable` | 117.8 | 2972 | ✓ |  |  |
| ConnyCarrot | `playable` | 60.9 | 1549 | ✓ |  |  |
| cosmo2x_01 | `playable` | 58.7 | 1489 | – | no-audio |  |
| CowSuckers-1.0 | `playable` | 61.5 | 1555 | ✓ |  |  |
| Crapong | `playable` | 61.0 | 1552 | ✓ |  |  |
| crazeeman | `playable` | 62.3 | 1574 | ✓ |  |  |
| crimsonV1 | `playable` | 57.2 | 1484 | ✓ |  |  |
| crocodingusgp2x | `playable` | 108.6 | 1920 | ✓ |  |  |
| CromoZome | `playable` | 20.4 | 531 | ✓ |  |  |
| crossroads | `playable` | 58.8 | 1484 | ✓ |  |  |
| CUBES | `playable` | 61.7 | 1565 | ✓ |  |  |
| cyberhockeyV2_6 | `playable` | 58.7 | 1482 | ✓ |  |  |
| DABAKKA-0 | `playable` | 61.2 | 1537 | ✓ |  |  |
| Dance2x Alpha GPE | `playable` | 61.0 | 1556 | ✓ |  |  |
| DangerMouse | `playable` | 117.7 | 2971 | ✓ |  |  |
| Dastardly dungeon 1.5 | `playable` | 60.5 | 1554 | ✓ |  |  |
| dd2x | `playable` | 108.5 | 493 | ✓ |  |  |
| DealOrNoDeal-v12 | `playable` | 61.0 | 1565 | ✓ |  |  |
| debian_vs_pimientos_2x_0.1.2 | `playable` | 60.1 | 567 | ✓ |  |  |
| defeatme-gp2x-1.0.1 | `playable` | 58.1 | 1464 | ✓ |  |  |
| diamant_1_01 | `playable` | 30.9 | 782 | ✓ |  |  |
| DontGetCrushed v1.0 for GP2X | `playable` | 62.3 | 1566 | ✓ |  |  |
| dopewars2x | `playable` | 58.6 | 1471 | – | no-audio |  |
| dosmugen | `playable` | 57.4 | 1480 | ✓ |  |  |
| Dr. Mates v1.0 | `playable` | 40.5 | 99 | ✓ |  |  |
| Drill2x_final | `playable` | 60.0 | 1532 | ✓ |  |  |
| drill2x_xtreme_v1.0.3 | `playable` | 57.9 | 1476 | ✓ |  |  |
| drod-gp2x-1_0 | `playable` | 56.0 | 1442 | – | no-audio |  |
| dstroyGP2X1402 | `playable` | 60.6 | 1554 | – | no-audio |  |
| DubaiRace038a | `playable` | 46.2 | 104 | ✓ |  |  |
| dyc_gp2x | `playable` | 110.2 | 2792 | ✓ |  |  |
| dynamategp2x | `playable` | 57.3 | 1486 | ✓ |  |  |
| E-Fighters2x_FIRST_ALPHA_0_0_5_fixedSound | `playable` | 117.0 | 2965 | ✓ |  |  |
| EasterQuest | `playable` | 117.4 | 2964 | ✓ |  |  |
| eggstreme3_v1-00_gp2x | `playable` | 58.8 | 1480 | ✓ |  |  |
| egoboo2xFeb1207 | `playable` | 95.5 | 2911 | ✓ |  |  |
| Electronia | `playable` | 62.0 | 1560 | ✓ |  |  |
| enigma | `playable` | 48.7 | 1297 | ✓ |  |  |
| entombed2x | `playable` | 58.2 | 1481 | ✓ |  |  |
| EpicFreeFall_GP2X | `playable` | 61.0 | 1553 | ✓ |  |  |
| EpicRocks_GP2X | `playable` | 60.6 | 1554 | ✓ |  |  |
| escapa-v1 | `playable` | 61.8 | 1559 | ✓ |  |  |
| escoba_exp-20101016.gp2x | `playable` | 58.2 | 1518 | – | no-audio |  |
| exi_shoot_gp2x | `playable` | 58.5 | 1526 | ✓ |  |  |
| extraterrestres | `playable` | 94.7 | 2547 | ✓ |  |  |
| extraterrestres-0 | `playable` | 55.9 | 1489 | – | no-audio |  |
| exult_rc3 | `playable` | 39.6 | 1465 | ✓ |  |  |
| Factor-v1.0-final | `playable` | 60.8 | 1552 | ✓ |  |  |
| falldown_gp2x | `playable` | 90.7 | 2279 | ✓ |  |  |
| FCRLG | `playable` | 61.0 | 1535 | – | no-audio |  |
| fenix | `playable` | 59.2 | 1557 | ✓ |  |  |
| fifteen_01 | `playable` | 60.1 | 564 | – | no-audio |  |
| FindMii | `playable` | 116.9 | 2968 | ✓ |  |  |
| Firewhip | `playable` | 110.2 | 2939 | ✓ |  |  |
| Fishball-1.2 | `playable` | 61.2 | 1542 | ✓ |  |  |
| fissionfield2x | `playable` | 59.0 | 1487 | ✓ |  |  |
| Flaschenspiel | `playable` | 61.0 | 1553 | – | no-audio |  |
| FleshChasmer | `playable` | 59.7 | 1526 | ✓ |  |  |
| FleshChasmer Zero | `playable` | 60.8 | 1533 | ✓ |  |  |
| floaters | `playable` | 58.6 | 1482 | ✓ |  |  |
| flobopuyo0.20.1 | `playable` | 57.6 | 1457 | ✓ |  |  |
| flurkies_v1-25_gp2x | `playable` | 59.1 | 1488 | ✓ |  |  |
| fm | `playable` | 97.1 | 2437 | ✓ |  |  |
| Football2X | `playable` | 117.6 | 2965 | ✓ |  |  |
| formula1gp2x | `playable` | 58.2 | 1484 | ✓ |  |  |
| Fragger2x | `playable` | 61.8 | 1558 | ✓ |  |  |
| freec2x | `playable` | 40.9 | 1035 | ✓ |  |  |
| freecell_1 | `playable` | 53.1 | 111 | ✓ |  |  |
| freedroid2x06 | `playable` | 90.7 | 2333 | ✓ |  |  |
| freesci | `playable` | 56.6 | 255 | – | no-audio |  |
| friq-beta-07 | `playable` | 58.7 | 1486 | ✓ |  |  |
| frozen2x-0.1 | `playable` | 79.8 | 749 | ✓ |  |  |
| fruits2x | `playable` | 58.5 | 1480 | – | no-audio |  |
| fruits_gp2x | `playable` | 58.9 | 1486 | ✓ |  |  |
| FullBoard (test ver.) | `playable` | 49.1 | 101 | ✓ |  |  |
| fvc | `playable` | 58.6 | 1473 | ✓ |  |  |
| FyWod_2x | `playable` | 60.7 | 1555 | ✓ |  |  |
| game-watch-mario-bros | `playable` | 58.8 | 1480 | ✓ |  |  |
| gchess-v1.0.1-bin | `playable` | 61.5 | 1557 | – | no-audio |  |
| gchess-v1.1.0-bin | `playable` | 58.9 | 1489 | – | no-audio |  |
| Geek 'em up GP2X | `playable` | 61.6 | 1716 | ✓ |  |  |
| gemdrop2x_v02 | `playable` | 58.6 | 1502 | ✓ |  |  |
| GeneralPromise | `playable` | 117.0 | 2971 | ✓ |  |  |
| Ghostbusters_WIP | `playable` | 61.0 | 573 | ✓ |  |  |
| ghostpix_v10_gp2x | `playable` | 58.4 | 666 | ✓ |  |  |
| glouton | `playable` | 59.1 | 1486 | ✓ |  |  |
| gnp_104 | `playable` | 55.5 | 1503 | ✓ |  |  |
| gnugo2x | `playable` | 58.8 | 1489 | – | no-audio |  |
| gnurobbo_0.66_open2x | `playable` | 53.5 | 1377 | ✓ |  |  |
| godori | `playable` | 61.8 | 1551 | – | no-audio |  |
| GoitGP | `playable` | 60.4 | 1554 | ✓ |  |  |
| gorillaz | `playable` | 54.3 | 1364 | ✓ |  |  |
| gp2hanoi_0.8.1_gp2x | `playable` | 59.0 | 1487 | ✓ |  |  |
| gp2x-blobwars-0.1 | `playable` | 60.6 | 1537 | ✓ |  |  |
| gp2x-bubbletrain-0.1 | `playable` | 57.6 | 1486 | ✓ |  |  |
| gp2x-ceferino-0.1 | `playable` | 56.4 | 1492 | – | no-audio |  |
| gp2x-formido-0.1 | `playable` | 44.0 | 1558 | ✓ |  |  |
| gp2x-invaders-preview-version | `playable` | 59.1 | 1490 | ✓ |  |  |
| gp2x-netrok-0.1 | `playable` | 52.0 | 1553 | ✓ |  |  |
| gp2x-shienso-bin_061021 | `playable` | 59.0 | 1487 | ✓ |  |  |
| gp2x-smc-0.1 | `playable` | 55.5 | 1484 | ✓ |  |  |
| gp2x-tenmado-0.1 | `playable` | 58.5 | 1485 | – | no-audio |  |
| gp2x-tong-v1 | `playable` | 111.8 | 2826 | – | no-audio |  |
| gp2x_2xmas | `playable` | 20.7 | 54 | ✓ |  |  |
| GP2X_BallGame_0.49 | `playable` | 60.9 | 1531 | ✓ |  |  |
| gp2x_drench | `playable` | 59.2 | 1498 | ✓ |  |  |
| GP2X_TLI | `playable` | 31.3 | 790 | ✓ |  |  |
| gp2xbug | `playable` | 116.6 | 2986 | ✓ |  |  |
| gp2xgo-v1.1.0-bin | `playable` | 61.5 | 1556 | – | no-audio |  |
| gp2xjunkie | `playable` | 58.7 | 1506 | ✓ |  |  |
| gp2xmancala-v1.1.1-bin | `playable` | 58.9 | 1486 | – | no-audio |  |
| GP2XOfLife | `playable` | 113.3 | 2866 | – | no-audio |  |
| gp2xpang-v.1.1.1 | `playable` | 95.2 | 2444 | ✓ |  |  |
| gp2xrick 1.0 | `playable` | 58.0 | 1468 | ✓ |  |  |
| GpFrontier v0.1 | `playable` | 61.8 | 1568 | ✓ |  |  |
| gpfrontier v0.4 | `playable` | 56.9 | 1493 | ✓ |  |  |
| gpnoid2x | `playable` | 58.5 | 1486 | ✓ |  |  |
| GPrina-GP2x_v1.0 | `playable` | 60.5 | 1554 | ✓ |  |  |
| GPSquares_GP2X | `playable` | 61.7 | 1552 | – | no-audio |  |
| gr-v1001-gp2x | `playable` | 57.7 | 1558 | ✓ |  |  |
| green | `playable` | 60.5 | 1535 | ✓ |  |  |
| grow | `playable` | 61.0 | 1533 | – | no-audio |  |
| gxeskiv | `playable` | 55.7 | 1404 | – | no-audio |  |
| HamstersEscape (Gp2x F-100 F-200) | `playable` | 29.5 | 278 | ✓ |  |  |
| hanagechu2x_gbax2007 | `playable` | 63.6 | 1603 | ✓ |  |  |
| hanagechu2xalpha | `playable` | 59.2 | 1491 | ✓ |  |  |
| Heretic2X_v0.5 | `playable` | 60.7 | 1528 | ✓ |  |  |
| heroes2x02 | `playable` | 49.4 | 1249 | ✓ |  |  |
| hex-a-hop | `playable` | 59.1 | 1486 | – | no-audio |  |
| hexbattle2x | `playable` | 58.6 | 1489 | ✓ |  |  |
| HumphreyGP2X | `playable` | 61.0 | 1550 | ✓ |  |  |
| Hyperion_GP2X_demo | `playable` | 61.8 | 1562 | ✓ |  |  |
| jump_n_blob_gp2x | `playable` | 59.9 | 1577 | ✓ |  |  |
| jumpnbumpgp2x | `playable` | 58.3 | 1491 | ✓ |  |  |
| Jurlx2 | `playable` | 61.4 | 1552 | ✓ |  |  |
| kampfimall-gp2x | `playable` | 58.9 | 1484 | – | no-audio |  |
| kampfimall-gp2x-music | `playable` | 60.4 | 526 | ✓ |  |  |
| ketm_2x_gp2x | `playable` | 51.1 | 1491 | ✓ |  |  |
| KicknPLay_1.1 | `playable` | 61.8 | 1553 | ✓ |  |  |
| Klaur | `playable` | 115.9 | 2972 | ✓ |  |  |
| koules2x_02 | `playable` | 58.7 | 1493 | ✓ |  |  |
| kuklomenos_gp2x_201209 | `playable` | 58.6 | 1496 | ✓ |  |  |
| kurukuru2x | `playable` | 61.4 | 1557 | ✓ |  |  |
| la | `playable` | 47.4 | 103 | ✓ |  |  |
| LABYRINTH | `playable` | 61.7 | 1557 | – | no-audio |  |
| ladykiller | `playable` | 61.0 | 1555 | ✓ |  |  |
| las-tres-luces-de-glaurung-remake | `playable` | 60.5 | 1546 | ✓ |  |  |
| lbreakoutgp2x | `playable` | 56.7 | 1497 | ✓ |  |  |
| levelshmup | `playable` | 83.6 | 2132 | ✓ |  |  |
| Lexeme | `playable` | 116.8 | 2968 | ✓ |  |  |
| lights-out | `playable` | 57.0 | 1437 | – | no-audio |  |
| LinesXv3 | `playable` | 61.6 | 1545 | ✓ |  |  |
| logicx | `playable` | 58.9 | 1478 | ✓ |  |  |
| Logoball | `playable` | 116.0 | 2967 | ✓ |  |  |
| lumix-beta-01 | `playable` | 63.0 | 1588 | – | no-audio |  |
| mad-mix-game-20b-final | `playable` | 58.8 | 1537 | ✓ |  |  |
| madbomber | `playable` | 58.5 | 1530 | ✓ |  |  |
| malvado2x | `playable` | 57.9 | 1489 | ✓ |  |  |
| MAME-N22_51 | `playable` | 59.3 | 1565 | ✓ |  |  |
| mancala-v1.0.1 | `playable` | 58.8 | 1491 | – | no-audio |  |
| March of the mini tux | `playable` | 64.1 | 1628 | ✓ |  |  |
| Marte Necesita Vacas GP2X | `playable` | 64.3 | 1649 | ✓ |  |  |
| Masteries_Journey_to_the_Center_of_the_earth_GP2X | `playable` | 59.6 | 1552 | ✓ |  |  |
| masterpiece2x | `playable` | 59.3 | 1494 | – | no-audio |  |
| MazeThingie | `playable` | 61.9 | 1561 | ✓ |  |  |
| MazezaMGP2X | `playable` | 97.5 | 2538 | ✓ |  |  |
| memory | `playable` | 59.4 | 1511 | ✓ |  |  |
| MemoryGP2X-v11 | `playable` | 61.6 | 1559 | ✓ |  |  |
| meritous | `playable` | 61.0 | 1556 | ✓ |  |  |
| Merlin2x_beta_021 | `playable` | 58.1 | 542 | ✓ |  |  |
| metaphysik | `playable` | 64.4 | 1626 | ✓ |  |  |
| methaneV1 | `playable` | 61.5 | 1554 | ✓ |  |  |
| minigolf | `playable` | 58.7 | 1488 | – | no-audio |  |
| minos-gp2x | `playable` | 57.7 | 1461 | ✓ |  |  |
| misterhachi | `playable` | 48.6 | 1431 | ✓ |  |  |
| mk13.gpe | `playable` | 58.8 | 1476 | ✓ |  |  |
| mkACE.gpe | `playable` | 58.8 | 1477 | ✓ |  |  |
| mkONE.gpe | `playable` | 58.7 | 1475 | ✓ |  |  |
| MM2X | `playable` | 60.7 | 1533 | ✓ |  |  |
| monacoGP | `playable` | 61.0 | 1561 | ✓ |  |  |
| monochromeworlds-gp2x-1.0.0 | `playable` | 61.1 | 1542 | ✓ |  |  |
| moonlander | `playable` | 57.7 | 1473 | ✓ |  |  |
| MouthTrap | `playable` | 117.9 | 2968 | ✓ |  |  |
| mueppv32 | `playable` | 114.0 | 2884 | ✓ |  |  |
| mush_gp2x | `playable` | 39.2 | 1008 | ✓ |  |  |
| Mutant Tank Knights | `playable` | 56.5 | 341 | ✓ |  |  |
| MyriadUpdated | `playable` | 59.8 | 1553 | ✓ |  |  |
| mzx-2.84c | `playable` | 56.3 | 790 | ✓ |  |  |
| mzx282-gp2x | `playable` | 59.0 | 1487 | ✓ |  |  |
| n-tris_v1.0 | `playable` | 73.3 | 1844 | ✓ |  |  |
| nanobounce-pacc-gp2x | `playable` | 51.6 | 395 | ✓ |  |  |
| nazcarunners-0 | `playable` | 43.9 | 54 | ✓ |  |  |
| nazcasphere | `playable` | 45.1 | 56 | ✓ |  |  |
| ne_deluxe_gp2x | `playable` | 61.5 | 1552 | ✓ |  |  |
| ne_gp2x | `playable` | 57.2 | 1443 | ✓ |  |  |
| Nebulus_gp2x | `playable` | 61.7 | 1555 | – | no-audio |  |
| NecNec2x | `playable` | 61.7 | 1564 | ✓ |  |  |
| Net-Bubble-gp2x_1-21-06_bin | `playable` | 54.6 | 510 | – | no-audio |  |
| newsuperpang | `playable` | 58.8 | 1488 | ✓ |  |  |
| Nifty | `playable` | 61.0 | 1554 | ✓ |  |  |
| noiz2saV3 | `playable` | 66.1 | 1681 | ✓ |  |  |
| Nom | `playable` | 61.3 | 1543 | ✓ |  |  |
| odonata_demo | `playable` | 57.7 | 1458 | ✓ |  |  |
| omok | `playable` | 51.8 | 109 | ✓ |  |  |
| OpenBOR_v3.0_Build_2615_&_2637 | `playable` | 61.0 | 1564 | ✓ |  |  |
| openglad2x | `playable` | 57.2 | 1486 | – | no-audio |  |
| opentyrian2x_0.3_complete | `playable` | 56.9 | 1540 | ✓ |  |  |
| opposite_lock | `playable` | 52.9 | 1500 | ✓ |  |  |
| OrbitalSniper2x_v1.1 | `playable` | 58.2 | 142 | ✓ |  |  |
| othello_v1.0 | `playable` | 62.2 | 1561 | ✓ |  |  |
| oxov06 | `playable` | 45.0 | 92 | – | no-audio |  |
| PAF | `playable` | 61.4 | 1552 | ✓ |  |  |
| PantaVsDragon (Gp2x F-100 F-200) | `playable` | 52.1 | 1338 | ✓ |  |  |
| paraballgp2x | `playable` | 58.8 | 1491 | ✓ |  |  |
| Payback | `playable` | 53.3 | 245 | ✓ |  |  |
| PaybackDemo | `playable` | 32.9 | 856 | ✓ |  |  |
| pc | `playable` | 59.6 | 676 | ✓ |  |  |
| pdcv060b | `playable` | 48.2 | 457 | ✓ |  |  |
| Pentominos | `playable` | 61.8 | 1553 | ✓ |  |  |
| PerfectFit | `playable` | 61.7 | 1556 | – | no-audio |  |
| Peuppy_10_GP2X | `playable` | 33.8 | 854 | ✓ |  |  |
| pez | `playable` | 59.0 | 1490 | – | no-audio |  |
| Phishy-0 | `playable` | 61.0 | 1555 | ✓ |  |  |
| physique | `playable` | 59.0 | 1488 | ✓ |  |  |
| Pika2x | `playable` | 60.4 | 572 | ✓ |  |  |
| pixpang | `playable` | 54.5 | 1555 | ✓ |  |  |
| PocketSnes_SMRPG | `playable` | 118.3 | 2978 | – | no-audio |  |
| Poker2x | `playable` | 111.0 | 2819 | ✓ |  |  |
| Poker_Gp2Xv1.0 | `playable` | 116.3 | 2970 | ✓ |  |  |
| Pond2X | `playable` | 61.1 | 1539 | – | no-audio |  |
| Pong | `playable` | 61.6 | 1546 | – | no-audio |  |
| pong2player | `playable` | 57.9 | 118 | – | no-audio |  |
| pong2v060x | `playable` | 54.2 | 111 | – | no-audio |  |
| Pool Panic | `playable` | 60.9 | 1534 | ✓ |  |  |
| powder2x-112 | `playable` | 61.0 | 1558 | – | no-audio |  |
| powermanga-0.80 | `playable` | 54.1 | 1414 | ✓ |  |  |
| PowerSlide | `playable` | 60.7 | 1541 | ✓ |  |  |
| PPlane | `playable` | 60.0 | 1516 | ✓ |  |  |
| PPlane2.GP2X | `playable` | 59.0 | 1554 | ✓ |  |  |
| prboom-gp2x | `playable` | 21.6 | 1219 | – | no-audio |  |
| proj0-demo_01 | `playable` | 26.9 | 1208 | ✓ |  |  |
| puckman_gp2x | `playable` | 31.4 | 1561 | ✓ |  |  |
| PulplifeWars | `playable` | 59.8 | 1553 | ✓ |  |  |
| qfg3-0 | `playable` | 40.5 | 1499 | ✓ |  |  |
| Quad | `playable` | 117.1 | 2959 | ✓ |  |  |
| quartz2_v1-50_gp2x | `playable` | 60.3 | 1519 | ✓ |  |  |
| Rabbit_vs_Flies_0.9 | `playable` | 61.2 | 1555 | ✓ |  |  |
| ramon atacks | `playable` | 60.2 | 1519 | ✓ |  |  |
| Release GP2X MST_RUNNERS | `playable` | 59.9 | 1551 | ✓ |  |  |
| retrovirus_1_1 | `playable` | 59.1 | 1494 | ✓ |  |  |
| RevoltOfTheBinaryCouriers GP2X | `playable` | 60.7 | 1530 | – | no-audio |  |
| reword_v0.5 | `playable` | 59.9 | 1525 | ✓ |  |  |
| rg_105 | `playable` | 59.4 | 1572 | ✓ |  |  |
| rg_ura_103 | `playable` | 59.6 | 1574 | ✓ |  |  |
| river | `playable` | 62.0 | 1556 | ✓ |  |  |
| RockRain | `playable` | 61.6 | 1553 | ✓ |  |  |
| rockrain2_exp-20100925 | `playable` | 61.1 | 1546 | ✓ |  |  |
| rookiehero_EXP.gp2x.v20120220 | `playable` | 60.7 | 1550 | ✓ |  |  |
| RoundEmUp-alpha3 | `playable` | 61.5 | 1558 | ✓ |  |  |
| rRootage_v1.0 | `playable` | 111.5 | 2864 | ✓ |  |  |
| rubidogp2x | `playable` | 60.3 | 1556 | ✓ |  |  |
| rubik | `playable` | 120.4 | 3048 | – | no-audio |  |
| ruckman_v1.03 | `playable` | 58.7 | 1526 | ✓ |  |  |
| Runner_GP2X | `playable` | 60.8 | 1555 | ✓ |  |  |
| s-tris2_v1-64_gp2x | `playable` | 60.7 | 1527 | ✓ |  |  |
| Sachunsung2_1 | `playable` | 74.7 | 158 | ✓ |  |  |
| sachunsungx | `playable` | 60.4 | 1518 | ✓ |  |  |
| santaMania | `playable` | 59.5 | 1520 | ✓ |  |  |
| ScorchedGPBeta2 | `playable` | 60.2 | 720 | ✓ |  |  |
| scummvm-0.11.1-gp2x | `playable` | 58.8 | 1526 | ✓ |  |  |
| scummvm-1.2.0-gp2x | `playable` | 58.9 | 1534 | ✓ |  |  |
| scummvm-kor0.4.2cvs | `playable` | 59.3 | 1499 | – | no-audio |  |
| SdLame | `playable` | 61.2 | 1561 | ✓ |  |  |
| sdlmonkey_0.1 | `playable` | 60.2 | 1517 | – | no-audio |  |
| sdlscav_gp2x_0.2.0 | `playable` | 111.9 | 2821 | ✓ |  |  |
| Shangai v2 | `playable` | 71.4 | 150 | ✓ |  |  |
| ShanghaiX | `playable` | 61.3 | 1537 | ✓ |  |  |
| SheepDash | `playable` | 59.9 | 1562 | ✓ |  |  |
| Shippy84 | `playable` | 60.7 | 1558 | ✓ |  |  |
| Simon2X | `playable` | 29.1 | 1322 | – | no-audio |  |
| SimOniZ | `playable` | 111.6 | 2869 | ✓ |  |  |
| siv050 | `playable` | 57.6 | 1524 | ✓ |  |  |
| sleuth slots 2x | `playable` | 111.3 | 2878 | ✓ |  |  |
| snail runers | `playable` | 60.9 | 1556 | ✓ |  |  |
| snake2x-1.1 | `playable` | 61.2 | 1560 | – | no-audio |  |
| snowedin5_v1-00_gp2x | `playable` | 61.8 | 1554 | ✓ |  |  |
| SOD v1.1 | `playable` | 59.9 | 1553 | ✓ |  |  |
| sokobangp2x | `playable` | 55.6 | 1561 | ✓ |  |  |
| sopwith_camel_rc3 | `playable` | 44.9 | 96 | ✓ |  |  |
| sources_MEMORY2X | `playable` | 61.1 | 1557 | ✓ |  |  |
| sources_Yahtzee | `playable` | 60.8 | 1558 | ✓ |  |  |
| space squares | `playable` | 58.6 | 1487 | – | no-audio |  |
| space52_gp2x(oficial) | `playable` | 47.7 | 1251 | ✓ |  |  |
| space52_gp2x(open2x) | `playable` | 47.4 | 1244 | ✓ |  |  |
| space_varments_v1.0 | `playable` | 59.0 | 1517 | ✓ |  |  |
| spacestorm | `playable` | 55.4 | 1396 | ✓ |  |  |
| spartak-chess_0.0.4_gp2x | `playable` | 59.9 | 1516 | – | no-audio |  |
| Sponge Blob Tennis | `playable` | 40.5 | 1498 | – | no-audio |  |
| spout | `playable` | 60.4 | 1520 | – | no-audio |  |
| sprint_race | `playable` | 59.7 | 1519 | ✓ |  |  |
| Sqcolony | `playable` | 41.9 | 1518 | – | no-audio |  |
| Sqdef 1.4 | `playable` | 41.1 | 1497 | ✓ |  |  |
| Squares-v051 | `playable` | 59.4 | 1506 | ✓ |  |  |
| Squaresliding | `playable` | 59.5 | 1496 | ✓ |  |  |
| StairwayToHeaven | `playable` | 54.1 | 116 | ✓ |  |  |
| starfighter-gp2x-0.01 | `playable` | 63.5 | 1100 | ✓ |  |  |
| StarTrucker | `playable` | 59.2 | 1498 | ✓ |  |  |
| stppc2x-v1.0 | `playable` | 41.3 | 1531 | ✓ |  |  |
| stransball2 | `playable` | 59.3 | 1501 | ✓ |  |  |
| street2x | `playable` | 57.2 | 1559 | ✓ |  |  |
| subhunt | `playable` | 60.0 | 1519 | ✓ |  |  |
| sudoku-v1.0 | `playable` | 60.1 | 1518 | – | no-audio |  |
| sudoku2x-0.5 | `playable` | 59.5 | 1500 | – | no-audio |  |
| SuperChickenFallDemo | `playable` | 61.9 | 1559 | ✓ |  |  |
| SuperPaf_v1.0 | `playable` | 61.0 | 1555 | ✓ |  |  |
| superpang | `playable` | 59.4 | 1516 | ✓ |  |  |
| SuperPixelJumper v1.1 for GP2X | `playable` | 61.1 | 1539 | ✓ |  |  |
| SuperSonicSpeed | `playable` | 61.5 | 1555 | ✓ |  |  |
| supertux-0.1.3-gp2x-v4 | `playable` | 53.6 | 1460 | ✓ |  |  |
| survival | `playable` | 59.9 | 1523 | ✓ |  |  |
| symbolica-0.8 | `playable` | 61.0 | 1544 | ✓ |  |  |
| tail-tale | `playable` | 61.8 | 1556 | ✓ |  |  |
| Tangle | `playable` | 59.4 | 1492 | – | no-audio |  |
| tecnoballz-0.91-gp2x | `playable` | 55.7 | 1439 | ✓ |  |  |
| tetwins | `playable` | 52.7 | 108 | ✓ |  |  |
| the reversed preacher II | `playable` | 76.6 | 2833 | ✓ |  |  |
| ThreeTs_Game | `playable` | 56.4 | 120 | ✓ |  |  |
| Thruster_GP2X | `playable` | 59.1 | 1493 | ✓ |  |  |
| tikka_dungeons_demo_1 | `playable` | 59.7 | 1502 | ✓ |  |  |
| tilematch-0.6 | `playable` | 93.3 | 2358 | ✓ |  |  |
| tileworld2x | `playable` | 56.0 | 1477 | ✓ |  |  |
| tilt | `playable` | 60.2 | 1518 | ✓ |  |  |
| TimeFrack2D for GP2X | `playable` | 49.6 | 101 | – | no-audio |  |
| TouchGames | `playable` | 113.3 | 2863 | ✓ |  |  |
| tower | `playable` | 111.1 | 2813 | – | no-audio |  |
| towertopplergp2x | `playable` | 58.1 | 1517 | ✓ |  |  |
| TRAINS | `playable` | 61.4 | 1552 | ✓ |  |  |
| Trap75 | `playable` | 59.2 | 1488 | ✓ |  |  |
| treev060 | `playable` | 59.0 | 1524 | ✓ |  |  |
| ttxbeta170706b | `playable` | 57.4 | 1516 | – | no-audio |  |
| TUcS.app(V0.7.0 - GP2X) | `playable` | 32.0 | 808 | ✓ |  |  |
| Txishos (Gp2x F-200) | `playable` | 49.0 | 1263 | ✓ |  |  |
| Unicolor | `playable` | 61.2 | 1555 | ✓ |  |  |
| uqm2x_release_1.1 | `playable` | 65.7 | 1675 | ✓ |  |  |
| UQMgp2x-0.5.0_with_content | `playable` | 65.2 | 1651 | ✓ |  |  |
| vectoroids-2x | `playable` | 60.0 | 1516 | ✓ |  |  |
| VekDemo2 | `playable` | 118.8 | 2994 | ✓ |  |  |
| Vektar | `playable` | 115.0 | 2968 | ✓ |  |  |
| vektar-free | `playable` | 30.0 | 759 | ✓ |  |  |
| vektarpack_v1 | `playable` | 88.1 | 2233 | ✓ |  |  |
| Ventifact | `playable` | 59.0 | 1510 | ✓ |  |  |
| vexed-gp2x-10 | `playable` | 59.6 | 1502 | ✓ |  |  |
| vexedb1 | `playable` | 60.2 | 1518 | – | no-audio |  |
| vorton-b4 | `playable` | 58.4 | 1516 | ✓ |  |  |
| vwars | `playable` | 58.8 | 1516 | ✓ |  |  |
| waffle2x | `playable` | 46.5 | 97 | – | no-audio |  |
| war_and_warriorgp2x | `playable` | 60.3 | 1518 | ✓ |  |  |
| warcraft | `playable` | 60.5 | 1562 | ✓ |  |  |
| warehouse_panic_v1.1_gp2x | `playable` | 41.2 | 593 | ✓ |  |  |
| waternetgp2x | `playable` | 56.7 | 1526 | ✓ |  |  |
| wehaveballs | `playable` | 59.3 | 1493 | ✓ |  |  |
| whacky | `playable` | 59.7 | 1505 | ✓ |  |  |
| WindAndWater_teaser_110 | `playable` | 59.0 | 1482 | ✓ |  |  |
| Winter_Jumper | `playable` | 59.4 | 1501 | – | no-audio |  |
| wire3d | `playable` | 57.0 | 1489 | – | no-audio |  |
| wnw | `playable` | 59.2 | 1497 | ✓ |  |  |
| xenitris_demo | `playable` | 62.4 | 1568 | ✓ |  |  |
| xigon-X-gp2x-V1 | `playable` | 60.1 | 1518 | ✓ |  |  |
| Xpired2x 1.0 beta | `playable` | 61.3 | 1554 | ✓ |  |  |
| xRick | `playable` | 59.7 | 1519 | ✓ |  |  |
| yahtzee-v21 | `playable` | 60.1 | 1514 | ✓ |  |  |
| znax | `playable` | 58.1 | 1519 | ✓ |  |  |
| Znumbers | `playable` | 83.1 | 176 | ✓ |  |  |
| Zoids Quest2X-0.0.1-2 | `playable` | 61.4 | 1568 | ✓ |  |  |
| zoltan 2x | `playable` | 58.6 | 1512 | ✓ |  |  |
| zombiesorbet_v1.0_gp2x | `playable` | 59.2 | 1250 | ✓ |  |  |
| zooov11 | `playable` | 32.0 | 806 | ✓ |  |  |
| ztunnel-0 | `playable` | 60.8 | 1536 | ✓ |  |  |

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
| pgw | `incompatible` | 15.1 | 20 | ✓ | no-frames |  |
| quake1-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| quake_0.03 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| RailroadRampage_1.2_Wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| rott | `incompatible` | 0.0 | 0 | – | no-frames |  |
| SmallBall_Wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| srb2 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| wolf4sdl_wiz_svn | `incompatible` | 0.0 | 0 | – | no-frames |  |
| abuse-wiz | `black` | 3.2 | 8 | ✓ | black-screen |  |
| albion-v1.0.1-wiz | `black` | 24.0 | 44 | ✓ | black-screen |  |
| Balloonacy_wiz_wip | `black` | 111.0 | 2815 | ✓ | black-screen |  |
| blingo | `black` | 29.4 | 75 | ✓ | black-screen |  |
| CartoonWiz | `black` | 113.4 | 2858 | ✓ | black-screen |  |
| ColonyConflict_V1.1_B6 | `black` | 106.1 | 2818 | ✓ | black-screen |  |
| Dastardly_Dungeon | `black` | 20.8 | 38 | ✓ | black-screen |  |
| DungeonRunner | `black` | 108.5 | 2817 | ✓ | black-screen |  |
| DuoWIZ_Pong | `black` | 110.1 | 2837 | ✓ | black-screen |  |
| eduke32 | `black` | 11.5 | 16 | – | black-screen |  |
| freecell2x | `black` | 104.1 | 2803 | ✓ | black-screen |  |
| malvado | `black` | 26.7 | 76 | ✓ | black-screen |  |
| March of the mini tux(wiz version) | `black` | 113.7 | 2874 | ✓ | black-screen |  |
| nazcadreams | `black` | 26.1 | 50 | ✓ | black-screen |  |
| nazcarunners | `black` | 29.8 | 62 | ✓ | black-screen |  |
| Nazcasphere | `black` | 33.1 | 70 | ✓ | black-screen |  |
| openjazz-wiz | `black` | 11.2 | 23 | ✓ | black-screen |  |
| opentyrian | `black` | 13.0 | 9 | – | black-screen |  |
| paraballwiz | `black` | 4.8 | 7 | – | black-screen |  |
| PEZ | `black` | 10.6 | 14 | – | black-screen |  |
| PPlane2.WIZ | `black` | 106.6 | 2834 | ✓ | black-screen |  |
| protozoa | `black` | 20.7 | 40 | ✓ | black-screen |  |
| Ruckman-Wiz | `black` | 21.8 | 64 | ✓ | black-screen |  |
| SimOniZ | `black` | 111.4 | 2841 | ✓ | black-screen |  |
| tetwizdownload | `black` | 117.8 | 2969 | ✓ | black-screen |  |
| The Minigame Project | `black` | 54.9 | 1467 | ✓ | black-screen |  |
| tilt | `black` | 18.4 | 25 | ✓ | black-screen |  |
| tricorder | `black` | 30.6 | 66 | ✓ | black-screen |  |
| TUcS.app(V0.7.0 - Wiz) | `black` | 110.4 | 2826 | ✓ | black-screen |  |
| warcraft-beta3-wiz | `black` | 26.1 | 44 | ✓ | black-screen |  |
| wiz-car-binary_090818a | `black` | 58.9 | 1482 | ✓ | black-screen |  |
| Wiz_Blox | `black` | 112.2 | 2871 | ✓ | black-screen |  |
| wiz_drench | `black` | 110.9 | 2816 | ✓ | black-screen |  |
| Wiz_Propis_Demo | `black` | 26.8 | 43 | ✓ | black-screen |  |
| WIZ_S4S | `black` | 112.2 | 2835 | ✓ | black-screen |  |
| WizSticks | `black` | 18.3 | 59 | ✓ | black-screen |  |
| xcom1-v1.0.2-wiz | `black` | 51.9 | 139 | ✓ | black-screen |  |
| xcom2-v1.0.1-wiz | `black` | 112.7 | 2875 | ✓ | black-screen |  |
| [DEMO] Wiztern | `playable` | 55.8 | 973 | ✓ |  |  |
| AdamantArmorAffectionWiz | `playable` | 59.0 | 1491 | ✓ |  |  |
| airstrike-wiz-1.01 | `playable` | 58.8 | 1489 | ✓ |  |  |
| alexsfalldown | `playable` | 59.7 | 1502 | ✓ |  |  |
| altitude | `playable` | 33.0 | 148 | ✓ |  |  |
| Animatch Wiz | `playable` | 56.4 | 1512 | ✓ |  |  |
| Art Shot Wiz | `playable` | 54.2 | 1387 | ✓ |  |  |
| Asteroids | `playable` | 57.7 | 1463 | ✓ |  |  |
| battlejewels-wiz-public001demo | `playable` | 58.6 | 1484 | ✓ |  |  |
| beat2x-wiz | `playable` | 59.3 | 1494 | ✓ |  |  |
| Biological Defend | `playable` | 56.9 | 1452 | ✓ |  |  |
| BitDEFENSE | `playable` | 47.0 | 1253 | – | no-audio |  |
| BlastRiot122Wiz | `playable` | 59.6 | 1503 | ✓ |  |  |
| Blix2x | `playable` | 59.7 | 1499 | ✓ |  |  |
| Boomshine2x_1.12_wiz | `playable` | 57.1 | 1456 | ✓ |  |  |
| BubbleTrainWiz_5-20-09 | `playable` | 55.6 | 1459 | ✓ |  |  |
| BugwarsSE | `playable` | 57.5 | 1458 | ✓ |  |  |
| Camelot Warriors | `playable` | 55.8 | 1459 | ✓ |  |  |
| CDogs-wiz | `playable` | 56.1 | 1420 | ✓ |  |  |
| cgenius-wiz | `playable` | 102.1 | 2658 | ✓ |  |  |
| Dd2x | `playable` | 57.7 | 260 | ✓ |  |  |
| deicide3_eng | `playable` | 58.1 | 411 | ✓ |  |  |
| Demons World | `playable` | 60.1 | 1509 | ✓ |  |  |
| EpicFreeFall_Wiz | `playable` | 50.1 | 1290 | ✓ |  |  |
| epiphany | `playable` | 59.0 | 1512 | ✓ |  |  |
| Geca Blaster 2 (Gp2x Wiz) | `playable` | 52.6 | 1345 | ✓ |  |  |
| Ghostpix | `playable` | 57.6 | 1468 | ✓ |  |  |
| gnurobbo_0.65_wiz | `playable` | 54.1 | 1384 | ✓ |  |  |
| gobble | `playable` | 40.8 | 141 | – | no-audio |  |
| gr-v1001-wiz | `playable` | 56.0 | 1446 | ✓ |  |  |
| herknights | `playable` | 57.0 | 1481 | ✓ |  |  |
| hexen2 | `playable` | 54.8 | 1408 | ✓ |  |  |
| kuklomenos | `playable` | 57.5 | 1523 | ✓ |  |  |
| midway | `playable` | 91.3 | 808 | ✓ |  |  |
| Minigolf | `playable` | 57.3 | 1455 | – | no-audio |  |
| minos-gp2x-wiz | `playable` | 59.7 | 1510 | ✓ |  |  |
| Monster2-1.0-wiz | `playable` | 53.7 | 1536 | ✓ |  |  |
| mush_gp2x | `playable` | 45.1 | 1152 | ✓ |  |  |
| mush_gp2x-0 | `playable` | 35.0 | 922 | ✓ |  |  |
| Myriad | `playable` | 58.4 | 1515 | ✓ |  |  |
| nethack-wiz | `playable` | 59.9 | 1510 | – | no-audio |  |
| NewSuperPang05 | `playable` | 59.5 | 1506 | ✓ |  |  |
| noiz2sa_wiz | `playable` | 58.4 | 1488 | ✓ |  |  |
| openggs | `playable` | 57.2 | 1466 | ✓ |  |  |
| Out Zone | `playable` | 61.8 | 1551 | ✓ |  |  |
| paf | `playable` | 59.4 | 1507 | ✓ |  |  |
| Pentominos | `playable` | 59.8 | 1505 | ✓ |  |  |
| Pharaoh | `playable` | 49.2 | 102 | ✓ |  |  |
| PhishyWiz | `playable` | 57.5 | 1468 | ✓ |  |  |
| Powder2X_wiz_114_v01 | `playable` | 55.7 | 1414 | – | no-audio |  |
| PPlane | `playable` | 60.7 | 1536 | ✓ |  |  |
| prboom-wiz | `playable` | 59.5 | 1515 | – | no-audio |  |
| preggo_Wiz | `playable` | 48.8 | 354 | ✓ |  |  |
| Propis | `playable` | 49.1 | 1239 | ✓ |  |  |
| PuzzleDevilWizDemo | `playable` | 54.6 | 1415 | ✓ |  |  |
| Rezerwar | `playable` | 51.2 | 490 | ✓ |  |  |
| roadfighter | `playable` | 58.9 | 1492 | ✓ |  |  |
| rockrain-gp2x-wiz | `playable` | 59.4 | 1498 | ✓ |  |  |
| Sachunsung2 | `playable` | 49.3 | 103 | ✓ |  |  |
| scummvm-1.2.0-gp2xwiz | `playable` | 54.4 | 1421 | ✓ |  |  |
| Shanghai2 | `playable` | 51.4 | 114 | ✓ |  |  |
| Shock Troopers Base Defense | `playable` | 55.1 | 1463 | ✓ |  |  |
| Skull (Windows, Linux & Gp2x Wiz) | `playable` | 48.0 | 1270 | ✓ |  |  |
| sleuthslots | `playable` | 56.2 | 1458 | ✓ |  |  |
| smw_1.7 | `playable` | 54.5 | 1445 | ✓ |  |  |
| Snow Bros 2 | `playable` | 60.1 | 1509 | ✓ |  |  |
| SOD_Wiz | `playable` | 56.9 | 1467 | ✓ |  |  |
| Sopwith | `playable` | 59.7 | 1501 | ✓ |  |  |
| Space Varments | `playable` | 53.0 | 591 | ✓ |  |  |
| spout | `playable` | 59.3 | 1492 | – | no-audio |  |
| Sqdef_Wiz_14A | `playable` | 56.5 | 1440 | ✓ |  |  |
| Sudoku2X | `playable` | 59.7 | 1503 | – | no-audio |  |
| SudoQ | `playable` | 31.3 | 296 | ✓ |  |  |
| supertux-wiz | `playable` | 54.4 | 1482 | ✓ |  |  |
| Tail Tale | `playable` | 59.6 | 1499 | ✓ |  |  |
| Trap75 | `playable` | 59.5 | 1496 | ✓ |  |  |
| Twin Cobra | `playable` | 60.1 | 1527 | ✓ |  |  |
| uqm2x_release.1.1 | `playable` | 69.7 | 1771 | ✓ |  |  |
| wizchess-v1.1.0-bin | `playable` | 59.3 | 1496 | – | no-audio |  |
| wizchess-v1.2.0-bin | `playable` | 59.1 | 1492 | – | no-audio |  |
| WizFrontier v0.1 | `playable` | 60.7 | 1590 | ✓ |  |  |
| wizgo-v1.1.0-bin | `playable` | 59.0 | 1496 | – | no-audio |  |
| WizGolf | `playable` | 59.1 | 1493 | – | no-audio |  |
| wizmancala-v1.1.2-bin | `playable` | 61.2 | 1544 | – | no-audio |  |
| wizpong | `playable` | 58.9 | 1495 | – | no-audio |  |
| wizznic-0.9.9-wiz | `playable` | 55.7 | 1449 | ✓ |  |  |
| wnw_demo | `playable` | 59.0 | 1488 | ✓ |  |  |
| Worship Vector | `playable` | 61.0 | 1535 | ✓ |  |  |
| WWII | `playable` | 55.9 | 1455 | ✓ |  |  |
| xpiredwiz.eng.101 | `playable` | 58.4 | 1481 | ✓ |  |  |
| xRick | `playable` | 58.9 | 1493 | ✓ |  |  |
| Zero Wing | `playable` | 61.7 | 1549 | ✓ |  |  |
| znumbers | `playable` | 28.4 | 59 | ✓ |  |  |
| Zoltan | `playable` | 58.8 | 1511 | ✓ |  |  |

### Caanoo (194 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| Abbaye_caanoo | `incompatible` | 1.3 | 2 | ✓ | no-frames |  |
| Abbaye_caanoo_v3 | `incompatible` | 2.8 | 3 | ✓ | no-frames |  |
| aggressivepong-pre21.1-gph-uni | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ArtShotCaanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
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
| supertux | `incompatible` | 1.6 | 1 | ✓ | no-frames |  |
| tmw_v1.0.0-beta-2_caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| warcraft-beta3-caanoo | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| zlocada-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zsxd | `incompatible` | 0.0 | 0 | – | no-frames |  |
| aquaVenture | `black` | 46.3 | 472 | ✓ | black-screen |  |
| arcadevol1 | `black` | 5.4 | 5 | ✓ | black-screen |  |
| B'lox! | `black` | 33.3 | 327 | ✓ | black-screen |  |
| Balloonacy | `black` | 40.5 | 398 | ✓ | black-screen |  |
| Blitz | `black` | 14.2 | 39 | ✓ | black-screen |  |
| BubbleTrain | `black` | 1.1 | 2 | ✓ | black-screen |  |
| cat_trap | `black` | 43.3 | 427 | ✓ | black-screen |  |
| Drench | `black` | 20.7 | 53 | ✓ | black-screen |  |
| Flappynerd_Caanoo | `black` | 17.6 | 45 | ✓ | black-screen |  |
| Geek_em_up_CAANOO | `black` | 24.6 | 255 | ✓ | black-screen |  |
| Guru Logic | `black` | 41.2 | 406 | ✓ | black-screen |  |
| JUMPNRUN | `black` | 56.6 | 1543 | ✓ | black-screen |  |
| kenlab-caanoo | `black` | 44.1 | 420 | – | black-screen |  |
| MNV_Caanoo_Release1 | `black` | 20.4 | 52 | – | black-screen |  |
| powermanga-0.80 | `black` | 46.4 | 1209 | ✓ | black-screen |  |
| SantaMania | `black` | 18.0 | 57 | ✓ | black-screen |  |
| STRATEGY | `black` | 9.5 | 12 | ✓ | black-screen |  |
| xcom1-v1.0.2-caanoo | `black` | 33.0 | 44 | ✓ | black-screen |  |
| xcom2-v1.0.1-caanoo | `black` | 58.6 | 1489 | ✓ | black-screen |  |
| aimcaanoo | `ingame` | 46.2 | 1208 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 160px; left and right halves are near- |
| apocalypso Caanoo | `ingame` | 17.1 | 450 | ✓ | low-fps |  |
| Arcadevol3 | `ingame` | 57.8 | 1500 | – | flat-fill |  |
| BubblePop (Caanoo) | `ingame` | 17.7 | 452 | ✓ | low-fps |  |
| caanoo-biniax2-v1.30-bin | `ingame` | 15.7 | 395 | ✓ | low-fps |  |
| can-zomb_3 | `ingame` | 18.9 | 548 | ✓ | low-fps |  |
| chexquest-caanoo | `ingame` | 17.5 | 448 | ✓ | low-fps |  |
| Coral Sea (Caanoo - Bennu) | `ingame` | 14.4 | 374 | ✓ | low-fps |  |
| Deadly Eye (Caanoo) | `ingame` | 18.1 | 462 | ✓ | low-fps |  |
| DefendorX_C | `ingame` | 4.0 | 144 | ✓ | low-fps |  |
| deminor | `ingame` | 11.9 | 27 | – | low-fps |  |
| EEEEK! EEEEEK! HOOOOOOK!!! | `ingame` | 4.9 | 170 | ✓ | garbled-visuals | renders at 640x480 instead of 320x240 |
| EpicFreeFall | `ingame` | 4.2 | 136 | ✓ | low-fps |  |
| EpicFreeFall Caanoo | `ingame` | 2.0 | 65 | ✓ | low-fps |  |
| Firewhip-Caanoo | `ingame` | 19.7 | 522 | ✓ | low-fps |  |
| Geca Blaster 2 (Caanoo) | `ingame` | 13.5 | 347 | ✓ | low-fps |  |
| gnp_104 | `ingame` | 50.0 | 1329 | ✓ | flat-fill |  |
| gnuRobbo | `ingame` | 16.3 | 219 | ✓ | low-fps |  |
| gr-v1001-caanoo | `ingame` | 9.6 | 251 | ✓ | low-fps |  |
| Hamster's Escape 3D (Caanoo) | `ingame` | 6.2 | 58 | ✓ | low-fps |  |
| HamstersEscape (Caanoo) | `ingame` | 8.6 | 80 | ✓ | low-fps |  |
| Hardcore Fight (Caanoo) | `ingame` | 6.5 | 61 | ✓ | low-fps |  |
| jump_n_blob_caanoo | `ingame` | 4.3 | 113 | ✓ | low-fps |  |
| knight | `ingame` | 106.5 | 2682 | ✓ | flat-fill |  |
| Liar | `ingame` | 12.5 | 86 | ✓ | low-fps |  |
| llcpcls-caanoo | `ingame` | 16.6 | 35 | ✓ | low-fps |  |
| MasteriesRunners (Caanoo) | `ingame` | 10.0 | 258 | ✓ | low-fps |  |
| Metal Slug Zombies | `ingame` | 30.3 | 826 | ✓ | garbled-visuals | renders at 640x480 instead of 320x240 |
| mtknights | `ingame` | 30.9 | 779 | ✓ | flat-fill |  |
| nlove_0.6.2_(beta)_caanoo | `ingame` | 17.8 | 44 | – | low-fps |  |
| noiz2sa_caanoo | `ingame` | 39.4 | 1002 | ✓ | flat-fill |  |
| OperationFenix (Caanoo) | `ingame` | 9.1 | 246 | ✓ | low-fps |  |
| PantaVsDragon (Caanoo) | `ingame` | 16.0 | 412 | ✓ | low-fps |  |
| Pharaoh | `ingame` | 12.2 | 25 | ✓ | low-fps |  |
| Protect&rescue | `ingame` | 13.8 | 378 | ✓ | low-fps |  |
| purito_cycling_1.5_Caanoo | `ingame` | 3.7 | 96 | ✓ | low-fps |  |
| pushover-v0.2-bin | `ingame` | 17.2 | 437 | ✓ | low-fps |  |
| rg_ura_103 | `ingame` | 54.6 | 1423 | ✓ | flat-fill |  |
| sbtime_caanoo | `ingame` | 5.9 | 160 | ✓ | low-fps |  |
| Skull (Caanoo) | `ingame` | 15.7 | 403 | ✓ | flat-fill |  |
| smallball | `ingame` | 18.2 | 478 | ✓ | low-fps |  |
| smallball-Caanoo | `ingame` | 18.5 | 476 | ✓ | low-fps |  |
| SnailRace_C | `ingame` | 18.8 | 488 | – | low-fps |  |
| SORRv5_Caanoo | `ingame` | 0.1 | 2 | ✓ | low-fps |  |
| the solitarie | `ingame` | 15.6 | 414 | ✓ | low-fps |  |
| Txishos (Caanoo) | `ingame` | 16.7 | 428 | ✓ | low-fps |  |
| xpiredcan.eng.101 | `ingame` | 0.1 | 3 | ✓ | low-fps |  |
| Zverealm-Caanoo | `ingame` | 8.7 | 273 | ✓ | low-fps |  |
| 20110831 - Bomber Run Redux | `playable` | 36.1 | 932 | – | no-audio |  |
| aaa | `playable` | 58.8 | 1486 | ✓ |  |  |
| aaaa | `playable` | 53.9 | 1368 | ✓ |  |  |
| ADVENTURE | `playable` | 56.3 | 1478 | ✓ |  |  |
| Amoebax | `playable` | 57.2 | 1453 | ✓ |  |  |
| animatch | `playable` | 20.2 | 542 | ✓ |  |  |
| Arcadevol2 | `playable` | 59.0 | 1503 | ✓ |  |  |
| balls12_caanoo_bin | `playable` | 37.0 | 76 | – | no-audio |  |
| battlejewels-105-caanoo-beta | `playable` | 52.4 | 1324 | ✓ |  |  |
| Blackjack21v1.1 | `playable` | 58.2 | 1467 | – | no-audio |  |
| Blingo | `playable` | 58.0 | 1505 | ✓ |  |  |
| Blix2x | `playable` | 60.0 | 1507 | ✓ |  |  |
| caanoo-12swap-v1.0-bin | `playable` | 36.0 | 911 | ✓ |  |  |
| caanoo-chess-v1.1.0-bin | `playable` | 43.9 | 1108 | – | no-audio |  |
| caanoo-gnurobbo-0.68 | `playable` | 32.2 | 430 | ✓ |  |  |
| caanoo-go-v1.1.0-bin | `playable` | 45.3 | 1146 | – | no-audio |  |
| caanoo-mancala-v1.1.0-bin | `playable` | 42.6 | 1077 | – | no-audio |  |
| cavestory | `playable` | 57.0 | 1527 | ✓ |  |  |
| ccrg-caanoo | `playable` | 46.0 | 197 | ✓ |  |  |
| cgenius-caanoo | `playable` | 26.6 | 688 | ✓ |  |  |
| cllwrth | `playable` | 24.7 | 624 | ✓ |  |  |
| cooldowncaanoo | `playable` | 749.4 | 19845 | ✓ |  |  |
| daff_s_adventure_2_caanoo | `playable` | 25.8 | 660 | ✓ |  |  |
| deadlyc | `playable` | 55.4 | 1396 | ✓ |  |  |
| DealorNoDeal | `playable` | 20.3 | 733 | ✓ |  |  |
| demons | `playable` | 116.5 | 2928 | ✓ |  |  |
| dynamate_c | `playable` | 27.9 | 725 | ✓ |  |  |
| echo_caanoo | `playable` | 23.1 | 659 | ✓ |  |  |
| fleshchasmer | `playable` | 54.0 | 1378 | ✓ |  |  |
| freeheroes2_c | `playable` | 41.6 | 85 | ✓ |  |  |
| fshark | `playable` | 84.6 | 2132 | ✓ |  |  |
| Fywod_caanoo | `playable` | 54.2 | 1377 | ✓ |  |  |
| getstar | `playable` | 84.6 | 2123 | ✓ |  |  |
| gravityforcev2 | `playable` | 45.7 | 1154 | ✓ |  |  |
| hellfire | `playable` | 116.5 | 2926 | ✓ |  |  |
| Hero_The_Realm-DEMO | `playable` | 21.6 | 558 | ✓ |  |  |
| HeroTheRealm_DEMOv2 | `playable` | 21.2 | 547 | ✓ |  |  |
| hexahop_1.0 | `playable` | 53.5 | 1346 | – | no-audio |  |
| Humos-Caanoo | `playable` | 24.6 | 684 | ✓ |  |  |
| instead-1.6.1-caanoo | `playable` | 45.1 | 1180 | ✓ |  |  |
| jumpToTheMoon_c | `playable` | 25.6 | 240 | ✓ |  |  |
| ketm | `playable` | 38.2 | 1262 | – | no-audio |  |
| KOF (Ver. 5f) (Caanoo) | `playable` | 27.9 | 801 | ✓ |  |  |
| laserchess_c | `playable` | 20.1 | 41 | – | no-audio |  |
| lmission_0.5 | `playable` | 57.5 | 1446 | ✓ |  |  |
| meritous | `playable` | 82.7 | 2110 | ✓ |  |  |
| MISC | `playable` | 59.7 | 1574 | – | no-audio |  |
| Mission_faileD 1.2 [Caanoo] | `playable` | 32.9 | 914 | ✓ |  |  |
| monster | `playable` | 23.2 | 620 | ✓ |  |  |
| next_element | `playable` | 60.3 | 1520 | ✓ |  |  |
| nuclearchess | `playable` | 220.0 | 5553 | – | no-audio |  |
| outzone | `playable` | 83.8 | 2103 | ✓ |  |  |
| pang | `playable` | 59.0 | 1509 | ✓ |  |  |
| pengupop | `playable` | 37.8 | 169 | ✓ |  |  |
| powder | `playable` | 52.4 | 1331 | – | no-audio |  |
| prboom-caanoo | `playable` | 59.6 | 1516 | – | no-audio |  |
| profanation_Caanoo | `playable` | 20.7 | 45 | ✓ |  |  |
| propis | `playable` | 49.1 | 1235 | ✓ |  |  |
| puzsion | `playable` | 27.2 | 766 | ✓ |  |  |
| PUZZLEBOARDS | `playable` | 109.1 | 2949 | ✓ |  |  |
| RACING | `playable` | 59.8 | 1557 | ✓ |  |  |
| rhythmosplay_1.1.12 | `playable` | 51.2 | 1297 | ✓ |  |  |
| Sachunsung2 | `playable` | 28.0 | 57 | ✓ |  |  |
| sbt | `playable` | 31.2 | 855 | ✓ |  |  |
| Shanghai2 | `playable` | 48.1 | 99 | ✓ |  |  |
| SHOOTERS | `playable` | 76.5 | 2068 | ✓ |  |  |
| SimOniZ | `playable` | 54.3 | 1386 | ✓ |  |  |
| Sitwell (Caanoo) | `playable` | 36.8 | 945 | ✓ |  |  |
| Slap | `playable` | 85.8 | 2153 | ✓ |  |  |
| smw_1.7 | `playable` | 32.0 | 851 | ✓ |  |  |
| snowbros | `playable` | 116.5 | 2925 | ✓ |  |  |
| snowbros2 | `playable` | 115.9 | 2911 | ✓ |  |  |
| SOD(r181) | `playable` | 23.3 | 615 | ✓ |  |  |
| space52_caanoo | `playable` | 33.0 | 876 | ✓ |  |  |
| SPORTS | `playable` | 59.0 | 1525 | ✓ |  |  |
| sqrxz-v0996-caanoo | `playable` | 52.3 | 1334 | ✓ |  |  |
| sqrxz2-v0.80-caanoo | `playable` | 57.2 | 1455 | ✓ |  |  |
| stppc-caanoo-29-11-2010 | `playable` | 33.1 | 322 | ✓ |  |  |
| tailtale4c | `playable` | 61.6 | 1550 | ✓ |  |  |
| Tigerhell | `playable` | 84.7 | 2126 | ✓ |  |  |
| Tile | `playable` | 54.6 | 1504 | ✓ |  |  |
| tlosaf_v12-caanoo | `playable` | 61.6 | 1550 | – | no-audio |  |
| tong-caanoo | `playable` | 56.3 | 1471 | ✓ |  |  |
| Trap75 | `playable` | 52.1 | 1312 | ✓ |  |  |
| Truxton | `playable` | 84.0 | 2108 | ✓ |  |  |
| truxton2 | `playable` | 84.2 | 2114 | ✓ |  |  |
| twincobr | `playable` | 83.8 | 2106 | ✓ |  |  |
| twinhawk | `playable` | 83.5 | 2100 | ✓ |  |  |
| Vigo | `playable` | 55.2 | 1427 | – | no-audio |  |
| Wardner | `playable` | 118.8 | 2985 | ✓ |  |  |
| warehouse_panic_v1.1_caanoo | `playable` | 29.3 | 420 | ✓ |  |  |
| WindandWater | `playable` | 59.5 | 1504 | ✓ |  |  |
| Wizznic 0.9.2- preview | `playable` | 30.5 | 812 | ✓ |  |  |
| wolf4sdl-caanoo | `playable` | 59.3 | 1516 | – | no-audio |  |
| wvector | `playable` | 44.0 | 1106 | ✓ |  |  |
| zelda-roth-olb-3t_caanoo | `playable` | 20.6 | 549 | ✓ |  |  |
| zerowing | `playable` | 118.6 | 2979 | ✓ |  |  |
| zombiesorbet_v1.0_caanoo | `playable` | 23.1 | 592 | ✓ |  |  |
| zomg-Caanoo | `playable` | 21.3 | 566 | ✓ |  |  |
