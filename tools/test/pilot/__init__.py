"""Closed-loop headless input driver ("the pilot").

The rest of tools/test answers "did it boot and keep rendering". The pilot answers "does it
respond, and how far in can we get", by watching the framebuffer and choosing the next button
from what it sees, instead of replaying one fixed script at every title in the corpus.

Pure stdlib, so run_corpus.py keeps its no-dependency property.
"""
