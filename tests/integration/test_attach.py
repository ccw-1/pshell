#!/usr/bin/env python3
"""Integration test for `pshellctl attach` (interactive terminal over the Unix socket).

Starts a private daemon, creates a bash session, attaches to it through a pty
(attach needs a tty), types a command, detaches with Ctrl-], re-attaches and
checks the screen redraw, then exits the shell and expects "session ended".
"""
import os
import pty
import select
import signal
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


class Attach:
    def __init__(self, sock, sid):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execv(PSHELLCTL, [PSHELLCTL, "-s", sock, "attach", sid])
        self.buf = b""

    def read_until(self, needle, timeout=8):
        end = time.time() + timeout
        while needle.encode() not in self.buf and time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.2)
            if r:
                try:
                    data = os.read(self.fd, 4096)
                except OSError:
                    break
                if not data:
                    break
                self.buf += data
        return needle.encode() in self.buf

    def send(self, s):
        os.write(self.fd, s.encode())

    def drain(self):
        while select.select([self.fd], [], [], 0.2)[0]:
            try:
                data = os.read(self.fd, 4096)
            except OSError:
                break
            if not data:
                break
            self.buf += data

    def wait(self, timeout=8):
        end = time.time() + timeout
        while time.time() < end:
            p, status = os.waitpid(self.pid, os.WNOHANG)
            if p:
                self.drain()
                return os.WEXITSTATUS(status)
            time.sleep(0.1)
        os.kill(self.pid, signal.SIGKILL)
        os.waitpid(self.pid, 0)
        return None


def main():
    tmp = tempfile.mkdtemp(prefix="pshell-attach-")
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
max_attach = 2
""")
    daemon = subprocess.Popen([PSHELL, "-f", "-c", conf],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(50):
            if os.path.exists(sock):
                break
            time.sleep(0.1)
        check("daemon socket up", os.path.exists(sock))

        rc, out = ctl(sock, "create", "--command", "bash --norc --noprofile")
        sid = ""
        for tok in out.replace('"', " ").replace(":", " ").split():
            if len(tok) >= 32 and "-" in tok:
                sid = tok
                break
        if not sid:
            rc, out = ctl(sock, "list")
            for tok in out.split():
                if len(tok) >= 32 and "-" in tok:
                    sid = tok
                    break
        check("session created", bool(sid), out)
        if not sid:
            return

        # attach without a tty must refuse cleanly
        rc, out = ctl(sock, "attach", sid)
        check("attach without tty refused", rc != 0 and "terminal" in out, out)

        # attach unknown session
        a = Attach(sock, "no-such-session")
        rc = a.wait()
        check("attach unknown session fails", rc == 1, str(rc))

        # first attach: type a command, see the output
        a = Attach(sock, sid)
        time.sleep(0.5)
        a.send("echo ATTACH$((1+2))OK\n")
        check("command output arrives", a.read_until("ATTACH3OK"), a.buf.decode(errors="replace"))
        a.send("\x1d")
        rc = a.wait()
        check("Ctrl-] detaches with exit 0", rc == 0, str(rc))
        check("detach notice printed", b"[detached]" in a.buf, a.buf.decode(errors="replace"))

        # session still alive and re-attach shows the earlier output
        rc, out = ctl(sock, "list")
        check("session survives detach", sid in out, out)
        b = Attach(sock, sid)
        check("re-attach redraws screen", b.read_until("ATTACH3OK"), b.buf.decode(errors="replace"))

        # two terminals at once: output reaches both
        c = Attach(sock, sid)
        time.sleep(0.5)
        b.send("echo TWO$((2*3))X\n")
        check("first terminal sees output", b.read_until("TWO6X"), b.buf.decode(errors="replace"))
        check("second terminal sees output", c.read_until("TWO6X"), c.buf.decode(errors="replace"))

        # max_attach = 2: a third terminal is refused, the others keep working
        d = Attach(sock, sid)
        rc = d.wait()
        check("third terminal refused at max_attach=2", rc != 0, "%s %r" % (rc, d.buf))
        b.send("echo STILL$((4+4))OK\n")
        check("existing terminals unaffected", b.read_until("STILL8OK"), b.buf.decode(errors="replace"))
        c.send("\x1d")
        c.wait()
        e = Attach(sock, sid)
        time.sleep(0.5)
        e.send("echo FREED$((5+5))X\n")
        check("slot reusable after detach", e.read_until("FREED10X"), e.buf.decode(errors="replace"))
        e.send("\x1d")
        e.wait()

        # shell exits -> attached terminal is told the session ended
        b.send("exit\n")
        check("session end reported", b.read_until("[session ended]"), b.buf.decode(errors="replace"))
        rc = b.wait()
        check("attach exits 0 when session ends", rc == 0, str(rc))
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
