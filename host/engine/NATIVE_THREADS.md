# Native-threads rearchitecture for the Unicorn engine

**Goal:** replace `me_unicorn.c`'s cooperative scheduler + synchronous-fork with **real host
threads**, matching the QEMU backend's model — so the engine reaches parity (Payback → menus →
gameplay) and gets the speed back (block chaining stays on; no instruction-count slicing).

## Why the cooperative model failed
One `uc` instance time-multiplexed via `uc_context` save/restore + a snapshot/restore "fork".
It deadlocks/crashes where real threads wouldn't: a userspace spinlock starves the lock holder,
and the synchronous fork leaks engine state (signals/fds) the holder needs. Diagnosed:
main null-derefs through an uninitialised vtable after `system("exit 0")`; thread 2 spins a lock
main can't release.

## Model (mirrors qemu-user)
- **One `uc_engine` per guest thread, each on its own host thread** (`SDL_Thread` for portability).
- **Shared guest RAM via `uc_mem_map_ptr`**: every guest region is ONE host allocation mapped
  into every `uc` instance at the same guest address. So `clone(CLONE_VM)` threads genuinely
  share memory (real host memory, real cross-thread visibility). The TCG cache is per-instance
  (each thread re-JITs what it runs) — more warmup/RAM, but correct and fast (chaining on).
- **No scheduler, no `uc_emu_stop` slicing** — the host OS schedules the threads.

## Synchronization
- `clone(CLONE_VM|...)` → spawn a host thread: new `uc` over the shared memory, child regs set,
  run from the post-clone PC. (Drop the cooperative `g_th` ctx-switch machinery.)
- **`futex`** → portable futex: a global table of (uaddr → host mutex+condvar); WAIT/WAKE map to
  condvar wait/signal. (On Linux could use the real futex, but keep it portable for Win/macOS.)
- **LinuxThreads restart/cancel signals** → per-thread host event/condvar; `kill(tid,sig)` sets a
  pending bit and wakes the target; the target checks pending at safe points (syscall return,
  and via a UC_HOOK that polls a per-thread "signal pending" flag).
- **kuser `cmpxchg` @0xffff0fc0** → intercept and implement as a **host atomic CAS** on the shared
  memory (a UC_HOOK at that address, or a syscall-like trap). This is THE atomic primitive on
  ARMv5 GP2X — getting it host-correct makes all guest locks work across threads. Pre-ARMv6 ⇒ no
  `ldrex`/`strex` to worry about; add a real memory barrier for kuser_memory_barrier.
- **Big-engine lock**: a single host mutex around the syscall shim + device model + mmap/brk
  allocator (shared mutable state). CPU execution runs lock-free; only syscalls/device touches
  take it. (qemu does the same — execution parallel, syscalls serialized enough.)

## Keep / defer
- **Keep** the synchronous snapshot/restore **fork** (process fork is rare — `system()`); it's
  orthogonal to threads. Revisit if it still leaks (snapshot signal/fd state too).
- **Keep** the device model, viewer, shm contract, SMC-freeze fork.

## Risks
- Cross-instance SMC (thread A edits code B cached): GP2X installs code once at startup → rare;
  SMC-freeze covers `.iwram`. Add a cross-instance TB-flush on the rare runtime code-write if needed.
- Memory ordering: the big-engine lock + host-atomic cmpxchg + barrier should suffice for
  LinuxThreads' coarse locking.

## Phased plan
1. **Modularize first** (the file split the operator asked for): carve `me_unicorn.c` into
   `host/engine/{main,elf,mem,syscalls,threads,cpu,devices}.c` + `platform/`. Native threads land
   in a fresh `threads.c` rather than growing the monolith.
2. **Shared-memory core**: switch guest RAM to a host-owned allocator + `uc_mem_map_ptr`; make
   `uc` creation a factory that maps all current regions into a new instance.
3. **Native `clone` + per-thread host thread**; host-atomic kuser cmpxchg + barrier.
4. **Portable futex + signal-wake**; big-engine lock.
5. **Bring up Payback** interactively: menus → gameplay; measure fps with/without SMC-freeze
   (the deferred Phase-0 gate, now meaningful).
6. Re-verify the other GP2X titles; then resume the cross-platform single-binary/CMake work.
