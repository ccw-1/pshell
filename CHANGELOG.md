# Changelog

All notable changes to pshell are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The current version is defined in [VERSION](VERSION) and `include/pshell/version.h`.

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
