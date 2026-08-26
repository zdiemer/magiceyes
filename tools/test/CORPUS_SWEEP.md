# magiceyes compatibility sweep

Every GP2X / Wiz / Caanoo title on the corpus share, booted headlessly through the native engine (`bin/me_unicorn`) and scored from the shm framebuffer + the structured run report. Regenerate with `tools/test/compat_report.py`.


## Summary

| Platform | Titles | Playable | Ingame | Black | Incompatible | Crashed |
|---|--:|--:|--:|--:|--:|--:|
| GP2X | 631 | 503 | 8 | 33 | 87 | 0 |
| Wiz | 147 | 117 | 3 | 14 | 13 | 0 |
| Caanoo | 194 | 119 | 44 | 5 | 26 | 0 |
| **All** | **972** | **739** | **55** | **52** | **126** | **0** |

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
| **Renders at speed but no audio** (`no-audio`) | 111 | Caanoo, GP2X, Wiz | n/a |
| **Never rendered a frame (cause unknown)** (`no-frames`) | 62 | Caanoo, GP2X, Wiz | n/a |
| **Boots but renders only black** (`black-screen`) | 52 | Caanoo, GP2X, Wiz | n/a |
| **Not a 32-bit ARM ELF** (`not-arm-elf`) | 45 | GP2X | n/a |
| **Renders but below 20 fps** (`low-fps`) | 42 | Caanoo, GP2X, Wiz | n/a |
| **Renders, but the picture is wrong** (`garbled-visuals`) | 12 | Caanoo, GP2X, Wiz | n/a |
| **Game data files are missing from the dump** (`missing-game-data`) | 7 | Caanoo, GP2X | n/a |
| **Archive extraction failed** (`archive-failed`) | 5 | Caanoo, GP2X | n/a |
| **Unknown /dev node** (`unknown-device`) | 5 | Caanoo, GP2X, Wiz | `/dev/accel` ×1, `/dev/input/mouse/0` ×1, `/dev/cx25874` ×1, `/dev/graphics/fb0` ×1 |
| **Unimplemented syscall** (`unimplemented-syscall`) | 2 | GP2X | `9437188` ×1, `11711` ×1 |
| **Could not open a display** (`display-init-failed`) | 1 | GP2X | n/a |
| **Draws only a flat colour** (`flat-fill`) | 1 | GP2X | n/a |

## Renders, but the picture is wrong

These 12 titles pass the running checks (frames advancing, frame rate, audio) while the frame itself is visibly broken, so they are graded `ingame` rather than `playable`. The reasons come from measuring the captured frame: a consistent per-row offset means a stride/pitch mismatch, large-scale repetition means the screen holds more than one copy of itself, and noise far above what dithered artwork reaches means corrupt memory.

| Title | Platform | fps | What the frame looks like |
|---|---|--:|---|
| aimcaanoo | Caanoo | 47.2 | the screen holds a second copy of itself, offset by 160px; left and right halves are near-identical; top and bottom halves are near-identical |
| EEEEK! EEEEEK! HOOOOOOK!!! | Caanoo | 23.2 | renders at 640x480 instead of 320x240 |
| Metal Slug Zombies | Caanoo | 29.5 | renders at 640x480 instead of 320x240 |
| mtknights | Caanoo | 37.2 | the screen holds a second copy of itself, offset by 92px |
| Skull (Caanoo) | Caanoo | 14.6 | renders at 320x200 instead of 320x240 |
| BunnyTraps-v11 | GP2X | 60.1 | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like corrupt memory |
| Life.0.1 | GP2X | 60.2 | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like corrupt memory |
| MoveSweep2X | GP2X | 61.1 | the screen holds a second copy of itself, offset by 96px; left and right halves are near-identical |
| SpaceSnake | GP2X | 116.1 | pixel-to-pixel noise of 113, far above what dithered artwork reaches; the frame looks like corrupt memory |
| Winter_Jumper | GP2X | 60.0 | top and bottom halves are near-identical |
| blingo | Wiz | 56.7 | pixel-to-pixel noise of 100, far above what dithered artwork reaches; the frame looks like corrupt memory |
| Ruckman-Wiz | Wiz | 54.9 | pixel-to-pixel noise of 102, far above what dithered artwork reaches; the frame looks like corrupt memory |

## Scored as working, but only painting a flat colour

These 1 titles advanced frames, kept audio running, and held frame rate, so they land in `playable`/`renders`. Their framebuffer never held more than one or two colours, which means the tier overstates them. Worth treating as broken.

| Title | Platform | Status | fps |
|---|---|---|--:|
| dumbbell2x-01 | GP2X | `renders` | 61.8 |

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
| `unknown_mmio:0x910` | 111 |
| `unknown_mmio:0x1988` | 57 |
| `unknown_mmio:0x19c0` | 57 |
| `unknown_mmio:0x19c4` | 57 |
| `unknown_mmio:0x924` | 53 |
| `unknown_mmio:0x91c` | 51 |
| `unknown_mmio:0x3b46` | 46 |
| `unknown_mmio:0x3802` | 24 |
| `unknown_mmio:0x3804` | 24 |
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
| `unknown_mmio:0x307c` | 3 |
| `unknown_mmio:0x307e` | 3 |
| `unknown_mmio:0x3080` | 3 |

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
| OpenBOR_v2.1933 | `incompatible` | 60.8 | 1243 | ✓ | no-frames |  |
| OpenTTD | `incompatible` | 0.0 | 0 | – | no-frames |  |
| pacmame | `incompatible` | 8.9 | 6 | – | no-frames |  |
| Phantomas1.8X | `incompatible` | 0.0 | 0 | – | no-frames |  |
| Pipes2_0 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Pipes2_0/Pipes/Pipes.gpe' is not an ARM ELF and no runna |
| Pipes_v2.1 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Pipes_v2.1/Pipes/Pipes.gpe' is not an ARM ELF and no run |
| pykaraoke-0.6-gp2x | `incompatible` | 0.0 | 0 | – | no-frames |  |
| pySlide | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/pySlide/pySlide/pySlide.gpe' is not an ARM ELF and no ru |
| pyTetris | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/pyTetris/pyTetris/pyTetris.gpe' is not an ARM ELF and no |
| Quake Mods 5 | `incompatible` | 0.0 | 0 | – | not-arm-elf | magiceyes: '/mnt/s/GP2X/Quake Mods 5/czg07.gpe' is not an ARM ELF and no runnabl |
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
| 2xHexen2 v0.05 PB2 | `black` | 38.2 | 31 | – | black-screen |  |
| 2xWargus_PB1.3 | `black` | 1.0 | 17 | ✓ | black-screen |  |
| 2xZdoom_PB1.2 | `black` | 38.2 | 58 | – | black-screen |  |
| A1GP2XV1_1 | `black` | 20.0 | 13 | – | black-screen |  |
| albion-v1.0.1-gp2x | `black` | 23.6 | 28 | ✓ | black-screen |  |
| AlienBlaster_1.02 | `black` | 12.7 | 16 | ✓ | black-screen |  |
| BubbleTrain_GP2X-2006_Entry | `black` | 1.3 | 3 | ✓ | black-screen |  |
| CaptainCrusader_GP2XDemo | `black` | 6.0 | 3 | – | black-screen |  |
| CaptainCrusader_GP2XFull | `black` | 5.8 | 3 | – | black-screen |  |
| d2x-gp2x-0.02 | `black` | 8.0 | 10 | ✓ | black-screen |  |
| duke2x004 | `black` | 17.5 | 9 | – | black-screen |  |
| egoboo-cramfs | `black` | 27.2 | 73 | ✓ | black-screen |  |
| FFDoom | `black` | 2.6 | 2 | – | black-screen |  |
| FleshChasmer132c_patch | `black` | 9.2 | 5 | ✓ | black-screen |  |
| FleshChasmer_Dpad | `black` | 5.6 | 3 | ✓ | black-screen |  |
| gp2xDoukutsu-1.04 | `black` | 16.5 | 16 | ✓ | black-screen |  |
| gp2xJenkasNightmare | `black` | 18.0 | 16 | ✓ | black-screen |  |
| GPgeneral | `black` | 4.0 | 2 | – | black-screen |  |
| liquidwar2x02 | `black` | 4.5 | 3 | – | black-screen |  |
| openjazz-gp2x | `black` | 15.8 | 15 | ✓ | black-screen |  |
| PrBoom PWAD pack | `black` | 2.0 | 5 | – | black-screen |  |
| raw2xv0.3.1 | `black` | 11.4 | 6 | – | black-screen |  |
| ShadowWarrior2X | `black` | 11.1 | 6 | – | black-screen |  |
| step2x02 | `black` | 60.2 | 1553 | ✓ | black-screen |  |
| supertux-0.1.3-gp2x-v4 | `black` | 51.7 | 1444 | ✓ | black-screen |  |
| uhexen | `black` | 7.5 | 4 | – | black-screen |  |
| ultratumba_exp-20100925.gp2x | `black` | 10.8 | 7 | ✓ | black-screen |  |
| warcraft-beta3-gp2x | `black` | 27.7 | 27 | ✓ | black-screen |  |
| xbak-0.1.3 | `black` | 30.9 | 16 | – | black-screen |  |
| xcom1-v1.0.2-gp2x | `black` | 41.0 | 1519 | ✓ | black-screen |  |
| xcom2-v1.0.1-gp2x | `black` | 59.2 | 1524 | ✓ | black-screen |  |
| xump2x_beta2 | `black` | 27.8 | 14 | ✓ | black-screen |  |
| zcgp2x_211B18_0.4alpha | `black` | 17.4 | 21 | – | black-screen |  |
| BunnyTraps-v11 | `ingame` | 60.1 | 1514 | ✓ | garbled-visuals | pixel-to-pixel noise of 173, far above what dithered artwork reaches; the frame looks like |
| dumbbell2x-01 | `ingame` | 61.8 | 934 | – | flat-fill |  |
| Life.0.1 | `ingame` | 60.2 | 1514 | – | garbled-visuals | pixel-to-pixel noise of 159, far above what dithered artwork reaches; the frame looks like |
| MoveSweep2X | `ingame` | 61.1 | 1535 | – | garbled-visuals | the screen holds a second copy of itself, offset by 96px; left and right halves are near-i |
| pintor2x | `ingame` | 18.8 | 473 | ✓ | low-fps |  |
| SpaceSnake | `ingame` | 116.1 | 2930 | ✓ | garbled-visuals | pixel-to-pixel noise of 113, far above what dithered artwork reaches; the frame looks like |
| TUcS.app(V0.7.0 - GP2X) | `ingame` | 17.6 | 450 | ✓ | low-fps |  |
| Winter_Jumper | `ingame` | 60.0 | 1518 | – | garbled-visuals | top and bottom halves are near-identical |
| 1945_GP2X_0.2b | `playable` | 59.8 | 1518 | ✓ |  |  |
| 2xpong_gp2x | `playable` | 59.6 | 1500 | ✓ |  |  |
| 2xtron-v01 | `playable` | 59.7 | 1507 | ✓ |  |  |
| 2xZdoom_selector | `playable` | 108.1 | 2741 | ✓ |  |  |
| 4WE_GP2x | `playable` | 115.7 | 2917 | ✓ |  |  |
| 9 Lives | `playable` | 77.0 | 1944 | ✓ |  |  |
| _-The Reversed Preacher 3-_Hack bIld_ | `playable` | 60.0 | 1524 | ✓ |  |  |
| _-the reversed preacher II-_ | `playable` | 56.6 | 2096 | ✓ |  |  |
| a_sn-pong | `playable` | 40.8 | 1511 | – | no-audio |  |
| abe | `playable` | 61.3 | 1545 | ✓ |  |  |
| abuse_1.0 | `playable` | 56.5 | 1553 | ✓ |  |  |
| AbusimbelProfanationDeluxe | `playable` | 59.9 | 1515 | ✓ |  |  |
| AdamantArmorAffection2x | `playable` | 58.9 | 1489 | ✓ |  |  |
| ADIC2X | `playable` | 113.8 | 2858 | ✓ |  |  |
| AfterBurner-GP2X | `playable` | 38.4 | 975 | ✓ |  |  |
| Airplyr | `playable` | 60.0 | 1514 | ✓ |  |  |
| airstrike-1.1 | `playable` | 59.4 | 1513 | ✓ |  |  |
| Akd_BB | `playable` | 60.0 | 1513 | ✓ |  |  |
| alex | `playable` | 59.8 | 1509 | ✓ |  |  |
| Alex's Falldown | `playable` | 59.8 | 1513 | ✓ |  |  |
| alex4_gp2x | `playable` | 59.7 | 1506 | ✓ |  |  |
| altitude | `playable` | 57.2 | 1511 | ✓ |  |  |
| AMazing-3D | `playable` | 64.0 | 1614 | – | no-audio |  |
| amoebax-0.2.1-gp2x | `playable` | 57.4 | 1458 | ✓ |  |  |
| angband2x-v2 | `playable` | 60.9 | 1557 | – | no-audio |  |
| armorcity-0_30b | `playable` | 58.8 | 1513 | ✓ |  |  |
| ASCIIPong2xV0.4 | `playable` | 66.6 | 1676 | ✓ |  |  |
| Asteroids | `playable` | 59.1 | 1512 | ✓ |  |  |
| astrochaos | `playable` | 59.6 | 1512 | ✓ |  |  |
| atris-1.0.7 | `playable` | 62.6 | 1593 | ✓ |  |  |
| B'lox! | `playable` | 113.4 | 2906 | ✓ |  |  |
| bang_gp | `playable` | 59.4 | 1514 | ✓ |  |  |
| BareFistFighter | `playable` | 59.4 | 1495 | ✓ |  |  |
| barrage | `playable` | 59.9 | 1513 | ✓ |  |  |
| Batiscafo (versin EXP) | `playable` | 59.7 | 1504 | ✓ |  |  |
| battlejewels-gp2x-062-100 | `playable` | 115.5 | 2914 | ✓ |  |  |
| BearOids | `playable` | 60.1 | 1512 | ✓ |  |  |
| beat2x-0.5-bin | `playable` | 61.3 | 1556 | ✓ |  |  |
| Beatbox_1.2 | `playable` | 58.3 | 1555 | ✓ |  |  |
| BeetleRun | `playable` | 62.0 | 1561 | ✓ |  |  |
| biniax-gp2x_v1.2 | `playable` | 61.0 | 1536 | ✓ |  |  |
| Biniax2_gp2x | `playable` | 59.8 | 1511 | ✓ |  |  |
| Biohazard2 | `playable` | 60.1 | 1513 | ✓ |  |  |
| BioShoot GP2X | `playable` | 58.7 | 1510 | ✓ |  |  |
| Birdshoot | `playable` | 60.4 | 1515 | – | no-audio |  |
| BisfoG | `playable` | 114.5 | 2879 | ✓ |  |  |
| blastriot1.21 | `playable` | 58.6 | 1482 | ✓ |  |  |
| blazar_v1-30_gp2x | `playable` | 60.1 | 1515 | ✓ |  |  |
| blingo 1.2 | `playable` | 57.7 | 1514 | ✓ |  |  |
| blipsgp2x | `playable` | 57.1 | 1518 | ✓ |  |  |
| Blix2x | `playable` | 60.1 | 1511 | ✓ |  |  |
| blobbyvolley | `playable` | 59.3 | 1505 | ✓ |  |  |
| blobwars_2x | `playable` | 62.1 | 1586 | ✓ |  |  |
| block | `playable` | 60.0 | 1514 | ✓ |  |  |
| blockdudegp2x | `playable` | 58.3 | 1517 | ✓ |  |  |
| Blocked | `playable` | 113.9 | 2906 | ✓ |  |  |
| blockoid | `playable` | 59.0 | 1517 | ✓ |  |  |
| Blockrage2x | `playable` | 61.4 | 1552 | ✓ |  |  |
| blox | `playable` | 44.2 | 1111 | ✓ |  |  |
| Bloxz_DEMO | `playable` | 60.0 | 1518 | ✓ |  |  |
| bluecube2x | `playable` | 60.1 | 1514 | ✓ |  |  |
| bobtron-gp2x | `playable` | 60.0 | 1514 | ✓ |  |  |
| Bombs Panic | `playable` | 115.6 | 3000 | ✓ |  |  |
| Boomshine2x_1.12_gp2x | `playable` | 58.1 | 1513 | ✓ |  |  |
| Boulders-0 | `playable` | 59.2 | 1509 | ✓ |  |  |
| brassmunkey_gp2x_1.0 | `playable` | 60.6 | 1537 | ✓ |  |  |
| BubbleX | `playable` | 72.8 | 1831 | ✓ |  |  |
| BubTrain_GP2X-2006_Entry_No-Sound | `playable` | 57.4 | 1556 | ✓ |  |  |
| bugafactorx-v03-beta | `playable` | 59.3 | 1513 | – | no-audio |  |
| BugWarsSE_v1.0 | `playable` | 60.1 | 1556 | ✓ |  |  |
| bumprace-0.2 | `playable` | 58.1 | 1511 | ✓ |  |  |
| BurokkuDemo1 | `playable` | 59.9 | 1523 | ✓ |  |  |
| buscaminas | `playable` | 59.4 | 1513 | – | no-audio |  |
| buttongame | `playable` | 61.0 | 1534 | ✓ |  |  |
| BuzzysBadDay-1.0 | `playable` | 59.9 | 1514 | ✓ |  |  |
| cackb2 | `playable` | 45.8 | 196 | ✓ |  |  |
| CamelotWarriors-GP2x_v1.0 | `playable` | 59.1 | 1510 | ✓ |  |  |
| cardm | `playable` | 59.6 | 1512 | – | no-audio |  |
| CascadeBeneath v1.0 for GP2X | `playable` | 60.4 | 1519 | ✓ |  |  |
| cat_trap | `playable` | 111.8 | 2836 | ✓ |  |  |
| cavecopter_gp2x | `playable` | 20.3 | 511 | – | no-audio |  |
| ccrg | `playable` | 59.5 | 1496 | ✓ |  |  |
| cdogs2x04 | `playable` | 86.4 | 2205 | ✓ |  |  |
| cgenius-gp2x | `playable` | 55.8 | 1459 | ✓ |  |  |
| chaos2x | `playable` | 60.0 | 1514 | ✓ |  |  |
| checkersgp2x | `playable` | 56.0 | 1560 | ✓ |  |  |
| chess2x05 | `playable` | 59.8 | 1561 | ✓ |  |  |
| chicken-puyopuyo | `playable` | 57.0 | 1444 | – | no-audio |  |
| Chopper | `playable` | 59.4 | 1514 | – | no-audio |  |
| ChopperAttackv1.0.17 | `playable` | 99.1 | 2822 | ✓ |  |  |
| Chroma | `playable` | 115.2 | 2910 | ✓ |  |  |
| chuckiev12 | `playable` | 59.7 | 1513 | ✓ |  |  |
| Clonk2X_1.0 | `playable` | 41.6 | 1049 | – | not-arm-elf | magiceyes: reload of '/bin/sh' failed |
| Codemaster | `playable` | 115.6 | 2909 | ✓ |  |  |
| Comando2gp2xEN | `playable` | 60.4 | 1518 | ✓ |  |  |
| ConnyCarrot | `playable` | 59.3 | 1504 | ✓ |  |  |
| coppergreen | `playable` | 58.0 | 1518 | ✓ |  |  |
| cosmo2x_01 | `playable` | 59.7 | 1516 | – | no-audio |  |
| CowSuckers-1.0 | `playable` | 59.9 | 1510 | ✓ |  |  |
| Crapong | `playable` | 59.5 | 1507 | ✓ |  |  |
| crazeeman | `playable` | 118.4 | 2992 | ✓ |  |  |
| crimsonV1 | `playable` | 58.8 | 1559 | ✓ |  |  |
| crocodingusgp2x | `playable` | 115.3 | 2898 | ✓ |  |  |
| CromoZome | `playable` | 56.7 | 1516 | ✓ |  |  |
| crossroads | `playable` | 60.0 | 1521 | ✓ |  |  |
| CUBES | `playable` | 60.1 | 1516 | ✓ |  |  |
| cyberhockeyV2_6 | `playable` | 60.1 | 1513 | ✓ |  |  |
| DABAKKA-0 | `playable` | 71.0 | 1786 | ✓ |  |  |
| Dance2x Alpha GPE | `playable` | 60.4 | 665 | ✓ |  |  |
| DangerMouse | `playable` | 119.2 | 3000 | ✓ |  |  |
| Dark_Light_SDL2X | `playable` | 61.4 | 1562 | ✓ |  |  |
| Dastardly dungeon 1.5 | `playable` | 60.5 | 1555 | ✓ |  |  |
| dd2x | `playable` | 118.8 | 2992 | ✓ |  |  |
| DealOrNoDeal-v12 | `playable` | 59.0 | 1516 | ✓ |  |  |
| DeathTrap1_1 | `playable` | 56.5 | 1510 | ✓ |  |  |
| debian_vs_pimientos_2x_0.1.2 | `playable` | 59.4 | 1517 | ✓ |  |  |
| defeatme-gp2x-1.0.1 | `playable` | 61.0 | 1538 | ✓ |  |  |
| diamant_1_01 | `playable` | 33.6 | 849 | ✓ |  |  |
| Digger | `playable` | 85.6 | 2295 | ✓ |  |  |
| dodge | `playable` | 58.8 | 1516 | ✓ |  |  |
| DontGetCrushed v1.0 for GP2X | `playable` | 60.5 | 1520 | ✓ |  |  |
| dopewars2x | `playable` | 60.0 | 1510 | – | no-audio |  |
| dosmugen | `playable` | 58.6 | 1519 | ✓ |  |  |
| Dr. Mates v1.0 | `playable` | 39.1 | 172 | ✓ |  |  |
| Drill2x_final | `playable` | 58.7 | 1492 | ✓ |  |  |
| drill2x_xtreme_v1.0.3 | `playable` | 59.3 | 1512 | ✓ |  |  |
| drod-gp2x-1_0 | `playable` | 53.6 | 1390 | – | no-audio |  |
| dstroyGP2X1402 | `playable` | 60.4 | 1554 | – | no-audio |  |
| DubaiRace038a | `playable` | 66.9 | 1697 | ✓ |  |  |
| dyc_gp2x | `playable` | 112.2 | 2848 | ✓ |  |  |
| dynamategp2x | `playable` | 56.7 | 1513 | ✓ |  |  |
| E-Fighters2x_FIRST_ALPHA_0_0_5_fixedSound | `playable` | 114.6 | 2914 | ✓ |  |  |
| EasterQuest | `playable` | 115.4 | 2915 | ✓ |  |  |
| eggstreme3_v1-00_gp2x | `playable` | 60.0 | 1512 | ✓ |  |  |
| egoboo2xFeb1207 | `playable` | 95.0 | 2959 | ✓ |  |  |
| Electronia | `playable` | 60.3 | 1517 | ✓ |  |  |
| enigma | `playable` | 48.7 | 1311 | ✓ |  |  |
| entombed2x | `playable` | 60.6 | 1553 | ✓ |  |  |
| EpicFreeFall_GP2X | `playable` | 59.1 | 1513 | ✓ |  |  |
| EpicRocks_GP2X | `playable` | 60.3 | 1556 | ✓ |  |  |
| escapa-v1 | `playable` | 60.3 | 1518 | ✓ |  |  |
| escoba_exp-20101016.gp2x | `playable` | 59.8 | 1515 | – | no-audio |  |
| exi_shoot_gp2x | `playable` | 59.5 | 1498 | ✓ |  |  |
| extraterrestres | `playable` | 92.8 | 2661 | ✓ |  |  |
| extraterrestres-0 | `playable` | 54.7 | 1524 | – | no-audio |  |
| exult_rc3 | `playable` | 40.4 | 1495 | ✓ |  |  |
| Factor-v1.0-final | `playable` | 60.5 | 1555 | ✓ |  |  |
| falldown_gp2x | `playable` | 91.5 | 2299 | ✓ |  |  |
| FCRLG | `playable` | 61.0 | 1537 | – | no-audio |  |
| fenix | `playable` | 58.5 | 1557 | ✓ |  |  |
| fenixGamePack | `playable` | 56.2 | 1514 | ✓ |  |  |
| fifteen_01 | `playable` | 59.8 | 1517 | – | no-audio |  |
| FindMii | `playable` | 118.8 | 3015 | ✓ |  |  |
| Firewhip | `playable` | 95.6 | 2671 | ✓ |  |  |
| Fishball-1.2 | `playable` | 58.6 | 1476 | ✓ |  |  |
| fissionfield2x | `playable` | 60.1 | 1516 | ✓ |  |  |
| Flappynerd_GP2X | `playable` | 30.3 | 767 | ✓ |  |  |
| Flaschenspiel | `playable` | 59.4 | 1510 | – | no-audio |  |
| FleshChasmer | `playable` | 58.3 | 1487 | ✓ |  |  |
| FleshChasmer Zero | `playable` | 58.9 | 1490 | ✓ |  |  |
| FlipIR_GP2X | `playable` | 115.6 | 2916 | ✓ |  |  |
| floaters | `playable` | 60.0 | 1519 | ✓ |  |  |
| flobopuyo0.20.1 | `playable` | 58.4 | 1486 | ✓ |  |  |
| flowflowmania-0_6-gp2x | `playable` | 39.6 | 1464 | – | no-audio |  |
| flurkies_v1-25_gp2x | `playable` | 60.2 | 1516 | ✓ |  |  |
| fm | `playable` | 75.0 | 1893 | ✓ |  |  |
| Football2X | `playable` | 115.6 | 2919 | ✓ |  |  |
| formula1gp2x | `playable` | 61.6 | 1557 | ✓ |  |  |
| Fragger2x | `playable` | 60.0 | 1510 | ✓ |  |  |
| freec2x | `playable` | 44.9 | 1137 | ✓ |  |  |
| freecell_1 | `playable` | 60.0 | 1505 | ✓ |  |  |
| freedroid2x06 | `playable` | 67.2 | 1733 | ✓ |  |  |
| freesci | `playable` | 60.2 | 1514 | – | no-audio |  |
| friq-beta-07 | `playable` | 60.0 | 1518 | ✓ |  |  |
| frozen2x-0.1 | `playable` | 66.5 | 1676 | ✓ |  |  |
| fruits2x | `playable` | 59.8 | 1511 | – | no-audio |  |
| fruits_gp2x | `playable` | 60.0 | 1514 | ✓ |  |  |
| FullBoard (test ver.) | `playable` | 61.4 | 1543 | ✓ |  |  |
| fvc | `playable` | 61.3 | 1543 | ✓ |  |  |
| FyWod_2x | `playable` | 59.0 | 1515 | ✓ |  |  |
| game bIld 2 | `playable` | 61.8 | 1557 | ✓ |  |  |
| game-watch-mario-bros | `playable` | 59.7 | 1513 | ✓ |  |  |
| gchess-v1.0.1-bin | `playable` | 59.7 | 1517 | – | no-audio |  |
| gchess-v1.1.0-bin | `playable` | 59.7 | 1514 | – | no-audio |  |
| Geek 'em up GP2X | `playable` | 59.9 | 1714 | ✓ |  |  |
| gemdrop2x_v02 | `playable` | 58.5 | 1565 | ✓ |  |  |
| GeneralPromise | `playable` | 118.4 | 2999 | ✓ |  |  |
| GF | `playable` | 61.3 | 1561 | ✓ |  |  |
| Ghostbusters_WIP | `playable` | 61.8 | 1561 | ✓ |  |  |
| ghostpix_v10_gp2x | `playable` | 60.6 | 1557 | ✓ |  |  |
| glouton | `playable` | 61.3 | 1541 | ✓ |  |  |
| gnp_104 | `playable` | 55.5 | 1528 | ✓ |  |  |
| gnugo2x | `playable` | 59.8 | 1518 | – | no-audio |  |
| gnurobbo_0.66_open2x | `playable` | 52.3 | 1361 | ✓ |  |  |
| godori | `playable` | 68.4 | 1720 | – | no-audio |  |
| GoitGP | `playable` | 58.3 | 1516 | ✓ |  |  |
| gorillaz | `playable` | 83.7 | 2105 | ✓ |  |  |
| gp2hanoi_0.8.1_gp2x | `playable` | 59.9 | 1511 | ✓ |  |  |
| gp2x-blobwars-0.1 | `playable` | 63.0 | 1597 | ✓ |  |  |
| gp2x-bubbletrain-0.1 | `playable` | 57.9 | 1551 | ✓ |  |  |
| gp2x-ceferino-0.1 | `playable` | 54.5 | 1514 | – | no-audio |  |
| gp2x-formido-0.1 | `playable` | 40.9 | 1514 | ✓ |  |  |
| gp2x-invaders-preview-version | `playable` | 59.9 | 1518 | ✓ |  |  |
| gp2x-netrok-0.1 | `playable` | 49.8 | 1520 | ✓ |  |  |
| gp2x-sand-0.3 | `playable` | 60.1 | 1513 | – | no-audio |  |
| gp2x-shienso-bin_061021 | `playable` | 61.7 | 1557 | ✓ |  |  |
| gp2x-smc-0.1 | `playable` | 56.3 | 1511 | ✓ |  |  |
| gp2x-tenmado-0.1 | `playable` | 59.4 | 1512 | – | no-audio |  |
| gp2x-tong-v1 | `playable` | 113.9 | 2881 | – | no-audio |  |
| gp2x_2xmas | `playable` | 59.8 | 1519 | ✓ |  |  |
| GP2X_BallGame_0.49 | `playable` | 66.5 | 1675 | ✓ |  |  |
| gp2x_drench | `playable` | 58.2 | 1480 | ✓ |  |  |
| GP2X_Nat2007 | `playable` | 46.4 | 1169 | ✓ |  |  |
| GP2X_TLI | `playable` | 29.4 | 743 | ✓ |  |  |
| gp2xbug | `playable` | 114.4 | 2925 | ✓ |  |  |
| gp2xgo-v1.1.0-bin | `playable` | 61.1 | 1555 | – | no-audio |  |
| gp2xjunkie | `playable` | 58.1 | 1505 | ✓ |  |  |
| gp2xmancala-v1.1.1-bin | `playable` | 59.9 | 1518 | – | no-audio |  |
| GP2XOfLife | `playable` | 60.0 | 1513 | – | no-audio |  |
| gp2xpang-v.1.1.1 | `playable` | 95.3 | 2494 | ✓ |  |  |
| gp2xrick 1.0 | `playable` | 59.1 | 1499 | ✓ |  |  |
| GpFrontier v0.1 | `playable` | 61.5 | 1564 | ✓ |  |  |
| gpfrontier v0.4 | `playable` | 58.2 | 1653 | ✓ |  |  |
| gpnoid2x | `playable` | 60.8 | 1555 | ✓ |  |  |
| GPrina-GP2x_v1.0 | `playable` | 59.0 | 1508 | ✓ |  |  |
| GPSquares_GP2X | `playable` | 60.0 | 1510 | – | no-audio |  |
| gr-v1001-gp2x | `playable` | 58.1 | 1555 | ✓ |  |  |
| green | `playable` | 58.9 | 1495 | ✓ |  |  |
| grow | `playable` | 59.6 | 1496 | – | no-audio |  |
| gxeskiv | `playable` | 57.6 | 1447 | – | no-audio |  |
| HamstersEscape (Gp2x F-100 F-200) | `playable` | 47.3 | 1223 | ✓ |  |  |
| hanagechu2x_gbax2007 | `playable` | 74.8 | 1899 | ✓ |  |  |
| hanagechu2xalpha | `playable` | 60.3 | 1518 | ✓ |  |  |
| Heretic2X_v0.5 | `playable` | 59.3 | 1492 | ✓ |  |  |
| heroes2x02 | `playable` | 46.6 | 1219 | ✓ |  |  |
| hex-a-hop | `playable` | 61.8 | 1554 | – | no-audio |  |
| hexbattle2x | `playable` | 59.2 | 1515 | ✓ |  |  |
| HumphreyGP2X | `playable` | 59.4 | 1517 | ✓ |  |  |
| Hyperion_GP2X_demo | `playable` | 60.5 | 1526 | ✓ |  |  |
| jump_n_blob_gp2x | `playable` | 72.9 | 1931 | ✓ |  |  |
| jumpnbumpgp2x | `playable` | 60.6 | 1558 | ✓ |  |  |
| Jurlx2 | `playable` | 61.6 | 1556 | ✓ |  |  |
| just4qix | `playable` | 59.9 | 1518 | ✓ |  |  |
| kampfimall-gp2x | `playable` | 59.7 | 1506 | – | no-audio |  |
| kampfimall-gp2x-music | `playable` | 61.8 | 1551 | ✓ |  |  |
| ketm_2x_gp2x | `playable` | 50.8 | 1518 | ✓ |  |  |
| KicknPLay_1.1 | `playable` | 60.2 | 1512 | ✓ |  |  |
| Klaur | `playable` | 114.2 | 2926 | ✓ |  |  |
| Knight Lore | `playable` | 60.2 | 1512 | ✓ |  |  |
| koules2x_02 | `playable` | 70.7 | 1809 | ✓ |  |  |
| kuklomenos_gp2x_201209 | `playable` | 59.2 | 1522 | ✓ |  |  |
| kurukuru2x | `playable` | 60.2 | 1523 | ✓ |  |  |
| la | `playable` | 59.4 | 1498 | ✓ |  |  |
| LABYRINTH | `playable` | 60.0 | 1515 | – | no-audio |  |
| ladykiller | `playable` | 59.1 | 1517 | ✓ |  |  |
| las-tres-luces-de-glaurung-remake | `playable` | 60.3 | 1542 | ✓ |  |  |
| lbreakoutgp2x | `playable` | 56.2 | 1525 | ✓ |  |  |
| levelEdit | `playable` | 60.1 | 1526 | – | no-audio |  |
| levelshmup | `playable` | 92.1 | 2349 | ✓ |  |  |
| Lexeme | `playable` | 114.4 | 2916 | ✓ |  |  |
| lights-out | `playable` | 59.6 | 1497 | – | no-audio |  |
| LinesXv3 | `playable` | 70.6 | 1770 | ✓ |  |  |
| logicx | `playable` | 67.9 | 1706 | ✓ |  |  |
| Logoball | `playable` | 114.8 | 2927 | ✓ |  |  |
| lumix-beta-01 | `playable` | 59.6 | 1504 | – | no-audio |  |
| mad-mix-game-20b-final | `playable` | 61.2 | 1548 | ✓ |  |  |
| madbomber | `playable` | 60.6 | 1550 | ✓ |  |  |
| malvado2x | `playable` | 59.5 | 1551 | ✓ |  |  |
| MAME-N22_51 | `playable` | 63.6 | 2355 | ✓ |  |  |
| mancala-v1.0.1 | `playable` | 60.1 | 1527 | – | no-audio |  |
| March of the mini tux | `playable` | 63.1 | 1597 | ✓ |  |  |
| Marte Necesita Vacas GP2X | `playable` | 58.0 | 1517 | ✓ |  |  |
| Masteries_Journey_to_the_Center_of_the_earth_GP2X | `playable` | 57.7 | 1553 | ✓ |  |  |
| masterpiece2x | `playable` | 60.6 | 1529 | – | no-audio |  |
| MazeThingie | `playable` | 60.3 | 1517 | ✓ |  |  |
| MazezaMGP2X | `playable` | 86.6 | 2243 | ✓ |  |  |
| memory | `playable` | 60.9 | 1567 | ✓ |  |  |
| MemoryGP2X-v11 | `playable` | 60.0 | 1516 | ✓ |  |  |
| meritous | `playable` | 59.8 | 1526 | ✓ |  |  |
| Merlin2x_beta_021 | `playable` | 59.4 | 1514 | ✓ |  |  |
| metaphysik | `playable` | 62.6 | 1589 | ✓ |  |  |
| methaneV1 | `playable` | 60.0 | 1521 | ✓ |  |  |
| minigolf | `playable` | 60.0 | 1517 | – | no-audio |  |
| minos-gp2x | `playable` | 58.1 | 1477 | ✓ |  |  |
| misterhachi | `playable` | 48.5 | 1475 | ✓ |  |  |
| mk13.gpe | `playable` | 69.7 | 1749 | ✓ |  |  |
| mkACE.gpe | `playable` | 69.5 | 1745 | ✓ |  |  |
| mkONE.gpe | `playable` | 69.4 | 1744 | ✓ |  |  |
| MM2X | `playable` | 59.5 | 1497 | ✓ |  |  |
| monacoGP | `playable` | 60.9 | 1561 | ✓ |  |  |
| monochromeworlds-gp2x-1.0.0 | `playable` | 59.7 | 1512 | ✓ |  |  |
| moonlander | `playable` | 58.7 | 1507 | ✓ |  |  |
| MouthTrap | `playable` | 115.7 | 2919 | ✓ |  |  |
| mueppv32 | `playable` | 115.2 | 2925 | ✓ |  |  |
| mush_gp2x | `playable` | 35.8 | 922 | ✓ |  |  |
| Mutant Tank Knights | `playable` | 58.1 | 432 | ✓ |  |  |
| MyriadUpdated | `playable` | 59.8 | 1511 | ✓ |  |  |
| mzx-2.84c | `playable` | 60.5 | 1526 | ✓ |  |  |
| mzx282-gp2x | `playable` | 60.6 | 1524 | ✓ |  |  |
| n-tris_v1.0 | `playable` | 61.8 | 1554 | ✓ |  |  |
| nanobounce-pacc-gp2x | `playable` | 59.4 | 1524 | ✓ |  |  |
| nazcarunners-0 | `playable` | 47.2 | 65 | ✓ |  |  |
| nazcasphere | `playable` | 46.1 | 62 | ✓ |  |  |
| ne_deluxe_gp2x | `playable` | 60.2 | 1521 | ✓ |  |  |
| ne_gp2x | `playable` | 58.5 | 1480 | ✓ |  |  |
| Nebulus_gp2x | `playable` | 60.3 | 1514 | – | no-audio |  |
| NecNec2x | `playable` | 59.9 | 1519 | ✓ |  |  |
| Net-Bubble-gp2x_1-21-06_bin | `playable` | 58.8 | 1517 | – | no-audio |  |
| newsuperpang | `playable` | 60.1 | 1524 | ✓ |  |  |
| Nifty | `playable` | 58.5 | 1511 | ✓ |  |  |
| noiz2saV3 | `playable` | 70.4 | 1795 | ✓ |  |  |
| Nom | `playable` | 59.7 | 1503 | ✓ |  |  |
| odonata_demo | `playable` | 59.0 | 1492 | ✓ |  |  |
| omok | `playable` | 62.2 | 1560 | ✓ |  |  |
| OpenBOR_v3.0_Build_2615_&_2637 | `playable` | 59.3 | 1518 | ✓ |  |  |
| openggs | `playable` | 60.2 | 1552 | ✓ |  |  |
| openglad2x | `playable` | 59.2 | 1547 | – | no-audio |  |
| opentyrian2x_0.3_complete | `playable` | 52.8 | 1538 | ✓ |  |  |
| opposite_lock | `playable` | 50.2 | 1513 | ✓ |  |  |
| OrbitalSniper2x_v1.1 | `playable` | 60.3 | 1515 | ✓ |  |  |
| othello_v1.0 | `playable` | 61.9 | 1555 | ✓ |  |  |
| oxov06 | `playable` | 61.3 | 1548 | – | no-audio |  |
| PAF | `playable` | 59.8 | 1509 | ✓ |  |  |
| PantaVsDragon (Gp2x F-100 F-200) | `playable` | 49.5 | 1275 | ✓ |  |  |
| para3 | `playable` | 61.7 | 1557 | ✓ |  |  |
| paraballgp2x | `playable` | 61.1 | 1550 | ✓ |  |  |
| Payback | `playable` | 74.3 | 2040 | ✓ |  |  |
| PaybackDemo | `playable` | 33.5 | 877 | ✓ |  |  |
| pc | `playable` | 60.5 | 1552 | ✓ |  |  |
| pdcv060b | `playable` | 61.8 | 1561 | ✓ |  |  |
| Pentominos | `playable` | 60.2 | 1512 | ✓ |  |  |
| PerfectFit | `playable` | 60.1 | 1513 | – | no-audio |  |
| Peuppy_10_GP2X | `playable` | 35.7 | 898 | ✓ |  |  |
| pez | `playable` | 61.4 | 1552 | – | no-audio |  |
| Phishy-0 | `playable` | 59.3 | 1514 | ✓ |  |  |
| physique | `playable` | 61.3 | 1548 | ✓ |  |  |
| Pika2x | `playable` | 59.8 | 1508 | ✓ |  |  |
| pixpang | `playable` | 51.5 | 1549 | ✓ |  |  |
| PocketSnes_SMRPG | `playable` | 120.9 | 3038 | – | no-audio |  |
| Poker2x | `playable` | 118.5 | 2987 | ✓ |  |  |
| Poker_Gp2Xv1.0 | `playable` | 114.6 | 2922 | ✓ |  |  |
| Pond2X | `playable` | 66.8 | 1693 | – | no-audio |  |
| Pong | `playable` | 59.9 | 1505 | – | no-audio |  |
| pong2player | `playable` | 61.6 | 1548 | – | no-audio |  |
| pong2v060x | `playable` | 61.7 | 1554 | – | no-audio |  |
| Pool Panic | `playable` | 59.2 | 1493 | ✓ |  |  |
| powder2x-112 | `playable` | 60.8 | 1555 | – | no-audio |  |
| powermanga-0.80 | `playable` | 55.3 | 1461 | ✓ |  |  |
| PowerSlide | `playable` | 60.6 | 1541 | ✓ |  |  |
| PPlane | `playable` | 48.2 | 1216 | ✓ |  |  |
| PPlane2.GP2X | `playable` | 45.7 | 1229 | ✓ |  |  |
| prboom-gp2x | `playable` | 61.3 | 1559 | – | no-audio |  |
| proj0-demo_01 | `playable` | 60.3 | 1539 | ✓ |  |  |
| protozoa v1.0 | `playable` | 60.8 | 1553 | ✓ |  |  |
| puckman_gp2x | `playable` | 116.5 | 2958 | ✓ |  |  |
| PulplifeWars | `playable` | 56.6 | 1510 | ✓ |  |  |
| puzzlelandgp2x | `playable` | 56.3 | 1557 | ✓ |  |  |
| qfg3-0 | `playable` | 59.5 | 1555 | ✓ |  |  |
| Quad | `playable` | 115.9 | 2941 | ✓ |  |  |
| quartz2_v1-50_gp2x | `playable` | 61.8 | 1557 | ✓ |  |  |
| Rabbit_vs_Flies_0.9 | `playable` | 59.8 | 1515 | ✓ |  |  |
| ramon atacks | `playable` | 61.8 | 1555 | ✓ |  |  |
| Release GP2X MST_RUNNERS | `playable` | 56.7 | 1510 | ✓ |  |  |
| retrovirus_1_1 | `playable` | 64.9 | 1647 | ✓ |  |  |
| RevoltOfTheBinaryCouriers GP2X | `playable` | 61.1 | 1534 | – | no-audio |  |
| reword_v0.5 | `playable` | 61.2 | 1560 | ✓ |  |  |
| rg_105 | `playable` | 58.2 | 1569 | ✓ |  |  |
| rg_ura_103 | `playable` | 57.9 | 1554 | ✓ |  |  |
| river | `playable` | 61.9 | 1553 | ✓ |  |  |
| robot-escape | `playable` | 87.3 | 2212 | ✓ |  |  |
| RockRain | `playable` | 60.1 | 1514 | ✓ |  |  |
| rockrain2_exp-20100925 | `playable` | 61.0 | 1547 | ✓ |  |  |
| rookiehero_EXP.gp2x.v20120220 | `playable` | 60.3 | 1544 | ✓ |  |  |
| RoundEmUp-alpha3 | `playable` | 61.8 | 1560 | ✓ |  |  |
| rRootage_v1.0 | `playable` | 108.8 | 2815 | ✓ |  |  |
| rubidogp2x | `playable` | 59.3 | 1556 | ✓ |  |  |
| rubik | `playable` | 121.8 | 3105 | – | no-audio |  |
| ruckman_v1.03 | `playable` | 58.9 | 1551 | ✓ |  |  |
| Runner_GP2X | `playable` | 60.9 | 1557 | ✓ |  |  |
| s-tris2_v1-64_gp2x | `playable` | 61.7 | 1549 | ✓ |  |  |
| Sachunsung2_1 | `playable` | 93.2 | 2337 | ✓ |  |  |
| sachunsungx | `playable` | 73.0 | 1832 | ✓ |  |  |
| santaMania | `playable` | 62.1 | 1564 | ✓ |  |  |
| ScorchedGPBeta2 | `playable` | 59.8 | 1515 | ✓ |  |  |
| scummvm-0.11.1-gp2x | `playable` | 59.1 | 1550 | ✓ |  |  |
| scummvm-1.2.0-gp2x | `playable` | 60.8 | 1562 | ✓ |  |  |
| scummvm-kor0.4.2cvs | `playable` | 61.2 | 1545 | – | no-audio |  |
| SdLame | `playable` | 59.9 | 1516 | ✓ |  |  |
| sdlmonkey_0.1 | `playable` | 61.7 | 1552 | – | no-audio |  |
| sdlscav_gp2x_0.2.0 | `playable` | 114.6 | 2892 | ✓ |  |  |
| Shangai v2 | `playable` | 90.5 | 2275 | ✓ |  |  |
| ShanghaiX | `playable` | 70.7 | 1777 | ✓ |  |  |
| SheepDash | `playable` | 58.3 | 1560 | ✓ |  |  |
| Shippy84 | `playable` | 57.1 | 1516 | ✓ |  |  |
| Simon2X | `playable` | 59.2 | 1501 | – | no-audio |  |
| SimOniZ | `playable` | 111.4 | 2850 | ✓ |  |  |
| siv050 | `playable` | 57.4 | 1556 | ✓ |  |  |
| sleuth slots 2x | `playable` | 114.3 | 2891 | ✓ |  |  |
| SmallBall_GP | `playable` | 58.7 | 1517 | ✓ |  |  |
| SmashGp2x02 | `playable` | 59.9 | 1519 | ✓ |  |  |
| snail runers | `playable` | 60.8 | 1553 | ✓ |  |  |
| snake2x-1.1 | `playable` | 61.1 | 1559 | – | no-audio |  |
| snowedin5_v1-00_gp2x | `playable` | 61.6 | 1552 | ✓ |  |  |
| SOD v1.1 | `playable` | 57.4 | 1514 | ✓ |  |  |
| sokobangp2x | `playable` | 51.9 | 1558 | ✓ |  |  |
| Solitaire2x-v1.4 | `playable` | 74.3 | 1920 | – | no-audio |  |
| sopwith_camel_rc3 | `playable` | 60.5 | 1523 | ✓ |  |  |
| sources_MEMORY2X | `playable` | 60.7 | 1555 | ✓ |  |  |
| sources_Yahtzee | `playable` | 60.5 | 1555 | ✓ |  |  |
| space squares | `playable` | 70.2 | 1788 | – | no-audio |  |
| space52_gp2x(oficial) | `playable` | 44.9 | 1211 | ✓ |  |  |
| space52_gp2x(open2x) | `playable` | 45.3 | 1220 | ✓ |  |  |
| space_varments_v1.0 | `playable` | 59.1 | 1555 | ✓ |  |  |
| SpaceRocks2X | `playable` | 113.2 | 2864 | – | no-audio |  |
| spacestorm | `playable` | 56.6 | 1425 | ✓ |  |  |
| spartak-chess_0.0.4_gp2x | `playable` | 60.0 | 1528 | – | no-audio |  |
| Sponge Blob Tennis | `playable` | 41.1 | 1521 | – | no-audio |  |
| spout | `playable` | 60.6 | 1525 | – | no-audio |  |
| sprint_race | `playable` | 59.2 | 1524 | ✓ |  |  |
| Sqcolony | `playable` | 61.2 | 1538 | – | no-audio |  |
| Sqdef 1.4 | `playable` | 59.5 | 1515 | ✓ |  |  |
| Squares-v051 | `playable` | 60.3 | 1525 | ✓ |  |  |
| Squaresliding | `playable` | 60.4 | 1515 | ✓ |  |  |
| StairwayToHeaven | `playable` | 60.2 | 1516 | ✓ |  |  |
| starfighter-gp2x-0.01 | `playable` | 63.4 | 1593 | ✓ |  |  |
| starsystem | `playable` | 60.3 | 1527 | ✓ |  |  |
| StarTrucker | `playable` | 59.4 | 1513 | ✓ |  |  |
| stppc2x-v1.0 | `playable` | 42.3 | 1564 | ✓ |  |  |
| stransball2 | `playable` | 66.6 | 1689 | ✓ |  |  |
| street2x | `playable` | 54.0 | 1557 | ✓ |  |  |
| subhunt | `playable` | 61.4 | 1549 | ✓ |  |  |
| sudoku-v1.0 | `playable` | 61.6 | 1553 | – | no-audio |  |
| sudoku2x-0.5 | `playable` | 63.1 | 1595 | – | no-audio |  |
| SuperChickenFallDemo | `playable` | 60.1 | 1520 | ✓ |  |  |
| SuperPaf_v1.0 | `playable` | 58.2 | 1515 | ✓ |  |  |
| superpang | `playable` | 60.7 | 1553 | ✓ |  |  |
| SuperPixelJumper v1.1 for GP2X | `playable` | 59.4 | 1498 | ✓ |  |  |
| SuperSonicSpeed | `playable` | 60.5 | 1555 | ✓ |  |  |
| survival | `playable` | 60.0 | 1529 | ✓ |  |  |
| symbolica-0.8 | `playable` | 59.7 | 1513 | ✓ |  |  |
| tail-tale | `playable` | 60.6 | 1529 | ✓ |  |  |
| Tangle | `playable` | 59.8 | 1506 | – | no-audio |  |
| tecnoballz-0.91-gp2x | `playable` | 54.7 | 1427 | ✓ |  |  |
| tesla-Siren | `playable` | 60.7 | 1535 | ✓ |  |  |
| Tetrablocks.0.4.GP2X | `playable` | 61.1 | 1541 | ✓ |  |  |
| tetwins | `playable` | 61.5 | 1542 | ✓ |  |  |
| the reversed preacher II | `playable` | 61.3 | 1542 | ✓ |  |  |
| ThreeTs_Game | `playable` | 60.0 | 1512 | ✓ |  |  |
| Thruster_GP2X | `playable` | 59.4 | 1510 | ✓ |  |  |
| tikka_dungeons_demo_1 | `playable` | 60.0 | 1511 | ✓ |  |  |
| tilematch-0.6 | `playable` | 91.2 | 2301 | ✓ |  |  |
| tileworld2x | `playable` | 53.6 | 1476 | ✓ |  |  |
| tilt | `playable` | 61.4 | 1552 | ✓ |  |  |
| TimeFrack2D for GP2X | `playable` | 60.9 | 1532 | – | no-audio |  |
| TouchGames | `playable` | 118.2 | 3000 | ✓ |  |  |
| tower | `playable` | 105.9 | 2680 | – | no-audio |  |
| towertopplergp2x | `playable` | 58.8 | 1552 | ✓ |  |  |
| TRAINS | `playable` | 58.7 | 1498 | ✓ |  |  |
| Trap75 | `playable` | 61.6 | 1552 | ✓ |  |  |
| treev060 | `playable` | 59.6 | 1527 | ✓ |  |  |
| ttxbeta170706b | `playable` | 56.6 | 1524 | – | no-audio |  |
| Txishos (Gp2x F-200) | `playable` | 48.6 | 1262 | ✓ |  |  |
| Unicolor | `playable` | 59.3 | 1517 | ✓ |  |  |
| uqm2x_release_1.1 | `playable` | 66.1 | 1696 | ✓ |  |  |
| UQMgp2x-0.5.0_with_content | `playable` | 64.5 | 1654 | ✓ |  |  |
| vectoroids-2x | `playable` | 60.4 | 1523 | ✓ |  |  |
| VekDemo2 | `playable` | 114.9 | 2907 | ✓ |  |  |
| Vektar | `playable` | 115.8 | 2915 | ✓ |  |  |
| vektar-free | `playable` | 33.2 | 842 | ✓ |  |  |
| vektarpack_v1 | `playable` | 40.2 | 1023 | ✓ |  |  |
| Ventifact | `playable` | 58.9 | 1514 | ✓ |  |  |
| vexed-gp2x-10 | `playable` | 59.5 | 1506 | ✓ |  |  |
| vexedb1 | `playable` | 60.5 | 1526 | – | no-audio |  |
| Volleyball | `playable` | 60.0 | 1514 | ✓ |  |  |
| vorton-b4 | `playable` | 58.5 | 1526 | ✓ |  |  |
| vwars | `playable` | 58.9 | 1520 | ✓ |  |  |
| waffle2x | `playable` | 59.9 | 1504 | – | no-audio |  |
| war_and_warriorgp2x | `playable` | 60.8 | 1528 | ✓ |  |  |
| warcraft | `playable` | 59.3 | 1557 | ✓ |  |  |
| warehouse_panic_v1.1_gp2x | `playable` | 46.3 | 1522 | ✓ |  |  |
| waternetgp2x | `playable` | 54.5 | 1532 | ✓ |  |  |
| wehaveballs | `playable` | 59.8 | 1503 | ✓ |  |  |
| whacky | `playable` | 60.1 | 1515 | ✓ |  |  |
| WindAndWater_teaser_110 | `playable` | 59.4 | 1502 | ✓ |  |  |
| wire3d | `playable` | 55.2 | 1495 | – | no-audio |  |
| Wiztern Demo | `playable` | 101.3 | 2731 | ✓ |  |  |
| Wizznic_2x_07alpha2 | `playable` | 57.6 | 1522 | ✓ |  |  |
| wizznic_gp2x-0.9.9 | `playable` | 58.5 | 1555 | ✓ |  |  |
| wnw | `playable` | 60.8 | 1537 | ✓ |  |  |
| wolfdx | `playable` | 60.5 | 1522 | ✓ |  |  |
| xenitris_demo | `playable` | 60.4 | 1522 | ✓ |  |  |
| xigon-X-gp2x-V1 | `playable` | 60.6 | 1528 | ✓ |  |  |
| Xpired2x 1.0 beta | `playable` | 59.6 | 1513 | ✓ |  |  |
| xRick | `playable` | 59.7 | 1524 | ✓ |  |  |
| yahtzee-v21 | `playable` | 62.1 | 1567 | ✓ |  |  |
| Zelda_roth_US_gp2x | `playable` | 59.0 | 1567 | ✓ |  |  |
| znax | `playable` | 56.9 | 1526 | ✓ |  |  |
| Znumbers | `playable` | 98.2 | 2464 | ✓ |  |  |
| Zoids Quest2X-0.0.1-2 | `playable` | 58.9 | 1516 | ✓ |  |  |
| zoltan 2x | `playable` | 58.6 | 1522 | ✓ |  |  |
| zombiesorbet_v1.0_gp2x | `playable` | 60.9 | 1563 | ✓ |  |  |
| zooov11 | `playable` | 31.6 | 793 | ✓ |  |  |
| ztunnel-0 | `playable` | 59.0 | 1498 | ✓ |  |  |

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
| albion-v1.0.1-wiz | `black` | 24.6 | 45 | ✓ | black-screen |  |
| chroma 1.01 v1 | `black` | 0.3 | 2 | – | black-screen |  |
| eduke32 | `black` | 12.4 | 17 | – | black-screen |  |
| nazcadreams | `black` | 25.1 | 55 | ✓ | black-screen |  |
| nazcarunners | `black` | 31.2 | 81 | ✓ | black-screen |  |
| Nazcasphere | `black` | 32.3 | 80 | ✓ | black-screen |  |
| openjazz-wiz | `black` | 17.0 | 23 | ✓ | black-screen |  |
| opentyrian | `black` | 14.5 | 9 | – | black-screen |  |
| paraballwiz | `black` | 3.5 | 6 | – | black-screen |  |
| supertux-wiz | `black` | 52.4 | 1459 | ✓ | black-screen |  |
| warcraft-beta3-wiz | `black` | 24.6 | 47 | ✓ | black-screen |  |
| WizSticks | `black` | 14.0 | 57 | ✓ | black-screen |  |
| xcom1-v1.0.2-wiz | `black` | 49.4 | 129 | ✓ | black-screen |  |
| xcom2-v1.0.1-wiz | `black` | 113.6 | 2912 | ✓ | black-screen |  |
| blingo | `ingame` | 56.7 | 1468 | ✓ | garbled-visuals | pixel-to-pixel noise of 100, far above what dithered artwork reaches; the frame looks like |
| PPlane | `ingame` | 15.9 | 404 | ✓ | low-fps |  |
| Ruckman-Wiz | `ingame` | 54.9 | 1464 | ✓ | garbled-visuals | pixel-to-pixel noise of 102, far above what dithered artwork reaches; the frame looks like |
| [DEMO] Wiztern | `playable` | 56.1 | 1460 | ✓ |  |  |
| abuse-wiz | `playable` | 56.3 | 1496 | ✓ |  |  |
| AdamantArmorAffectionWiz | `playable` | 59.3 | 1502 | ✓ |  |  |
| airstrike-wiz-1.01 | `playable` | 58.8 | 1492 | ✓ |  |  |
| alexsfalldown | `playable` | 60.3 | 1517 | ✓ |  |  |
| altitude | `playable` | 55.5 | 1453 | ✓ |  |  |
| Animatch Wiz | `playable` | 55.3 | 1504 | ✓ |  |  |
| Art Shot Wiz | `playable` | 53.1 | 1370 | ✓ |  |  |
| Asteroids | `playable` | 57.8 | 1467 | ✓ |  |  |
| Balloonacy_wiz_wip | `playable` | 111.8 | 2840 | ✓ |  |  |
| battlejewels-wiz-public001demo | `playable` | 58.9 | 1489 | ✓ |  |  |
| beat2x-wiz | `playable` | 61.0 | 1537 | ✓ |  |  |
| Biological Defend | `playable` | 56.6 | 1451 | ✓ |  |  |
| BitDEFENSE | `playable` | 45.4 | 1247 | – | no-audio |  |
| BlastRiot122Wiz | `playable` | 60.2 | 1518 | ✓ |  |  |
| Blix2x | `playable` | 60.3 | 1515 | ✓ |  |  |
| Boomshine2x_1.12_wiz | `playable` | 56.8 | 1461 | ✓ |  |  |
| BubbleTrainWiz_5-20-09 | `playable` | 55.3 | 1472 | ✓ |  |  |
| BugwarsSE | `playable` | 57.6 | 1466 | ✓ |  |  |
| Camelot Warriors | `playable` | 54.8 | 1461 | ✓ |  |  |
| CartoonWiz | `playable` | 114.3 | 2885 | ✓ |  |  |
| CDogs-wiz | `playable` | 56.1 | 1425 | ✓ |  |  |
| cgenius-wiz | `playable` | 56.2 | 1465 | ✓ |  |  |
| ColonyConflict_V1.1_B6 | `playable` | 102.7 | 2765 | ✓ |  |  |
| Dastardly_Dungeon | `playable` | 57.2 | 1470 | ✓ |  |  |
| Dd2x | `playable` | 60.3 | 1516 | ✓ |  |  |
| deicide3_eng | `playable` | 61.1 | 1536 | ✓ |  |  |
| Demons World | `playable` | 59.7 | 1500 | ✓ |  |  |
| DungeonRunner | `playable` | 106.7 | 2842 | ✓ |  |  |
| DuoWIZ_Pong | `playable` | 106.6 | 2770 | ✓ |  |  |
| EpicFreeFall_Wiz | `playable` | 48.3 | 1260 | ✓ |  |  |
| epiphany | `playable` | 59.2 | 1516 | ✓ |  |  |
| freecell2x | `playable` | 105.7 | 2892 | ✓ |  |  |
| Geca Blaster 2 (Gp2x Wiz) | `playable` | 51.4 | 1323 | ✓ |  |  |
| Ghostpix | `playable` | 57.2 | 1469 | ✓ |  |  |
| gnurobbo_0.65_wiz | `playable` | 54.3 | 1396 | ✓ |  |  |
| gobble | `playable` | 58.0 | 1470 | – | no-audio |  |
| gr-v1001-wiz | `playable` | 57.8 | 1500 | ✓ |  |  |
| herknights | `playable` | 58.5 | 1529 | ✓ |  |  |
| hexen2 | `playable` | 55.0 | 1426 | ✓ |  |  |
| kuklomenos | `playable` | 58.2 | 1492 | ✓ |  |  |
| malvado | `playable` | 56.4 | 1460 | ✓ |  |  |
| March of the mini tux(wiz version) | `playable` | 116.3 | 2948 | ✓ |  |  |
| midway | `playable` | 70.0 | 1784 | ✓ |  |  |
| Minigolf | `playable` | 58.7 | 1495 | – | no-audio |  |
| minos-gp2x-wiz | `playable` | 58.0 | 1470 | ✓ |  |  |
| Monster2-1.0-wiz | `playable` | 40.7 | 1507 | ✓ |  |  |
| mush_gp2x | `playable` | 42.4 | 1092 | ✓ |  |  |
| mush_gp2x-0 | `playable` | 29.0 | 769 | ✓ |  |  |
| Myriad | `playable` | 58.1 | 1473 | ✓ |  |  |
| nethack-wiz | `playable` | 60.4 | 1521 | – | no-audio |  |
| NewSuperPang05 | `playable` | 57.4 | 1466 | ✓ |  |  |
| noiz2sa_wiz | `playable` | 58.2 | 1499 | ✓ |  |  |
| openggs | `playable` | 58.3 | 1484 | ✓ |  |  |
| Out Zone | `playable` | 59.8 | 1500 | ✓ |  |  |
| paf | `playable` | 57.8 | 1468 | ✓ |  |  |
| Pentominos | `playable` | 61.6 | 1546 | ✓ |  |  |
| PEZ | `playable` | 58.0 | 1466 | – | no-audio |  |
| pgw | `playable` | 58.2 | 1523 | ✓ |  |  |
| Pharaoh | `playable` | 59.9 | 1504 | ✓ |  |  |
| PhishyWiz | `playable` | 57.0 | 1465 | ✓ |  |  |
| Powder2X_wiz_114_v01 | `playable` | 56.0 | 1426 | – | no-audio |  |
| PPlane2.WIZ | `playable` | 101.3 | 2841 | ✓ |  |  |
| prboom-wiz | `playable` | 58.5 | 1485 | – | no-audio |  |
| preggo_Wiz | `playable` | 57.1 | 1469 | ✓ |  |  |
| Propis | `playable` | 49.4 | 1247 | ✓ |  |  |
| protozoa | `playable` | 57.2 | 1474 | ✓ |  |  |
| PuzzleDevilWizDemo | `playable` | 54.6 | 1382 | ✓ |  |  |
| Rezerwar | `playable` | 57.7 | 1476 | ✓ |  |  |
| roadfighter | `playable` | 59.4 | 1509 | ✓ |  |  |
| rockrain-gp2x-wiz | `playable` | 60.2 | 1515 | ✓ |  |  |
| Sachunsung2 | `playable` | 59.9 | 1505 | ✓ |  |  |
| scummvm-1.2.0-gp2xwiz | `playable` | 56.2 | 1449 | ✓ |  |  |
| Shanghai2 | `playable` | 60.2 | 1514 | ✓ |  |  |
| Shock Troopers Base Defense | `playable` | 52.6 | 1457 | ✓ |  |  |
| SimOniZ | `playable` | 110.1 | 2836 | ✓ |  |  |
| Skull (Windows, Linux & Gp2x Wiz) | `playable` | 45.3 | 1256 | ✓ |  |  |
| sleuthslots | `playable` | 58.4 | 1476 | ✓ |  |  |
| smw_1.7 | `playable` | 54.3 | 1459 | ✓ |  |  |
| Snow Bros 2 | `playable` | 59.7 | 1500 | ✓ |  |  |
| SOD_Wiz | `playable` | 56.1 | 1464 | ✓ |  |  |
| Sopwith | `playable` | 59.9 | 1509 | ✓ |  |  |
| Space Varments | `playable` | 56.1 | 1466 | ✓ |  |  |
| spout | `playable` | 61.0 | 1537 | – | no-audio |  |
| Sqdef_Wiz_14A | `playable` | 56.2 | 1439 | ✓ |  |  |
| Sudoku2X | `playable` | 61.0 | 1539 | – | no-audio |  |
| SudoQ | `playable` | 46.7 | 1205 | ✓ |  |  |
| Tail Tale | `playable` | 61.2 | 1539 | ✓ |  |  |
| tetwizdownload | `playable` | 115.4 | 2903 | ✓ |  |  |
| The Minigame Project | `playable` | 54.0 | 1468 | ✓ |  |  |
| tilt | `playable` | 58.3 | 1475 | ✓ |  |  |
| Trap75 | `playable` | 59.2 | 1489 | ✓ |  |  |
| tricorder | `playable` | 58.0 | 1473 | ✓ |  |  |
| TUcS.app(V0.7.0 - Wiz) | `playable` | 111.2 | 2905 | ✓ |  |  |
| Twin Cobra | `playable` | 59.7 | 1500 | ✓ |  |  |
| uqm2x_release.1.1 | `playable` | 80.6 | 2049 | ✓ |  |  |
| wiz-car-binary_090818a | `playable` | 61.1 | 1536 | ✓ |  |  |
| Wiz_Blox | `playable` | 115.5 | 2951 | ✓ |  |  |
| wiz_drench | `playable` | 114.8 | 2912 | ✓ |  |  |
| Wiz_Propis_Demo | `playable` | 38.7 | 984 | ✓ |  |  |
| WIZ_S4S | `playable` | 112.1 | 2835 | ✓ |  |  |
| wizchess-v1.1.0-bin | `playable` | 61.2 | 1545 | – | no-audio |  |
| wizchess-v1.2.0-bin | `playable` | 60.1 | 1515 | – | no-audio |  |
| WizFrontier v0.1 | `playable` | 56.9 | 1524 | ✓ |  |  |
| wizgo-v1.1.0-bin | `playable` | 59.8 | 1515 | – | no-audio |  |
| WizGolf | `playable` | 60.5 | 1533 | – | no-audio |  |
| wizmancala-v1.1.2-bin | `playable` | 60.0 | 1514 | – | no-audio |  |
| wizpong | `playable` | 57.6 | 1461 | – | no-audio |  |
| wizznic-0.9.9-wiz | `playable` | 56.5 | 1502 | ✓ |  |  |
| wnw_demo | `playable` | 59.5 | 1502 | ✓ |  |  |
| Worship Vector | `playable` | 59.5 | 1497 | ✓ |  |  |
| WWII | `playable` | 55.3 | 1457 | ✓ |  |  |
| xpiredwiz.eng.101 | `playable` | 59.1 | 1498 | ✓ |  |  |
| xRick | `playable` | 59.7 | 1520 | ✓ |  |  |
| Zero Wing | `playable` | 59.7 | 1501 | ✓ |  |  |
| znumbers | `playable` | 61.2 | 1538 | ✓ |  |  |
| Zoltan | `playable` | 56.4 | 1469 | ✓ |  |  |

### Caanoo (194 titles)

| Title | Tier | fps | Frames | Audio | Failure group | Detail |
|---|---|--:|--:|:-:|---|---|
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
| supertux | `incompatible` | 1.4 | 1 | ✓ | no-frames |  |
| tmw_v1.0.0-beta-2_caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| warcraft-beta3-caanoo | `incompatible` | 0.0 | 0 | ✓ | no-frames |  |
| zlocada-caanoo | `incompatible` | 0.0 | 0 | – | no-frames |  |
| zsxd | `incompatible` | 0.0 | 0 | – | no-frames |  |
| arcadevol1 | `black` | 3.8 | 4 | ✓ | black-screen |  |
| BubbleTrain | `black` | 1.0 | 2 | ✓ | black-screen |  |
| kenlab-caanoo | `black` | 53.1 | 1492 | – | black-screen |  |
| xcom1-v1.0.2-caanoo | `black` | 26.9 | 34 | ✓ | black-screen |  |
| xcom2-v1.0.1-caanoo | `black` | 56.9 | 1457 | ✓ | black-screen |  |
| aimcaanoo | `ingame` | 47.2 | 1219 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 160px; left and right halves are near- |
| apocalypso Caanoo | `ingame` | 16.3 | 435 | ✓ | low-fps |  |
| BubblePop (Caanoo) | `ingame` | 17.5 | 450 | ✓ | low-fps |  |
| caanoo-biniax2-v1.30-bin | `ingame` | 16.1 | 406 | ✓ | low-fps |  |
| can-zomb_3 | `ingame` | 17.5 | 525 | ✓ | low-fps |  |
| chexquest-caanoo | `ingame` | 17.5 | 448 | ✓ | low-fps |  |
| Coral Sea (Caanoo - Bennu) | `ingame` | 17.6 | 456 | ✓ | low-fps |  |
| Deadly Eye (Caanoo) | `ingame` | 18.3 | 471 | ✓ | low-fps |  |
| DefendorX_C | `ingame` | 16.2 | 424 | ✓ | low-fps |  |
| EEEEK! EEEEEK! HOOOOOOK!!! | `ingame` | 23.2 | 648 | ✓ | garbled-visuals | renders at 640x480 instead of 320x240 |
| EpicFreeFall | `ingame` | 18.0 | 471 | ✓ | low-fps |  |
| EpicFreeFall Caanoo | `ingame` | 18.3 | 471 | ✓ | low-fps |  |
| Firewhip-Caanoo | `ingame` | 19.5 | 520 | ✓ | low-fps |  |
| Geca Blaster 2 (Caanoo) | `ingame` | 13.4 | 343 | ✓ | low-fps |  |
| gnuRobbo | `ingame` | 19.6 | 521 | ✓ | low-fps |  |
| gr-v1001-caanoo | `ingame` | 9.5 | 250 | ✓ | low-fps |  |
| Hamster's Escape 3D (Caanoo) | `ingame` | 10.7 | 276 | ✓ | low-fps |  |
| HamstersEscape (Caanoo) | `ingame` | 18.7 | 482 | ✓ | low-fps |  |
| Hardcore Fight (Caanoo) | `ingame` | 13.6 | 349 | ✓ | low-fps |  |
| Liar | `ingame` | 13.5 | 339 | ✓ | low-fps |  |
| llcpcls-caanoo | `ingame` | 19.4 | 493 | ✓ | low-fps |  |
| MasteriesRunners (Caanoo) | `ingame` | 9.1 | 236 | ✓ | low-fps |  |
| Metal Slug Zombies | `ingame` | 29.5 | 804 | ✓ | garbled-visuals | renders at 640x480 instead of 320x240 |
| mtknights | `ingame` | 37.2 | 939 | ✓ | garbled-visuals | the screen holds a second copy of itself, offset by 92px |
| nlove_0.6.2_(beta)_caanoo | `ingame` | 14.1 | 356 | – | low-fps |  |
| OperationFenix (Caanoo) | `ingame` | 6.4 | 177 | ✓ | low-fps |  |
| PantaVsDragon (Caanoo) | `ingame` | 15.4 | 397 | ✓ | low-fps |  |
| Pharaoh | `ingame` | 10.1 | 254 | ✓ | low-fps |  |
| prboom-caanoo | `ingame` | 9.5 | 243 | – | low-fps |  |
| Protect&rescue | `ingame` | 13.4 | 365 | ✓ | low-fps |  |
| purito_cycling_1.5_Caanoo | `ingame` | 3.1 | 81 | ✓ | low-fps |  |
| pushover-v0.2-bin | `ingame` | 17.5 | 443 | ✓ | low-fps |  |
| sbtime_caanoo | `ingame` | 5.4 | 148 | ✓ | low-fps |  |
| Skull (Caanoo) | `ingame` | 14.6 | 378 | ✓ | garbled-visuals | renders at 320x200 instead of 320x240 |
| smallball | `ingame` | 17.9 | 464 | ✓ | low-fps |  |
| smallball-Caanoo | `ingame` | 18.0 | 464 | ✓ | low-fps |  |
| smw_1.7 | `ingame` | 19.9 | 532 | ✓ | low-fps |  |
| SnailRace_C | `ingame` | 19.3 | 501 | – | low-fps |  |
| SORRv5_Caanoo | `ingame` | 0.1 | 2 | ✓ | low-fps |  |
| the solitarie | `ingame` | 14.9 | 400 | ✓ | low-fps |  |
| Txishos (Caanoo) | `ingame` | 16.3 | 421 | ✓ | low-fps |  |
| xpiredcan.eng.101 | `ingame` | 0.1 | 3 | ✓ | low-fps |  |
| zelda-roth-olb-3t_caanoo | `ingame` | 18.2 | 499 | ✓ | low-fps |  |
| Zverealm-Caanoo | `ingame` | 8.4 | 261 | ✓ | low-fps |  |
| 20110831 - Bomber Run Redux | `playable` | 57.1 | 1481 | ✓ |  |  |
| aaa | `playable` | 58.7 | 1486 | ✓ |  |  |
| aaaa | `playable` | 52.2 | 1326 | ✓ |  |  |
| Abbaye_caanoo | `playable` | 56.5 | 1490 | ✓ |  |  |
| Abbaye_caanoo_v3 | `playable` | 58.7 | 1541 | ✓ |  |  |
| ADVENTURE | `playable` | 57.4 | 1516 | ✓ |  |  |
| Amoebax | `playable` | 59.0 | 1497 | ✓ |  |  |
| animatch | `playable` | 20.1 | 544 | ✓ |  |  |
| aquaVenture | `playable` | 54.0 | 1364 | ✓ |  |  |
| Arcadevol2 | `playable` | 60.8 | 1555 | ✓ |  |  |
| Arcadevol3 | `playable` | 58.3 | 1523 | – | no-audio |  |
| B'lox! | `playable` | 55.0 | 1413 | ✓ |  |  |
| Balloonacy | `playable` | 52.0 | 1320 | ✓ |  |  |
| balls12_caanoo_bin | `playable` | 59.2 | 1508 | – | no-audio |  |
| battlejewels-105-caanoo-beta | `playable` | 24.4 | 615 | ✓ |  |  |
| Blackjack21v1.1 | `playable` | 58.9 | 1486 | – | no-audio |  |
| Blingo | `playable` | 58.8 | 1527 | ✓ |  |  |
| Blitz | `playable` | 52.9 | 1369 | ✓ |  |  |
| Blix2x | `playable` | 60.6 | 1524 | ✓ |  |  |
| caanoo-12swap-v1.0-bin | `playable` | 35.2 | 891 | ✓ |  |  |
| caanoo-chess-v1.1.0-bin | `playable` | 40.8 | 1031 | – | no-audio |  |
| caanoo-gnurobbo-0.68 | `playable` | 39.3 | 1001 | ✓ |  |  |
| caanoo-go-v1.1.0-bin | `playable` | 28.3 | 715 | – | no-audio |  |
| caanoo-mancala-v1.1.0-bin | `playable` | 31.9 | 807 | – | no-audio |  |
| cat_trap | `playable` | 51.4 | 1303 | ✓ |  |  |
| cavestory | `playable` | 56.1 | 1524 | ✓ |  |  |
| ccrg-caanoo | `playable` | 48.5 | 1221 | ✓ |  |  |
| cgenius-caanoo | `playable` | 23.9 | 623 | ✓ |  |  |
| cllwrth | `playable` | 24.2 | 612 | ✓ |  |  |
| cooldowncaanoo | `playable` | 714.4 | 18727 | ✓ |  |  |
| daff_s_adventure_2_caanoo | `playable` | 25.2 | 647 | ✓ |  |  |
| deadlyc | `playable` | 54.0 | 1365 | ✓ |  |  |
| DealorNoDeal | `playable` | 59.5 | 1529 | ✓ |  |  |
| deminor | `playable` | 31.5 | 791 | – | no-audio |  |
| demons | `playable` | 61.8 | 1555 | ✓ |  |  |
| Drench | `playable` | 54.2 | 1373 | ✓ |  |  |
| dynamate_c | `playable` | 45.0 | 1197 | ✓ |  |  |
| echo_caanoo | `playable` | 22.0 | 643 | ✓ |  |  |
| Flappynerd_Caanoo | `playable` | 27.9 | 707 | ✓ |  |  |
| fleshchasmer | `playable` | 50.6 | 1297 | ✓ |  |  |
| freeheroes2_c | `playable` | 60.4 | 1535 | ✓ |  |  |
| fshark | `playable` | 60.7 | 1526 | ✓ |  |  |
| Fywod_caanoo | `playable` | 53.8 | 1364 | ✓ |  |  |
| Geek_em_up_CAANOO | `playable` | 45.4 | 1276 | ✓ |  |  |
| getstar | `playable` | 90.6 | 2277 | ✓ |  |  |
| gnp_104 | `playable` | 54.0 | 1462 | ✓ |  |  |
| gravityforcev2 | `playable` | 48.4 | 1228 | ✓ |  |  |
| Guru Logic | `playable` | 52.7 | 1335 | ✓ |  |  |
| hellfire | `playable` | 60.6 | 1523 | ✓ |  |  |
| Hero_The_Realm-DEMO | `playable` | 21.4 | 552 | ✓ |  |  |
| HeroTheRealm_DEMOv2 | `playable` | 21.2 | 550 | ✓ |  |  |
| hexahop_1.0 | `playable` | 45.6 | 1149 | – | no-audio |  |
| Humos-Caanoo | `playable` | 23.5 | 656 | ✓ |  |  |
| instead-1.6.1-caanoo | `playable` | 41.0 | 1079 | ✓ |  |  |
| jump_n_blob_caanoo | `playable` | 25.4 | 690 | ✓ |  |  |
| JUMPNRUN | `playable` | 54.7 | 1518 | ✓ |  |  |
| jumpToTheMoon_c | `playable` | 28.0 | 707 | ✓ |  |  |
| ketm | `playable` | 38.4 | 1361 | – | no-audio |  |
| knight | `playable` | 60.5 | 1524 | ✓ |  |  |
| KOF (Ver. 5f) (Caanoo) | `playable` | 25.9 | 768 | ✓ |  |  |
| laserchess_c | `playable` | 55.3 | 1432 | – | no-audio |  |
| lmission_0.5 | `playable` | 57.3 | 1442 | ✓ |  |  |
| meritous | `playable` | 31.0 | 788 | ✓ |  |  |
| MISC | `playable` | 59.6 | 1566 | – | no-audio |  |
| Mission_faileD 1.2 [Caanoo] | `playable` | 31.3 | 893 | ✓ |  |  |
| MNV_Caanoo_Release1 | `playable` | 53.1 | 1399 | ✓ |  |  |
| monster | `playable` | 23.2 | 624 | ✓ |  |  |
| next_element | `playable` | 60.1 | 1519 | ✓ |  |  |
| noiz2sa_caanoo | `playable` | 39.0 | 995 | ✓ |  |  |
| nuclearchess | `playable` | 219.9 | 5558 | – | no-audio |  |
| outzone | `playable` | 60.5 | 1521 | ✓ |  |  |
| pang | `playable` | 57.4 | 1474 | ✓ |  |  |
| pengupop | `playable` | 36.3 | 914 | ✓ |  |  |
| powder | `playable` | 56.4 | 1423 | – | no-audio |  |
| powermanga-0.80 | `playable` | 39.0 | 1029 | ✓ |  |  |
| profanation_Caanoo | `playable` | 32.3 | 815 | ✓ |  |  |
| propis | `playable` | 49.1 | 1235 | ✓ |  |  |
| puzsion | `playable` | 26.5 | 759 | ✓ |  |  |
| PUZZLEBOARDS | `playable` | 107.8 | 2945 | ✓ |  |  |
| RACING | `playable` | 58.0 | 1528 | ✓ |  |  |
| rg_ura_103 | `playable` | 56.5 | 1479 | ✓ |  |  |
| rhythmosplay_1.1.12 | `playable` | 51.8 | 1312 | ✓ |  |  |
| Sachunsung2 | `playable` | 54.6 | 1373 | ✓ |  |  |
| SantaMania | `playable` | 55.8 | 1408 | ✓ |  |  |
| sbt | `playable` | 30.5 | 841 | ✓ |  |  |
| Shanghai2 | `playable` | 54.8 | 1378 | ✓ |  |  |
| SHOOTERS | `playable` | 90.3 | 2447 | ✓ |  |  |
| SimOniZ | `playable` | 54.5 | 1390 | ✓ |  |  |
| Sitwell (Caanoo) | `playable` | 34.7 | 897 | ✓ |  |  |
| Slap | `playable` | 90.3 | 2266 | ✓ |  |  |
| snowbros | `playable` | 87.8 | 2203 | ✓ |  |  |
| snowbros2 | `playable` | 60.5 | 1520 | ✓ |  |  |
| SOD(r181) | `playable` | 23.0 | 605 | ✓ |  |  |
| space52_caanoo | `playable` | 32.3 | 862 | ✓ |  |  |
| SPORTS | `playable` | 59.1 | 1527 | ✓ |  |  |
| sqrxz-v0996-caanoo | `playable` | 56.9 | 1452 | ✓ |  |  |
| sqrxz2-v0.80-caanoo | `playable` | 51.8 | 1314 | ✓ |  |  |
| stppc-caanoo-29-11-2010 | `playable` | 31.2 | 1155 | ✓ |  |  |
| STRATEGY | `playable` | 59.7 | 1562 | ✓ |  |  |
| tailtale4c | `playable` | 60.3 | 1521 | ✓ |  |  |
| Tigerhell | `playable` | 60.8 | 1529 | ✓ |  |  |
| Tile | `playable` | 53.5 | 1493 | ✓ |  |  |
| tlosaf_v12-caanoo | `playable` | 60.4 | 1519 | – | no-audio |  |
| tong-caanoo | `playable` | 55.5 | 1468 | ✓ |  |  |
| Trap75 | `playable` | 52.5 | 1322 | ✓ |  |  |
| Truxton | `playable` | 60.6 | 1522 | ✓ |  |  |
| truxton2 | `playable` | 87.4 | 2191 | ✓ |  |  |
| twincobr | `playable` | 60.6 | 1525 | ✓ |  |  |
| twinhawk | `playable` | 61.9 | 1556 | ✓ |  |  |
| Vigo | `playable` | 54.0 | 1397 | – | no-audio |  |
| Wardner | `playable` | 90.7 | 2277 | ✓ |  |  |
| warehouse_panic_v1.1_caanoo | `playable` | 44.8 | 1350 | ✓ |  |  |
| WindandWater | `playable` | 60.5 | 1530 | ✓ |  |  |
| Wizznic 0.9.2- preview | `playable` | 24.7 | 669 | ✓ |  |  |
| wolf4sdl-caanoo | `playable` | 58.7 | 1504 | – | no-audio |  |
| wvector | `playable` | 43.4 | 1091 | ✓ |  |  |
| zerowing | `playable` | 90.2 | 2268 | ✓ |  |  |
| zombiesorbet_v1.0_caanoo | `playable` | 22.5 | 578 | ✓ |  |  |
| zomg-Caanoo | `playable` | 20.2 | 544 | ✓ |  |  |
