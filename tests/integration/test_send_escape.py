#!/usr/bin/env python3
"""Integration test: `pshellctl send` / `create` must JSON-escape quotes and backslashes."""
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
                       capture_output=True, text=True, timeout=20)
    return r.returncode, r.stdout + r.stderr


def wait_screen(sock, sid, needle, timeout=6):
    end = time.time() + timeout
    out = ""
    while time.time() < end:
        _, out = ctl(sock, "screen", sid)
        if needle in out:
            return True, out
        time.sleep(0.2)
    return False, out


def main():
    tmp = tempfile.mkdtemp(prefix="pshell-esc-")
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
        check("daemon socket up", os.path.exists(sock))

        # --command containing double quotes and a backslash
        rc, out = ctl(sock, "create", "--command", 'bash --norc --noprofile -c "echo CMDQ$((1+1)); exec cat"')
        sid = out.strip().splitlines()[-1] if rc == 0 else ""
        check("create with quoted command", rc == 0 and len(sid) >= 32, out)
        if sid:
            ok, scr = wait_screen(sock, sid, "CMDQ2")
            check("quoted command ran", ok, scr)
            ctl(sock, "destroy", sid)

        rc, out = ctl(sock, "create", "--command", "bash --norc --noprofile")
        sid = out.strip().splitlines()[-1] if rc == 0 else ""
        check("session created", rc == 0 and len(sid) >= 32, out)
        if not sid:
            return
        time.sleep(0.5)

        # double quotes and a backslash (\\ in the argument = one backslash)
        rc, out = ctl(sock, "send", sid, 'echo "Q$((1+1))" \'back\\\\slash\'\\n')
        check("send with quotes/backslash accepted", rc == 0, out)
        ok, scr = wait_screen(sock, sid, "Q2 back\\slash")
        check("quotes and backslash arrive intact", ok, scr)

        # a tab and a control character are escaped too
        rc, out = ctl(sock, "send", sid, "echo T$((2*3))X\\n")
        check("send with newline accepted", rc == 0, out)
        ok, scr = wait_screen(sock, sid, "T6X")
        check("plain send still works", ok, scr)
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
