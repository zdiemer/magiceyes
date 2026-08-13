# magiceyes compatibility sweep

Every GP2X / Wiz / Caanoo title on the corpus share, booted headlessly through the native engine (`bin/me_unicorn`) and scored from the shm framebuffer + the structured run report. Regenerate with `tools/test/compat_report.py`.


## Summary

| Platform | Titles | Playable | Ingame | Black | Incompatible | Crashed |
|---|--:|--:|--:|--:|--:|--:|
| GP2X | 631 | 439 | 18 | 86 | 88 | 0 |
| Wiz | 147 | 75 | 3 | 42 | 27 | 0 |
| Caanoo | 194 | 113 | 28 | 23 | 30 | 0 |
| **All** | **972** | **627** | **49** | **151** | **145** | **0** |

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
| **Boots but renders only black** (`black-screen`) | 151 | Caanoo, GP2X, Wiz | n/a |
| **Renders at speed but no audio** (`no-audio`) | 100 | Caanoo, GP2X, Wiz | n/a |
| **Never rendered a frame (cause unknown)** (`no-frames`) | 79 | Caanoo, GP2X, Wiz | n/a |
| **Not a 32-bit ARM ELF** (`not-arm-elf`) | 45 | GP2X | n/a |
| **Renders but below 20 fps** (`low-fps`) | 19 | Caanoo, GP2X, Wiz | n/a |
| **Draws only a flat colour** (`flat-fill`) | 17 | Caanoo, GP2X, Wiz | n/a |
| **Renders, but the picture is wrong** (`garbled-visuals`) | 12 | Caanoo, GP2X, Wiz | n/a |
| **Game data files are missing from the dump** (`missing-game-data`) | 7 | Caanoo, GP2X | n/a |
| **Unknown /dev node** (`unknown-device`) | 6 | Caanoo, GP2X | `/dev/input/mouse/0` ×3, `/dev/accel` ×1, `/dev/cx25874` ×1, `/dev/graphics/fb0` ×1 |
| **Archive extraction failed** (`archive-failed`) | 5 | Caanoo, GP2X | n/a |
| **Unimplemented syscall** (`unimplemented-syscall`) | 2 | GP2X | `113` ×1, `117` ×1 |
| **Could not open a display** (`display-init-failed`) | 1 | GP2X | n/a |
| **Spins forever polling an MMSP2 register** (`mmio-spin`) | 1 | Wiz | `0x4000` ×1 |

## Renders, but the picture is wrong

These 12 titles pass the running checks (frames advancing, frame rate, audio) while the frame itself is visibly broken, so they are graded `ingame` rather than `playable`. The reasons come from measuring the captured frame: a consistent per-row offset means a stride/pitch mismatch, large-scale repetition means the screen holds more than one copy of itself, and noise far above what dithered artwork reaches means corrupt memory.

| Title | Platform | fps | What the frame looks like |
|---|---|--:|---|
| aimcaanoo | Caanoo | 49.4 | the screen holds a second copy of itself, offset by 160px; left and right halves are near-identical; top and bottom halves are near-identical |
| EEEEK! EEEEEK! HOOOOOOK!!! | Caanoo | 20.8 | renders at 640x480 instead of 320x240 |
| Metal Slug Zombies | Caanoo | 26.1 | renders at 640x480 instead of 320x240 |
| mtknights | Caanoo | 31.2 | the screen holds a second copy of itself, offset by 128px |
| Skull (Caanoo) | Caanoo | 104.8 | renders at 320x200 instead of 320x240 |
| 1945_GP2X_0.2b | GP2X | 57.9 | pixel-to-pixel noise of 96, far above what dithered artwork reaches; the frame looks like corrupt memory |
| BunnyTraps-v11 | GP2X | 61.9 | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like corrupt memory |
| FleshChasmer | GP2X | 60.1 | the screen holds a second copy of itself, offset by 160px; left and right halves are near-identical |
| GF | GP2X | 61.2 | top and bottom halves are near-identical |
| Life.0.1 | GP2X | 61.9 | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like corrupt memory |
| MoveSweep2X | GP2X | 50.1 | the screen holds a second copy of itself, offset by 96px; left and right halves are near-identical |
| Worship Vector | Wiz | 60.9 | the screen holds a second copy of itself, offset by 160px; left and right halves are near-identical |

## Scored as working, but only painting a flat colour

These 17 titles advanced frames, kept audio running, and held frame rate, so they land in `playable`/`renders`. Their framebuffer never held more than one or two colours, which means the tier overstates them. Worth treating as broken.

| Title | Platform | Status | fps |
|---|---|---|--:|
| Arcadevol3 | Caanoo | `renders` | 58.9 |
| gnp_104 | Caanoo | `playable` | 50.3 |
| knight | Caanoo | `playable` | 105.6 |
| meritous | Caanoo | `playable` | 67.7 |
| noiz2sa_caanoo | Caanoo | `playable` | 40.3 |
| rg_ura_103 | Caanoo | `playable` | 53.9 |
| _-The Reversed Preacher 3-_Hack bIld_ | GP2X | `playable` | 55.4 |
| _-the reversed preacher II-_ | GP2X | `playable` | 58.5 |
| ASCIIPong2xV0.4 | GP2X | `playable` | 54.3 |
| dumbbell2x-01 | GP2X | `renders` | 60.9 |
| game bIld 2 | GP2X | `playable` | 61.6 |
| Knight Lore | GP2X | `playable` | 106.6 |
| levelEdit | GP2X | `renders` | 61.6 |
| robot-escape | GP2X | `playable` | 86.9 |
| superpang | GP2X | `playable` | 45.8 |
| the reversed preacher II | GP2X | `playable` | 54.5 |
| Skull (Windows, Linux & Gp2x Wiz) | Wiz | `playable` | 20.2 |

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
| `/dev/input/mouse/0` | 211 |
| `/dev/psaux` | 184 |
| `/dev/usbmouse` | 184 |
| `/dev/input/mouse0` | 20 |
| `/dev/accel` | 17 |
| `/dev/input/mice` | 4 |
| `/dev/mouse` | 4 |
| `/dev/` | 3 |
| `/dev/pts/` | 3 |
| `/dev/gpmdata` | 3 |
| `/dev/pollux_batt` | 3 |
| `/dev/batt` | 3 |
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
| `unknown_mmio:0x90a` | 414 |
| `unknown_ioctl:fb` | 239 |
| `unknown_mmio:0x4058` | 216 |
| `unknown_mmio:0x405c` | 211 |
| `unknown_mmio:0x4060` | 211 |
| `unknown_mmio:0x910` | 105 |
| `unknown_mmio:0x1988` | 56 |
| `unknown_mmio:0x19c0` | 56 |
| `unknown_mmio:0x19c4` | 56 |
| `unknown_mmio:0x924` | 48 |
| `unknown_mmio:0x3b46` | 46 |
| `unknown_mmio:0x91c` | 46 |
| `unknown_mmio:0x4070` | 37 |
| `unknown_mmio:0x3802` | 22 |
| `unknown_mmio:0x3804` | 22 |
| `unknown_mmio:0xfff6f004` | 13 |
| `unknown_mmio:0xf16` | 12 |
| `unknown_mmio:0xf58` | 12 |
| `unknown_mmio:0x808` | 12 |
| `unsupported_blit:dst-unmapped` | 11 |
| `unknown_mmio:0xfffe2880` | 9 |
| `unknown_mmio:0xfffe2906` | 9 |
| `unknown_mmio:0xfffe2908` | 9 |
| `unknown_mmio:0xfffe290a` | 9 |
| `unknown_mmio:0xfffe2912` | 9 |

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
| blocksGP2X-0 | `incompatible` | 0.0 | 0 | – | unimplemented-syscall | 113 |
| Boomshine2x_(java) | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Boomshine2x_(java)/Boomshine2x/Boomshine2x.gpe' is not a |
| bunkermaster2x04 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| cackb2 | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| chicken-puyopuyo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| CloneKeen2X-1.0a | `incompatible` | 0.0 | 0 | – | no-frames |  |
| d1x-rebirth-gp2x_v0.50a | `incompatible` | 0.0 | 0 | – | unimplemented-syscall | 117 |
| DeathChase4GP2X-V0.1b | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/DeathChase4GP2X-V0.1b/deathchase3d-0.9/deathchase3d/Deat |
| dkbk2x-0.1 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| doom | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/doom/doom/10sector.gpe' is not an ARM ELF and no runnabl |
| doom_mod_examples | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/doom_mod_examples/game/interpreters/doom/pwad1/prboom_gm |
| DoomPwadPack | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/DoomPwadPack/AliensTC.gpe' is not an ARM ELF and no runn |
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
| nethack-ascii-3.4.3port1 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| nethack-caduhack.r01 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| nethack06 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ohthehumanity-1.0.0 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/ohthehumanity-1.0.0/ohthehumanity/ohthehumanity.gpe' is  |
| onscripter2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| OpenTTD | `incompatible` | 0.0 | 0 | – | no-frames |  |
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
| UQMgp2x-0.5.0_with_content | `incompatible` | 2.0 | 1 | – | no-frames |  |
| wads1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/wads1/wads1/requiem.gpe' is not an ARM ELF and no runnab |
| wads2 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/wads2/wads2/h2h-xmas.gpe' is not an ARM ELF and no runna |
| Wolf4SDL | `incompatible` | 0.0 | 0 | – | no-frames |  |
| worminator302 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Zombiepox2X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| 2xHexen2 v0.05 PB2 | `black` | 27.4 | 28 | – | black-screen |  |
| 2xWargus_PB1.3 | `black` | 0.9 | 15 | ✓ | black-screen |  |
| 2xZdoom_PB1.2 | `black` | 33.7 | 52 | – | black-screen |  |
| A1GP2XV1_1 | `black` | 26.3 | 15 | – | black-screen |  |
| abuse_1.0 | `black` | 26.2 | 697 | ✓ | black-screen |  |
| albion-v1.0.1-gp2x | `black` | 26.3 | 28 | ✓ | black-screen |  |
| AlienBlaster_1.02 | `black` | 11.3 | 16 | ✓ | black-screen |  |
| angband2x-v2 | `black` | 36.6 | 79 | – | black-screen |  |
| bang_gp | `black` | 39.9 | 38 | ✓ | black-screen |  |
| BeetleRun | `black` | 19.5 | 10 | ✓ | black-screen |  |
| blockoid | `black` | 9.5 | 7 | ✓ | black-screen |  |
| Bombs Panic | `black` | 91.6 | 194 | ✓ | black-screen |  |
| Boulders-0 | `black` | 22.9 | 17 | ✓ | black-screen |  |
| BubbleTrain_GP2X-2006_Entry | `black` | 1.1 | 2 | ✓ | black-screen |  |
| CaptainCrusader_GP2XDemo | `black` | 5.8 | 3 | – | black-screen |  |
| CaptainCrusader_GP2XFull | `black` | 6.0 | 3 | – | black-screen |  |
| cat_trap | `black` | 112.3 | 2873 | ✓ | black-screen |  |
| Comando2gp2xEN | `black` | 31.2 | 16 | ✓ | black-screen |  |
| coppergreen | `black` | 14.5 | 16 | ✓ | black-screen |  |
| d2x-gp2x-0.02 | `black` | 20.9 | 17 | ✓ | black-screen |  |
| Dark_Light_SDL2X | `black` | 19.9 | 14 | ✓ | black-screen |  |
| DeathTrap1_1 | `black` | 11.4 | 16 | ✓ | black-screen |  |
| Digger | `black` | 36.0 | 83 | ✓ | black-screen |  |
| dodge | `black` | 20.4 | 20 | ✓ | black-screen |  |
| duke2x004 | `black` | 17.6 | 9 | – | black-screen |  |
| egoboo-cramfs | `black` | 36.6 | 67 | ✓ | black-screen |  |
| fenix | `black` | 12.6 | 29 | ✓ | black-screen |  |
| fenixGamePack | `black` | 21.0 | 73 | ✓ | black-screen |  |
| FFDoom | `black` | 4.0 | 2 | – | black-screen |  |
| Flappynerd_GP2X | `black` | 45.9 | 122 | ✓ | black-screen |  |
| FleshChasmer132c_patch | `black` | 11.9 | 6 | ✓ | black-screen |  |
| FleshChasmer_Dpad | `black` | 10.0 | 5 | ✓ | black-screen |  |
| FlipIR_GP2X | `black` | 115.6 | 2925 | ✓ | black-screen |  |
| godori | `black` | 7.7 | 4 | – | black-screen |  |
| gp2x-blobwars-0.1 | `black` | 115.8 | 2927 | ✓ | black-screen |  |
| gp2x-bubbletrain-0.1 | `black` | 111.6 | 2913 | ✓ | black-screen |  |
| gp2x-netrok-0.1 | `black` | 106.7 | 3043 | ✓ | black-screen |  |
| gp2x-sand-0.3 | `black` | 58.0 | 119 | – | black-screen |  |
| gp2x-tenmado-0.1 | `black` | 114.5 | 2911 | – | black-screen |  |
| GP2X_Nat2007 | `black` | 115.5 | 2916 | ✓ | black-screen |  |
| gp2xDoukutsu-1.04 | `black` | 20.3 | 17 | ✓ | black-screen |  |
| gp2xJenkasNightmare | `black` | 19.6 | 18 | ✓ | black-screen |  |
| GPgeneral | `black` | 3.8 | 2 | – | black-screen |  |
| gpnoid2x | `black` | 27.5 | 25 | ✓ | black-screen |  |
| GPrina-GP2x_v1.0 | `black` | 60.5 | 1546 | ✓ | black-screen |  |
| just4qix | `black` | 18.7 | 10 | ✓ | black-screen |  |
| liquidwar2x02 | `black` | 5.5 | 3 | – | black-screen |  |
| nazcarunners-0 | `black` | 20.6 | 13 | ✓ | black-screen |  |
| nazcasphere | `black` | 43.3 | 55 | ✓ | black-screen |  |
| openggs | `black` | 60.6 | 1555 | ✓ | black-screen |  |
| openjazz-gp2x | `black` | 14.7 | 14 | ✓ | black-screen |  |
| oxov06 | `black` | 42.7 | 88 | – | black-screen |  |
| pacmame | `black` | 11.0 | 7 | – | black-screen |  |
| para3 | `black` | 56.3 | 150 | ✓ | black-screen |  |
| pez | `black` | 19.9 | 10 | – | black-screen |  |
| PrBoom PWAD pack | `black` | 2.4 | 5 | – | black-screen |  |
| protozoa v1.0 | `black` | 31.5 | 34 | ✓ | black-screen |  |
| raw2xv0.3.1 | `black` | 13.9 | 7 | – | black-screen |  |
| ruckman_v1.03 | `black` | 32.6 | 98 | ✓ | black-screen |  |
| ShadowWarrior2X | `black` | 8.5 | 5 | – | black-screen |  |
| SimOniZ | `black` | 111.8 | 2866 | ✓ | black-screen |  |
| SmashGp2x02 | `black` | 60.8 | 1542 | ✓ | black-screen |  |
| sopwith_camel_rc3 | `black` | 7.8 | 4 | ✓ | black-screen |  |
| space52_gp2x(oficial) | `black` | 0.9 | 17 | – | black-screen |  |
| sprint_race | `black` | 11.1 | 7 | – | black-screen |  |
| starsystem | `black` | 37.1 | 29 | ✓ | black-screen |  |
| step2x02 | `black` | 50.3 | 108 | ✓ | black-screen |  |
| tesla-Siren | `black` | 27.2 | 22 | ✓ | black-screen |  |
| Tetrablocks.0.4.GP2X | `black` | 46.2 | 104 | ✓ | black-screen |  |
| tilt | `black` | 36.1 | 27 | ✓ | black-screen |  |
| TRAINS | `black` | 28.0 | 18 | ✓ | black-screen |  |
| uhexen | `black` | 6.4 | 4 | – | black-screen |  |
| ultratumba_exp-20100925.gp2x | `black` | 14.0 | 8 | ✓ | black-screen |  |
| uqm2x_release_1.1 | `black` | 78.7 | 368 | – | black-screen |  |
| Volleyball | `black` | 53.1 | 110 | ✓ | black-screen |  |
| warcraft-beta3-gp2x | `black` | 34.4 | 30 | ✓ | black-screen |  |
| Winter_Jumper | `black` | 5.6 | 3 | – | black-screen |  |
| Wizznic_2x_07alpha2 | `black` | 10.7 | 19 | ✓ | black-screen |  |
| wizznic_gp2x-0.9.9 | `black` | 10.5 | 17 | ✓ | black-screen |  |
| wolfdx | `black` | 38.0 | 19 | ✓ | black-screen |  |
| xbak-0.1.3 | `black` | 27.8 | 14 | – | black-screen |  |
| xcom1-v1.0.2-gp2x | `black` | 43.0 | 1591 | ✓ | black-screen |  |
| xcom2-v1.0.1-gp2x | `black` | 106.7 | 2789 | ✓ | black-screen |  |
| xump2x_beta2 | `black` | 27.0 | 14 | ✓ | black-screen |  |
| zcgp2x_211B18_0.4alpha | `black` | 20.0 | 22 | – | black-screen |  |
| Zelda_roth_US_gp2x | `black` | 19.3 | 41 | ✓ | black-screen |  |
| 1945_GP2X_0.2b | `ingame` | 57.9 | 542 | ✓ | garbled-visuals | pixel-to-pixel noise of 96, far above what dithered artwork reaches; the frame looks like  |
| _-The Reversed Preacher 3-_Hack bIld_ | `ingame` | 55.4 | 130 | ✓ | flat-fill |  |
| _-the reversed preacher II-_ | `ingame` | 58.5 | 136 | ✓ | flat-fill |  |
| ASCIIPong2xV0.4 | `ingame` | 54.3 | 1370 | ✓ | flat-fill |  |
| BunnyTraps-v11 | `ingame` | 61.9 | 1559 | ✓ | garbled-visuals | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like |
| Clonk2X_1.0 | `ingame` | 16.2 | 409 | – | not-arm-elf | magiceyes: reload of '/bin/sh' failed |
| CromoZome | `ingame` | 19.6 | 510 | ✓ | low-fps |  |
| dumbbell2x-01 | `ingame` | 60.9 | 569 | – | flat-fill |  |
| FleshChasmer | `ingame` | 60.1 | 1532 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 160px; left and right halves are near- |
| game bIld 2 | `ingame` | 61.6 | 1550 | ✓ | flat-fill |  |
| GF | `ingame` | 61.2 | 1553 | ✓ | garbled-visuals | top and bottom halves are near-identical |
| Knight Lore | `ingame` | 106.6 | 2683 | ✓ | flat-fill |  |
| levelEdit | `ingame` | 61.6 | 1554 | – | flat-fill |  |
| Life.0.1 | `ingame` | 61.9 | 1558 | – | garbled-visuals | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like |
| MoveSweep2X | `ingame` | 50.1 | 104 | – | garbled-visuals | the screen holds a second copy of itself, offset by 96px; left and right halves are near-i |
| robot-escape | `ingame` | 86.9 | 188 | ✓ | flat-fill |  |
| superpang | `ingame` | 45.8 | 156 | ✓ | flat-fill |  |
| the reversed preacher II | `ingame` | 54.5 | 126 | ✓ | flat-fill |  |
| 2xpong_gp2x | `playable` | 61.1 | 1536 | ✓ |  |  |
| 2xtron-v01 | `playable` | 61.9 | 1559 | ✓ |  |  |
| 2xZdoom_selector | `playable` | 65.8 | 152 | ✓ |  |  |
| 4WE_GP2x | `playable` | 115.5 | 1980 | ✓ |  |  |
| 9 Lives | `playable` | 69.8 | 1765 | ✓ |  |  |
| a_sn-pong | `playable` | 41.6 | 1538 | – | no-audio |  |
| abe | `playable` | 60.9 | 1534 | ✓ |  |  |
| AbusimbelProfanationDeluxe | `playable` | 118.6 | 3005 | ✓ |  |  |
| AdamantArmorAffection2x | `playable` | 60.6 | 1535 | ✓ |  |  |
| ADIC2X | `playable` | 87.6 | 1041 | ✓ |  |  |
| AfterBurner-GP2X | `playable` | 35.8 | 909 | ✓ |  |  |
| Airplyr | `playable` | 61.8 | 1555 | ✓ |  |  |
| airstrike-1.1 | `playable` | 60.9 | 1548 | ✓ |  |  |
| Akd_BB | `playable` | 61.2 | 1551 | ✓ |  |  |
| alex | `playable` | 61.5 | 1546 | ✓ |  |  |
| Alex's Falldown | `playable` | 61.4 | 1554 | ✓ |  |  |
| alex4_gp2x | `playable` | 61.4 | 1548 | ✓ |  |  |
| altitude | `playable` | 44.1 | 142 | ✓ |  |  |
| AMazing-3D | `playable` | 64.2 | 1620 | – | no-audio |  |
| amoebax-0.2.1-gp2x | `playable` | 54.7 | 1391 | ✓ |  |  |
| armorcity-0_30b | `playable` | 60.4 | 1551 | ✓ |  |  |
| Asteroids | `playable` | 60.3 | 1549 | ✓ |  |  |
| astrochaos | `playable` | 58.1 | 333 | ✓ |  |  |
| atris-1.0.7 | `playable` | 60.5 | 1563 | ✓ |  |  |
| B'lox! | `playable` | 114.2 | 2932 | ✓ |  |  |
| BareFistFighter | `playable` | 61.2 | 1538 | ✓ |  |  |
| barrage | `playable` | 61.9 | 1559 | ✓ |  |  |
| Batiscafo (versin EXP) | `playable` | 61.2 | 1543 | ✓ |  |  |
| battlejewels-gp2x-062-100 | `playable` | 115.5 | 2914 | ✓ |  |  |
| BearOids | `playable` | 61.1 | 1539 | ✓ |  |  |
| beat2x-0.5-bin | `playable` | 61.0 | 1548 | ✓ |  |  |
| Beatbox_1.2 | `playable` | 59.3 | 1549 | ✓ |  |  |
| biniax-gp2x_v1.2 | `playable` | 61.0 | 1539 | ✓ |  |  |
| Biniax2_gp2x | `playable` | 81.4 | 2065 | ✓ |  |  |
| Biohazard2 | `playable` | 61.9 | 1560 | ✓ |  |  |
| BioShoot GP2X | `playable` | 60.3 | 1544 | ✓ |  |  |
| Birdshoot | `playable` | 61.7 | 1548 | – | no-audio |  |
| BisfoG | `playable` | 77.1 | 921 | ✓ |  |  |
| blastriot1.21 | `playable` | 58.8 | 1484 | ✓ |  |  |
| blazar_v1-30_gp2x | `playable` | 61.5 | 1550 | ✓ |  |  |
| blingo 1.2 | `playable` | 48.5 | 151 | ✓ |  |  |
| blipsgp2x | `playable` | 60.5 | 1558 | ✓ |  |  |
| Blix2x | `playable` | 61.6 | 1554 | ✓ |  |  |
| blobbyvolley | `playable` | 60.7 | 1535 | ✓ |  |  |
| blobwars_2x | `playable` | 62.3 | 1578 | ✓ |  |  |
| block | `playable` | 61.2 | 1557 | ✓ |  |  |
| blockdudegp2x | `playable` | 57.8 | 1553 | ✓ |  |  |
| Blocked | `playable` | 111.7 | 2844 | ✓ |  |  |
| Blockrage2x | `playable` | 60.7 | 1534 | ✓ |  |  |
| blox | `playable` | 44.9 | 1132 | ✓ |  |  |
| Bloxz_DEMO | `playable` | 61.5 | 1557 | ✓ |  |  |
| bluecube2x | `playable` | 61.5 | 1548 | ✓ |  |  |
| bobtron-gp2x | `playable` | 61.5 | 1549 | ✓ |  |  |
| Boomshine2x_1.12_gp2x | `playable` | 60.5 | 1550 | ✓ |  |  |
| brassmunkey_gp2x_1.0 | `playable` | 60.6 | 1534 | ✓ |  |  |
| BubbleX | `playable` | 61.4 | 1541 | ✓ |  |  |
| BubTrain_GP2X-2006_Entry_No-Sound | `playable` | 58.7 | 1542 | ✓ |  |  |
| bugafactorx-v03-beta | `playable` | 61.0 | 1549 | – | no-audio |  |
| BugWarsSE_v1.0 | `playable` | 60.0 | 1547 | ✓ |  |  |
| bumprace-0.2 | `playable` | 59.9 | 1551 | ✓ |  |  |
| BurokkuDemo1 | `playable` | 61.7 | 1558 | ✓ |  |  |
| buscaminas | `playable` | 61.3 | 1548 | – | no-audio |  |
| buttongame | `playable` | 48.1 | 99 | ✓ |  |  |
| BuzzysBadDay-1.0 | `playable` | 61.3 | 1551 | ✓ |  |  |
| CamelotWarriors-GP2x_v1.0 | `playable` | 60.8 | 1547 | ✓ |  |  |
| cardm | `playable` | 61.1 | 1547 | – | no-audio |  |
| CascadeBeneath v1.0 for GP2X | `playable` | 62.0 | 1563 | ✓ |  |  |
| cavecopter_gp2x | `playable` | 20.9 | 527 | – | no-audio |  |
| ccrg | `playable` | 55.2 | 242 | ✓ |  |  |
| cdogs2x04 | `playable` | 97.4 | 2474 | ✓ |  |  |
| cgenius-gp2x | `playable` | 58.2 | 1506 | ✓ |  |  |
| chaos2x | `playable` | 61.8 | 1554 | ✓ |  |  |
| checkersgp2x | `playable` | 59.4 | 1553 | ✓ |  |  |
| chess2x05 | `playable` | 60.5 | 1555 | ✓ |  |  |
| Chopper | `playable` | 60.9 | 1547 | – | no-audio |  |
| ChopperAttackv1.0.17 | `playable` | 103.4 | 2862 | ✓ |  |  |
| Chroma | `playable` | 115.8 | 2924 | ✓ |  |  |
| chuckiev12 | `playable` | 61.5 | 1548 | ✓ |  |  |
| Codemaster | `playable` | 116.3 | 2932 | ✓ |  |  |
| ConnyCarrot | `playable` | 60.9 | 1547 | ✓ |  |  |
| cosmo2x_01 | `playable` | 61.8 | 1563 | – | no-audio |  |
| CowSuckers-1.0 | `playable` | 61.5 | 1552 | ✓ |  |  |
| Crapong | `playable` | 60.8 | 1545 | ✓ |  |  |
| crazeeman | `playable` | 64.4 | 1630 | ✓ |  |  |
| crimsonV1 | `playable` | 59.7 | 1552 | ✓ |  |  |
| crocodingusgp2x | `playable` | 112.6 | 1512 | ✓ |  |  |
| crossroads | `playable` | 61.3 | 1550 | ✓ |  |  |
| CUBES | `playable` | 61.7 | 1560 | ✓ |  |  |
| cyberhockeyV2_6 | `playable` | 61.4 | 1545 | ✓ |  |  |
| DABAKKA-0 | `playable` | 61.5 | 1543 | ✓ |  |  |
| Dance2x Alpha GPE | `playable` | 60.7 | 1552 | ✓ |  |  |
| DangerMouse | `playable` | 116.0 | 2928 | ✓ |  |  |
| Dastardly dungeon 1.5 | `playable` | 45.0 | 112 | ✓ |  |  |
| dd2x | `playable` | 108.3 | 484 | ✓ |  |  |
| DealOrNoDeal-v12 | `playable` | 61.0 | 1561 | ✓ |  |  |
| debian_vs_pimientos_2x_0.1.2 | `playable` | 58.4 | 551 | ✓ |  |  |
| defeatme-gp2x-1.0.1 | `playable` | 60.9 | 1533 | ✓ |  |  |
| diamant_1_01 | `playable` | 32.1 | 813 | ✓ |  |  |
| DontGetCrushed v1.0 for GP2X | `playable` | 62.3 | 1565 | ✓ |  |  |
| dopewars2x | `playable` | 61.1 | 1534 | – | no-audio |  |
| dosmugen | `playable` | 59.9 | 1550 | ✓ |  |  |
| Dr. Mates v1.0 | `playable` | 43.3 | 100 | ✓ |  |  |
| Drill2x_final | `playable` | 59.9 | 1533 | ✓ |  |  |
| drill2x_xtreme_v1.0.3 | `playable` | 60.9 | 1549 | ✓ |  |  |
| drod-gp2x-1_0 | `playable` | 51.4 | 1335 | – | no-audio |  |
| dstroyGP2X1402 | `playable` | 60.2 | 1547 | – | no-audio |  |
| DubaiRace038a | `playable` | 49.7 | 114 | ✓ |  |  |
| dyc_gp2x | `playable` | 112.0 | 2846 | ✓ |  |  |
| dynamategp2x | `playable` | 60.1 | 1551 | ✓ |  |  |
| E-Fighters2x_FIRST_ALPHA_0_0_5_fixedSound | `playable` | 115.0 | 2920 | ✓ |  |  |
| EasterQuest | `playable` | 115.7 | 2927 | ✓ |  |  |
| eggstreme3_v1-00_gp2x | `playable` | 61.4 | 1546 | ✓ |  |  |
| egoboo2xFeb1207 | `playable` | 99.8 | 3049 | ✓ |  |  |
| Electronia | `playable` | 61.9 | 1559 | ✓ |  |  |
| enigma | `playable` | 51.1 | 1369 | ✓ |  |  |
| entombed2x | `playable` | 61.0 | 1550 | ✓ |  |  |
| EpicFreeFall_GP2X | `playable` | 60.8 | 1550 | ✓ |  |  |
| EpicRocks_GP2X | `playable` | 58.5 | 684 | ✓ |  |  |
| escapa-v1 | `playable` | 61.6 | 1554 | ✓ |  |  |
| escoba_exp-20101016.gp2x | `playable` | 61.0 | 1550 | – | no-audio |  |
| exi_shoot_gp2x | `playable` | 61.1 | 1540 | ✓ |  |  |
| extraterrestres | `playable` | 96.8 | 2612 | ✓ |  |  |
| extraterrestres-0 | `playable` | 58.3 | 1563 | – | no-audio |  |
| exult_rc3 | `playable` | 41.7 | 1543 | ✓ |  |  |
| Factor-v1.0-final | `playable` | 60.8 | 1547 | ✓ |  |  |
| falldown_gp2x | `playable` | 89.2 | 2237 | ✓ |  |  |
| FCRLG | `playable` | 61.2 | 1540 | – | no-audio |  |
| fifteen_01 | `playable` | 60.0 | 563 | – | no-audio |  |
| FindMii | `playable` | 115.3 | 2928 | ✓ |  |  |
| Firewhip | `playable` | 100.6 | 1477 | ✓ |  |  |
| Fishball-1.2 | `playable` | 60.6 | 1531 | ✓ |  |  |
| fissionfield2x | `playable` | 61.5 | 1553 | ✓ |  |  |
| Flaschenspiel | `playable` | 61.0 | 1551 | ✓ |  |  |
| FleshChasmer Zero | `playable` | 60.8 | 1535 | ✓ |  |  |
| floaters | `playable` | 61.2 | 1550 | ✓ |  |  |
| flobopuyo0.20.1 | `playable` | 59.8 | 1521 | ✓ |  |  |
| flowflowmania-0_6-gp2x | `playable` | 44.7 | 435 | – | no-audio |  |
| flurkies_v1-25_gp2x | `playable` | 61.5 | 1552 | ✓ |  |  |
| fm | `playable` | 110.1 | 2760 | ✓ |  |  |
| Football2X | `playable` | 116.1 | 2922 | ✓ |  |  |
| formula1gp2x | `playable` | 61.0 | 1550 | ✓ |  |  |
| Fragger2x | `playable` | 61.9 | 1556 | ✓ |  |  |
| freec2x | `playable` | 41.5 | 1054 | ✓ |  |  |
| freecell_1 | `playable` | 52.3 | 111 | ✓ |  |  |
| freedroid2x06 | `playable` | 86.8 | 2244 | ✓ |  |  |
| freesci | `playable` | 56.5 | 255 | – | no-audio |  |
| friq-beta-07 | `playable` | 61.3 | 1556 | ✓ |  |  |
| frozen2x-0.1 | `playable` | 76.3 | 714 | ✓ |  |  |
| fruits2x | `playable` | 52.2 | 111 | – | no-audio |  |
| fruits_gp2x | `playable` | 61.3 | 1549 | ✓ |  |  |
| FullBoard (test ver.) | `playable` | 51.0 | 108 | ✓ |  |  |
| fvc | `playable` | 60.7 | 1525 | ✓ |  |  |
| FyWod_2x | `playable` | 60.6 | 1546 | ✓ |  |  |
| game-watch-mario-bros | `playable` | 61.4 | 1551 | ✓ |  |  |
| gchess-v1.0.1-bin | `playable` | 61.2 | 1555 | – | no-audio |  |
| gchess-v1.1.0-bin | `playable` | 61.2 | 1550 | – | no-audio |  |
| Geek 'em up GP2X | `playable` | 60.6 | 1699 | ✓ |  |  |
| gemdrop2x_v02 | `playable` | 60.2 | 1549 | ✓ |  |  |
| GeneralPromise | `playable` | 115.3 | 2929 | ✓ |  |  |
| Ghostbusters_WIP | `playable` | 61.2 | 577 | ✓ |  |  |
| ghostpix_v10_gp2x | `playable` | 60.7 | 1546 | ✓ |  |  |
| glouton | `playable` | 61.5 | 1546 | ✓ |  |  |
| gnp_104 | `playable` | 57.9 | 1585 | ✓ |  |  |
| gnugo2x | `playable` | 61.2 | 1552 | – | no-audio |  |
| gnurobbo_0.66_open2x | `playable` | 57.1 | 1467 | ✓ |  |  |
| GoitGP | `playable` | 60.4 | 1546 | ✓ |  |  |
| gorillaz | `playable` | 53.2 | 1339 | ✓ |  |  |
| gp2hanoi_0.8.1_gp2x | `playable` | 61.5 | 1552 | ✓ |  |  |
| gp2x-ceferino-0.1 | `playable` | 58.3 | 1551 | – | no-audio |  |
| gp2x-formido-0.1 | `playable` | 44.0 | 1553 | ✓ |  |  |
| gp2x-invaders-preview-version | `playable` | 61.5 | 1555 | ✓ |  |  |
| gp2x-shienso-bin_061021 | `playable` | 61.4 | 1552 | ✓ |  |  |
| gp2x-smc-0.1 | `playable` | 59.0 | 1549 | ✓ |  |  |
| gp2x-tong-v1 | `playable` | 114.1 | 2889 | – | no-audio |  |
| gp2x_2xmas | `playable` | 41.6 | 85 | ✓ |  |  |
| GP2X_BallGame_0.49 | `playable` | 61.1 | 1534 | ✓ |  |  |
| gp2x_drench | `playable` | 59.1 | 1499 | ✓ |  |  |
| GP2X_TLI | `playable` | 31.4 | 791 | ✓ |  |  |
| gp2xbug | `playable` | 114.4 | 2920 | ✓ |  |  |
| gp2xgo-v1.1.0-bin | `playable` | 61.2 | 1551 | – | no-audio |  |
| gp2xjunkie | `playable` | 58.3 | 1499 | ✓ |  |  |
| gp2xmancala-v1.1.1-bin | `playable` | 61.5 | 1554 | – | no-audio |  |
| GP2XOfLife | `playable` | 111.8 | 2826 | – | no-audio |  |
| gp2xpang-v.1.1.1 | `playable` | 100.1 | 2576 | ✓ |  |  |
| gp2xrick 1.0 | `playable` | 60.1 | 1522 | ✓ |  |  |
| GpFrontier v0.1 | `playable` | 61.9 | 1567 | ✓ |  |  |
| gpfrontier v0.4 | `playable` | 59.4 | 1560 | ✓ |  |  |
| GPSquares_GP2X | `playable` | 61.9 | 1553 | – | no-audio |  |
| gr-v1001-gp2x | `playable` | 60.0 | 1546 | ✓ |  |  |
| green | `playable` | 60.3 | 1531 | ✓ |  |  |
| grow | `playable` | 41.4 | 1532 | – | no-audio |  |
| gxeskiv | `playable` | 58.0 | 1471 | – | no-audio |  |
| HamstersEscape (Gp2x F-100 F-200) | `playable` | 25.5 | 654 | ✓ |  |  |
| hanagechu2x_gbax2007 | `playable` | 66.7 | 1682 | ✓ |  |  |
| hanagechu2xalpha | `playable` | 61.4 | 1550 | ✓ |  |  |
| Heretic2X_v0.5 | `playable` | 60.7 | 1527 | ✓ |  |  |
| heroes2x02 | `playable` | 52.0 | 1316 | ✓ |  |  |
| hex-a-hop | `playable` | 61.5 | 1550 | – | no-audio |  |
| hexbattle2x | `playable` | 60.8 | 1546 | ✓ |  |  |
| HumphreyGP2X | `playable` | 61.0 | 1551 | ✓ |  |  |
| Hyperion_GP2X_demo | `playable` | 61.6 | 1556 | ✓ |  |  |
| jump_n_blob_gp2x | `playable` | 60.4 | 1574 | ✓ |  |  |
| jumpnbumpgp2x | `playable` | 60.0 | 1556 | ✓ |  |  |
| Jurlx2 | `playable` | 61.0 | 1550 | ✓ |  |  |
| kampfimall-gp2x | `playable` | 61.0 | 1539 | – | no-audio |  |
| kampfimall-gp2x-music | `playable` | 60.1 | 527 | ✓ |  |  |
| ketm_2x_gp2x | `playable` | 52.5 | 1552 | ✓ |  |  |
| KicknPLay_1.1 | `playable` | 61.6 | 1547 | ✓ |  |  |
| Klaur | `playable` | 114.3 | 2925 | ✓ |  |  |
| koules2x_02 | `playable` | 60.7 | 1553 | ✓ |  |  |
| kuklomenos_gp2x_201209 | `playable` | 60.8 | 1556 | ✓ |  |  |
| kurukuru2x | `playable` | 61.4 | 1557 | ✓ |  |  |
| la | `playable` | 50.3 | 109 | ✓ |  |  |
| LABYRINTH | `playable` | 61.6 | 1553 | – | no-audio |  |
| ladykiller | `playable` | 60.7 | 1549 | ✓ |  |  |
| las-tres-luces-de-glaurung-remake | `playable` | 60.3 | 1543 | ✓ |  |  |
| lbreakoutgp2x | `playable` | 59.0 | 1562 | ✓ |  |  |
| levelshmup | `playable` | 85.3 | 2183 | ✓ |  |  |
| Lexeme | `playable` | 115.3 | 2934 | ✓ |  |  |
| lights-out | `playable` | 59.5 | 1499 | – | no-audio |  |
| LinesXv3 | `playable` | 61.5 | 1541 | ✓ |  |  |
| logicx | `playable` | 61.6 | 1548 | ✓ |  |  |
| Logoball | `playable` | 117.3 | 2927 | ✓ |  |  |
| lumix-beta-01 | `playable` | 65.1 | 1645 | – | no-audio |  |
| mad-mix-game-20b-final | `playable` | 62.5 | 1540 | ✓ |  |  |
| madbomber | `playable` | 60.6 | 1550 | ✓ |  |  |
| malvado2x | `playable` | 45.3 | 156 | ✓ |  |  |
| MAME-N22_51 | `playable` | 59.3 | 1561 | ✓ |  |  |
| mancala-v1.0.1 | `playable` | 60.7 | 1541 | – | no-audio |  |
| March of the mini tux | `playable` | 63.8 | 1617 | ✓ |  |  |
| Marte Necesita Vacas GP2X | `playable` | 63.8 | 1643 | ✓ |  |  |
| Masteries_Journey_to_the_Center_of_the_earth_GP2X | `playable` | 59.8 | 1549 | ✓ |  |  |
| masterpiece2x | `playable` | 61.4 | 1549 | – | no-audio |  |
| MazeThingie | `playable` | 61.8 | 1560 | ✓ |  |  |
| MazezaMGP2X | `playable` | 96.4 | 2495 | ✓ |  |  |
| memory | `playable` | 61.9 | 1581 | ✓ |  |  |
| MemoryGP2X-v11 | `playable` | 61.5 | 1554 | ✓ |  |  |
| meritous | `playable` | 60.5 | 1545 | ✓ |  |  |
| Merlin2x_beta_021 | `playable` | 58.3 | 544 | ✓ |  |  |
| metaphysik | `playable` | 63.7 | 1609 | ✓ |  |  |
| methaneV1 | `playable` | 61.1 | 1544 | ✓ |  |  |
| minigolf | `playable` | 61.0 | 1547 | – | no-audio |  |
| minos-gp2x | `playable` | 60.1 | 1519 | ✓ |  |  |
| misterhachi | `playable` | 52.1 | 1527 | ✓ |  |  |
| mk13.gpe | `playable` | 61.4 | 1539 | ✓ |  |  |
| mkACE.gpe | `playable` | 61.3 | 1538 | ✓ |  |  |
| mkONE.gpe | `playable` | 61.5 | 1543 | ✓ |  |  |
| MM2X | `playable` | 61.0 | 1535 | ✓ |  |  |
| monacoGP | `playable` | 60.6 | 1556 | ✓ |  |  |
| monochromeworlds-gp2x-1.0.0 | `playable` | 60.6 | 1534 | ✓ |  |  |
| moonlander | `playable` | 59.4 | 1530 | ✓ |  |  |
| MouthTrap | `playable` | 116.0 | 2924 | ✓ |  |  |
| mueppv32 | `playable` | 114.8 | 2914 | ✓ |  |  |
| mush_gp2x | `playable` | 40.2 | 1042 | ✓ |  |  |
| Mutant Tank Knights | `playable` | 57.6 | 308 | ✓ |  |  |
| MyriadUpdated | `playable` | 59.9 | 1549 | ✓ |  |  |
| mzx-2.84c | `playable` | 60.2 | 834 | ✓ |  |  |
| mzx282-gp2x | `playable` | 60.7 | 814 | ✓ |  |  |
| n-tris_v1.0 | `playable` | 72.2 | 1814 | ✓ |  |  |
| nanobounce-pacc-gp2x | `playable` | 58.7 | 1548 | ✓ |  |  |
| ne_deluxe_gp2x | `playable` | 61.3 | 1549 | ✓ |  |  |
| ne_gp2x | `playable` | 59.6 | 1506 | ✓ |  |  |
| Nebulus_gp2x | `playable` | 61.6 | 1545 | – | no-audio |  |
| NecNec2x | `playable` | 61.7 | 1564 | ✓ |  |  |
| Net-Bubble-gp2x_1-21-06_bin | `playable` | 55.7 | 521 | – | no-audio |  |
| newsuperpang | `playable` | 60.8 | 1543 | ✓ |  |  |
| Nifty | `playable` | 60.8 | 1549 | ✓ |  |  |
| noiz2saV3 | `playable` | 68.5 | 1749 | ✓ |  |  |
| Nom | `playable` | 61.1 | 1541 | ✓ |  |  |
| odonata_demo | `playable` | 59.8 | 1516 | ✓ |  |  |
| omok | `playable` | 52.1 | 108 | ✓ |  |  |
| OpenBOR_v2.1933 | `playable` | 59.9 | 1068 | ✓ |  |  |
| OpenBOR_v3.0_Build_2615_&_2637 | `playable` | 60.7 | 1557 | ✓ |  |  |
| openglad2x | `playable` | 59.3 | 1542 | ✓ |  |  |
| opentyrian2x_0.3_complete | `playable` | 57.4 | 1532 | ✓ |  |  |
| opposite_lock | `playable` | 53.2 | 1526 | ✓ |  |  |
| OrbitalSniper2x_v1.1 | `playable` | 58.1 | 144 | ✓ |  |  |
| othello_v1.0 | `playable` | 61.8 | 1551 | ✓ |  |  |
| PAF | `playable` | 61.0 | 1543 | ✓ |  |  |
| PantaVsDragon (Gp2x F-100 F-200) | `playable` | 25.3 | 653 | ✓ |  |  |
| paraballgp2x | `playable` | 60.8 | 1546 | ✓ |  |  |
| Payback | `playable` | 73.9 | 2007 | ✓ |  |  |
| PaybackDemo | `playable` | 32.4 | 844 | ✓ |  |  |
| pc | `playable` | 59.2 | 673 | ✓ |  |  |
| pdcv060b | `playable` | 56.1 | 522 | ✓ |  |  |
| Pentominos | `playable` | 61.5 | 1545 | ✓ |  |  |
| PerfectFit | `playable` | 61.7 | 1555 | – | no-audio |  |
| Peuppy_10_GP2X | `playable` | 33.7 | 850 | ✓ |  |  |
| Phishy-0 | `playable` | 60.8 | 1551 | ✓ |  |  |
| physique | `playable` | 61.1 | 1545 | ✓ |  |  |
| Pika2x | `playable` | 60.3 | 569 | ✓ |  |  |
| pintor2x | `playable` | 95.1 | 2390 | ✓ |  |  |
| pixpang | `playable` | 55.7 | 1533 | ✓ |  |  |
| PocketSnes_SMRPG | `playable` | 116.4 | 2926 | – | no-audio |  |
| Poker2x | `playable` | 111.3 | 2816 | ✓ |  |  |
| Poker_Gp2Xv1.0 | `playable` | 114.4 | 2922 | ✓ |  |  |
| Pond2X | `playable` | 60.7 | 1528 | – | no-audio |  |
| Pong | `playable` | 61.4 | 1543 | – | no-audio |  |
| pong2player | `playable` | 56.5 | 115 | – | no-audio |  |
| pong2v060x | `playable` | 54.4 | 111 | – | no-audio |  |
| Pool Panic | `playable` | 60.6 | 1530 | ✓ |  |  |
| powder2x-112 | `playable` | 60.8 | 1555 | – | no-audio |  |
| powermanga-0.80 | `playable` | 56.1 | 1474 | ✓ |  |  |
| PowerSlide | `playable` | 60.6 | 1539 | ✓ |  |  |
| PPlane | `playable` | 60.3 | 1525 | ✓ |  |  |
| PPlane2.GP2X | `playable` | 59.3 | 1572 | ✓ |  |  |
| prboom-gp2x | `playable` | 60.4 | 1551 | – | no-audio |  |
| proj0-demo_01 | `playable` | 59.9 | 1535 | ✓ |  |  |
| puckman_gp2x | `playable` | 115.2 | 2917 | ✓ |  |  |
| PulplifeWars | `playable` | 59.2 | 1545 | ✓ |  |  |
| puzzlelandgp2x | `playable` | 56.7 | 1553 | ✓ |  |  |
| qfg3-0 | `playable` | 60.1 | 1557 | ✓ |  |  |
| Quad | `playable` | 115.4 | 2919 | ✓ |  |  |
| quartz2_v1-50_gp2x | `playable` | 61.5 | 1549 | ✓ |  |  |
| Rabbit_vs_Flies_0.9 | `playable` | 61.1 | 1548 | ✓ |  |  |
| ramon atacks | `playable` | 61.4 | 1550 | ✓ |  |  |
| Release GP2X MST_RUNNERS | `playable` | 59.9 | 1547 | ✓ |  |  |
| retrovirus_1_1 | `playable` | 59.6 | 1511 | ✓ |  |  |
| RevoltOfTheBinaryCouriers GP2X | `playable` | 60.9 | 1531 | – | no-audio |  |
| reword_v0.5 | `playable` | 61.2 | 1561 | ✓ |  |  |
| rg_105 | `playable` | 59.3 | 1585 | ✓ |  |  |
| rg_ura_103 | `playable` | 58.8 | 1578 | ✓ |  |  |
| river | `playable` | 61.9 | 1553 | ✓ |  |  |
| RockRain | `playable` | 61.4 | 1549 | ✓ |  |  |
| rockrain2_exp-20100925 | `playable` | 61.0 | 1547 | ✓ |  |  |
| rookiehero_EXP.gp2x.v20120220 | `playable` | 60.7 | 1549 | ✓ |  |  |
| RoundEmUp-alpha3 | `playable` | 61.7 | 1560 | ✓ |  |  |
| rRootage_v1.0 | `playable` | 112.4 | 2896 | ✓ |  |  |
| rubidogp2x | `playable` | 59.2 | 1552 | ✓ |  |  |
| rubik | `playable` | 122.6 | 3106 | – | no-audio |  |
| Runner_GP2X | `playable` | 60.3 | 1553 | ✓ |  |  |
| s-tris2_v1-64_gp2x | `playable` | 61.3 | 1542 | ✓ |  |  |
| Sachunsung2_1 | `playable` | 52.2 | 107 | ✓ |  |  |
| sachunsungx | `playable` | 61.2 | 1536 | ✓ |  |  |
| santaMania | `playable` | 60.8 | 1549 | ✓ |  |  |
| ScorchedGPBeta2 | `playable` | 62.5 | 1547 | ✓ |  |  |
| scummvm-0.11.1-gp2x | `playable` | 59.0 | 1541 | ✓ |  |  |
| scummvm-1.2.0-gp2x | `playable` | 59.9 | 1556 | ✓ |  |  |
| scummvm-kor0.4.2cvs | `playable` | 60.5 | 1533 | – | no-audio |  |
| SdLame | `playable` | 61.0 | 1554 | ✓ |  |  |
| sdlmonkey_0.1 | `playable` | 61.5 | 1550 | – | no-audio |  |
| sdlscav_gp2x_0.2.0 | `playable` | 114.1 | 2883 | ✓ |  |  |
| Shangai v2 | `playable` | 51.6 | 108 | ✓ |  |  |
| ShanghaiX | `playable` | 61.4 | 1542 | ✓ |  |  |
| SheepDash | `playable` | 60.2 | 1563 | ✓ |  |  |
| Shippy84 | `playable` | 60.4 | 1555 | ✓ |  |  |
| Simon2X | `playable` | 60.4 | 1533 | – | no-audio |  |
| siv050 | `playable` | 60.1 | 1561 | ✓ |  |  |
| sleuth slots 2x | `playable` | 110.0 | 2872 | ✓ |  |  |
| SmallBall_GP | `playable` | 60.6 | 1551 | ✓ |  |  |
| snail runers | `playable` | 60.8 | 1552 | ✓ |  |  |
| snake2x-1.1 | `playable` | 60.9 | 1558 | – | no-audio |  |
| snowedin5_v1-00_gp2x | `playable` | 61.5 | 1548 | ✓ |  |  |
| SOD v1.1 | `playable` | 60.0 | 1549 | ✓ |  |  |
| sokobangp2x | `playable` | 54.4 | 1557 | ✓ |  |  |
| Solitaire2x-v1.4 | `playable` | 82.0 | 766 | – | no-audio |  |
| sources_MEMORY2X | `playable` | 60.4 | 1548 | – | no-audio |  |
| sources_Yahtzee | `playable` | 52.0 | 228 | ✓ |  |  |
| space squares | `playable` | 60.7 | 1545 | – | no-audio |  |
| space52_gp2x(open2x) | `playable` | 20.2 | 538 | ✓ |  |  |
| space_varments_v1.0 | `playable` | 56.5 | 576 | ✓ |  |  |
| SpaceRocks2X | `playable` | 54.8 | 158 | – | no-audio |  |
| SpaceSnake | `playable` | 114.7 | 1966 | ✓ |  |  |
| spacestorm | `playable` | 56.1 | 1413 | ✓ |  |  |
| spartak-chess_0.0.4_gp2x | `playable` | 61.3 | 1551 | – | no-audio |  |
| Sponge Blob Tennis | `playable` | 41.9 | 1551 | – | no-audio |  |
| spout | `playable` | 61.3 | 1545 | – | no-audio |  |
| Sqcolony | `playable` | 62.4 | 1577 | – | no-audio |  |
| Sqdef 1.4 | `playable` | 60.8 | 1553 | ✓ |  |  |
| Squares-v051 | `playable` | 61.1 | 1550 | ✓ |  |  |
| Squaresliding | `playable` | 61.7 | 1551 | ✓ |  |  |
| StairwayToHeaven | `playable` | 52.5 | 111 | ✓ |  |  |
| starfighter-gp2x-0.01 | `playable` | 65.6 | 1136 | ✓ |  |  |
| StarTrucker | `playable` | 61.0 | 1548 | ✓ |  |  |
| stppc2x-v1.0 | `playable` | 42.1 | 1558 | ✓ |  |  |
| stransball2 | `playable` | 60.2 | 1526 | ✓ |  |  |
| street2x | `playable` | 54.4 | 1541 | ✓ |  |  |
| subhunt | `playable` | 61.0 | 1547 | ✓ |  |  |
| sudoku-v1.0 | `playable` | 61.3 | 1546 | – | no-audio |  |
| sudoku2x-0.5 | `playable` | 60.7 | 1529 | – | no-audio |  |
| SuperChickenFallDemo | `playable` | 61.6 | 1551 | ✓ |  |  |
| SuperPaf_v1.0 | `playable` | 60.4 | 1546 | ✓ |  |  |
| SuperPixelJumper v1.1 for GP2X | `playable` | 60.8 | 1533 | ✓ |  |  |
| SuperSonicSpeed | `playable` | 61.3 | 1552 | ✓ |  |  |
| supertux-0.1.3-gp2x-v4 | `playable` | 53.4 | 1484 | ✓ |  |  |
| survival | `playable` | 61.0 | 1554 | ✓ |  |  |
| symbolica-0.8 | `playable` | 60.5 | 1533 | ✓ |  |  |
| tail-tale | `playable` | 61.5 | 1551 | ✓ |  |  |
| Tangle | `playable` | 62.0 | 1558 | – | no-audio |  |
| tecnoballz-0.91-gp2x | `playable` | 56.4 | 1463 | ✓ |  |  |
| tetwins | `playable` | 51.4 | 107 | ✓ |  |  |
| ThreeTs_Game | `playable` | 56.4 | 121 | ✓ |  |  |
| Thruster_GP2X | `playable` | 61.5 | 1555 | ✓ |  |  |
| tikka_dungeons_demo_1 | `playable` | 60.9 | 1532 | ✓ |  |  |
| tilematch-0.6 | `playable` | 101.4 | 2572 | ✓ |  |  |
| tileworld2x | `playable` | 55.9 | 1514 | ✓ |  |  |
| TimeFrack2D for GP2X | `playable` | 49.5 | 102 | – | no-audio |  |
| TouchGames | `playable` | 115.2 | 2926 | ✓ |  |  |
| tower | `playable` | 107.5 | 2733 | – | no-audio |  |
| towertopplergp2x | `playable` | 59.4 | 1550 | ✓ |  |  |
| Trap75 | `playable` | 61.8 | 1554 | ✓ |  |  |
| treev060 | `playable` | 60.1 | 1558 | ✓ |  |  |
| ttxbeta170706b | `playable` | 59.3 | 1550 | – | no-audio |  |
| TUcS.app(V0.7.0 - GP2X) | `playable` | 31.5 | 798 | ✓ |  |  |
| Txishos (Gp2x F-200) | `playable` | 22.6 | 584 | – | no-audio |  |
| Unicolor | `playable` | 61.2 | 1557 | ✓ |  |  |
| vectoroids-2x | `playable` | 61.2 | 1549 | ✓ |  |  |
| VekDemo2 | `playable` | 115.7 | 2917 | ✓ |  |  |
| Vektar | `playable` | 116.3 | 2926 | ✓ |  |  |
| vektar-free | `playable` | 72.0 | 1824 | ✓ |  |  |
| vektarpack_v1 | `playable` | 86.6 | 2203 | ✓ |  |  |
| Ventifact | `playable` | 60.2 | 1557 | ✓ |  |  |
| vexed-gp2x-10 | `playable` | 61.0 | 1539 | ✓ |  |  |
| vexedb1 | `playable` | 61.1 | 1552 | – | no-audio |  |
| vorton-b4 | `playable` | 59.4 | 1548 | ✓ |  |  |
| vwars | `playable` | 59.7 | 1549 | ✓ |  |  |
| waffle2x | `playable` | 41.5 | 88 | – | no-audio |  |
| war_and_warriorgp2x | `playable` | 61.7 | 1551 | ✓ |  |  |
| warcraft | `playable` | 60.1 | 1557 | ✓ |  |  |
| warehouse_panic_v1.1_gp2x | `playable` | 42.4 | 588 | ✓ |  |  |
| waternetgp2x | `playable` | 57.1 | 1565 | ✓ |  |  |
| wehaveballs | `playable` | 61.0 | 1535 | ✓ |  |  |
| whacky | `playable` | 61.0 | 1539 | ✓ |  |  |
| WindAndWater_teaser_110 | `playable` | 61.0 | 1536 | ✓ |  |  |
| wire3d | `playable` | 58.4 | 1525 | – | no-audio |  |
| Wiztern Demo | `playable` | 105.1 | 1830 | ✓ |  |  |
| wnw | `playable` | 60.6 | 1538 | ✓ |  |  |
| xenitris_demo | `playable` | 61.9 | 1569 | ✓ |  |  |
| xigon-X-gp2x-V1 | `playable` | 61.4 | 1552 | ✓ |  |  |
| Xpired2x 1.0 beta | `playable` | 61.1 | 1549 | ✓ |  |  |
| xRick | `playable` | 61.1 | 1555 | ✓ |  |  |
| yahtzee-v21 | `playable` | 61.5 | 1557 | ✓ |  |  |
| znax | `playable` | 58.4 | 1557 | ✓ |  |  |
| Znumbers | `playable` | 52.0 | 109 | ✓ |  |  |
| Zoids Quest2X-0.0.1-2 | `playable` | 60.7 | 1558 | ✓ |  |  |
| zoltan 2x | `playable` | 59.3 | 1549 | ✓ |  |  |
| zombiesorbet_v1.0_gp2x | `playable` | 60.3 | 1562 | ✓ |  |  |
| zooov11 | `playable` | 32.3 | 814 | ✓ |  |  |
| ztunnel-0 | `playable` | 60.1 | 1522 | ✓ |  |  |

### Wiz (147 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| alephone-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| BlastRiot122Wiz | `incompatible` | 0.0 | 0 | ✓ | mmio-spin | 0x4000 |
| cgenius-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| CloneKeen2X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Demons World | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| EpicRocks_Wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| epiphany | `incompatible` | 0.0 | 0 | – | no-frames |  |
| hheretic | `incompatible` | 0.0 | 0 | – | no-frames |  |
| hhexen | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ioquake2 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| kuklomenos | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Monster2-1.0-wiz | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| nethack-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| noiz2sa_wiz | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| Out Zone | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| pgw | `incompatible` | 11.8 | 19 | ✓ | no-frames |  |
| prboom-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| quake1-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| quake_0.03 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| RailroadRampage_1.2_Wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| rott | `incompatible` | 0.0 | 0 | – | no-frames |  |
| SmallBall_Wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Snow Bros 2 | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| srb2 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Twin Cobra | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| wolf4sdl_wiz_svn | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Zero Wing | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| abuse-wiz | `black` | 3.9 | 9 | ✓ | black-screen |  |
| albion-v1.0.1-wiz | `black` | 30.9 | 43 | ✓ | black-screen |  |
| Balloonacy_wiz_wip | `black` | 111.5 | 2858 | ✓ | black-screen |  |
| blingo | `black` | 31.1 | 71 | ✓ | black-screen |  |
| BubbleTrainWiz_5-20-09 | `black` | 44.7 | 1193 | ✓ | black-screen |  |
| CartoonWiz | `black` | 117.3 | 2873 | ✓ | black-screen |  |
| ColonyConflict_V1.1_B6 | `black` | 107.0 | 2875 | ✓ | black-screen |  |
| Dastardly_Dungeon | `black` | 25.4 | 39 | ✓ | black-screen |  |
| DungeonRunner | `black` | 109.3 | 2872 | ✓ | black-screen |  |
| DuoWIZ_Pong | `black` | 110.3 | 2866 | ✓ | black-screen |  |
| eduke32 | `black` | 11.8 | 17 | – | black-screen |  |
| freecell2x | `black` | 105.3 | 2865 | ✓ | black-screen |  |
| malvado | `black` | 30.4 | 77 | ✓ | black-screen |  |
| March of the mini tux(wiz version) | `black` | 113.0 | 2864 | ✓ | black-screen |  |
| nazcadreams | `black` | 29.6 | 52 | ✓ | black-screen |  |
| nazcarunners | `black` | 32.3 | 67 | ✓ | black-screen |  |
| Nazcasphere | `black` | 39.0 | 77 | ✓ | black-screen |  |
| openjazz-wiz | `black` | 23.2 | 21 | ✓ | black-screen |  |
| opentyrian | `black` | 11.0 | 9 | – | black-screen |  |
| paraballwiz | `black` | 5.8 | 7 | – | black-screen |  |
| PEZ | `black` | 12.3 | 12 | – | black-screen |  |
| PPlane2.WIZ | `black` | 107.4 | 2871 | ✓ | black-screen |  |
| protozoa | `black` | 27.3 | 48 | ✓ | black-screen |  |
| Ruckman-Wiz | `black` | 25.8 | 69 | ✓ | black-screen |  |
| SimOniZ | `black` | 112.2 | 2863 | ✓ | black-screen |  |
| supertux-wiz | `black` | 3.5 | 12 | ✓ | black-screen |  |
| tetwizdownload | `black` | 113.6 | 2858 | ✓ | black-screen |  |
| The Minigame Project | `black` | 57.0 | 1528 | ✓ | black-screen |  |
| tilt | `black` | 25.4 | 33 | ✓ | black-screen |  |
| Trap75 | `black` | 60.9 | 1533 | ✓ | black-screen |  |
| tricorder | `black` | 34.6 | 74 | ✓ | black-screen |  |
| TUcS.app(V0.7.0 - Wiz) | `black` | 112.0 | 2872 | ✓ | black-screen |  |
| uqm2x_release.1.1 | `black` | 73.9 | 292 | – | black-screen |  |
| warcraft-beta3-wiz | `black` | 40.7 | 44 | ✓ | black-screen |  |
| wiz-car-binary_090818a | `black` | 60.1 | 1512 | ✓ | black-screen |  |
| Wiz_Blox | `black` | 111.7 | 2861 | ✓ | black-screen |  |
| wiz_drench | `black` | 112.9 | 2874 | ✓ | black-screen |  |
| Wiz_Propis_Demo | `black` | 26.0 | 47 | ✓ | black-screen |  |
| WIZ_S4S | `black` | 113.4 | 2861 | ✓ | black-screen |  |
| WizSticks | `black` | 106.7 | 2868 | ✓ | black-screen |  |
| xcom1-v1.0.2-wiz | `black` | 65.5 | 119 | ✓ | black-screen |  |
| xcom2-v1.0.1-wiz | `black` | 119.7 | 3063 | ✓ | black-screen |  |
| chroma 1.01 v1 | `ingame` | 0.9 | 5 | – | low-fps |  |
| Skull (Windows, Linux & Gp2x Wiz) | `ingame` | 20.2 | 536 | ✓ | flat-fill |  |
| Worship Vector | `ingame` | 60.9 | 1532 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 160px; left and right halves are near- |
| [DEMO] Wiztern | `playable` | 58.3 | 641 | ✓ |  |  |
| AdamantArmorAffectionWiz | `playable` | 59.9 | 1532 | ✓ |  |  |
| airstrike-wiz-1.01 | `playable` | 60.4 | 1534 | ✓ |  |  |
| alexsfalldown | `playable` | 61.5 | 1545 | ✓ |  |  |
| altitude | `playable` | 38.4 | 148 | ✓ |  |  |
| Animatch Wiz | `playable` | 57.4 | 1525 | ✓ |  |  |
| Art Shot Wiz | `playable` | 32.7 | 853 | ✓ |  |  |
| Asteroids | `playable` | 59.5 | 1523 | ✓ |  |  |
| battlejewels-wiz-public001demo | `playable` | 60.5 | 1530 | ✓ |  |  |
| beat2x-wiz | `playable` | 59.9 | 1516 | ✓ |  |  |
| Biological Defend | `playable` | 58.6 | 1518 | ✓ |  |  |
| BitDEFENSE | `playable` | 23.8 | 650 | – | no-audio |  |
| Blix2x | `playable` | 61.4 | 1542 | ✓ |  |  |
| Boomshine2x_1.12_wiz | `playable` | 58.6 | 1523 | ✓ |  |  |
| BugwarsSE | `playable` | 59.5 | 1520 | ✓ |  |  |
| Camelot Warriors | `playable` | 59.4 | 1526 | ✓ |  |  |
| CDogs-wiz | `playable` | 60.6 | 1543 | ✓ |  |  |
| Dd2x | `playable` | 57.4 | 261 | ✓ |  |  |
| deicide3_eng | `playable` | 57.2 | 397 | ✓ |  |  |
| EpicFreeFall_Wiz | `playable` | 27.8 | 722 | ✓ |  |  |
| Geca Blaster 2 (Gp2x Wiz) | `playable` | 31.6 | 814 | ✓ |  |  |
| Ghostpix | `playable` | 59.0 | 1507 | ✓ |  |  |
| gnurobbo_0.65_wiz | `playable` | 57.0 | 1460 | ✓ |  |  |
| gobble | `playable` | 45.6 | 143 | – | no-audio |  |
| gr-v1001-wiz | `playable` | 59.0 | 1530 | ✓ |  |  |
| herknights | `playable` | 57.7 | 1517 | ✓ |  |  |
| hexen2 | `playable` | 57.7 | 1483 | ✓ |  |  |
| midway | `playable` | 99.1 | 833 | ✓ |  |  |
| Minigolf | `playable` | 59.8 | 1523 | – | no-audio |  |
| minos-gp2x-wiz | `playable` | 59.3 | 1500 | ✓ |  |  |
| mush_gp2x | `playable` | 46.2 | 1185 | ✓ |  |  |
| mush_gp2x-0 | `playable` | 35.4 | 936 | ✓ |  |  |
| Myriad | `playable` | 58.9 | 1528 | ✓ |  |  |
| NewSuperPang05 | `playable` | 60.1 | 1525 | ✓ |  |  |
| openggs | `playable` | 59.2 | 1506 | ✓ |  |  |
| paf | `playable` | 59.8 | 1516 | ✓ |  |  |
| Pentominos | `playable` | 60.8 | 1528 | ✓ |  |  |
| Pharaoh | `playable` | 50.2 | 105 | ✓ |  |  |
| PhishyWiz | `playable` | 59.4 | 1522 | ✓ |  |  |
| Powder2X_wiz_114_v01 | `playable` | 55.0 | 1403 | – | no-audio |  |
| PPlane | `playable` | 60.5 | 1534 | ✓ |  |  |
| preggo_Wiz | `playable` | 51.2 | 353 | ✓ |  |  |
| Propis | `playable` | 49.9 | 1257 | ✓ |  |  |
| PuzzleDevilWizDemo | `playable` | 57.1 | 1445 | ✓ |  |  |
| Rezerwar | `playable` | 52.0 | 499 | ✓ |  |  |
| roadfighter | `playable` | 60.2 | 1525 | ✓ |  |  |
| rockrain-gp2x-wiz | `playable` | 62.9 | 1536 | ✓ |  |  |
| Sachunsung2 | `playable` | 51.3 | 109 | ✓ |  |  |
| scummvm-1.2.0-gp2xwiz | `playable` | 56.0 | 1478 | ✓ |  |  |
| Shanghai2 | `playable` | 50.8 | 114 | ✓ |  |  |
| Shock Troopers Base Defense | `playable` | 56.6 | 1525 | ✓ |  |  |
| sleuthslots | `playable` | 57.5 | 1516 | ✓ |  |  |
| smw_1.7 | `playable` | 55.7 | 1477 | ✓ |  |  |
| SOD_Wiz | `playable` | 59.0 | 1522 | ✓ |  |  |
| Sopwith | `playable` | 61.1 | 1536 | ✓ |  |  |
| Space Varments | `playable` | 58.6 | 588 | ✓ |  |  |
| spout | `playable` | 60.6 | 1525 | – | no-audio |  |
| Sqdef_Wiz_14A | `playable` | 60.1 | 1535 | ✓ |  |  |
| Sudoku2X | `playable` | 60.8 | 1533 | – | no-audio |  |
| SudoQ | `playable` | 22.2 | 569 | ✓ |  |  |
| Tail Tale | `playable` | 60.9 | 1530 | ✓ |  |  |
| wizchess-v1.1.0-bin | `playable` | 60.4 | 1529 | – | no-audio |  |
| wizchess-v1.2.0-bin | `playable` | 60.9 | 1538 | – | no-audio |  |
| WizFrontier v0.1 | `playable` | 61.7 | 1622 | ✓ |  |  |
| wizgo-v1.1.0-bin | `playable` | 60.6 | 1536 | – | no-audio |  |
| WizGolf | `playable` | 60.6 | 1532 | – | no-audio |  |
| wizmancala-v1.1.2-bin | `playable` | 60.8 | 1536 | – | no-audio |  |
| wizpong | `playable` | 59.9 | 1522 | – | no-audio |  |
| wizznic-0.9.9-wiz | `playable` | 58.8 | 1530 | ✓ |  |  |
| wnw_demo | `playable` | 60.7 | 1533 | ✓ |  |  |
| WWII | `playable` | 58.2 | 1521 | ✓ |  |  |
| xpiredwiz.eng.101 | `playable` | 58.7 | 1490 | ✓ |  |  |
| xRick | `playable` | 60.7 | 1542 | ✓ |  |  |
| znumbers | `playable` | 52.2 | 111 | ✓ |  |  |
| Zoltan | `playable` | 59.2 | 1526 | ✓ |  |  |

### Caanoo (194 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| Abbaye_caanoo | `incompatible` | 0.5 | 1 | ✓ | no-frames |  |
| Abbaye_caanoo_v3 | `incompatible` | 0.6 | 1 | ✓ | no-frames |  |
| aggressivepong-pre21.1-gph-uni | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ArtShotCaanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| audiorace-v1.5-can | `incompatible` | 0.0 | 0 | – | no-frames |  |
| BermudaS_caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Blix2x | `incompatible` | 0.0 | 0 | ✓ | unknown-device | /dev/input/mouse/0 |
| caanoo-tyrian-v1.1-bin | `incompatible` | 0.0 | 0 | ✓ | missing-game-data |  |
| Echo V.1.3.2 (Caanoo) | `incompatible` | 0.0 | 0 | – | no-frames |  |
| freedroid_Caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| fungp.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X Caanoo/fungp.zip' (exit 32512) |
| instead-1.6.1-caanoo | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| liar.zip | `incompatible` | 0.0 | 0 | – | archive-failed | magiceyes: failed to extract '/mnt/s/GP2X Caanoo/liar.zip' (exit 32512) |
| Liquid Counter.caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| openjazz-caanoo | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| openttd_c | `incompatible` | 0.0 | 0 | – | no-frames |  |
| quake1-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| quake1_build-20111024 | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/accel |
| quake2-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| RailroadRampage_1.2_Caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| reminiscence-v0.1.10-bin | `incompatible` | 0.0 | 0 | – | no-frames |  |
| rotate | `incompatible` | 0.5 | 1 | – | no-frames |  |
| runner-Caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| sdllopan_v4-all | `incompatible` | 0.0 | 1 | ✓ | no-frames |  |
| sdlquake_build-20111113-0 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| supertux | `incompatible` | 1.4 | 1 | ✓ | no-frames |  |
| tmw_v1.0.0-beta-2_caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| warcraft-beta3-caanoo | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| zlocada-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zsxd | `incompatible` | 0.0 | 0 | – | no-frames |  |
| aquaVenture | `black` | 12.4 | 35 | ✓ | black-screen |  |
| arcadevol1 | `black` | 6.4 | 7 | ✓ | black-screen |  |
| B'lox! | `black` | 31.7 | 312 | ✓ | black-screen |  |
| Balloonacy | `black` | 35.4 | 347 | ✓ | black-screen |  |
| Blingo | `black` | 38.4 | 109 | ✓ | black-screen |  |
| Blitz | `black` | 39.7 | 391 | ✓ | black-screen |  |
| BubbleTrain | `black` | 1.0 | 2 | ✓ | black-screen |  |
| cat_trap | `black` | 12.5 | 35 | – | black-screen |  |
| Drench | `black` | 12.4 | 34 | – | black-screen |  |
| Flappynerd_Caanoo | `black` | 35.6 | 351 | ✓ | black-screen |  |
| Geek_em_up_CAANOO | `black` | 26.3 | 276 | ✓ | black-screen |  |
| Guru Logic | `black` | 12.2 | 34 | – | black-screen |  |
| Hardcore Fight (Caanoo) | `black` | 0.3 | 3 | ✓ | black-screen |  |
| JUMPNRUN | `black` | 55.4 | 1543 | ✓ | black-screen |  |
| kenlab-caanoo | `black` | 42.9 | 414 | – | black-screen |  |
| laserchess_c | `black` | 18.5 | 38 | – | black-screen |  |
| MNV_Caanoo_Release1 | `black` | 16.5 | 42 | – | black-screen |  |
| powermanga-0.80 | `black` | 48.8 | 1274 | ✓ | black-screen |  |
| SantaMania | `black` | 11.8 | 33 | – | black-screen |  |
| STRATEGY | `black` | 10.5 | 18 | ✓ | black-screen |  |
| Trap75 | `black` | 48.6 | 1223 | ✓ | black-screen |  |
| xcom1-v1.0.2-caanoo | `black` | 13.9 | 9 | ✓ | black-screen |  |
| xcom2-v1.0.1-caanoo | `black` | 62.2 | 1583 | ✓ | black-screen |  |
| aimcaanoo | `ingame` | 49.4 | 1270 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 160px; left and right halves are near- |
| Arcadevol3 | `ingame` | 58.9 | 1546 | – | flat-fill |  |
| caanoo-biniax2-v1.30-bin | `ingame` | 16.6 | 418 | ✓ | low-fps |  |
| chexquest-caanoo | `ingame` | 17.4 | 448 | ✓ | low-fps |  |
| Coral Sea (Caanoo - Bennu) | `ingame` | 14.6 | 377 | ✓ | low-fps |  |
| deminor | `ingame` | 11.6 | 27 | – | low-fps |  |
| EEEEK! EEEEEK! HOOOOOOK!!! | `ingame` | 20.8 | 569 | ✓ | garbled-visuals | renders at 640x480 instead of 320x240 |
| gnp_104 | `ingame` | 50.3 | 1350 | ✓ | flat-fill |  |
| gnuRobbo | `ingame` | 15.2 | 204 | ✓ | low-fps |  |
| gr-v1001-caanoo | `ingame` | 10.1 | 262 | ✓ | low-fps |  |
| jump_n_blob_caanoo | `ingame` | 2.2 | 58 | ✓ | low-fps |  |
| knight | `ingame` | 105.6 | 2660 | ✓ | flat-fill |  |
| Liar | `ingame` | 12.4 | 85 | ✓ | low-fps |  |
| llcpcls-caanoo | `ingame` | 16.4 | 35 | ✓ | low-fps |  |
| meritous | `ingame` | 67.7 | 1723 | ✓ | flat-fill |  |
| Metal Slug Zombies | `ingame` | 26.1 | 700 | ✓ | garbled-visuals | renders at 640x480 instead of 320x240 |
| mtknights | `ingame` | 31.2 | 785 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 128px |
| nlove_0.6.2_(beta)_caanoo | `ingame` | 16.0 | 40 | – | low-fps |  |
| noiz2sa_caanoo | `ingame` | 40.3 | 1026 | ✓ | flat-fill |  |
| Pharaoh | `ingame` | 12.3 | 25 | ✓ | low-fps |  |
| profanation_Caanoo | `ingame` | 18.1 | 39 | ✓ | low-fps |  |
| pushover-v0.2-bin | `ingame` | 17.9 | 455 | ✓ | low-fps |  |
| rg_ura_103 | `ingame` | 53.9 | 1415 | ✓ | flat-fill |  |
| sbtime_caanoo | `ingame` | 5.5 | 150 | ✓ | low-fps |  |
| Skull (Caanoo) | `ingame` | 104.8 | 2715 | ✓ | garbled-visuals | renders at 320x200 instead of 320x240 |
| SORRv5_Caanoo | `ingame` | 0.1 | 2 | ✓ | low-fps |  |
| xpiredcan.eng.101 | `ingame` | 0.1 | 3 | ✓ | low-fps |  |
| Zverealm-Caanoo | `ingame` | 17.8 | 556 | ✓ | low-fps |  |
| 20110831 - Bomber Run Redux | `playable` | 37.9 | 1001 | – | no-audio |  |
| aaa | `playable` | 59.1 | 1498 | ✓ |  |  |
| aaaa | `playable` | 55.3 | 1406 | ✓ |  |  |
| ADVENTURE | `playable` | 55.6 | 1506 | ✓ |  |  |
| Amoebax | `playable` | 53.8 | 1390 | ✓ |  |  |
| animatch | `playable` | 365.6 | 9764 | ✓ |  |  |
| apocalypso Caanoo | `playable` | 131.7 | 3486 | ✓ |  |  |
| Arcadevol2 | `playable` | 59.8 | 1538 | ✓ |  |  |
| balls12_caanoo_bin | `playable` | 35.1 | 72 | – | no-audio |  |
| battlejewels-105-caanoo-beta | `playable` | 55.4 | 1399 | ✓ |  |  |
| Blackjack21v1.1 | `playable` | 59.3 | 1498 | – | no-audio |  |
| BubblePop (Caanoo) | `playable` | 38.4 | 990 | ✓ |  |  |
| caanoo-12swap-v1.0-bin | `playable` | 36.6 | 927 | ✓ |  |  |
| caanoo-chess-v1.1.0-bin | `playable` | 44.7 | 1133 | – | no-audio |  |
| caanoo-gnurobbo-0.68 | `playable` | 30.9 | 415 | ✓ |  |  |
| caanoo-go-v1.1.0-bin | `playable` | 46.7 | 1180 | – | no-audio |  |
| caanoo-mancala-v1.1.0-bin | `playable` | 45.6 | 1149 | – | no-audio |  |
| can-zomb_3 | `playable` | 25.8 | 762 | ✓ |  |  |
| cavestory | `playable` | 57.6 | 1552 | ✓ |  |  |
| ccrg-caanoo | `playable` | 45.9 | 198 | ✓ |  |  |
| cgenius-caanoo | `playable` | 28.3 | 734 | ✓ |  |  |
| cllwrth | `playable` | 24.7 | 624 | ✓ |  |  |
| cooldowncaanoo | `playable` | 774.9 | 20091 | ✓ |  |  |
| daff_s_adventure_2_caanoo | `playable` | 26.1 | 669 | ✓ |  |  |
| Deadly Eye (Caanoo) | `playable` | 26.2 | 673 | ✓ |  |  |
| deadlyc | `playable` | 55.5 | 1397 | ✓ |  |  |
| DealorNoDeal | `playable` | 60.5 | 1552 | ✓ |  |  |
| DefendorX_C | `playable` | 319.1 | 8356 | ✓ |  |  |
| demons | `playable` | 118.4 | 2978 | ✓ |  |  |
| dynamate_c | `playable` | 28.5 | 744 | ✓ |  |  |
| echo_caanoo | `playable` | 943.3 | 26727 | ✓ |  |  |
| EpicFreeFall | `playable` | 86.2 | 2166 | ✓ |  |  |
| EpicFreeFall Caanoo | `playable` | 80.9 | 2091 | ✓ |  |  |
| Firewhip-Caanoo | `playable` | 51.8 | 1395 | ✓ |  |  |
| fleshchasmer | `playable` | 53.7 | 1368 | ✓ |  |  |
| freeheroes2_c | `playable` | 41.0 | 84 | ✓ |  |  |
| fshark | `playable` | 85.3 | 2142 | ✓ |  |  |
| Fywod_caanoo | `playable` | 55.6 | 1410 | ✓ |  |  |
| Geca Blaster 2 (Caanoo) | `playable` | 68.7 | 1764 | ✓ |  |  |
| getstar | `playable` | 85.5 | 2149 | ✓ |  |  |
| gravityforcev2 | `playable` | 48.0 | 1215 | ✓ |  |  |
| Hamster's Escape 3D (Caanoo) | `playable` | 185.6 | 1733 | ✓ |  |  |
| HamstersEscape (Caanoo) | `playable` | 23.0 | 215 | ✓ |  |  |
| hellfire | `playable` | 118.4 | 2978 | ✓ |  |  |
| Hero_The_Realm-DEMO | `playable` | 64.8 | 1673 | ✓ |  |  |
| HeroTheRealm_DEMOv2 | `playable` | 68.4 | 1764 | ✓ |  |  |
| hexahop_1.0 | `playable` | 52.4 | 1319 | – | no-audio |  |
| Humos-Caanoo | `playable` | 34.6 | 943 | ✓ |  |  |
| jumpToTheMoon_c | `playable` | 25.3 | 238 | ✓ |  |  |
| ketm | `playable` | 39.2 | 1299 | – | no-audio |  |
| KOF (Ver. 5f) (Caanoo) | `playable` | 44.9 | 1308 | ✓ |  |  |
| lmission_0.5 | `playable` | 58.5 | 1470 | ✓ |  |  |
| MasteriesRunners (Caanoo) | `playable` | 22.5 | 579 | ✓ |  |  |
| MISC | `playable` | 58.3 | 1563 | – | no-audio |  |
| Mission_faileD 1.2 [Caanoo] | `playable` | 38.1 | 1059 | ✓ |  |  |
| monster | `playable` | 41.6 | 1112 | ✓ |  |  |
| next_element | `playable` | 60.9 | 1537 | ✓ |  |  |
| nuclearchess | `playable` | 223.1 | 5623 | – | no-audio |  |
| OperationFenix (Caanoo) | `playable` | 33.7 | 923 | ✓ |  |  |
| outzone | `playable` | 85.5 | 2146 | ✓ |  |  |
| pang | `playable` | 60.3 | 1548 | ✓ |  |  |
| PantaVsDragon (Caanoo) | `playable` | 39.9 | 1027 | ✓ |  |  |
| pengupop | `playable` | 37.4 | 167 | ✓ |  |  |
| powder | `playable` | 53.7 | 1371 | – | no-audio |  |
| prboom-caanoo | `playable` | 60.2 | 1532 | – | no-audio |  |
| propis | `playable` | 49.5 | 1245 | ✓ |  |  |
| Protect&rescue | `playable` | 29.4 | 800 | ✓ |  |  |
| purito_cycling_1.5_Caanoo | `playable` | 219.0 | 5633 | ✓ |  |  |
| puzsion | `playable` | 123.3 | 3531 | ✓ |  |  |
| PUZZLEBOARDS | `playable` | 107.0 | 2924 | ✓ |  |  |
| RACING | `playable` | 59.0 | 1551 | ✓ |  |  |
| rhythmosplay_1.1.12 | `playable` | 50.9 | 1294 | ✓ |  |  |
| Sachunsung2 | `playable` | 24.4 | 50 | ✓ |  |  |
| sbt | `playable` | 42.7 | 1176 | ✓ |  |  |
| Shanghai2 | `playable` | 45.4 | 93 | ✓ |  |  |
| SHOOTERS | `playable` | 76.1 | 2093 | ✓ |  |  |
| SimOniZ | `playable` | 53.5 | 1371 | ✓ |  |  |
| Sitwell (Caanoo) | `playable` | 32.6 | 836 | ✓ |  |  |
| Slap | `playable` | 85.6 | 2147 | ✓ |  |  |
| smallball | `playable` | 63.6 | 1652 | ✓ |  |  |
| smallball-Caanoo | `playable` | 73.3 | 1894 | ✓ |  |  |
| smw_1.7 | `playable` | 29.7 | 793 | ✓ |  |  |
| SnailRace_C | `playable` | 20.3 | 526 | – | no-audio |  |
| snowbros | `playable` | 118.5 | 2974 | ✓ |  |  |
| snowbros2 | `playable` | 117.8 | 2959 | ✓ |  |  |
| SOD(r181) | `playable` | 433.3 | 11486 | ✓ |  |  |
| space52_caanoo | `playable` | 37.3 | 1001 | ✓ |  |  |
| SPORTS | `playable` | 59.1 | 1550 | ✓ |  |  |
| sqrxz-v0996-caanoo | `playable` | 54.2 | 1383 | ✓ |  |  |
| sqrxz2-v0.80-caanoo | `playable` | 58.3 | 1484 | ✓ |  |  |
| stppc-caanoo-29-11-2010 | `playable` | 34.2 | 340 | ✓ |  |  |
| tailtale4c | `playable` | 61.4 | 1548 | ✓ |  |  |
| the solitarie | `playable` | 294.1 | 7821 | ✓ |  |  |
| Tigerhell | `playable` | 85.7 | 2152 | ✓ |  |  |
| Tile | `playable` | 54.8 | 1517 | ✓ |  |  |
| tlosaf_v12-caanoo | `playable` | 60.6 | 1527 | – | no-audio |  |
| tong-caanoo | `playable` | 56.5 | 1475 | ✓ |  |  |
| Truxton | `playable` | 85.5 | 2146 | ✓ |  |  |
| truxton2 | `playable` | 85.6 | 2150 | ✓ |  |  |
| twincobr | `playable` | 85.6 | 2150 | ✓ |  |  |
| twinhawk | `playable` | 85.6 | 2150 | ✓ |  |  |
| Txishos (Caanoo) | `playable` | 36.9 | 945 | ✓ |  |  |
| Vigo | `playable` | 54.5 | 1420 | – | no-audio |  |
| Wardner | `playable` | 118.6 | 2984 | ✓ |  |  |
| warehouse_panic_v1.1_caanoo | `playable` | 31.5 | 422 | ✓ |  |  |
| WindandWater | `playable` | 60.6 | 1534 | ✓ |  |  |
| Wizznic 0.9.2- preview | `playable` | 29.9 | 812 | ✓ |  |  |
| wolf4sdl-caanoo | `playable` | 60.1 | 1529 | – | no-audio |  |
| wvector | `playable` | 44.7 | 1124 | ✓ |  |  |
| zelda-roth-olb-3t_caanoo | `playable` | 20.2 | 544 | ✓ |  |  |
| zerowing | `playable` | 118.5 | 2975 | ✓ |  |  |
| zombiesorbet_v1.0_caanoo | `playable` | 23.1 | 592 | ✓ |  |  |
| zomg-Caanoo | `playable` | 201.1 | 5388 | ✓ |  |  |
