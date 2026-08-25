# magiceyes compatibility sweep

Every GP2X / Wiz / Caanoo title on the corpus share, booted headlessly through the native engine (`bin/me_unicorn`) and scored from the shm framebuffer + the structured run report. Regenerate with `tools/test/compat_report.py`.


## Summary

| Platform | Titles | Playable | Ingame | Black | Incompatible | Crashed |
|---|--:|--:|--:|--:|--:|--:|
| GP2X | 631 | 505 | 7 | 33 | 86 | 0 |
| Wiz | 147 | 118 | 2 | 14 | 13 | 0 |
| Caanoo | 194 | 113 | 47 | 7 | 27 | 0 |
| **All** | **972** | **736** | **56** | **54** | **126** | **0** |

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
| **Renders at speed but no audio** (`no-audio`) | 114 | Caanoo, GP2X, Wiz | n/a |
| **Never rendered a frame (cause unknown)** (`no-frames`) | 63 | Caanoo, GP2X, Wiz | n/a |
| **Boots but renders only black** (`black-screen`) | 54 | Caanoo, GP2X, Wiz | n/a |
| **Not a 32-bit ARM ELF** (`not-arm-elf`) | 45 | GP2X | n/a |
| **Renders but below 20 fps** (`low-fps`) | 42 | Caanoo, GP2X | n/a |
| **Renders, but the picture is wrong** (`garbled-visuals`) | 11 | Caanoo, GP2X, Wiz | n/a |
| **Game data files are missing from the dump** (`missing-game-data`) | 6 | Caanoo, GP2X | n/a |
| **Archive extraction failed** (`archive-failed`) | 5 | Caanoo, GP2X | n/a |
| **Unknown /dev node** (`unknown-device`) | 5 | Caanoo, GP2X, Wiz | `/dev/accel` ×1, `/dev/input/mouse/0` ×1, `/dev/cx25874` ×1, `/dev/graphics/fb0` ×1 |
| **Unimplemented syscall** (`unimplemented-syscall`) | 2 | GP2X | `9437188` ×1, `11711` ×1 |
| **Draws only a flat colour** (`flat-fill`) | 2 | Caanoo, GP2X | n/a |
| **Could not open a display** (`display-init-failed`) | 1 | GP2X | n/a |

## Renders, but the picture is wrong

These 11 titles pass the running checks (frames advancing, frame rate, audio) while the frame itself is visibly broken, so they are graded `ingame` rather than `playable`. The reasons come from measuring the captured frame: a consistent per-row offset means a stride/pitch mismatch, large-scale repetition means the screen holds more than one copy of itself, and noise far above what dithered artwork reaches means corrupt memory.

| Title | Platform | fps | What the frame looks like |
|---|---|--:|---|
| aimcaanoo | Caanoo | 48.8 | the screen holds a second copy of itself, offset by 160px; left and right halves are near-identical; top and bottom halves are near-identical |
| EEEEK! EEEEEK! HOOOOOOK!!! | Caanoo | 23.4 | renders at 640x480 instead of 320x240 |
| Metal Slug Zombies | Caanoo | 29.5 | renders at 640x480 instead of 320x240 |
| mtknights | Caanoo | 32.9 | the screen holds a second copy of itself, offset by 156px; top and bottom halves are near-identical |
| Skull (Caanoo) | Caanoo | 14.4 | renders at 320x200 instead of 320x240 |
| BunnyTraps-v11 | GP2X | 62.0 | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like corrupt memory |
| GF | GP2X | 61.4 | top and bottom halves are near-identical |
| Life.0.1 | GP2X | 58.3 | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like corrupt memory |
| MoveSweep2X | GP2X | 57.1 | the screen holds a second copy of itself, offset by 96px; left and right halves are near-identical |
| blingo | Wiz | 54.6 | pixel-to-pixel noise of 100, far above what dithered artwork reaches; the frame looks like corrupt memory |
| Ruckman-Wiz | Wiz | 57.8 | pixel-to-pixel noise of 102, far above what dithered artwork reaches; the frame looks like corrupt memory |

## Scored as working, but only painting a flat colour

These 2 titles advanced frames, kept audio running, and held frame rate, so they land in `playable`/`renders`. Their framebuffer never held more than one or two colours, which means the tier overstates them. Worth treating as broken.

| Title | Platform | Status | fps |
|---|---|---|--:|
| truxton2 | Caanoo | `playable` | 81.8 |
| dumbbell2x-01 | GP2X | `renders` | 62.0 |

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
| `unknown_ioctl:fb` | 233 |
| `unknown_mmio:0x910` | 108 |
| `unknown_mmio:0x1988` | 57 |
| `unknown_mmio:0x19c0` | 57 |
| `unknown_mmio:0x19c4` | 57 |
| `unknown_mmio:0x924` | 51 |
| `unknown_mmio:0x91c` | 49 |
| `unknown_mmio:0x3b46` | 46 |
| `unknown_mmio:0x3802` | 23 |
| `unknown_mmio:0x3804` | 23 |
| `unknown_mmio:0xf16` | 12 |
| `unknown_mmio:0xf58` | 12 |
| `unknown_mmio:0x808` | 12 |
| `unsupported_blit:dst-unmapped` | 11 |
| `unknown_mmio:0xf004` | 8 |
| `unknown_mmio:0x14802` | 5 |
| `unknown_mmio:0x14804` | 5 |
| `unknown_mmio:0x3808` | 5 |
| `unsupported_gles:glEnable` | 4 |
| `unsupported_sdl:IMG_Load_unsupported` | 4 |
| `unknown_mmio:0xf07c` | 4 |
| `unknown_ioctl:dsp` | 3 |
| `unsupported_blit:src-unmapped` | 3 |
| `unknown_mmio:0x307c` | 3 |

## Per-title results


### GP2X (631 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| 2xquake003 | `incompatible` | 0.0 | 0 | – | missing-game-data |  |
| 2xquake2 | `incompatible` | 0.0 | 0 | ✓ | missing-game-data |  |
| 2XRally01 | `incompatible` | 0.0 | 0 | – | display-init-failed |  |
| abduction | `incompatible` | 0.0 | 0 | – | no-frames |  |
| airpong4GP2X0.0.4 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/airpong4GP2X0.0.4/airpong022/src/AirPong.gpe' is not an  |
| AlienZ | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
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
| OpenTTD | `incompatible` | 0.0 | 0 | – | no-frames |  |
| pacmame | `incompatible` | 10.0 | 5 | – | no-frames |  |
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
| Wolf4SDL | `incompatible` | 0.0 | 0 | – | no-frames |  |
| worminator302 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Zombiepox2X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| 2xHexen2 v0.05 PB2 | `black` | 35.5 | 25 | – | black-screen |  |
| 2xWargus_PB1.3 | `black` | 0.9 | 14 | ✓ | black-screen |  |
| 2xZdoom_PB1.2 | `black` | 45.6 | 50 | – | black-screen |  |
| A1GP2XV1_1 | `black` | 19.9 | 10 | – | black-screen |  |
| albion-v1.0.1-gp2x | `black` | 29.7 | 27 | ✓ | black-screen |  |
| AlienBlaster_1.02 | `black` | 13.1 | 16 | ✓ | black-screen |  |
| BubbleTrain_GP2X-2006_Entry | `black` | 1.3 | 2 | ✓ | black-screen |  |
| CaptainCrusader_GP2XDemo | `black` | 6.0 | 3 | – | black-screen |  |
| CaptainCrusader_GP2XFull | `black` | 6.0 | 3 | – | black-screen |  |
| d2x-gp2x-0.02 | `black` | 10.9 | 12 | ✓ | black-screen |  |
| duke2x004 | `black` | 17.5 | 9 | – | black-screen |  |
| egoboo-cramfs | `black` | 37.0 | 64 | ✓ | black-screen |  |
| FFDoom | `black` | 3.9 | 2 | – | black-screen |  |
| FleshChasmer132c_patch | `black` | 9.7 | 5 | ✓ | black-screen |  |
| FleshChasmer_Dpad | `black` | 10.0 | 5 | ✓ | black-screen |  |
| gp2xDoukutsu-1.04 | `black` | 20.0 | 17 | ✓ | black-screen |  |
| gp2xJenkasNightmare | `black` | 20.7 | 18 | ✓ | black-screen |  |
| GPgeneral | `black` | 3.9 | 2 | – | black-screen |  |
| liquidwar2x02 | `black` | 5.9 | 3 | – | black-screen |  |
| openjazz-gp2x | `black` | 16.0 | 14 | ✓ | black-screen |  |
| PrBoom PWAD pack | `black` | 2.8 | 5 | – | black-screen |  |
| raw2xv0.3.1 | `black` | 13.4 | 7 | – | black-screen |  |
| ShadowWarrior2X | `black` | 11.7 | 6 | – | black-screen |  |
| step2x02 | `black` | 57.9 | 1551 | ✓ | black-screen |  |
| supertux-0.1.3-gp2x-v4 | `black` | 51.2 | 1408 | ✓ | black-screen |  |
| uhexen | `black` | 10.0 | 5 | – | black-screen |  |
| ultratumba_exp-20100925.gp2x | `black` | 15.7 | 8 | ✓ | black-screen |  |
| warcraft-beta3-gp2x | `black` | 30.6 | 28 | ✓ | black-screen |  |
| xbak-0.1.3 | `black` | 31.4 | 16 | – | black-screen |  |
| xcom1-v1.0.2-gp2x | `black` | 43.6 | 1614 | ✓ | black-screen |  |
| xcom2-v1.0.1-gp2x | `black` | 110.0 | 2809 | ✓ | black-screen |  |
| xump2x_beta2 | `black` | 25.9 | 13 | ✓ | black-screen |  |
| zcgp2x_211B18_0.4alpha | `black` | 21.2 | 20 | – | black-screen |  |
| BunnyTraps-v11 | `ingame` | 62.0 | 1561 | ✓ | garbled-visuals | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like |
| Clonk2X_1.0 | `ingame` | 16.3 | 411 | – | not-arm-elf | magiceyes: reload of '/bin/sh' failed |
| CromoZome | `ingame` | 19.8 | 513 | ✓ | low-fps |  |
| dumbbell2x-01 | `ingame` | 62.0 | 578 | – | flat-fill |  |
| GF | `ingame` | 61.4 | 1557 | ✓ | garbled-visuals | top and bottom halves are near-identical |
| Life.0.1 | `ingame` | 58.3 | 1469 | – | garbled-visuals | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like |
| MoveSweep2X | `ingame` | 57.1 | 1434 | – | garbled-visuals | the screen holds a second copy of itself, offset by 96px; left and right halves are near-i |
| 1945_GP2X_0.2b | `playable` | 57.8 | 1455 | ✓ |  |  |
| 2xpong_gp2x | `playable` | 57.3 | 1440 | ✓ |  |  |
| 2xtron-v01 | `playable` | 58.1 | 1462 | ✓ |  |  |
| 2xZdoom_selector | `playable` | 108.9 | 2743 | ✓ |  |  |
| 4WE_GP2x | `playable` | 118.8 | 2995 | ✓ |  |  |
| 9 Lives | `playable` | 70.4 | 1778 | ✓ |  |  |
| _-The Reversed Preacher 3-_Hack bIld_ | `playable` | 58.1 | 1463 | ✓ |  |  |
| _-the reversed preacher II-_ | `playable` | 58.3 | 1464 | ✓ |  |  |
| a_sn-pong | `playable` | 42.0 | 1556 | – | no-audio |  |
| abe | `playable` | 57.3 | 1441 | ✓ |  |  |
| abuse_1.0 | `playable` | 54.9 | 1453 | ✓ |  |  |
| AbusimbelProfanationDeluxe | `playable` | 57.6 | 1450 | ✓ |  |  |
| AdamantArmorAffection2x | `playable` | 56.3 | 1424 | ✓ |  |  |
| ADIC2X | `playable` | 109.5 | 2745 | ✓ |  |  |
| AfterBurner-GP2X | `playable` | 49.4 | 1256 | ✓ |  |  |
| Airplyr | `playable` | 62.0 | 1559 | ✓ |  |  |
| airstrike-1.1 | `playable` | 57.3 | 1455 | ✓ |  |  |
| Akd_BB | `playable` | 61.5 | 1556 | ✓ |  |  |
| alex | `playable` | 57.7 | 1453 | ✓ |  |  |
| Alex's Falldown | `playable` | 58.0 | 1470 | ✓ |  |  |
| alex4_gp2x | `playable` | 57.7 | 1453 | ✓ |  |  |
| altitude | `playable` | 60.1 | 1555 | ✓ |  |  |
| AMazing-3D | `playable` | 60.8 | 1533 | – | no-audio |  |
| amoebax-0.2.1-gp2x | `playable` | 59.0 | 1500 | ✓ |  |  |
| angband2x-v2 | `playable` | 57.3 | 1548 | – | no-audio |  |
| armorcity-0_30b | `playable` | 56.9 | 1455 | ✓ |  |  |
| ASCIIPong2xV0.4 | `playable` | 56.3 | 1413 | ✓ |  |  |
| Asteroids | `playable` | 57.5 | 1469 | ✓ |  |  |
| astrochaos | `playable` | 57.4 | 1453 | ✓ |  |  |
| atris-1.0.7 | `playable` | 56.5 | 1459 | ✓ |  |  |
| B'lox! | `playable` | 110.3 | 2819 | ✓ |  |  |
| bang_gp | `playable` | 57.4 | 1458 | ✓ |  |  |
| BareFistFighter | `playable` | 57.5 | 1451 | ✓ |  |  |
| barrage | `playable` | 57.6 | 1454 | ✓ |  |  |
| Batiscafo (versin EXP) | `playable` | 57.8 | 1458 | ✓ |  |  |
| battlejewels-gp2x-062-100 | `playable` | 118.2 | 2985 | ✓ |  |  |
| BearOids | `playable` | 58.5 | 1472 | ✓ |  |  |
| beat2x-0.5-bin | `playable` | 61.2 | 1555 | ✓ |  |  |
| Beatbox_1.2 | `playable` | 59.6 | 1556 | ✓ |  |  |
| BeetleRun | `playable` | 58.4 | 1513 | ✓ |  |  |
| biniax-gp2x_v1.2 | `playable` | 57.3 | 1503 | ✓ |  |  |
| Biniax2_gp2x | `playable` | 39.8 | 1474 | ✓ |  |  |
| Biohazard2 | `playable` | 58.6 | 1478 | ✓ |  |  |
| BioShoot GP2X | `playable` | 57.3 | 1468 | ✓ |  |  |
| Birdshoot | `playable` | 58.5 | 1467 | – | no-audio |  |
| BisfoG | `playable` | 110.5 | 2773 | ✓ |  |  |
| blastriot1.21 | `playable` | 57.0 | 1440 | ✓ |  |  |
| blazar_v1-30_gp2x | `playable` | 57.8 | 1459 | ✓ |  |  |
| blingo 1.2 | `playable` | 56.2 | 1458 | ✓ |  |  |
| blipsgp2x | `playable` | 56.2 | 1462 | ✓ |  |  |
| Blix2x | `playable` | 62.1 | 1561 | ✓ |  |  |
| blobbyvolley | `playable` | 57.3 | 1451 | ✓ |  |  |
| blobwars_2x | `playable` | 62.7 | 1588 | ✓ |  |  |
| block | `playable` | 61.7 | 1560 | ✓ |  |  |
| blockdudegp2x | `playable` | 54.9 | 1459 | ✓ |  |  |
| Blocked | `playable` | 114.3 | 2910 | ✓ |  |  |
| blockoid | `playable` | 56.8 | 1453 | ✓ |  |  |
| Blockrage2x | `playable` | 58.7 | 1482 | ✓ |  |  |
| blox | `playable` | 42.3 | 1066 | ✓ |  |  |
| Bloxz_DEMO | `playable` | 58.2 | 1473 | ✓ |  |  |
| bluecube2x | `playable` | 57.6 | 1450 | ✓ |  |  |
| bobtron-gp2x | `playable` | 57.5 | 1449 | ✓ |  |  |
| Bombs Panic | `playable` | 112.1 | 2820 | ✓ |  |  |
| Boomshine2x_1.12_gp2x | `playable` | 57.2 | 1468 | ✓ |  |  |
| Boulders-0 | `playable` | 57.6 | 1464 | ✓ |  |  |
| brassmunkey_gp2x_1.0 | `playable` | 60.2 | 1521 | ✓ |  |  |
| BubbleX | `playable` | 69.4 | 1739 | ✓ |  |  |
| BubTrain_GP2X-2006_Entry_No-Sound | `playable` | 58.4 | 1554 | ✓ |  |  |
| bugafactorx-v03-beta | `playable` | 61.3 | 1555 | – | no-audio |  |
| BugWarsSE_v1.0 | `playable` | 60.4 | 1556 | ✓ |  |  |
| bumprace-0.2 | `playable` | 60.4 | 1561 | ✓ |  |  |
| BurokkuDemo1 | `playable` | 58.4 | 1477 | ✓ |  |  |
| buscaminas | `playable` | 57.2 | 1450 | – | no-audio |  |
| buttongame | `playable` | 57.0 | 1432 | ✓ |  |  |
| BuzzysBadDay-1.0 | `playable` | 58.1 | 1468 | ✓ |  |  |
| cackb2 | `playable` | 35.6 | 76 | ✓ |  |  |
| CamelotWarriors-GP2x_v1.0 | `playable` | 57.7 | 1466 | ✓ |  |  |
| cardm | `playable` | 57.1 | 1451 | – | no-audio |  |
| CascadeBeneath v1.0 for GP2X | `playable` | 59.2 | 1490 | ✓ |  |  |
| cat_trap | `playable` | 106.1 | 2706 | ✓ |  |  |
| cavecopter_gp2x | `playable` | 20.4 | 511 | – | no-audio |  |
| ccrg | `playable` | 61.3 | 1539 | ✓ |  |  |
| cdogs2x04 | `playable` | 98.6 | 2507 | ✓ |  |  |
| cgenius-gp2x | `playable` | 58.5 | 1511 | ✓ |  |  |
| chaos2x | `playable` | 57.8 | 1453 | ✓ |  |  |
| checkersgp2x | `playable` | 55.9 | 1454 | ✓ |  |  |
| chess2x05 | `playable` | 56.6 | 1458 | ✓ |  |  |
| chicken-puyopuyo | `playable` | 54.8 | 1383 | – | no-audio |  |
| Chopper | `playable` | 57.8 | 1466 | – | no-audio |  |
| ChopperAttackv1.0.17 | `playable` | 99.2 | 2748 | ✓ |  |  |
| Chroma | `playable` | 112.0 | 2829 | ✓ |  |  |
| chuckiev12 | `playable` | 57.5 | 1451 | ✓ |  |  |
| Codemaster | `playable` | 111.6 | 2806 | ✓ |  |  |
| Comando2gp2xEN | `playable` | 58.6 | 1473 | ✓ |  |  |
| ConnyCarrot | `playable` | 57.5 | 1460 | ✓ |  |  |
| coppergreen | `playable` | 56.2 | 1459 | ✓ |  |  |
| cosmo2x_01 | `playable` | 61.8 | 1561 | – | no-audio |  |
| CowSuckers-1.0 | `playable` | 61.5 | 1556 | ✓ |  |  |
| Crapong | `playable` | 61.1 | 1555 | ✓ |  |  |
| crazeeman | `playable` | 65.1 | 1645 | ✓ |  |  |
| crimsonV1 | `playable` | 56.3 | 1455 | ✓ |  |  |
| crocodingusgp2x | `playable` | 110.1 | 2765 | ✓ |  |  |
| crossroads | `playable` | 57.5 | 1452 | ✓ |  |  |
| CUBES | `playable` | 58.5 | 1479 | ✓ |  |  |
| cyberhockeyV2_6 | `playable` | 57.5 | 1447 | ✓ |  |  |
| DABAKKA-0 | `playable` | 65.4 | 1643 | ✓ |  |  |
| Dance2x Alpha GPE | `playable` | 57.3 | 1463 | ✓ |  |  |
| DangerMouse | `playable` | 111.1 | 2803 | ✓ |  |  |
| Dark_Light_SDL2X | `playable` | 57.4 | 1465 | ✓ |  |  |
| Dastardly dungeon 1.5 | `playable` | 56.8 | 1459 | ✓ |  |  |
| dd2x | `playable` | 123.0 | 3090 | ✓ |  |  |
| DealOrNoDeal-v12 | `playable` | 61.1 | 1562 | ✓ |  |  |
| DeathTrap1_1 | `playable` | 59.9 | 1558 | ✓ |  |  |
| debian_vs_pimientos_2x_0.1.2 | `playable` | 58.0 | 1529 | ✓ |  |  |
| defeatme-gp2x-1.0.1 | `playable` | 61.3 | 1541 | ✓ |  |  |
| diamant_1_01 | `playable` | 31.1 | 786 | ✓ |  |  |
| Digger | `playable` | 109.2 | 2877 | ✓ |  |  |
| dodge | `playable` | 58.0 | 1464 | ✓ |  |  |
| DontGetCrushed v1.0 for GP2X | `playable` | 62.1 | 1559 | ✓ |  |  |
| dopewars2x | `playable` | 58.0 | 1457 | – | no-audio |  |
| dosmugen | `playable` | 56.5 | 1468 | ✓ |  |  |
| Dr. Mates v1.0 | `playable` | 43.6 | 103 | ✓ |  |  |
| Drill2x_final | `playable` | 55.9 | 1435 | ✓ |  |  |
| drill2x_xtreme_v1.0.3 | `playable` | 57.4 | 1462 | ✓ |  |  |
| drod-gp2x-1_0 | `playable` | 55.8 | 1446 | – | no-audio |  |
| dstroyGP2X1402 | `playable` | 60.7 | 1557 | – | no-audio |  |
| DubaiRace038a | `playable` | 57.2 | 1438 | ✓ |  |  |
| dyc_gp2x | `playable` | 107.8 | 2734 | ✓ |  |  |
| dynamategp2x | `playable` | 56.9 | 1460 | ✓ |  |  |
| E-Fighters2x_FIRST_ALPHA_0_0_5_fixedSound | `playable` | 110.1 | 2800 | ✓ |  |  |
| EasterQuest | `playable` | 111.0 | 2807 | ✓ |  |  |
| eggstreme3_v1-00_gp2x | `playable` | 57.9 | 1458 | ✓ |  |  |
| egoboo2xFeb1207 | `playable` | 93.5 | 3056 | ✓ |  |  |
| Electronia | `playable` | 61.8 | 1559 | ✓ |  |  |
| enigma | `playable` | 47.1 | 1268 | ✓ |  |  |
| entombed2x | `playable` | 57.3 | 1524 | ✓ |  |  |
| EpicFreeFall_GP2X | `playable` | 57.2 | 1459 | ✓ |  |  |
| EpicRocks_GP2X | `playable` | 57.1 | 1457 | ✓ |  |  |
| escapa-v1 | `playable` | 58.0 | 1463 | ✓ |  |  |
| escoba_exp-20101016.gp2x | `playable` | 57.4 | 1457 | – | no-audio |  |
| exi_shoot_gp2x | `playable` | 57.2 | 1441 | ✓ |  |  |
| extraterrestres | `playable` | 94.6 | 2535 | ✓ |  |  |
| extraterrestres-0 | `playable` | 55.0 | 1465 | – | no-audio |  |
| exult_rc3 | `playable` | 41.6 | 1541 | ✓ |  |  |
| Factor-v1.0-final | `playable` | 57.4 | 1460 | ✓ |  |  |
| falldown_gp2x | `playable` | 95.3 | 2393 | ✓ |  |  |
| FCRLG | `playable` | 57.3 | 1442 | – | no-audio |  |
| fenix | `playable` | 58.8 | 1557 | ✓ |  |  |
| fenixGamePack | `playable` | 59.8 | 1554 | ✓ |  |  |
| fifteen_01 | `playable` | 57.8 | 1456 | – | no-audio |  |
| FindMii | `playable` | 110.3 | 2794 | ✓ |  |  |
| Firewhip | `playable` | 105.2 | 2811 | ✓ |  |  |
| Fishball-1.2 | `playable` | 61.1 | 1540 | ✓ |  |  |
| fissionfield2x | `playable` | 57.7 | 1457 | ✓ |  |  |
| Flappynerd_GP2X | `playable` | 29.1 | 735 | ✓ |  |  |
| Flaschenspiel | `playable` | 57.2 | 1455 | – | no-audio |  |
| FleshChasmer | `playable` | 56.4 | 1437 | ✓ |  |  |
| FleshChasmer Zero | `playable` | 57.0 | 1436 | ✓ |  |  |
| FlipIR_GP2X | `playable` | 110.7 | 2791 | ✓ |  |  |
| floaters | `playable` | 57.8 | 1461 | ✓ |  |  |
| flobopuyo0.20.1 | `playable` | 56.0 | 1424 | ✓ |  |  |
| flowflowmania-0_6-gp2x | `playable` | 39.3 | 1456 | – | no-audio |  |
| flurkies_v1-25_gp2x | `playable` | 62.0 | 1559 | ✓ |  |  |
| fm | `playable` | 104.0 | 2607 | ✓ |  |  |
| Football2X | `playable` | 111.3 | 2803 | ✓ |  |  |
| formula1gp2x | `playable` | 57.5 | 1458 | ✓ |  |  |
| Fragger2x | `playable` | 61.9 | 1557 | ✓ |  |  |
| freec2x | `playable` | 38.2 | 966 | ✓ |  |  |
| freecell_1 | `playable` | 57.8 | 1448 | ✓ |  |  |
| freedroid2x06 | `playable` | 80.4 | 2073 | ✓ |  |  |
| freesci | `playable` | 61.9 | 1559 | – | no-audio |  |
| friq-beta-07 | `playable` | 57.8 | 1536 | ✓ |  |  |
| frozen2x-0.1 | `playable` | 65.0 | 1635 | ✓ |  |  |
| fruits2x | `playable` | 57.4 | 1454 | – | no-audio |  |
| fruits_gp2x | `playable` | 57.7 | 1457 | ✓ |  |  |
| FullBoard (test ver.) | `playable` | 61.5 | 1543 | ✓ |  |  |
| fvc | `playable` | 57.5 | 1447 | ✓ |  |  |
| FyWod_2x | `playable` | 61.0 | 1557 | ✓ |  |  |
| game bIld 2 | `playable` | 57.7 | 1452 | ✓ |  |  |
| game-watch-mario-bros | `playable` | 61.6 | 1556 | ✓ |  |  |
| gchess-v1.0.1-bin | `playable` | 61.5 | 1561 | – | no-audio |  |
| gchess-v1.1.0-bin | `playable` | 61.5 | 1556 | – | no-audio |  |
| Geek 'em up GP2X | `playable` | 56.6 | 1594 | ✓ |  |  |
| gemdrop2x_v02 | `playable` | 57.2 | 1465 | ✓ |  |  |
| GeneralPromise | `playable` | 109.9 | 2790 | ✓ |  |  |
| Ghostbusters_WIP | `playable` | 58.0 | 1459 | ✓ |  |  |
| ghostpix_v10_gp2x | `playable` | 57.7 | 1455 | ✓ |  |  |
| glouton | `playable` | 57.9 | 1454 | ✓ |  |  |
| gnp_104 | `playable` | 54.0 | 1468 | ✓ |  |  |
| gnugo2x | `playable` | 57.5 | 1455 | – | no-audio |  |
| gnurobbo_0.66_open2x | `playable` | 52.2 | 1351 | ✓ |  |  |
| godori | `playable` | 69.5 | 1745 | – | no-audio |  |
| GoitGP | `playable` | 56.9 | 1454 | ✓ |  |  |
| gorillaz | `playable` | 52.6 | 1322 | ✓ |  |  |
| gp2hanoi_0.8.1_gp2x | `playable` | 57.7 | 1454 | ✓ |  |  |
| gp2x-blobwars-0.1 | `playable` | 59.4 | 1502 | ✓ |  |  |
| gp2x-bubbletrain-0.1 | `playable` | 55.8 | 1451 | ✓ |  |  |
| gp2x-ceferino-0.1 | `playable` | 55.4 | 1456 | – | no-audio |  |
| gp2x-formido-0.1 | `playable` | 42.5 | 1553 | ✓ |  |  |
| gp2x-invaders-preview-version | `playable` | 61.6 | 1560 | ✓ |  |  |
| gp2x-netrok-0.1 | `playable` | 51.7 | 1459 | ✓ |  |  |
| gp2x-sand-0.3 | `playable` | 57.8 | 1451 | – | no-audio |  |
| gp2x-shienso-bin_061021 | `playable` | 57.6 | 1454 | ✓ |  |  |
| gp2x-smc-0.1 | `playable` | 53.8 | 1444 | ✓ |  |  |
| gp2x-tenmado-0.1 | `playable` | 56.9 | 1451 | – | no-audio |  |
| gp2x-tong-v1 | `playable` | 109.3 | 2764 | – | no-audio |  |
| gp2x_2xmas | `playable` | 61.9 | 1558 | ✓ |  |  |
| GP2X_BallGame_0.49 | `playable` | 62.4 | 1567 | ✓ |  |  |
| gp2x_drench | `playable` | 59.1 | 1502 | ✓ |  |  |
| GP2X_Nat2007 | `playable` | 46.2 | 1166 | ✓ |  |  |
| GP2X_TLI | `playable` | 29.2 | 738 | ✓ |  |  |
| gp2xbug | `playable` | 116.6 | 2978 | ✓ |  |  |
| gp2xgo-v1.1.0-bin | `playable` | 57.4 | 1478 | – | no-audio |  |
| gp2xjunkie | `playable` | 61.8 | 1589 | ✓ |  |  |
| gp2xmancala-v1.1.1-bin | `playable` | 57.6 | 1459 | – | no-audio |  |
| GP2XOfLife | `playable` | 105.9 | 2677 | – | no-audio |  |
| gp2xpang-v.1.1.1 | `playable` | 92.7 | 2383 | ✓ |  |  |
| gp2xrick 1.0 | `playable` | 56.7 | 1438 | ✓ |  |  |
| GpFrontier v0.1 | `playable` | 58.0 | 1473 | ✓ |  |  |
| gpfrontier v0.4 | `playable` | 55.0 | 1455 | ✓ |  |  |
| gpnoid2x | `playable` | 61.2 | 1557 | ✓ |  |  |
| GPrina-GP2x_v1.0 | `playable` | 60.8 | 1556 | ✓ |  |  |
| GPSquares_GP2X | `playable` | 62.0 | 1556 | – | no-audio |  |
| gr-v1001-gp2x | `playable` | 56.2 | 1458 | ✓ |  |  |
| green | `playable` | 56.4 | 1436 | ✓ |  |  |
| grow | `playable` | 57.3 | 1438 | – | no-audio |  |
| gxeskiv | `playable` | 54.9 | 1387 | – | no-audio |  |
| HamstersEscape (Gp2x F-100 F-200) | `playable` | 53.0 | 1347 | ✓ |  |  |
| hanagechu2x_gbax2007 | `playable` | 63.3 | 1598 | ✓ |  |  |
| hanagechu2xalpha | `playable` | 57.9 | 1461 | ✓ |  |  |
| Heretic2X_v0.5 | `playable` | 60.8 | 1530 | ✓ |  |  |
| heroes2x02 | `playable` | 52.2 | 1322 | ✓ |  |  |
| hex-a-hop | `playable` | 61.9 | 1559 | – | no-audio |  |
| hexbattle2x | `playable` | 57.5 | 1457 | ✓ |  |  |
| HumphreyGP2X | `playable` | 61.2 | 1553 | ✓ |  |  |
| Hyperion_GP2X_demo | `playable` | 58.1 | 1471 | ✓ |  |  |
| jump_n_blob_gp2x | `playable` | 56.9 | 1519 | ✓ |  |  |
| jumpnbumpgp2x | `playable` | 56.6 | 1460 | ✓ |  |  |
| Jurlx2 | `playable` | 57.2 | 1453 | ✓ |  |  |
| just4qix | `playable` | 57.5 | 1457 | ✓ |  |  |
| kampfimall-gp2x | `playable` | 57.5 | 1449 | – | no-audio |  |
| kampfimall-gp2x-music | `playable` | 61.7 | 1547 | ✓ |  |  |
| ketm_2x_gp2x | `playable` | 50.4 | 1520 | ✓ |  |  |
| KicknPLay_1.1 | `playable` | 57.8 | 1452 | ✓ |  |  |
| Klaur | `playable` | 108.3 | 2783 | ✓ |  |  |
| Knight Lore | `playable` | 57.8 | 1456 | ✓ |  |  |
| koules2x_02 | `playable` | 61.3 | 1555 | ✓ |  |  |
| kuklomenos_gp2x_201209 | `playable` | 56.9 | 1472 | ✓ |  |  |
| kurukuru2x | `playable` | 57.2 | 1461 | ✓ |  |  |
| la | `playable` | 57.0 | 1429 | ✓ |  |  |
| LABYRINTH | `playable` | 57.7 | 1457 | – | no-audio |  |
| ladykiller | `playable` | 56.9 | 1554 | ✓ |  |  |
| las-tres-luces-de-glaurung-remake | `playable` | 56.7 | 1550 | ✓ |  |  |
| lbreakoutgp2x | `playable` | 58.4 | 1557 | ✓ |  |  |
| levelEdit | `playable` | 61.7 | 1556 | – | no-audio |  |
| levelshmup | `playable` | 85.1 | 2182 | ✓ |  |  |
| Lexeme | `playable` | 109.4 | 2837 | ✓ |  |  |
| lights-out | `playable` | 55.6 | 1401 | – | no-audio |  |
| LinesXv3 | `playable` | 65.3 | 1639 | ✓ |  |  |
| logicx | `playable` | 63.9 | 1611 | ✓ |  |  |
| Logoball | `playable` | 108.5 | 2776 | ✓ |  |  |
| lumix-beta-01 | `playable` | 60.3 | 1519 | – | no-audio |  |
| mad-mix-game-20b-final | `playable` | 57.3 | 1454 | ✓ |  |  |
| madbomber | `playable` | 56.9 | 1454 | ✓ |  |  |
| malvado2x | `playable` | 56.3 | 1456 | ✓ |  |  |
| MAME-N22_51 | `playable` | 55.7 | 1465 | ✓ |  |  |
| mancala-v1.0.1 | `playable` | 61.4 | 1556 | – | no-audio |  |
| March of the mini tux | `playable` | 63.2 | 1606 | ✓ |  |  |
| Marte Necesita Vacas GP2X | `playable` | 64.2 | 1654 | ✓ |  |  |
| Masteries_Journey_to_the_Center_of_the_earth_GP2X | `playable` | 60.0 | 1553 | ✓ |  |  |
| masterpiece2x | `playable` | 57.8 | 1458 | – | no-audio |  |
| MazeThingie | `playable` | 57.9 | 1459 | ✓ |  |  |
| MazezaMGP2X | `playable` | 91.0 | 2345 | ✓ |  |  |
| memory | `playable` | 58.0 | 1480 | ✓ |  |  |
| MemoryGP2X-v11 | `playable` | 57.9 | 1462 | ✓ |  |  |
| meritous | `playable` | 56.8 | 1454 | ✓ |  |  |
| Merlin2x_beta_021 | `playable` | 61.5 | 1558 | ✓ |  |  |
| metaphysik | `playable` | 60.1 | 1521 | ✓ |  |  |
| methaneV1 | `playable` | 57.4 | 1452 | ✓ |  |  |
| minigolf | `playable` | 57.3 | 1554 | – | no-audio |  |
| minos-gp2x | `playable` | 56.3 | 1439 | ✓ |  |  |
| misterhachi | `playable` | 48.1 | 1407 | ✓ |  |  |
| mk13.gpe | `playable` | 65.2 | 1635 | ✓ |  |  |
| mkACE.gpe | `playable` | 65.4 | 1641 | ✓ |  |  |
| mkONE.gpe | `playable` | 65.2 | 1636 | ✓ |  |  |
| MM2X | `playable` | 57.1 | 1439 | ✓ |  |  |
| monacoGP | `playable` | 56.9 | 1462 | ✓ |  |  |
| monochromeworlds-gp2x-1.0.0 | `playable` | 60.8 | 1541 | ✓ |  |  |
| moonlander | `playable` | 60.1 | 1540 | ✓ |  |  |
| MouthTrap | `playable` | 110.5 | 2787 | ✓ |  |  |
| mueppv32 | `playable` | 117.8 | 2981 | ✓ |  |  |
| mush_gp2x | `playable` | 43.4 | 1118 | ✓ |  |  |
| Mutant Tank Knights | `playable` | 56.6 | 295 | ✓ |  |  |
| MyriadUpdated | `playable` | 60.2 | 1557 | ✓ |  |  |
| mzx-2.84c | `playable` | 57.8 | 1456 | ✓ |  |  |
| mzx282-gp2x | `playable` | 57.6 | 1453 | ✓ |  |  |
| n-tris_v1.0 | `playable` | 66.9 | 1682 | ✓ |  |  |
| nanobounce-pacc-gp2x | `playable` | 61.0 | 1556 | ✓ |  |  |
| nazcarunners-0 | `playable` | 45.0 | 51 | ✓ |  |  |
| nazcasphere | `playable` | 47.0 | 53 | ✓ |  |  |
| ne_deluxe_gp2x | `playable` | 57.5 | 1452 | ✓ |  |  |
| ne_gp2x | `playable` | 56.0 | 1412 | ✓ |  |  |
| Nebulus_gp2x | `playable` | 62.0 | 1556 | – | no-audio |  |
| NecNec2x | `playable` | 58.0 | 1469 | ✓ |  |  |
| Net-Bubble-gp2x_1-21-06_bin | `playable` | 57.4 | 1458 | – | no-audio |  |
| newsuperpang | `playable` | 57.4 | 1453 | ✓ |  |  |
| Nifty | `playable` | 56.9 | 1451 | ✓ |  |  |
| noiz2saV3 | `playable` | 64.4 | 1640 | ✓ |  |  |
| Nom | `playable` | 57.3 | 1444 | ✓ |  |  |
| odonata_demo | `playable` | 56.4 | 1427 | ✓ |  |  |
| omok | `playable` | 57.6 | 1443 | ✓ |  |  |
| OpenBOR_v2.1933 | `playable` | 57.7 | 1466 | ✓ |  |  |
| OpenBOR_v3.0_Build_2615_&_2637 | `playable` | 57.4 | 1566 | ✓ |  |  |
| openggs | `playable` | 56.7 | 1454 | ✓ |  |  |
| openglad2x | `playable` | 55.9 | 1449 | – | no-audio |  |
| opentyrian2x_0.3_complete | `playable` | 57.7 | 1542 | ✓ |  |  |
| opposite_lock | `playable` | 54.7 | 1526 | ✓ |  |  |
| OrbitalSniper2x_v1.1 | `playable` | 58.1 | 1459 | ✓ |  |  |
| othello_v1.0 | `playable` | 58.0 | 1498 | ✓ |  |  |
| oxov06 | `playable` | 57.0 | 1439 | – | no-audio |  |
| PAF | `playable` | 57.5 | 1455 | ✓ |  |  |
| PantaVsDragon (Gp2x F-100 F-200) | `playable` | 47.9 | 1233 | ✓ |  |  |
| para3 | `playable` | 58.1 | 1460 | ✓ |  |  |
| paraballgp2x | `playable` | 59.6 | 1514 | ✓ |  |  |
| Payback | `playable` | 70.3 | 1931 | ✓ |  |  |
| PaybackDemo | `playable` | 30.7 | 810 | ✓ |  |  |
| pc | `playable` | 61.6 | 1557 | ✓ |  |  |
| pdcv060b | `playable` | 61.9 | 1559 | ✓ |  |  |
| Pentominos | `playable` | 58.1 | 1460 | ✓ |  |  |
| PerfectFit | `playable` | 58.1 | 1461 | – | no-audio |  |
| Peuppy_10_GP2X | `playable` | 31.9 | 808 | ✓ |  |  |
| pez | `playable` | 61.5 | 1555 | – | no-audio |  |
| Phishy-0 | `playable` | 57.3 | 1461 | ✓ |  |  |
| physique | `playable` | 57.6 | 1538 | ✓ |  |  |
| Pika2x | `playable` | 58.0 | 1458 | ✓ |  |  |
| pintor2x | `playable` | 94.4 | 2371 | ✓ |  |  |
| pixpang | `playable` | 52.2 | 1453 | ✓ |  |  |
| PocketSnes_SMRPG | `playable` | 119.6 | 3004 | – | no-audio |  |
| Poker2x | `playable` | 110.1 | 2788 | ✓ |  |  |
| Poker_Gp2Xv1.0 | `playable` | 117.2 | 2987 | ✓ |  |  |
| Pond2X | `playable` | 61.4 | 1546 | – | no-audio |  |
| Pong | `playable` | 57.9 | 1454 | – | no-audio |  |
| pong2player | `playable` | 58.0 | 1456 | – | no-audio |  |
| pong2v060x | `playable` | 61.8 | 1556 | – | no-audio |  |
| Pool Panic | `playable` | 57.2 | 1442 | ✓ |  |  |
| powder2x-112 | `playable` | 57.5 | 1559 | – | no-audio |  |
| powermanga-0.80 | `playable` | 57.8 | 1510 | ✓ |  |  |
| PowerSlide | `playable` | 57.2 | 1449 | ✓ |  |  |
| PPlane | `playable` | 56.3 | 1421 | ✓ |  |  |
| PPlane2.GP2X | `playable` | 57.5 | 1522 | ✓ |  |  |
| prboom-gp2x | `playable` | 61.2 | 1560 | – | no-audio |  |
| proj0-demo_01 | `playable` | 60.6 | 1544 | ✓ |  |  |
| protozoa v1.0 | `playable` | 56.9 | 1454 | ✓ |  |  |
| puckman_gp2x | `playable` | 109.7 | 2790 | ✓ |  |  |
| PulplifeWars | `playable` | 55.9 | 1458 | ✓ |  |  |
| puzzlelandgp2x | `playable` | 53.8 | 1455 | ✓ |  |  |
| qfg3-0 | `playable` | 56.5 | 1459 | ✓ |  |  |
| Quad | `playable` | 109.4 | 2783 | ✓ |  |  |
| quartz2_v1-50_gp2x | `playable` | 57.8 | 1455 | ✓ |  |  |
| Rabbit_vs_Flies_0.9 | `playable` | 61.7 | 1562 | ✓ |  |  |
| ramon atacks | `playable` | 57.5 | 1455 | ✓ |  |  |
| Release GP2X MST_RUNNERS | `playable` | 60.4 | 1553 | ✓ |  |  |
| retrovirus_1_1 | `playable` | 59.0 | 1497 | ✓ |  |  |
| RevoltOfTheBinaryCouriers GP2X | `playable` | 57.1 | 1438 | – | no-audio |  |
| reword_v0.5 | `playable` | 57.4 | 1466 | ✓ |  |  |
| rg_105 | `playable` | 54.7 | 1456 | ✓ |  |  |
| rg_ura_103 | `playable` | 55.0 | 1469 | ✓ |  |  |
| river | `playable` | 58.0 | 1454 | ✓ |  |  |
| robot-escape | `playable` | 93.8 | 202 | ✓ |  |  |
| RockRain | `playable` | 58.0 | 1464 | ✓ |  |  |
| rockrain2_exp-20100925 | `playable` | 61.2 | 1552 | ✓ |  |  |
| rookiehero_EXP.gp2x.v20120220 | `playable` | 56.7 | 1449 | ✓ |  |  |
| RoundEmUp-alpha3 | `playable` | 57.9 | 1467 | ✓ |  |  |
| rRootage_v1.0 | `playable` | 105.9 | 2721 | ✓ |  |  |
| rubidogp2x | `playable` | 56.3 | 1457 | ✓ |  |  |
| rubik | `playable` | 115.0 | 2907 | – | no-audio |  |
| ruckman_v1.03 | `playable` | 55.9 | 1458 | ✓ |  |  |
| Runner_GP2X | `playable` | 57.3 | 1461 | ✓ |  |  |
| s-tris2_v1-64_gp2x | `playable` | 57.9 | 1457 | ✓ |  |  |
| Sachunsung2_1 | `playable` | 96.8 | 2426 | ✓ |  |  |
| sachunsungx | `playable` | 64.9 | 1630 | ✓ |  |  |
| santaMania | `playable` | 60.6 | 1551 | ✓ |  |  |
| ScorchedGPBeta2 | `playable` | 57.7 | 1460 | ✓ |  |  |
| scummvm-0.11.1-gp2x | `playable` | 60.2 | 1553 | ✓ |  |  |
| scummvm-1.2.0-gp2x | `playable` | 60.6 | 1561 | ✓ |  |  |
| scummvm-kor0.4.2cvs | `playable` | 61.0 | 1541 | – | no-audio |  |
| SdLame | `playable` | 57.5 | 1462 | ✓ |  |  |
| sdlmonkey_0.1 | `playable` | 61.8 | 1556 | – | no-audio |  |
| sdlscav_gp2x_0.2.0 | `playable` | 108.2 | 2726 | ✓ |  |  |
| Shangai v2 | `playable` | 90.1 | 2260 | ✓ |  |  |
| ShanghaiX | `playable` | 65.5 | 1641 | ✓ |  |  |
| SheepDash | `playable` | 56.6 | 1468 | ✓ |  |  |
| Shippy84 | `playable` | 57.0 | 1463 | ✓ |  |  |
| Simon2X | `playable` | 60.5 | 1538 | – | no-audio |  |
| SimOniZ | `playable` | 113.3 | 2902 | ✓ |  |  |
| siv050 | `playable` | 55.4 | 1464 | ✓ |  |  |
| sleuth slots 2x | `playable` | 106.0 | 2746 | ✓ |  |  |
| SmallBall_GP | `playable` | 57.1 | 1520 | ✓ |  |  |
| SmashGp2x02 | `playable` | 57.6 | 1460 | ✓ |  |  |
| snail runers | `playable` | 57.3 | 1459 | ✓ |  |  |
| snake2x-1.1 | `playable` | 57.1 | 1464 | – | no-audio |  |
| snowedin5_v1-00_gp2x | `playable` | 57.9 | 1459 | ✓ |  |  |
| SOD v1.1 | `playable` | 56.7 | 1462 | ✓ |  |  |
| sokobangp2x | `playable` | 51.8 | 1463 | ✓ |  |  |
| Solitaire2x-v1.4 | `playable` | 114.8 | 2899 | – | no-audio |  |
| sopwith_camel_rc3 | `playable` | 56.8 | 1429 | ✓ |  |  |
| sources_MEMORY2X | `playable` | 57.2 | 1457 | ✓ |  |  |
| sources_Yahtzee | `playable` | 57.0 | 1459 | ✓ |  |  |
| space squares | `playable` | 64.3 | 1633 | – | no-audio |  |
| space52_gp2x(oficial) | `playable` | 43.8 | 1168 | ✓ |  |  |
| space52_gp2x(open2x) | `playable` | 44.8 | 1192 | ✓ |  |  |
| space_varments_v1.0 | `playable` | 56.6 | 1456 | ✓ |  |  |
| SpaceRocks2X | `playable` | 57.9 | 1459 | – | no-audio |  |
| SpaceSnake | `playable` | 111.1 | 2797 | ✓ |  |  |
| spacestorm | `playable` | 52.5 | 1322 | ✓ |  |  |
| spartak-chess_0.0.4_gp2x | `playable` | 57.5 | 1454 | – | no-audio |  |
| Sponge Blob Tennis | `playable` | 39.1 | 1461 | – | no-audio |  |
| spout | `playable` | 57.8 | 1454 | – | no-audio |  |
| sprint_race | `playable` | 57.2 | 1454 | ✓ |  |  |
| Sqcolony | `playable` | 64.0 | 1612 | – | no-audio |  |
| Sqdef 1.4 | `playable` | 61.1 | 1555 | ✓ |  |  |
| Squares-v051 | `playable` | 57.7 | 1466 | ✓ |  |  |
| Squaresliding | `playable` | 57.9 | 1455 | ✓ |  |  |
| StairwayToHeaven | `playable` | 58.0 | 1458 | ✓ |  |  |
| starfighter-gp2x-0.01 | `playable` | 58.1 | 1458 | ✓ |  |  |
| starsystem | `playable` | 61.5 | 1554 | ✓ |  |  |
| StarTrucker | `playable` | 57.6 | 1456 | ✓ |  |  |
| stppc2x-v1.0 | `playable` | 42.2 | 1561 | ✓ |  |  |
| stransball2 | `playable` | 66.5 | 1684 | ✓ |  |  |
| street2x | `playable` | 53.7 | 1461 | ✓ |  |  |
| subhunt | `playable` | 57.3 | 1452 | ✓ |  |  |
| sudoku-v1.0 | `playable` | 57.6 | 1453 | – | no-audio |  |
| sudoku2x-0.5 | `playable` | 62.8 | 1581 | – | no-audio |  |
| SuperChickenFallDemo | `playable` | 61.9 | 1561 | ✓ |  |  |
| SuperPaf_v1.0 | `playable` | 61.0 | 1556 | ✓ |  |  |
| superpang | `playable` | 60.8 | 1555 | ✓ |  |  |
| SuperPixelJumper v1.1 for GP2X | `playable` | 61.0 | 1538 | ✓ |  |  |
| SuperSonicSpeed | `playable` | 61.5 | 1554 | ✓ |  |  |
| survival | `playable` | 57.1 | 1462 | ✓ |  |  |
| symbolica-0.8 | `playable` | 57.6 | 1463 | ✓ |  |  |
| tail-tale | `playable` | 57.8 | 1459 | ✓ |  |  |
| Tangle | `playable` | 58.2 | 1461 | – | no-audio |  |
| tecnoballz-0.91-gp2x | `playable` | 53.3 | 1384 | ✓ |  |  |
| tesla-Siren | `playable` | 58.2 | 1464 | ✓ |  |  |
| Tetrablocks.0.4.GP2X | `playable` | 57.3 | 1437 | ✓ |  |  |
| tetwins | `playable` | 61.6 | 1546 | ✓ |  |  |
| the reversed preacher II | `playable` | 62.4 | 1568 | ✓ |  |  |
| ThreeTs_Game | `playable` | 62.3 | 1566 | ✓ |  |  |
| Thruster_GP2X | `playable` | 57.6 | 1458 | ✓ |  |  |
| tikka_dungeons_demo_1 | `playable` | 57.1 | 1440 | ✓ |  |  |
| tilematch-0.6 | `playable` | 91.0 | 2305 | ✓ |  |  |
| tileworld2x | `playable` | 53.4 | 1418 | ✓ |  |  |
| tilt | `playable` | 57.6 | 1456 | ✓ |  |  |
| TimeFrack2D for GP2X | `playable` | 57.0 | 1432 | – | no-audio |  |
| TouchGames | `playable` | 110.4 | 2793 | ✓ |  |  |
| tower | `playable` | 103.5 | 2618 | – | no-audio |  |
| towertopplergp2x | `playable` | 55.7 | 1456 | ✓ |  |  |
| TRAINS | `playable` | 57.3 | 1452 | ✓ |  |  |
| Trap75 | `playable` | 57.8 | 1454 | ✓ |  |  |
| treev060 | `playable` | 60.6 | 1564 | ✓ |  |  |
| ttxbeta170706b | `playable` | 55.0 | 1454 | – | no-audio |  |
| TUcS.app(V0.7.0 - GP2X) | `playable` | 29.9 | 758 | ✓ |  |  |
| Txishos (Gp2x F-200) | `playable` | 47.8 | 1227 | ✓ |  |  |
| Unicolor | `playable` | 57.2 | 1454 | ✓ |  |  |
| uqm2x_release_1.1 | `playable` | 64.7 | 1650 | ✓ |  |  |
| UQMgp2x-0.5.0_with_content | `playable` | 63.3 | 1606 | ✓ |  |  |
| vectoroids-2x | `playable` | 57.5 | 1456 | ✓ |  |  |
| VekDemo2 | `playable` | 118.3 | 2984 | ✓ |  |  |
| Vektar | `playable` | 119.1 | 2994 | ✓ |  |  |
| vektar-free | `playable` | 68.1 | 1722 | ✓ |  |  |
| vektarpack_v1 | `playable` | 82.7 | 2106 | ✓ |  |  |
| Ventifact | `playable` | 56.3 | 1453 | ✓ |  |  |
| vexed-gp2x-10 | `playable` | 59.9 | 1515 | ✓ |  |  |
| vexedb1 | `playable` | 61.6 | 1556 | – | no-audio |  |
| Volleyball | `playable` | 57.5 | 1447 | ✓ |  |  |
| vorton-b4 | `playable` | 59.5 | 1557 | ✓ |  |  |
| vwars | `playable` | 56.2 | 1452 | ✓ |  |  |
| waffle2x | `playable` | 57.1 | 1435 | – | no-audio |  |
| war_and_warriorgp2x | `playable` | 57.9 | 1455 | ✓ |  |  |
| warcraft | `playable` | 56.2 | 1454 | ✓ |  |  |
| warehouse_panic_v1.1_gp2x | `playable` | 56.3 | 1458 | ✓ |  |  |
| waternetgp2x | `playable` | 55.0 | 1566 | ✓ |  |  |
| wehaveballs | `playable` | 61.1 | 1536 | ✓ |  |  |
| whacky | `playable` | 61.2 | 1548 | ✓ |  |  |
| WindAndWater_teaser_110 | `playable` | 56.9 | 1434 | ✓ |  |  |
| Winter_Jumper | `playable` | 57.4 | 1450 | – | no-audio |  |
| wire3d | `playable` | 55.4 | 1499 | – | no-audio |  |
| Wiztern Demo | `playable` | 56.8 | 1455 | – | no-audio |  |
| Wizznic_2x_07alpha2 | `playable` | 55.1 | 1450 | ✓ |  |  |
| wizznic_gp2x-0.9.9 | `playable` | 55.0 | 1455 | ✓ |  |  |
| wnw | `playable` | 56.6 | 1432 | ✓ |  |  |
| wolfdx | `playable` | 57.7 | 1455 | ✓ |  |  |
| xenitris_demo | `playable` | 58.0 | 1528 | ✓ |  |  |
| xigon-X-gp2x-V1 | `playable` | 57.8 | 1459 | ✓ |  |  |
| Xpired2x 1.0 beta | `playable` | 61.2 | 1554 | ✓ |  |  |
| xRick | `playable` | 57.6 | 1559 | ✓ |  |  |
| yahtzee-v21 | `playable` | 57.7 | 1458 | ✓ |  |  |
| Zelda_roth_US_gp2x | `playable` | 56.3 | 1454 | ✓ |  |  |
| znax | `playable` | 55.4 | 1459 | ✓ |  |  |
| Znumbers | `playable` | 97.3 | 2439 | ✓ |  |  |
| Zoids Quest2X-0.0.1-2 | `playable` | 57.4 | 1452 | ✓ |  |  |
| zoltan 2x | `playable` | 56.7 | 1454 | ✓ |  |  |
| zombiesorbet_v1.0_gp2x | `playable` | 61.2 | 1571 | ✓ |  |  |
| zooov11 | `playable` | 31.9 | 803 | ✓ |  |  |
| ztunnel-0 | `playable` | 56.9 | 1442 | ✓ |  |  |

### Wiz (147 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| alephone-wiz | `incompatible` | 0.0 | 0 | – | unknown-device | /dev/ |
| CloneKeen2X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| EpicRocks_Wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| hheretic | `incompatible` | 0.0 | 0 | – | no-frames |  |
| hhexen | `incompatible` | 0.0 | 0 | – | no-frames |  |
| ioquake2 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| quake1-wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| quake_0.03 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| RailroadRampage_1.2_Wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| rott | `incompatible` | 0.0 | 0 | – | no-frames |  |
| SmallBall_Wiz | `incompatible` | 0.0 | 0 | – | no-frames |  |
| srb2 | `incompatible` | 0.0 | 0 | – | no-frames |  |
| wolf4sdl_wiz_svn | `incompatible` | 0.0 | 0 | – | no-frames |  |
| albion-v1.0.1-wiz | `black` | 24.8 | 43 | ✓ | black-screen |  |
| chroma 1.01 v1 | `black` | 0.4 | 2 | – | black-screen |  |
| eduke32 | `black` | 10.6 | 14 | – | black-screen |  |
| nazcadreams | `black` | 25.0 | 50 | ✓ | black-screen |  |
| nazcarunners | `black` | 31.0 | 72 | ✓ | black-screen |  |
| Nazcasphere | `black` | 33.9 | 77 | ✓ | black-screen |  |
| openjazz-wiz | `black` | 16.9 | 22 | ✓ | black-screen |  |
| opentyrian | `black` | 13.5 | 9 | – | black-screen |  |
| paraballwiz | `black` | 3.8 | 6 | – | black-screen |  |
| warcraft-beta3-wiz | `black` | 27.1 | 47 | ✓ | black-screen |  |
| Wiz_Propis_Demo | `black` | 28.6 | 44 | ✓ | black-screen |  |
| WizSticks | `black` | 18.4 | 58 | ✓ | black-screen |  |
| xcom1-v1.0.2-wiz | `black` | 53.4 | 105 | ✓ | black-screen |  |
| xcom2-v1.0.1-wiz | `black` | 109.3 | 2784 | ✓ | black-screen |  |
| blingo | `ingame` | 54.6 | 1405 | ✓ | garbled-visuals | pixel-to-pixel noise of 100, far above what dithered artwork reaches; the frame looks like |
| Ruckman-Wiz | `ingame` | 57.8 | 1504 | ✓ | garbled-visuals | pixel-to-pixel noise of 102, far above what dithered artwork reaches; the frame looks like |
| [DEMO] Wiztern | `playable` | 54.6 | 1400 | ✓ |  |  |
| abuse-wiz | `playable` | 53.1 | 1406 | ✓ |  |  |
| AdamantArmorAffectionWiz | `playable` | 60.1 | 1536 | ✓ |  |  |
| airstrike-wiz-1.01 | `playable` | 56.5 | 1432 | ✓ |  |  |
| alexsfalldown | `playable` | 61.5 | 1546 | ✓ |  |  |
| altitude | `playable` | 57.7 | 1503 | ✓ |  |  |
| Animatch Wiz | `playable` | 56.9 | 1508 | ✓ |  |  |
| Art Shot Wiz | `playable` | 55.0 | 1426 | ✓ |  |  |
| Asteroids | `playable` | 58.7 | 1504 | ✓ |  |  |
| Balloonacy_wiz_wip | `playable` | 113.6 | 2909 | ✓ |  |  |
| battlejewels-wiz-public001demo | `playable` | 60.0 | 1527 | ✓ |  |  |
| beat2x-wiz | `playable` | 61.0 | 1539 | ✓ |  |  |
| Biological Defend | `playable` | 56.9 | 1492 | ✓ |  |  |
| BitDEFENSE | `playable` | 45.1 | 1212 | – | no-audio |  |
| BlastRiot122Wiz | `playable` | 57.5 | 1451 | ✓ |  |  |
| Blix2x | `playable` | 57.7 | 1451 | ✓ |  |  |
| Boomshine2x_1.12_wiz | `playable` | 54.5 | 1397 | ✓ |  |  |
| BubbleTrainWiz_5-20-09 | `playable` | 53.3 | 1410 | ✓ |  |  |
| BugwarsSE | `playable` | 55.1 | 1401 | ✓ |  |  |
| Camelot Warriors | `playable` | 53.7 | 1399 | ✓ |  |  |
| CartoonWiz | `playable` | 109.6 | 2762 | ✓ |  |  |
| CDogs-wiz | `playable` | 54.1 | 1377 | ✓ |  |  |
| cgenius-wiz | `playable` | 37.1 | 1373 | ✓ |  |  |
| ColonyConflict_V1.1_B6 | `playable` | 99.9 | 2648 | ✓ |  |  |
| Dastardly_Dungeon | `playable` | 55.4 | 1408 | ✓ |  |  |
| Dd2x | `playable` | 57.6 | 1446 | ✓ |  |  |
| deicide3_eng | `playable` | 61.3 | 1537 | ✓ |  |  |
| Demons World | `playable` | 57.2 | 1437 | ✓ |  |  |
| DungeonRunner | `playable` | 104.5 | 2713 | ✓ |  |  |
| DuoWIZ_Pong | `playable` | 103.1 | 2649 | ✓ |  |  |
| EpicFreeFall_Wiz | `playable` | 47.8 | 1231 | ✓ |  |  |
| epiphany | `playable` | 55.6 | 1422 | ✓ |  |  |
| freecell2x | `playable` | 102.4 | 2910 | ✓ |  |  |
| Geca Blaster 2 (Gp2x Wiz) | `playable` | 49.9 | 1276 | ✓ |  |  |
| Ghostpix | `playable` | 55.7 | 1408 | ✓ |  |  |
| gnurobbo_0.65_wiz | `playable` | 52.9 | 1436 | ✓ |  |  |
| gobble | `playable` | 59.5 | 1505 | – | no-audio |  |
| gr-v1001-wiz | `playable` | 54.5 | 1406 | ✓ |  |  |
| herknights | `playable` | 54.8 | 1435 | ✓ |  |  |
| hexen2 | `playable` | 53.3 | 1370 | ✓ |  |  |
| kuklomenos | `playable` | 55.8 | 1424 | ✓ |  |  |
| malvado | `playable` | 54.2 | 1399 | ✓ |  |  |
| March of the mini tux(wiz version) | `playable` | 116.4 | 2949 | ✓ |  |  |
| midway | `playable` | 63.1 | 1601 | ✓ |  |  |
| Minigolf | `playable` | 58.6 | 1500 | – | no-audio |  |
| minos-gp2x-wiz | `playable` | 55.8 | 1411 | ✓ |  |  |
| Monster2-1.0-wiz | `playable` | 50.3 | 1421 | ✓ |  |  |
| mush_gp2x | `playable` | 42.2 | 1090 | ✓ |  |  |
| mush_gp2x-0 | `playable` | 33.2 | 885 | ✓ |  |  |
| Myriad | `playable` | 58.2 | 1507 | ✓ |  |  |
| nethack-wiz | `playable` | 57.6 | 1453 | – | no-audio |  |
| NewSuperPang05 | `playable` | 54.8 | 1391 | ✓ |  |  |
| noiz2sa_wiz | `playable` | 56.1 | 1432 | ✓ |  |  |
| openggs | `playable` | 55.5 | 1417 | ✓ |  |  |
| Out Zone | `playable` | 56.6 | 1421 | ✓ |  |  |
| paf | `playable` | 55.3 | 1432 | ✓ |  |  |
| Pentominos | `playable` | 61.7 | 1548 | ✓ |  |  |
| PEZ | `playable` | 54.7 | 1385 | – | no-audio |  |
| pgw | `playable` | 58.0 | 1464 | ✓ |  |  |
| Pharaoh | `playable` | 57.5 | 1443 | ✓ |  |  |
| PhishyWiz | `playable` | 55.0 | 1404 | ✓ |  |  |
| Powder2X_wiz_114_v01 | `playable` | 53.6 | 1368 | – | no-audio |  |
| PPlane | `playable` | 56.0 | 1417 | ✓ |  |  |
| PPlane2.WIZ | `playable` | 98.7 | 2628 | ✓ |  |  |
| prboom-wiz | `playable` | 55.6 | 1415 | – | no-audio |  |
| preggo_Wiz | `playable` | 54.5 | 1403 | ✓ |  |  |
| Propis | `playable` | 48.0 | 1211 | ✓ |  |  |
| protozoa | `playable` | 55.1 | 1406 | ✓ |  |  |
| PuzzleDevilWizDemo | `playable` | 52.4 | 1327 | ✓ |  |  |
| Rezerwar | `playable` | 60.0 | 1516 | ✓ |  |  |
| roadfighter | `playable` | 56.7 | 1439 | ✓ |  |  |
| rockrain-gp2x-wiz | `playable` | 61.3 | 1543 | ✓ |  |  |
| Sachunsung2 | `playable` | 57.5 | 1441 | ✓ |  |  |
| scummvm-1.2.0-gp2xwiz | `playable` | 56.8 | 1480 | ✓ |  |  |
| Shanghai2 | `playable` | 57.3 | 1438 | ✓ |  |  |
| Shock Troopers Base Defense | `playable` | 52.0 | 1396 | ✓ |  |  |
| SimOniZ | `playable` | 104.2 | 2657 | ✓ |  |  |
| Skull (Windows, Linux & Gp2x Wiz) | `playable` | 45.7 | 1214 | ✓ |  |  |
| sleuthslots | `playable` | 58.2 | 1506 | ✓ |  |  |
| smw_1.7 | `playable` | 52.1 | 1442 | ✓ |  |  |
| Snow Bros 2 | `playable` | 57.2 | 1439 | ✓ |  |  |
| SOD_Wiz | `playable` | 58.6 | 1509 | ✓ |  |  |
| Sopwith | `playable` | 61.3 | 1543 | ✓ |  |  |
| Space Varments | `playable` | 58.6 | 1508 | ✓ |  |  |
| spout | `playable` | 57.3 | 1441 | – | no-audio |  |
| Sqdef_Wiz_14A | `playable` | 57.9 | 1478 | ✓ |  |  |
| Sudoku2X | `playable` | 57.2 | 1441 | – | no-audio |  |
| SudoQ | `playable` | 48.5 | 1233 | ✓ |  |  |
| supertux-wiz | `playable` | 50.6 | 1391 | ✓ |  |  |
| Tail Tale | `playable` | 57.1 | 1437 | ✓ |  |  |
| tetwizdownload | `playable` | 109.9 | 2767 | ✓ |  |  |
| The Minigame Project | `playable` | 52.8 | 1407 | ✓ |  |  |
| tilt | `playable` | 55.6 | 1410 | ✓ |  |  |
| Trap75 | `playable` | 56.8 | 1429 | ✓ |  |  |
| tricorder | `playable` | 55.4 | 1409 | ✓ |  |  |
| TUcS.app(V0.7.0 - Wiz) | `playable` | 105.3 | 2713 | ✓ |  |  |
| Twin Cobra | `playable` | 57.3 | 1439 | ✓ |  |  |
| uqm2x_release.1.1 | `playable` | 71.6 | 1819 | ✓ |  |  |
| wiz-car-binary_090818a | `playable` | 61.1 | 1536 | ✓ |  |  |
| Wiz_Blox | `playable` | 108.3 | 2763 | ✓ |  |  |
| wiz_drench | `playable` | 114.7 | 2914 | ✓ |  |  |
| WIZ_S4S | `playable` | 107.6 | 2722 | ✓ |  |  |
| wizchess-v1.1.0-bin | `playable` | 57.2 | 1446 | – | no-audio |  |
| wizchess-v1.2.0-bin | `playable` | 57.2 | 1447 | – | no-audio |  |
| WizFrontier v0.1 | `playable` | 54.1 | 1414 | ✓ |  |  |
| wizgo-v1.1.0-bin | `playable` | 57.0 | 1449 | – | no-audio |  |
| WizGolf | `playable` | 56.9 | 1438 | – | no-audio |  |
| wizmancala-v1.1.2-bin | `playable` | 57.2 | 1447 | – | no-audio |  |
| wizpong | `playable` | 54.9 | 1396 | – | no-audio |  |
| wizznic-0.9.9-wiz | `playable` | 54.1 | 1408 | ✓ |  |  |
| wnw_demo | `playable` | 57.2 | 1472 | ✓ |  |  |
| Worship Vector | `playable` | 61.0 | 1535 | ✓ |  |  |
| WWII | `playable` | 53.2 | 1396 | ✓ |  |  |
| xpiredwiz.eng.101 | `playable` | 56.6 | 1435 | ✓ |  |  |
| xRick | `playable` | 57.1 | 1451 | ✓ |  |  |
| Zero Wing | `playable` | 61.2 | 1540 | ✓ |  |  |
| znumbers | `playable` | 57.6 | 1444 | ✓ |  |  |
| Zoltan | `playable` | 54.6 | 1410 | ✓ |  |  |

### Caanoo (194 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
| Abbaye_caanoo | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
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
| Abbaye_caanoo_v3 | `black` | 12.1 | 25 | ✓ | black-screen |  |
| arcadevol1 | `black` | 6.1 | 6 | ✓ | black-screen |  |
| BubbleTrain | `black` | 1.1 | 2 | ✓ | black-screen |  |
| kenlab-caanoo | `black` | 56.3 | 1430 | – | black-screen |  |
| laserchess_c | `black` | 14.6 | 30 | – | black-screen |  |
| xcom1-v1.0.2-caanoo | `black` | 41.5 | 42 | ✓ | black-screen |  |
| xcom2-v1.0.1-caanoo | `black` | 55.0 | 1400 | ✓ | black-screen |  |
| aimcaanoo | `ingame` | 48.8 | 1249 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 160px; left and right halves are near- |
| apocalypso Caanoo | `ingame` | 17.4 | 461 | ✓ | low-fps |  |
| BubblePop (Caanoo) | `ingame` | 16.2 | 416 | ✓ | low-fps |  |
| caanoo-biniax2-v1.30-bin | `ingame` | 15.7 | 396 | ✓ | low-fps |  |
| can-zomb_3 | `ingame` | 17.1 | 498 | ✓ | low-fps |  |
| chexquest-caanoo | `ingame` | 17.3 | 442 | ✓ | low-fps |  |
| Coral Sea (Caanoo - Bennu) | `ingame` | 19.0 | 494 | ✓ | low-fps |  |
| Deadly Eye (Caanoo) | `ingame` | 19.4 | 499 | ✓ | low-fps |  |
| DefendorX_C | `ingame` | 15.8 | 413 | ✓ | low-fps |  |
| deminor | `ingame` | 11.4 | 27 | – | low-fps |  |
| EEEEK! EEEEEK! HOOOOOOK!!! | `ingame` | 23.4 | 640 | ✓ | garbled-visuals | renders at 640x480 instead of 320x240 |
| EpicFreeFall | `ingame` | 17.3 | 450 | ✓ | low-fps |  |
| EpicFreeFall Caanoo | `ingame` | 17.8 | 457 | ✓ | low-fps |  |
| Geca Blaster 2 (Caanoo) | `ingame` | 12.9 | 332 | ✓ | low-fps |  |
| gnuRobbo | `ingame` | 19.7 | 516 | ✓ | low-fps |  |
| gr-v1001-caanoo | `ingame` | 9.8 | 255 | ✓ | low-fps |  |
| Hamster's Escape 3D (Caanoo) | `ingame` | 19.7 | 500 | ✓ | low-fps |  |
| HamstersEscape (Caanoo) | `ingame` | 18.5 | 490 | ✓ | low-fps |  |
| Hardcore Fight (Caanoo) | `ingame` | 13.3 | 337 | ✓ | low-fps |  |
| HeroTheRealm_DEMOv2 | `ingame` | 19.7 | 507 | ✓ | low-fps |  |
| jump_n_blob_caanoo | `ingame` | 3.0 | 81 | ✓ | low-fps |  |
| Liar | `ingame` | 13.6 | 340 | ✓ | low-fps |  |
| llcpcls-caanoo | `ingame` | 18.3 | 462 | ✓ | low-fps |  |
| MasteriesRunners (Caanoo) | `ingame` | 8.5 | 219 | ✓ | low-fps |  |
| meritous | `ingame` | 12.5 | 316 | ✓ | low-fps |  |
| Metal Slug Zombies | `ingame` | 29.5 | 800 | ✓ | garbled-visuals | renders at 640x480 instead of 320x240 |
| mtknights | `ingame` | 32.9 | 828 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 156px; top and bottom halves are near- |
| nlove_0.6.2_(beta)_caanoo | `ingame` | 11.3 | 28 | – | low-fps |  |
| OperationFenix (Caanoo) | `ingame` | 9.5 | 259 | ✓ | low-fps |  |
| PantaVsDragon (Caanoo) | `ingame` | 15.4 | 407 | ✓ | low-fps |  |
| Pharaoh | `ingame` | 12.2 | 25 | ✓ | low-fps |  |
| Protect&rescue | `ingame` | 12.9 | 351 | ✓ | low-fps |  |
| purito_cycling_1.5_Caanoo | `ingame` | 3.0 | 78 | ✓ | low-fps |  |
| pushover-v0.2-bin | `ingame` | 18.3 | 466 | ✓ | low-fps |  |
| sbtime_caanoo | `ingame` | 5.6 | 153 | ✓ | low-fps |  |
| Skull (Caanoo) | `ingame` | 14.4 | 371 | ✓ | garbled-visuals | renders at 320x200 instead of 320x240 |
| smallball | `ingame` | 17.4 | 473 | ✓ | low-fps |  |
| smallball-Caanoo | `ingame` | 17.5 | 471 | ✓ | low-fps |  |
| SnailRace_C | `ingame` | 18.1 | 470 | – | low-fps |  |
| SORRv5_Caanoo | `ingame` | 0.1 | 2 | ✓ | low-fps |  |
| the solitarie | `ingame` | 14.5 | 384 | ✓ | low-fps |  |
| truxton2 | `ingame` | 81.8 | 2057 | ✓ | flat-fill |  |
| Txishos (Caanoo) | `ingame` | 15.3 | 393 | ✓ | low-fps |  |
| xpiredcan.eng.101 | `ingame` | 0.1 | 3 | ✓ | low-fps |  |
| zelda-roth-olb-3t_caanoo | `ingame` | 17.8 | 484 | ✓ | low-fps |  |
| zomg-Caanoo | `ingame` | 19.6 | 521 | ✓ | low-fps |  |
| Zverealm-Caanoo | `ingame` | 7.6 | 240 | ✓ | low-fps |  |
| 20110831 - Bomber Run Redux | `playable` | 36.1 | 951 | – | no-audio |  |
| aaa | `playable` | 56.0 | 1417 | ✓ |  |  |
| aaaa | `playable` | 51.2 | 1298 | ✓ |  |  |
| ADVENTURE | `playable` | 52.6 | 1418 | ✓ |  |  |
| Amoebax | `playable` | 53.7 | 1389 | ✓ |  |  |
| animatch | `playable` | 20.7 | 552 | ✓ |  |  |
| aquaVenture | `playable` | 51.6 | 1305 | ✓ |  |  |
| Arcadevol2 | `playable` | 56.3 | 1449 | ✓ |  |  |
| Arcadevol3 | `playable` | 55.8 | 1444 | – | no-audio |  |
| B'lox! | `playable` | 55.5 | 1419 | ✓ |  |  |
| Balloonacy | `playable` | 50.7 | 1283 | ✓ |  |  |
| balls12_caanoo_bin | `playable` | 57.4 | 1445 | – | no-audio |  |
| battlejewels-105-caanoo-beta | `playable` | 49.7 | 1256 | ✓ |  |  |
| Blackjack21v1.1 | `playable` | 56.1 | 1453 | – | no-audio |  |
| Blingo | `playable` | 56.0 | 1456 | ✓ |  |  |
| Blitz | `playable` | 51.9 | 1307 | ✓ |  |  |
| Blix2x | `playable` | 57.8 | 1456 | ✓ |  |  |
| caanoo-12swap-v1.0-bin | `playable` | 33.2 | 839 | ✓ |  |  |
| caanoo-chess-v1.1.0-bin | `playable` | 33.0 | 833 | – | no-audio |  |
| caanoo-gnurobbo-0.68 | `playable` | 39.4 | 1002 | ✓ |  |  |
| caanoo-go-v1.1.0-bin | `playable` | 41.4 | 1046 | – | no-audio |  |
| caanoo-mancala-v1.1.0-bin | `playable` | 39.6 | 999 | – | no-audio |  |
| cat_trap | `playable` | 50.6 | 1279 | ✓ |  |  |
| cavestory | `playable` | 54.4 | 1457 | ✓ |  |  |
| ccrg-caanoo | `playable` | 46.6 | 1228 | ✓ |  |  |
| cgenius-caanoo | `playable` | 25.5 | 664 | ✓ |  |  |
| cllwrth | `playable` | 23.3 | 588 | ✓ |  |  |
| cooldowncaanoo | `playable` | 710.5 | 18389 | ✓ |  |  |
| daff_s_adventure_2_caanoo | `playable` | 24.6 | 630 | ✓ |  |  |
| deadlyc | `playable` | 52.6 | 1325 | ✓ |  |  |
| DealorNoDeal | `playable` | 60.7 | 1554 | ✓ |  |  |
| demons | `playable` | 118.6 | 2978 | ✓ |  |  |
| Drench | `playable` | 52.9 | 1336 | ✓ |  |  |
| dynamate_c | `playable` | 28.4 | 741 | ✓ |  |  |
| echo_caanoo | `playable` | 22.4 | 627 | ✓ |  |  |
| Firewhip-Caanoo | `playable` | 20.3 | 545 | ✓ |  |  |
| Flappynerd_Caanoo | `playable` | 28.7 | 722 | ✓ |  |  |
| fleshchasmer | `playable` | 51.0 | 1308 | ✓ |  |  |
| freeheroes2_c | `playable` | 57.0 | 1443 | ✓ |  |  |
| fshark | `playable` | 78.7 | 1979 | ✓ |  |  |
| Fywod_caanoo | `playable` | 51.2 | 1299 | ✓ |  |  |
| Geek_em_up_CAANOO | `playable` | 48.2 | 1229 | ✓ |  |  |
| getstar | `playable` | 81.3 | 2044 | ✓ |  |  |
| gnp_104 | `playable` | 49.5 | 1407 | ✓ |  |  |
| gravityforcev2 | `playable` | 43.7 | 1104 | ✓ |  |  |
| Guru Logic | `playable` | 51.2 | 1291 | ✓ |  |  |
| hellfire | `playable` | 110.6 | 2779 | ✓ |  |  |
| Hero_The_Realm-DEMO | `playable` | 20.0 | 517 | ✓ | no-audio |  |
| hexahop_1.0 | `playable` | 50.5 | 1272 | – | no-audio |  |
| Humos-Caanoo | `playable` | 22.5 | 623 | ✓ |  |  |
| instead-1.6.1-caanoo | `playable` | 42.0 | 1087 | ✓ |  |  |
| JUMPNRUN | `playable` | 53.2 | 1548 | ✓ |  |  |
| jumpToTheMoon_c | `playable` | 28.6 | 721 | ✓ |  |  |
| ketm | `playable` | 36.4 | 1193 | – | no-audio |  |
| knight | `playable` | 57.8 | 1455 | ✓ |  |  |
| KOF (Ver. 5f) (Caanoo) | `playable` | 28.6 | 828 | ✓ |  |  |
| lmission_0.5 | `playable` | 54.8 | 1379 | ✓ |  |  |
| MISC | `playable` | 55.5 | 1557 | – | no-audio |  |
| Mission_faileD 1.2 [Caanoo] | `playable` | 29.6 | 822 | ✓ |  |  |
| MNV_Caanoo_Release1 | `playable` | 51.9 | 1309 | ✓ |  |  |
| monster | `playable` | 24.1 | 645 | ✓ |  |  |
| next_element | `playable` | 61.5 | 1551 | ✓ |  |  |
| noiz2sa_caanoo | `playable` | 35.9 | 913 | ✓ |  |  |
| nuclearchess | `playable` | 217.4 | 5479 | – | no-audio |  |
| outzone | `playable` | 81.6 | 2049 | ✓ |  |  |
| pang | `playable` | 55.2 | 1410 | ✓ |  |  |
| pengupop | `playable` | 28.5 | 150 | ✓ |  |  |
| powder | `playable` | 49.3 | 1252 | – | no-audio |  |
| powermanga-0.80 | `playable` | 43.1 | 1121 | ✓ |  |  |
| prboom-caanoo | `playable` | 56.6 | 1440 | – | no-audio |  |
| profanation_Caanoo | `playable` | 29.6 | 746 | ✓ |  |  |
| propis | `playable` | 47.5 | 1199 | ✓ |  |  |
| puzsion | `playable` | 27.2 | 763 | ✓ |  |  |
| PUZZLEBOARDS | `playable` | 108.6 | 2985 | ✓ |  |  |
| RACING | `playable` | 55.2 | 1461 | ✓ |  |  |
| rg_ura_103 | `playable` | 52.1 | 1360 | ✓ |  |  |
| rhythmosplay_1.1.12 | `playable` | 49.3 | 1251 | ✓ |  |  |
| Sachunsung2 | `playable` | 55.8 | 1400 | ✓ |  |  |
| SantaMania | `playable` | 52.6 | 1324 | ✓ |  |  |
| sbt | `playable` | 29.2 | 799 | ✓ |  |  |
| Shanghai2 | `playable` | 57.3 | 1439 | ✓ |  |  |
| SHOOTERS | `playable` | 71.2 | 1972 | ✓ |  |  |
| SimOniZ | `playable` | 51.3 | 1309 | ✓ |  |  |
| Sitwell (Caanoo) | `playable` | 32.5 | 838 | ✓ |  |  |
| Slap | `playable` | 85.8 | 2154 | ✓ |  |  |
| smw_1.7 | `playable` | 27.6 | 733 | ✓ |  |  |
| snowbros | `playable` | 110.9 | 2782 | ✓ |  |  |
| snowbros2 | `playable` | 110.5 | 2775 | ✓ |  |  |
| SOD(r181) | `playable` | 21.6 | 570 | ✓ |  |  |
| space52_caanoo | `playable` | 30.2 | 810 | ✓ |  |  |
| SPORTS | `playable` | 59.6 | 1556 | ✓ |  |  |
| sqrxz-v0996-caanoo | `playable` | 53.1 | 1353 | ✓ |  |  |
| sqrxz2-v0.80-caanoo | `playable` | 58.3 | 1483 | ✓ |  |  |
| stppc-caanoo-29-11-2010 | `playable` | 30.9 | 1144 | ✓ |  |  |
| STRATEGY | `playable` | 58.7 | 1558 | ✓ |  |  |
| tailtale4c | `playable` | 57.9 | 1458 | ✓ |  |  |
| Tigerhell | `playable` | 81.9 | 2057 | ✓ |  |  |
| Tile | `playable` | 51.8 | 1432 | ✓ |  |  |
| tlosaf_v12-caanoo | `playable` | 57.3 | 1444 | – | no-audio |  |
| tong-caanoo | `playable` | 53.6 | 1405 | ✓ |  |  |
| Trap75 | `playable` | 50.1 | 1260 | ✓ |  |  |
| Truxton | `playable` | 81.6 | 2050 | ✓ |  |  |
| twincobr | `playable` | 85.6 | 2154 | ✓ |  |  |
| twinhawk | `playable` | 85.7 | 2152 | ✓ |  |  |
| Vigo | `playable` | 55.0 | 1426 | – | no-audio |  |
| Wardner | `playable` | 118.6 | 2985 | ✓ |  |  |
| warehouse_panic_v1.1_caanoo | `playable` | 51.1 | 1322 | ✓ |  |  |
| WindandWater | `playable` | 56.8 | 1438 | ✓ |  |  |
| Wizznic 0.9.2- preview | `playable` | 28.7 | 771 | ✓ |  |  |
| wolf4sdl-caanoo | `playable` | 56.4 | 1444 | – | no-audio |  |
| wvector | `playable` | 42.4 | 1066 | ✓ |  |  |
| zerowing | `playable` | 118.6 | 2980 | ✓ |  |  |
| zombiesorbet_v1.0_caanoo | `playable` | 21.6 | 552 | ✓ |  |  |
