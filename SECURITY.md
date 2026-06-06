# Security Policy

## Reporting a vulnerability

If you find a security issue in magiceyes (for example, a way for a malicious game file to
escape the emulator and run code on the host), please report it privately rather than
opening a public issue.

- Preferred: open a [GitHub security advisory](https://github.com/zdiemer/magiceyes/security/advisories/new).
- Or email the maintainer at zach.diemer@gmail.com.

Please include the game/input file (or a minimal reproducer), the magiceyes version, your
OS, and what you observed. We'll acknowledge your report and work on a fix.

## Scope

magiceyes runs untrusted ARM game binaries inside an emulated CPU. The host-facing attack
surface is the syscall/device emulation layer (`host/engine/`, `host/common/`) and file
parsing (ELF/GPEComp loaders). Bugs that let guest code read or write host memory, host
files outside the game directory, or execute host code are in scope.

Crashes that only take down the emulated game (not the host process) are tracked as normal
bugs, not security issues.
