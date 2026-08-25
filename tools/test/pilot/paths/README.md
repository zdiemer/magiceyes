# Per-title screen graphs

What the pilot learned about a title: the screens it saw, keyed by perceptual hash, and what each
button did on each of them. Hashes and verdicts only, no game imagery, so these are small and
committable.

A graph is written by `run_title.py --pilot` and read back on the next run, which is what makes the
pilot cumulative rather than starting from nothing every sweep:

- a title that ran out of budget half way in resumes where it got to, instead of re-deriving the
  same first three screens;
- a button that killed the title stays marked `lethal`, so it is never pressed again. Two lethal
  buttons and the pilot stops pressing that title altogether (`quits on early input`), which is how
  the angband2x-class titles get a clean hands-off run without a hand-written exception.

Outcomes per button: `moved` (went to another screen), `local` (this screen changed in place),
`dead` (nothing beyond what the screen does unprompted), `lethal` (the title exited).

`notes` is free-form and is where the MCP playtest loop writes what it worked out, e.g.
`{"family": "glbasic-wiz"}` to pick up that family's button conventions from `priors.py`.

A full sweep writes its graphs beside the staging (`~/me-sweep/paths`) rather than here, so the
working tree does not churn with a thousand files. Copy in the ones worth keeping.
