#!/usr/bin/env python3
#!/usr/bin/env python3
"""Integration test for pshell-mcp: drives the MCP stdio server over JSON-RPC 2.0.

Requires bin/pshell-mcp (built by make). The pshell daemon does NOT need to be
running: the MCP server starts it automatically (this is verified below).
"""
import json
import os
import subprocess
import sys
import time

BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "..", "bin", "pshell-mcp")
fails = []


def check(name, cond, detail=""):
    print(("PASS  " if cond else "FAIL  ") + name + (("  -- " + detail) if (detail and not cond) else ""))
    if not cond:
        fails.append(name)


class Mcp:
    def __init__(self):
        self.p = subprocess.Popen([BIN], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True, bufsize=1)
        self.next_id = 1

    def call(self, method, params=None):
        req = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        if params is not None:
            req["params"] = params
        self.next_id += 1
        self.p.stdin.write(json.dumps(req) + "\n")
        self.p.stdin.flush()
        line = self.p.stdout.readline()
        if not line:
            err = self.p.stderr.read()
            raise RuntimeError("no stdout response; stderr: " + err)
        return line.strip(), json.loads(line)

    def notify(self, method):
        self.p.stdin.write(json.dumps({"jsonrpc": "2.0", "method": method}) + "\n")
        self.p.stdin.flush()

    def tool(self, name, args):
        line, resp = self.call("tools/call", {"name": name, "arguments": args})
        check("response is single line", "\n" not in line.strip())
        check("response parses as JSON", isinstance(resp, dict))
        check("jsonrpc 2.0", resp.get("jsonrpc") == "2.0")
        result = resp.get("result", {})
        content = result.get("content", [])
        text = content[0]["text"] if content else ""
        return result, text


m = Mcp()

# --- initialize ---
_, init = m.call("initialize", {"protocolVersion": "2025-03-26",
                                "capabilities": {},
                                "clientInfo": {"name": "test", "version": "1"}})
res = init.get("result", {})
check("initialize protocolVersion", res.get("protocolVersion") in
      ("2024-11-05", "2025-03-26", "2025-06-18"))
check("initialize serverInfo name", res.get("serverInfo", {}).get("name") == "pshell-mcp")
check("initialize capabilities.tools", "tools" in res.get("capabilities", {}))
check("initialize instructions", bool(res.get("instructions")))

m.notify("notifications/initialized")

# --- tools/list ---
_, tl = m.call("tools/list")
tools = tl.get("result", {}).get("tools", [])
names = [t["name"] for t in tools]
expected = ["pshell_create_session", "pshell_list_sessions", "pshell_send",
            "pshell_read_output", "pshell_screen", "pshell_scrollback",
            "pshell_read_lines", "pshell_expect", "pshell_expect_incoming",
            "pshell_resize", "pshell_destroy_session", "pshell_status"]
check("tools/list has 12 tools", len(tools) == 12, str(names))
check("tools/list names match", names == expected, str(names))
check("every tool has inputSchema", all("inputSchema" in t for t in tools))
check("every tool has description", all(t.get("description") for t in tools))

# --- status (auto-start path: daemon killed before test) ---
t0 = time.time()
_, status_text = m.tool("pshell_status", {})
elapsed = (time.time() - t0) * 1000
check("pshell_status returned text", "running" in status_text, status_text)
print("      (pshell_status took %.0f ms; this call auto-started the daemon)" % elapsed)

# --- create ---
_, create_text = m.tool("pshell_create_session", {"command": "bash", "cols": 100})
check("create returned session id", create_text.startswith("Session created:"), create_text)
sid = create_text.split(":", 1)[1].split("\n", 1)[0].strip()
check("session id looks like uuid", len(sid) == 36 and sid.count("-") == 4, sid)

# --- send + expect ---
_, send_text = m.tool("pshell_send", {"session_id": sid, "text": "echo mcp-marker-42\n"})
check("send ok", "Sent" in send_text and "bytes" in send_text, send_text)

# No sleep: output arrives while the expect is waiting. The daemon runs the
# wait on a worker thread so the event loop keeps reading PTYs (regression
# test for the expect race fix).
_, exp_text = m.tool("pshell_expect", {"session_id": sid, "pattern": "mcp-marker-42",
                                       "timeout_ms": 5000})
check("expect matched", exp_text.startswith("Matched:"), exp_text)
check("expect matched text", "mcp-marker-42" in exp_text, exp_text)

# --- expect_incoming while true ---
_, send2 = m.tool("pshell_send", {"session_id": sid, "text": "echo incoming-test\n"})
check("send2 ok", "Sent" in send2, send2)
_, exp2 = m.tool("pshell_expect_incoming", {"session_id": sid, "pattern": "incoming-test",
                                            "timeout_ms": 5000})
check("expect_incoming matched (new output)", exp2.startswith("Matched:"), exp2)

# Historical data must not match incoming-only: wait until the marker has
# fully settled, then an incoming-only expect must time out (no new output).
time.sleep(0.8)
_, exp3 = m.tool("pshell_expect_incoming", {"session_id": sid, "pattern": "incoming-test",
                                            "timeout_ms": 800})
check("expect_incoming ignores historical data", exp3.startswith("No match ("), exp3)

# --- screen / read_output / scrollback ---
_, screen_text = m.tool("pshell_screen", {"session_id": sid})
check("screen has marker", "mcp-marker-42" in screen_text, screen_text[:200])
check("screen has cursor", "[Cursor at row" in screen_text)

_, out_text = m.tool("pshell_read_output", {"session_id": sid})
check("read_output returns text", isinstance(out_text, str) and len(out_text) > 0, out_text[:100])

_, sb_text = m.tool("pshell_scrollback", {"session_id": sid, "count": 50})
check("scrollback reports state", sb_text.startswith("Scrollback:") or
      sb_text.startswith("Scrollback is empty"), sb_text[:100])

# read_lines requires output logging: create a second session with output_file
_, create2 = m.tool("pshell_create_session", {"command": "bash", "output_file": "/tmp/mcp-test-session.log"})
sid2 = create2.split(":", 1)[1].split("\n", 1)[0].strip()
m.tool("pshell_send", {"session_id": sid2, "text": "echo LINE-LOG-TEST\n"})
time.sleep(1.2)  # let the log actually accumulate lines
_, lines_text = m.tool("pshell_read_lines", {"session_id": sid2, "tail": 5})
check("read_lines tail ok", "Total lines:" in lines_text and "LINE-LOG-TEST" in lines_text, lines_text[:200])
m.tool("pshell_destroy_session", {"session_id": sid2})

# --- resize ---
_, rz = m.tool("pshell_resize", {"session_id": sid, "rows": 30, "cols": 120})
check("resize ok", "Resized" in rz and "30x120" in rz, rz)

# --- list ---
_, list_text = m.tool("pshell_list_sessions", {})
check("list contains session", sid in list_text, list_text)

# --- error paths ---
_, err_text = m.tool("pshell_expect", {"session_id": sid, "pattern": "zzz-never-happens",
                                       "timeout_ms": 300})
check("expect timeout handled", "No match (timeout)" in err_text, err_text)

miss_res, miss_txt = m.tool("pshell_screen", {"session_id": "nonexistent-id"})
check("missing session reports error", miss_res.get("isError") is True, miss_txt)

bad_res, bad_txt = m.tool("pshell_no_such_tool", {})
check("unknown tool reports error", bad_res.get("isError") is True, bad_txt)

arg_res, arg_txt = m.tool("pshell_send", {"session_id": sid})
check("missing arg reports error", arg_res.get("isError") is True, arg_txt)

# --- ping ---
_, ping = m.call("ping")
check("ping returns empty result", ping.get("result") == {}, str(ping))

# --- unknown method ---
_, unknown = m.call("no/such_method")
check("unknown method error -32601", unknown.get("error", {}).get("code") == -32601, str(unknown))

# --- destroy ---
_, destroy_text = m.tool("pshell_destroy_session", {"session_id": sid})
check("destroy ok", "destroyed" in destroy_text, destroy_text)

_, list2 = m.tool("pshell_list_sessions", {})
check("session gone after destroy", sid not in list2, list2)

m.p.stdin.close()
m.p.wait(timeout=5)

print()
if fails:
    print("FAILED: %d checks: %s" % (len(fails), fails))
    sys.exit(1)
print("ALL TESTS PASSED")
