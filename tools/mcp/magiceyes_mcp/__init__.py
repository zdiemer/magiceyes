"""magiceyes MCP debug server.

Importing `env` here (not lazily in each module) is load-bearing: it puts tools/test on sys.path so
`import shmlib` works in every submodule regardless of import order. Without it, whichever module
imports shmlib before env wins or loses depending on ordering.
"""
from . import env as env  # noqa: F401  -- side effect: sys.path += tools/test

__version__ = "0.1.0"
