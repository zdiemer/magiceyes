magiceyes 0.6.0

Savestates, and a rendering bug that had been skewing every frame GP2X games drew into their
second framebuffer.

Savestates

- F5 saves where you are, F8 puts it back. Nine numbered slots as well: F6 and F7 pick one,
  Shift+F5 saves to it, Shift+F8 loads it. F4 opens a picker showing when each slot was saved
  and what was on screen at the time. All of it is on a new State menu too.
- A save is the whole machine, not a save file: both CPUs, all memory, the display and audio
  hardware, and the files the game had open, each reopened at the position it was at.
  A game's own save files are on disk and are not rewound with it, so loading a state from
  before an in-game save leaves that save file in place.
- Saves live in states/<game>/ beside the executable, alongside the existing config, firmware,
  cache and saves folders, and can be moved with File then Settings.
- Saving does not disturb the running game: it stops, reads, and starts again, and the game
  carries on unaware. A load asked for while the emulator is paused takes effect when you
  resume.
- Also driveable from the control channel and the MCP tools, for scripted testing.

Rendering

- GP2X games that draw through the second framebuffer, /dev/fb1, had those frames come out
  three rows up and 64 pixels left, wrapped around the screen. The address handed to the game
  was not page aligned, so the game drew at one place and the display read from another. A
  game that alternates the two buffers gets it on every second frame, which in Payback looked
  like the picture jumping sideways as you played.
- The display scanout address is a 32 bit value held in two 16 bit registers, and games write
  it one half at a time. The display no longer acts on a half written address, which used to
  point it at neither buffer.

Audio

- Opening a second game left the sound device running at the first game's sample rate, so the
  new game played an octave too high and stuttered. It now reopens when the format changes.

Emulation correctness

- The self modifying code table is cleared when a game is unloaded. It was carrying into the
  next game, where skipping code invalidation is not safe.
- The second CPU core now stops and starts with the rest of the machine when a savestate is
  taken on a paused emulator.

Packaging

- Release builds now apply the complete set of emulator core patches. Built from a clean
  checkout, which is what the release job does, four of them were being skipped, including the
  fix for the crash on loading a second game on Windows.

Testing

- Savestates are covered by unit tests for the file format, an end to end test that saves,
  moves the game on, restores and checks the picture came back, and refusal tests for damaged,
  foreign and incompatible states.
