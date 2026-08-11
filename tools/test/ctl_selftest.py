#!/usr/bin/env python3
"""Control-channel self-test: asserts the debugger's contract against a freestanding ARM guest.

Asset-free and deterministic, so it runs in CI. Deliberately checks the properties that broke
during development and would break again silently:

  * a breakpoint set while the guest is RUNNING actually fires (it only applied to already-parked
    threads once, so it silently did nothing);
  * a single step executes EXACTLY one instruction (verified on known 4-byte ADDs, not on library
    code where any PC delta looks plausible);
  * symbols include STT_NOTYPE assembly labels;
  * mem.write lands and mem.read sees it;
  * quit while paused does NOT hang -- a parked thread never returns from uc_emu_start, so a
    missing force-resume wedges teardown forever.

Usage: ctl_selftest.py <port>
"""
import json
import socket
import sys
import time

FAILURES = []


def check(cond, label, detail=""):
    print(("  ok   " if cond else "  FAIL ") + label + (("  -- " + str(detail)) if detail else ""))
    if not cond:
        FAILURES.append(label)
    return cond


class Ctl:
    def __init__(self, port):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=20)
        self.f = self.s.makefile("rb")

    def call(self, cmd, payload=None, **kw):
        req = dict(cmd=cmd, **kw)
        if payload is not None:
            req["bin"] = len(payload)
        self.s.sendall((json.dumps(req) + "\n").encode())
        if payload is not None:
            self.s.sendall(payload)
        line = self.f.readline()
        if not line:
            raise RuntimeError("control channel closed")
        hdr = json.loads(line)
        blob = self.f.read(hdr["bin"]) if hdr.get("bin") else b""
        return hdr, blob

    def regs(self, tid):
        hdr, _ = self.call("threads")
        for t in hdr["threads"]:
            if t["tid"] == tid:
                return t["regs"]
        return None


def main(port):
    c = Ctl(port)

    h, _ = c.call("hello")
    check(h.get("ok") and h.get("protocol") == 1, "hello", h.get("protocol"))

    h, _ = c.call("status")
    check(h.get("nth", 0) >= 1, "status reports a thread", h.get("nth"))

    h, _ = c.call("mem.map")
    check(h.get("count", 0) > 0, "mem.map returns regions", h.get("count"))

    # Symbols: step_zone is a NOTYPE assembly label.
    h, _ = c.call("sym.find", name="step_zone")
    check(h.get("ok"), "sym.find finds a NOTYPE label")
    zone = h.get("addr", 0)

    h, _ = c.call("sym.at", addr=zone)
    check(h.get("name") == "step_zone", "sym.at round-trips", h.get("name"))

    # Breakpoint set while RUNNING must fire.
    h, _ = c.call("bp.add", addr=zone)
    bpid = h.get("id")
    check(h.get("ok"), "bp.add while running")
    hit = False
    for _ in range(60):
        time.sleep(0.1)
        h, _ = c.call("status")
        if h.get("paused"):
            hit = True
            break
    check(hit, "breakpoint fired without a prior pause")
    h, _ = c.call("status")
    st = h.get("stop", {})
    check(st.get("reason") == "breakpoint" and st.get("pc") == zone,
          "stop reports reason=breakpoint at the right pc", st)
    c.call("bp.del", id=bpid)

    # Exactly-one-instruction stepping over known 4-byte ADDs.
    h, _ = c.call("threads")
    tid = h["threads"][0]["tid"]
    r = c.regs(tid)
    pc0, r00 = r[15], r[0]
    good = True
    for _ in range(4):
        c.call("step", tid=tid, n=1)
        r = c.regs(tid)
        if r[15] - pc0 != 4 or ((r[0] - r00) & 0xffffffff) != 1:
            good = False
            break
        pc0, r00 = r[15], r[0]
    check(good, "single step = exactly one instruction (+4 pc, +1 r0)")

    # mem.write must land while paused.
    h, blob = c.call("mem.read", addr=zone, len=4)
    check(h.get("ok") and len(blob) == 4, "mem.read")
    h, _ = c.call("mem.write", addr=zone, payload=b"\x01\x02\x03\x04")
    check(h.get("ok"), "mem.write while paused")
    h, after = c.call("mem.read", addr=zone, len=4)
    check(after == b"\x01\x02\x03\x04", "mem.write is visible to mem.read", after.hex())
    c.call("mem.write", addr=zone, payload=blob)     # restore

    # mem.read of an unmapped range must fail rather than allocate.
    h, _ = c.call("mem.read", addr=0xDEAD0000, len=16)
    check(not h.get("ok") and h.get("err") == "unmapped", "unmapped mem.read errors", h.get("err"))

    h, _ = c.call("resume")
    check(h.get("ok"), "resume")

    # Leave PAUSED and drop the connection: the engine must still exit cleanly. This is the
    # regression test for the teardown-vs-park deadlock.
    c.call("pause", timeout_ms=2000)
    c.s.close()
    print("  (left paused and disconnected -- the caller checks the engine still exits)")

    if FAILURES:
        print("CTL SELFTEST FAIL: %s" % ", ".join(FAILURES))
        return 1
    print("CTL SELFTEST PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(int(sys.argv[1])))
