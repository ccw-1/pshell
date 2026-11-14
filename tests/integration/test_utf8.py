#!/usr/bin/env python3
"""Integration test: the screen buffer keeps non-ASCII text intact.

Starts a private daemon, makes a bash session print box-drawing, accented,
wide (CJK), emoji and invalid-UTF-8 text, and checks that the screen
snapshot (`pshellctl screen`), the scrollback (`pshellctl scrollback`) and the
`attach` redraw all return valid UTF-8 with the characters in place.
"""
import os
import pty
import select
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
                       capture_output=True, timeout=20)
    return r.returncode, r.stdout.decode("utf-8", "replace") + r.stderr.decode("utf-8", "replace")


def ctl_raw(sock, *args):
    r = subprocess.run([PSHELLCTL, "-s", sock] + list(args),
                       capture_output=True, timeout=20)
    return r.stdout


def wait_screen(sock, sid, needle, timeout=8):
    end = time.time() + timeout
    while time.time() < end:
        _, out = ctl(sock, "screen", sid)
        if needle in out:
            return out
        time.sleep(0.2)
    return out


def main():
    tmp = tempfile.mkdtemp(prefix="pshell-utf8-")
    sock = os.path.join(tmp, "p.sock")
    conf = os.path.join(tmp, "p.conf")
    with open(conf, "w") as f:
        f.write(f"""[daemon]
pid_file = {tmp}/p.pid
log_file = {tmp}/p.log
log_level = info

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
    art = os.path.join(tmp, "art.txt")
    with open(art, "wb") as f:
        f.write("\x1b[2J\x1b[H".encode())
        f.write("╭──╮\r\n│é│\r\n日本語 ✓ 😀|\r\n".encode())
        f.write(b"bad:\xff\xe2\x94 end\r\n")                 # invalid UTF-8
        f.write("abc\x1b[3D\x1b[2Cxyz\r\n".encode())          # cursor moves
        f.write("dec: e\u0301 a\u0308\u0323 \U0001F44D\U0001F3FD |\r\n".encode())   # combining marks
    daemon = subprocess.Popen([PSHELL, "-f", "-c", conf],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(50):
            if os.path.exists(sock):
                break
            time.sleep(0.1)
        check("daemon socket up", os.path.exists(sock))

        rc, out = ctl(sock, "create", "--command", "bash --norc --noprofile",
                      "--rows", "10", "--cols", "40", "--scrollback", "100")
        sid = out.strip().splitlines()[-1].strip() if out.strip() else ""
        check("session created", len(sid) >= 32, out)
        if len(sid) < 32:
            return

        ctl(sock, "send", sid, "cat " + art + "\\n")
        out = wait_screen(sock, sid, "abxyz")
        raw = ctl_raw(sock, "screen", sid)
        try:
            raw.decode("utf-8")
            valid = True
        except UnicodeDecodeError:
            valid = False
        check("screen output is valid UTF-8", valid)
        check("box drawing intact", "╭──╮" in out, out)
        check("accent intact", "│é│" in out, out)
        check("CJK, check mark and emoji intact", "日本語 ✓ 😀|" in out, out)
        check("invalid bytes become U+FFFD", "bad:\ufffd\ufffd end" in out, out)
        check("cursor movement by columns, not bytes", "abxyz" in out, out)
        check("combining marks and modifiers kept",
              "dec: e\u0301 a\u0308\u0323 \U0001F44D\U0001F3FD |" in out, out)

        # Push the art into the scrollback: 10 rows, print 30 numbered lines
        ctl(sock, "send", sid, "for i in $(seq 1 30); do echo ─$i─; done\\n")
        wait_screen(sock, sid, "─30─")
        rc, out = ctl(sock, "scrollback", sid, "--count", "60")
        sb_raw = ctl_raw(sock, "scrollback", sid, "--count", "60")
        try:
            sb_raw.decode("utf-8")
            sb_valid = True
        except UnicodeDecodeError:
            sb_valid = False
        check("scrollback is valid UTF-8", sb_valid)
        check("scrollback keeps characters", "─1─" in out, out)

        # attach redraw
        pid, fd = pty.fork()
        if pid == 0:
            os.execv(PSHELLCTL, [PSHELLCTL, "-s", sock, "attach", sid])
        buf = b""
        end = time.time() + 3
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.2)
            if r:
                try:
                    data = os.read(fd, 4096)
                except OSError:
                    break
                if not data:
                    break
                buf += data
        os.write(fd, b"\x1d")
        time.sleep(0.3)
        os.close(fd)
        os.waitpid(pid, 0)
        check("attach redraw carries UTF-8", "─30─".encode() in buf, repr(buf[-200:]))

        # a screen wider than the old 8 KiB response limit
        rc, out = ctl(sock, "resize", sid, "50", "200")
        ctl(sock, "send", sid, "clear; for i in $(seq 1 49); do printf '─%.0s' $(seq 1 190); echo; done\\n")
        out = wait_screen(sock, sid, "─" * 150)
        check("large UTF-8 screen is returned", "─" * 150 in out, out[:200])
        check("daemon still alive", daemon.poll() is None)
    finally:
        daemon.terminate()
        try:
            daemon.wait(timeout=5)
        except subprocess.TimeoutExpired:
            daemon.kill()
    print("\nALL PASSED" if failed == 0 else f"\n{failed} FAILED")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
