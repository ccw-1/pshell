#!/usr/bin/env python3
"""Integration test: expect on large output and several concurrent expects on one session."""
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PSHELL = os.path.join(ROOT, "bin", "pshell")
PSHELLCTL = os.path.join(ROOT, "bin", "pshellctl")

failed = 0


def check(name, ok, detail=""):
    global failed
    print(("PASS: " if ok else "FAIL: ") + name + ("" if ok else "  " + detail))
    if not ok:
        failed += 1


def ctl(sock, *args):
    r = subprocess.run([PSHELLCTL, "-s", sock] + list(args),
                       capture_output=True, text=True, timeout=30)
    return r.returncode, r.stdout + r.stderr


def start_ctl(sock, *args):
    return subprocess.Popen([PSHELLCTL, "-s", sock] + list(args),
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def finish(p, timeout=30):
    out, _ = p.communicate(timeout=timeout)
    return p.returncode, out


def main():
    tmp = tempfile.mkdtemp(prefix="pshell-exp-")
    sock = os.path.join(tmp, "p.sock")
    conf = os.path.join(tmp, "p.conf")
    with open(conf, "w") as f:
        f.write(f"""[daemon]
pid_file = {tmp}/p.pid
log_file = {tmp}/p.log
log_level = debug

[http]
enabled = false

[socket]
enabled = true
socket_path = {sock}
socket_permissions = 0600

[auth]
enabled = false

[sessions]
max_sessions = 10
""")
    daemon = subprocess.Popen([PSHELL, "-f", "-c", conf],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(50):
            if os.path.exists(sock):
                break
            time.sleep(0.1)
        rc, out = ctl(sock, "create", "--command", "bash --norc --noprofile")
        sid = out.strip().splitlines()[-1] if rc == 0 else ""
        check("session created", rc == 0 and len(sid) >= 32, out)
        if not sid:
            return
        time.sleep(0.5)

        # 1. Output much larger than one read chunk (4 KB) and than the old 64 KB buffer:
        #    the marker at the very end must still be found (incoming-only and normal).
        p_in = start_ctl(sock, "expect-incoming", sid, "ENDBIG[0-9]+", "20000")
        p_all = start_ctl(sock, "expect", sid, "ENDBIG[0-9]+", "20000")
        time.sleep(0.5)
        ctl(sock, "send", sid, "seq 1 60000; echo ENDBIG$((40+2))\\n")
        rc, out = finish(p_in)
        check("incoming expect finds a marker after 330 KB of output", rc == 0 and "ENDBIG42" in out, out)
        rc, out = finish(p_all)
        check("normal expect finds it too", rc == 0 and "ENDBIG42" in out, out)

        # 2. Concurrent expects on one session: each waits for its own pattern, the
        #    one that times out does not disturb the others, finishing one does not
        #    stop the rest.
        pa = start_ctl(sock, "expect-incoming", sid, "ALPHA[0-9]", "15000")
        pb = start_ctl(sock, "expect-incoming", sid, "BRAVO[0-9]", "15000")
        pc = start_ctl(sock, "expect-incoming", sid, "NEVERSEEN", "2500")
        pd = start_ctl(sock, "expect-incoming", sid, "CHARLIE[0-9]", "15000")
        time.sleep(0.7)
        ctl(sock, "send", sid, "echo ALPHA$((1+0))\\n")
        rc, out = finish(pa)
        check("first concurrent expect matches", rc == 0 and "ALPHA1" in out, out)
        time.sleep(0.3)
        ctl(sock, "send", sid, "echo BRAVO$((1+1))\\n")
        rc, out = finish(pb)
        check("second concurrent expect matches", rc == 0 and "BRAVO2" in out, out)
        rc, out = finish(pc)
        check("third expect times out on its own", rc == 1 and "No match" in out, out)
        time.sleep(0.3)
        ctl(sock, "send", sid, "echo CHARLIE$((1+2))\\n")
        rc, out = finish(pd)
        check("expect still works after others finished or timed out", rc == 0 and "CHARLIE3" in out, out)

        # 3. Incoming-only does not see old output, a normal expect does
        rc, out = ctl(sock, "expect-incoming", sid, "ALPHA1", "700")
        check("incoming-only ignores old output", rc == 1 and "No match" in out, out)
        rc, out = ctl(sock, "expect", sid, "ALPHA1", "3000")
        check("normal expect sees old output on the screen", rc == 0, out)

        # 4. More concurrent expects than the limit are refused, the rest still work
        procs = [start_ctl(sock, "expect-incoming", sid, "LIMITHIT[0-9]", "8000") for _ in range(18)]
        time.sleep(1.5)
        ctl(sock, "send", sid, "echo LIMITHIT$((1+0))\\n")
        results = [finish(p) for p in procs]
        matched = [o for rc, o in results if rc == 0 and "LIMITHIT1" in o]
        refused = [o for rc, o in results if rc != 0 and "Expect failed" in o]
        check("16 concurrent expects accepted and matched", len(matched) == 16, str(results))
        check("expects beyond the limit refused", len(refused) == 2, str(results))

        # 5. Daemon still healthy, session can be destroyed once no expect is left
        rc, out = ctl(sock, "destroy", sid)
        check("destroy after the expects finished", rc == 0, out)
    finally:
        daemon.terminate()
        try:
            daemon.wait(timeout=10)
        except subprocess.TimeoutExpired:
            daemon.kill()


if __name__ == "__main__":
    main()
    print("\n%s" % ("ALL PASSED" if not failed else "%d FAILED" % failed))
    sys.exit(1 if failed else 0)
