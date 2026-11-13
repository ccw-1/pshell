# Changelog

All notable changes to pshell are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The current version is defined in [VERSION](VERSION) and `include/pshell/version.h`.

## [Unreleased]

### Added

- `pshellctl attach <session-id>`: interactive terminal on a session over the Unix socket (no HTTP). The `attach_session` socket method replies normally, then the connection becomes a raw byte stream owned by the session (client input to the PTY, PTY output to up to 4 attached clients, screen redraw on attach). The session is resized to the attaching terminal (also on SIGWINCH); `Ctrl-]` detaches. Test: `tests/integration/test_attach.py`.
### Fixed

- Daemon segfault (heap overflow) after a session was resized wider: `screen_buffer_resize` kept the scrollback allocated for the old column count, so lines scrolling off a wider screen overflowed it. The scrollback is now rebuilt at the new width (order preserved). Surfaced by `pshellctl attach`, which resizes the session to the attaching terminal; `pshellctl resize` hit the same bug. Regression test in `tests/unit/test_terminal.c` (run under ASAN).

- Daemon deadlock when a session's process exited: `session_manager_cleanup_dead_sessions` held `mgr->lock` while calling `session_manager_destroy_session`, which locks it again.

## [1.2.0] - 2026-10-14

### Fixed — Critical Memory Corruption & Security (13 bugs)

- **C1**: Off-by-one heap overflow in expect buffer seeding (`session.c:1098`) — clamp to `size - 1`
- **C2**: Buffer overflow in REST API JSON escaping (`rest_api.c:715,810`) — loop bound `-7` not `-2`
- **C3**: Stack overflow in JSON parser via deep nesting (`json_simple.c`) — depth limit 200
- **C4**: Integer overflow in CSI parameter parsing (`vt100_parser.c:223`) — clamp before multiply
- **C5**: Integer overflow in screen buffer resize (`screen_buffer.c:124`) — `SIZE_MAX` check
- **C6**: Partial read of message body (`socket_server.c`) — read-all loops
- **C7**: Partial write of response (`socket_protocol.c`) — write-all loops
- **C8**: Client partial write of request (`socket_client.c`) — write-all loops
- **C9**: No timeout on client response read — `SO_RCVTIMEO` 60s
- **C10**: `strdup` return not checked (`socket_server.c`) — NULL check + free
- **C11**: Integer overflow in scrollback count clamp — reordered comparison
- **C12**: `end_line` unbounded (memory exhaustion DoS) — clamp to `total_lines`
- **C13**: Data race on `raw_buffer` — `raw_buffer_lock` mutex

### Fixed — High Severity Data Loss & Reliability (22 bugs)

- **H1**: Data loss on partial write in output log — `memmove` remaining data
- **H2**: Non-ASCII bytes silently dropped — `unsigned char` cast + high-byte passthrough in VT100
- **H3**: Command not JSON-escaped in responses — `append_json_escaped` used
- **H4**: Response truncation produces invalid JSON — truncation check in `send_success`
- **H5**: `handle_list_sessions` truncation → invalid JSON — rollback to last good position
- **H6**: Undersized allocation in `handle_get_lines` — `content_len * 6`
- **H7**: `\F13` produces "F3" (digit eaten) — save/restore position
- **H8**: Session list accessed without lock — `mgr->lock` mutex
- **H9**: `session_manager_destroy` doesn't stop multiplexer — `io_multiplexer_stop` + join
- **H10**: TOCTOU race in `destroy_session` — atomic check-and-destroy under lock
- **H11**: `FD_SET` overflow — `fd >= FD_SETSIZE` check
- **H12**: `setenv()` not async-signal-safe — manual `environ` update
- **H13**: `drop_privileges()` uses logging in child — `child_fatal()` instead
- **H14**: `sysconf()` not async-signal-safe — compile-time constant
- **H15**: `session_clear_raw_buffer` doesn't reset `raw_available` — reset to 0
- **H16**: Data loss on `realloc` failure — return NULL on failure
- **H17**: `pty_fd == -1` not handled — fail session creation
- **H18**: Single `read()` truncates HTTP requests — read loop until `\r\n\r\n`
- **H19**: `http_client.c` snprintf OOB write — bounds check after each `snprintf`
- **H20**: `spawn_daemon` inherits stdin/stdout — close stdio before `exec`
- **H21**: `expect_client_gone` fd reuse race — connection ID instead of raw fd
- **H22**: `get_default_shell()` return not checked — NULL check + error

### Fixed — Medium Severity Correctness & Robustness (45 bugs)

- EINTR handling added to all read/write operations
- Monotonic clocks (`clock_gettime`) in expect engine and timeout manager
- SIGCHLD handler for zombie reaping
- JSON parser: `\b`/`\f`/`\uXXXX` support, number validation, trailing garbage rejection
- Dynamic buffers in `pshellctl`/`pshell_client` (no truncation)
- HTTP error body surfacing for 4xx/5xx, chunked encoding rejection
- TOCTOU fixes, partial write handling, deferred callback modifications
- `tcgetattr` return value checked, `config_get_int` range validation
- `session_is_alive` TOCTOU fixed with `waitpid(WNOHANG)`
- Public session functions check `active` flag
- `pty_write` handles partial writes, `process_wait` handles EINTR
- `file_map_count` capped, `session_read_line_range` iterates all entries

### Fixed — Low Severity & Code Quality (19 bugs)

- Dead code/state removal (`connections[]` array, buffer, expect_ctx)
- `send_error` JSON escaping, zero-length response handling
- `json_stringify` proper escaping
- `gethostbyname` → `getaddrinfo`, case-insensitive Content-Length
- Debug fprintfs removed, `expect_any` exponential backoff
- PID file write checked, `\0` escape doc warning

### Documentation

- Wiki: "15 routes" (was 14)
- `ESCAPE_SEQUENCES.md`: `\0` truncates string warning
- `README.md`: Last Updated date aligned with CHANGELOG (2026-10-10)
- Removed undeclared `session_manager_cleanup` from header

### Changed

- Version bumped to 1.2.0 (MINOR per SemVer: backward-compatible bug fixes)

## [1.1.0] - 2026-10-10

### Added

- **`pshell-mcp` - Model Context Protocol server** (`src/mcp/mcp_server.c`, `docs/MCP_SERVER.md`)
  - Speaks MCP over stdio (newline-delimited JSON-RPC 2.0), `initialize` / `ping` /
    `tools/list` / `tools/call`, protocol version negotiation, JSON-RPC error codes
  - 12 tools: `pshell_create_session`, `pshell_list_sessions`, `pshell_send`,
    `pshell_read_output`, `pshell_screen`, `pshell_scrollback`, `pshell_read_lines`,
    `pshell_expect`, `pshell_expect_incoming`, `pshell_resize`,
    `pshell_destroy_session`, `pshell_status`
  - **Auto-starts the daemon** when the Unix socket does not accept connections:
    resolves the daemon binary and config, writes a temporary config pinning
    pid/log/socket paths to writable locations, then waits for the socket
  - Options `-s` (socket), `-c` (config), `-b` (binary), `-n` (no autostart), `-v`, `-h`;
    environment: `PSHELL_SOCKET_PATH`, `PSHELL_CONFIG`, `PSHELL_DAEMON_BIN`, `PSHELL_NO_AUTOSTART`
  - `examples/mcp/opencode.json` and `examples/mcp/claude_desktop_config.json`
  - Integration test: `tests/integration/test_mcp_server.py` (98 checks, exercises the
    auto-start path with no daemon running)
- Versioning: `include/pshell/version.h` (single source of truth), `VERSION` file,
  this changelog, and `-v` output for `pshell` and `pshell-mcp`

### Fixed

- **Expect no longer loses output that arrives while it waits.** The wait previously ran
  inside the single-threaded event loop, stalling PTY reads, so any output produced during
  the call was never observed and the expect timed out. It now runs on a detached worker
  thread (`src/socket/socket_protocol.c`): the event loop keeps servicing PTYs, the PTY
  callback appends to the expect buffer, and the worker matches against it.
  - Expect buffer/line state is serialized with a per-session mutex
    (`src/session/session.c`); `session_expect_active()` added
  - Client-disconnect cancellation is preserved via an `expect_set_cancel_check()`
    probe on the connection fd (`src/pattern/expect_engine.c`)
  - `expect_pattern_incoming` (incoming-only) now works for the first time
  - `matched_text` is now JSON-escaped in responses
  - One expect per session; `destroy_session` is refused while an expect is active
- **Line-range API**: `current_line_count` is now maintained without log rotation, and
  `total_lines_written` / line snapshots / `file_map` are reset when a session slot is
  reused - previously counters drifted across sessions and range reads failed
- **`pshellctl lines --tail`** no longer dies from SIGPIPE: it reconnects between the two
  daemon round-trips (the daemon serves one message per connection), uses `get_session`
  for the line count, and reports an empty log cleanly
- **Clean builds**: `-D_DEFAULT_SOURCE` added to `CFLAGS`; `usleep()` previously failed
  to compile under `-std=c99 -D_POSIX_C_SOURCE=200809L` (e.g. `src/pattern/expect_engine.c`)
- **Repo integration test**: `tests/integration/test_full_workflow.sh` config now sets
  `[http] enabled = true` (the daemon defaults to disabled), so the suite runs past its
  health check

### Changed

- README documents the MCP server and the new daemon behaviour

## [1.0.0] - 2026-08-02

### Added

- Expect buffer feature: dedicated buffer with dual-write architecture and line
  position tracking
- Documentation: `EXPECT_BUFFER_IMPLEMENTATION.md`, `docs/MAINTENANCE.md`, `TODO.md`
- Configurable default shell, privilege separation, line-range API, output logging
  with rotation, Unix socket control utility (`pshellctl`), HTTP CLI client
  (`pshell-client`), web interface, VT100 emulation, scrollback buffer
- Security defaults: HTTP disabled, Unix socket enabled with 0600 permissions

[Unreleased]: https://github.com/ccw-1/pshell/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/ccw-1/pshell/releases/tag/v1.0.0
