# pshell-mcp — Model Context Protocol Server

`pshell-mcp` exposes pshell terminal sessions as [MCP](https://modelcontextprotocol.io) tools so AI agents (opencode, Claude Desktop, and any MCP client) can create and drive interactive terminal sessions.

It speaks **MCP over stdio** (newline-delimited JSON-RPC 2.0) and forwards every tool call to the pshell daemon over the **Unix domain socket**. If the daemon is not running, the MCP server **starts it automatically**.

## Build

```bash
make            # builds bin/pshell-mcp alongside pshell, pshell-client, pshellctl
make mcp        # builds only the MCP server
```

## Run

The binary is a stdio server — MCP hosts launch it themselves. For manual testing:

```bash
printf '%s\n' \
'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"t","version":"1"}}}' \
'{"jsonrpc":"2.0","method":"notifications/initialized"}' \
'{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
'{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"pshell_create_session","arguments":{"command":"bash"}}}' \
| ./bin/pshell-mcp
```

### Options and environment

| Option | Environment variable | Default | Meaning |
|---|---|---|---|
| `-s <path>` | `PSHELL_SOCKET_PATH` | value of `socket_path` in the config, else `/tmp/pshell.sock` | Daemon socket to talk to |
| `-c <path>` | `PSHELL_CONFIG` | `/etc/pshell/pshell.conf`, `../config/pshell.conf`, `config/pshell.conf`, `pshell.conf` | Config used when **starting** the daemon |
| `-b <path>` | `PSHELL_DAEMON_BIN` | `pshell` next to `pshell-mcp`, then `/usr/local/bin`, `/usr/bin`, `/bin` | Daemon binary to start |
| `-n` | `PSHELL_NO_AUTOSTART=1` | autostart on | Require an already-running daemon |
| `-h` | | | Help |

## Auto-start behaviour

On the first tool call, if the socket does not accept a connection the server:

1. Resolves the daemon binary (see above) and a config file.
2. Writes `/tmp/pshell-mcp-<pid>.conf` which pins `pid_file`, `log_file` and `socket_path` to writable `/tmp` paths (the daemon **exits** if it cannot write its PID file, and its built-in defaults point at `/var/run` and `/var/log`) and then appends the real config, so all other settings are preserved.
3. Execs the daemon, which daemonizes itself and keeps running after the MCP server exits.
4. Waits up to 5 s for the socket to accept connections, then proceeds.

The daemon survives MCP client restarts; kill it with `pkill -x pshell` (or `pshellctl`/systemd) to start over.

## Tools

| Tool | Purpose |
|---|---|
| `pshell_create_session` | Start a session (`command`, default `bash`; `rows`/`cols`/`scrollback_lines`; optional `output_file` logging with `rotate_count`/`rotate_lines`). Returns the session ID. |
| `pshell_list_sessions` | List sessions (ID, command, size, PID, state). |
| `pshell_send` | Send input (`text`). `process_escapes: true` interprets `\n`, `\cC`, `\UP`, `\F1`, … (see [ESCAPE_SEQUENCES.md](ESCAPE_SEQUENCES.md)). |
| `pshell_read_output` | Read new output since the last read (consuming). |
| `pshell_screen` | Current VT100 screen contents + cursor position. |
| `pshell_scrollback` | Terminal history lines (`start_line`, `count`). |
| `pshell_read_lines` | Global line range from the output log (`start_line`/`end_line` or `head`/`tail`). **Requires the session to be created with `output_file`.** |
| `pshell_expect` | Wait for an extended regex (`pattern`, `timeout_ms`, `case_sensitive`). Matches scrollback plus new output. |
| `pshell_expect_incoming` | Same, but only output produced after the call starts. |
| `pshell_resize` | Resize the terminal (`rows`, `cols`). |
| `pshell_destroy_session` | Kill the session and its process. |
| `pshell_status` | Daemon status, session count, uptime. |

Every tool returns MCP `content` text; failures set `isError: true` with the daemon's message.

## Driving full-screen (TUI) applications

Applications with their own line editor or input widget (Bob Shell, editors, curses menus) treat a burst of bytes arriving in one read as a *paste*. If the text and the Enter key are sent in a single `pshell_send`, the trailing `\r` is inserted as a newline in the input line instead of submitting it.

Send them as **two separate calls**:

1. `pshell_send` with the text (no newline), e.g. `/status`
2. `pshell_send` with `text: "\r"` and `process_escapes: true`

Other tips:

- `\cU` clears the current input line; `\e` (ESC) closes dialogs and overlays.
- Use `pshell_screen` (not `pshell_read_output`) to see what a TUI shows now; `pshell_read_output` returns the raw redraw stream.
- The screen buffer is UTF-8 aware: box drawing, accents, CJK and emoji appear in `pshell_screen` and `pshell_scrollback` and cursor movement counts columns, not bytes. Double-width characters (CJK, emoji) take two columns; combining marks (accents written as a separate code point) are dropped; invalid UTF-8 shows as U+FFFD. Cell attributes (colour, bold) are tracked but not returned.

To watch or type into a session yourself from a local terminal (no HTTP), use `pshellctl attach <session-id>` (`Ctrl-]` detaches).

## Client configuration

### opencode

`opencode.json`:

```json
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "pshell": {
      "type": "local",
      "command": ["/usr/local/bin/pshell-mcp"],
      "enabled": true
    }
  }
}
```

### Claude Desktop

`claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "pshell": {
      "command": "/usr/local/bin/pshell-mcp",
      "args": []
    }
  }
}
```

Use `"/path/to/bin/pshell-mcp"` while developing in the source tree.

## Security

The MCP server inherits the daemon's security model: the Unix socket is protected by file permissions (`socket_permissions = 0600` by default), so anyone who can run `pshell-mcp` can control sessions with the daemon owner's privileges — the same trade-off as `pshellctl`. On multi-user systems read [SECURITY.md](SECURITY.md) before exposing the daemon's HTTP interface; the MCP server itself only needs the socket.

## Tests

```bash
python3 tests/integration/test_mcp_server.py
```

Exercises initialize, `tools/list`, all twelve tools, error paths and the auto-start path (kill any running daemon first — the test starts its own).

## Daemon notes

- **`expect` runs asynchronously.** The daemon runs the wait on a detached worker thread, so the event loop keeps reading PTYs while an expect is pending — output produced during the wait is matched (previously it was lost and the call timed out). The client connection is kept open by the worker; if the client disconnects, the wait is cancelled and the daemon stays responsive. Consequences:
  - one expect per session at a time (a second one is rejected with `Expect already in progress for this session`)
  - `pshell_destroy_session` is refused while an expect is active (protects the worker); retry after the expect returns
- **`pshell_read_lines`** needs sessions created with `output_file` (the line counter is only maintained for output logging). Log data is buffered until a read flushes it, so `tail`/`head`/range reads see everything available.
- **`pshell_scrollback`** returns an empty note until lines scroll off the visible screen; use `pshell_screen` for live output.
- `pshellctl lines --tail` now reconnects between its two daemon round-trips (the daemon serves one message per connection); it previously died from SIGPIPE.
