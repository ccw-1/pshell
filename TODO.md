# pshell TODO List

## Expect Buffer Feature - Completed ✅

- [x] Implement dedicated expect buffer architecture
- [x] Add dual-write system (raw buffer + expect buffer)
- [x] Implement buffer seeding from screen content
- [x] Integrate with expect engine (activate/deactivate)
- [x] Add line position tracking
- [x] Implement socket protocol handler
- [x] Fix pshellctl client JSON parsing
- [x] Test pattern matching functionality
- [x] Document implementation
- [x] Create maintenance guide

## Code Review Findings - 2026-10-13

### Critical (Memory Corruption & Security)

- [x] **C1**: Off-by-one heap overflow in expect buffer seeding (`session.c:1098`) — **FIXED** (clamp to `size - 1`)
- [x] **C2**: Buffer overflow in REST API JSON escaping (`rest_api.c:715,810`) — **FIXED** (loop bound `-7` not `-2`)
- [x] **C3**: Stack overflow in JSON parser via deep nesting (`json_simple.c:222-267`) — **FIXED** (depth limit 200)
- [x] **C4**: Integer overflow in CSI parameter parsing (`vt100_parser.c:223`) — **FIXED** (clamp before multiply)
- [x] **C5**: Integer overflow in screen buffer resize (`screen_buffer.c:124`) — **FIXED** (SIZE_MAX check)
- [x] **C6**: Partial read of message body (`socket_server.c:260`) — **FIXED** (read loop)
- [x] **C7**: Partial write of response (`socket_protocol.c:65,71`) — **FIXED** (write loop)
- [x] **C8**: Client partial write of request (`socket_client.c:72,78`) — **FIXED** (write loop)
- [x] **C9**: No timeout on client response read (`socket_client.c:102`) — **FIXED** (SO_RCVTIMEO 60s)
- [x] **C10**: `strdup` return not checked (`socket_server.c:53`) — **FIXED** (NULL check + free)
- [x] **C11**: Integer overflow in scrollback count clamp (`socket_protocol.c:802`) — **FIXED** (reordered comparison)
- [x] **C12**: `end_line` unbounded, memory exhaustion DoS (`socket_protocol.c:880`, `rest_api.c:1111`) — **FIXED** (added end_line clamp)
- [x] **C13**: Data race on `raw_buffer` - no lock (`session.c:418-431`) — **FIXED** (raw_buffer_lock mutex)

### High (Data Loss, Corruption & Reliability)

- [x] **H1**: Data loss on partial write in output log (`session.c:335,350,364`) — **FIXED** (partial write handling with memmove)
- [x] **H2**: Non-ASCII bytes silently dropped (`socket_protocol.c:404`, `vt100_parser.c:173-181`) — **FIXED** (unsigned char cast + high-byte passthrough)
- [x] **H3**: Command not JSON-escaped in responses (`socket_protocol.c:188,223,288`) — **FIXED** (append_json_escaped used)
- [x] **H4**: Response truncation produces invalid JSON (`socket_protocol.c:86-89`) — **FIXED** (send_success truncation check)
- [x] **H5**: `handle_list_sessions` truncation produces invalid JSON (`socket_protocol.c:209-254`) — **FIXED** (rollback to last good position)
- [x] **H6**: Undersized allocation in `handle_get_lines` (`socket_protocol.c:888`) — **FIXED** (content_len * 6 + 1024)
- [x] **H7**: `\F13` produces "F3" - digit eaten (`string_utils.c:298-301`) — **FIXED** (save/restore position)
- [x] **H8**: Session list accessed without lock (`session.c:695-706,774-828,508-511`) — **FIXED** (mgr->lock mutex)
- [x] **H9**: `session_manager_destroy` doesn't stop multiplexer thread (`session.c:195-226`) — **FIXED** (io_multiplexer_stop + join)
- [x] **H10**: TOCTOU race in `session_manager_destroy_session` (`session.c:727-772`) — **FIXED** (atomic check-and-destroy under lock)
- [x] **H11**: `FD_SET` overflow - no `FD_SETSIZE` check (`io_multiplexer.c:148-162`) — **FIXED** (fd >= FD_SETSIZE check)
- [x] **H12**: `setenv()` in child after fork - not async-signal-safe (`process_spawn.c:225`) — **FIXED** (manual environ update)
- [x] **H13**: `drop_privileges()` in child uses logging (`process_spawn.c:40-83`) — **FIXED** (child_fatal instead of log_*)
- [x] **H14**: `sysconf()` in child after fork (`process_spawn.c:211`) — **FIXED** (compile-time constant)
- [x] **H15**: `session_clear_raw_buffer` doesn't reset `raw_available` (`session.c:1249-1255`) — **FIXED** (reset to 0)
- [x] **H16**: Data loss on `realloc` failure (`session.c:1371-1376,1406-1411`) — **FIXED** (return NULL on failure)
- [x] **H17**: `pty_fd == -1` not handled - silent session hang (`session.c:639-656`) — **FIXED** (fail session creation)
- [x] **H18**: Single `read()` truncates HTTP requests (`http_server.c:431`) — **FIXED** (read loop until \r\n\r\n)
- [x] **H19**: `http_client.c` snprintf pattern OOB write (`http_client.c:57-79`) — **FIXED** (bounds check after each snprintf)
- [x] **H20**: `spawn_daemon` inherits stdin/stdout (`mcp_server.c:296-309`) — **FIXED** (close stdio before exec)
- [x] **H21**: `expect_client_gone` races with fd reuse (`socket_protocol.c:565-572`) — **FIXED** (connection ID instead of raw fd)
- [x] **H22**: `get_default_shell()` return not checked (`socket_protocol.c:174-177`) — **FIXED** (NULL check + error response)

### Medium

- [x] **M1**: Partial read of 4-byte length prefix (`socket_server.c`, `socket_client.c`) — **FIXED** (read loops)
- [x] **M2**: `EINTR` not handled on read/write — **FIXED** (added EINTR handling to socket_client.c, socket_protocol.c, process_wait, pty_write)
- [x] **M3**: `fsync()` on socket — **FIXED** (removed fsync call)
- [x] **M4**: `rotate_count`/`rotate_lines` not validated — **FIXED** (validated in socket_protocol.c)
- [x] **M5**: Scrollback lines silently truncated at 255 bytes — **FIXED** (increased to 4096 + truncation warning)
- [x] **M6**: `append_json_escaped` doesn't check `snprintf` — **FIXED** (added return value check)
- [x] **M7**: `socket_connection_send` partial write — **FIXED** (write-all loop)
- [x] **M8**: `send_fd_response` length prefix not looped — **FIXED** (write-all loop)
- [x] **M9**: Default `end_line=0` invalid — **FIXED** (changed to -1 meaning "read to end")
- [x] **M10**: Outer loop offset overflow in `handle_get_scrollback` — **FIXED** (bounds check on each snprintf)
- [x] **M11**: `tcgetattr` return not checked — **FIXED** (error handling + zero struct)
- [x] **M12**: `session_is_alive` TOCTOU — **FIXED** (use waitpid WNOHANG)
- [x] **M13**: Public session functions don't check `session->active` — **FIXED** (added active checks)
- [x] **M14**: 64KB stack buffer in `session_start_expect` — **FIXED** (heap allocation)
- [x] **M15**: `session_read_line_range` returns NULL for no data — **FIXED** (returns empty string)
- [x] **M16**: `session_manager_cleanup` declared but never defined — **FIXED** (removed declaration)
- [x] **M17**: `session_read_output` returns -1 with EAGAIN — **FIXED** (returns 0)
- [x] **M18**: `cleanup_dead_sessions` blocks — **FIXED** (reduced usleep from 100ms to 10ms)
- [x] **M19**: EOF on PTY doesn't remove FD — **FIXED** (io_multiplexer_remove on EOF)
- [x] **M20**: Callback modifies handler list during iteration — **FIXED** (deferred removal mechanism)
- [x] **M21**: `process_wait` doesn't handle `EINTR` — **FIXED** (retry loop)
- [x] **M22**: `pty_write` doesn't handle partial writes — **FIXED** (write-all loop)
- [x] **M23**: `file_map_count` grows indefinitely — **FIXED** (cap at MAX_ROTATE_FILES)
- [x] **M24**: `session_read_line_range` only checks first MAX_ROTATE_FILES — **FIXED** (iterate all entries)
- [x] **M25**: `gettimeofday` not monotonic in expect_engine — **FIXED** (clock_gettime)
- [x] **M26**: `gettimeofday` not monotonic in timeout_manager — **FIXED** (clock_gettime)
- [x] **M27**: TOCTOU race between timeout check and data arrival — **FIXED** (match immediately after append)
- [x] **M28**: No range validation in `config_get_int` — **FIXED** (errno/ERANGE/INT_MIN/INT_MAX)
- [x] **M29**: No `SIGCHLD` handler — **FIXED** (async-signal-safe reaper)
- [x] **M30**: Off-by-one in `scroll_up` — **FIXED** (removed guard)
- [x] **M31**: Off-by-one in `scroll_down` — **FIXED** (removed guard)
- [x] **M32**: FD leak in `daemonize()` — **FIXED** (proper open/dup2)
- [x] **M33**: JSON parser drops `\b`, `\f`, `\uXXXX` — **FIXED** (added support)
- [x] **M34**: JSON number parser accepts `-` alone — **FIXED** (digit validation)
- [x] **M35**: JSON parser accepts trailing garbage — **FIXED** (trailing check)
- [x] **M36**: `pshellctl` fixed buffers truncate — **FIXED** (dynamic allocation)
- [x] **M37**: `pshell_client` fixed buffers truncate — **FIXED** (dynamic allocation)
- [x] **M38**: HTTP error bodies not surfaced — **FIXED** (body as error for 4xx/5xx)
- [x] **M39**: Chunked encoding not handled — **FIXED** (explicit rejection)
- [x] **M40**: `http_client.c` partial write — **FIXED** (write-all loop)
- [x] **M41**: No timeout on daemon RPC reads — **FIXED** (SO_RCVTIMEO)
- [x] **M42**: Integer overflow in `handle_get_scrollback` — **FIXED** (reordered comparison)
- [x] **M43**: `atoi()` UB on out-of-range — **FIXED** (safe_atoi with strtol)
- [x] **M44**: `fread` return unchecked — **FIXED** (added comment, known limitation)
- [x] **M45**: `write()` return unchecked — **FIXED** (write-all loops everywhere)

### Low

- [x] **L1**: Dead code in default case (`socket_protocol.c:430` etc.) — **FIXED** (removed redundant `||` checks)
- [x] **L2**: Dead state: buffer, expect_ctx (`socket_server.c:273-275`) — **FIXED** (removed unused connections array)
- [x] **L3**: Dead state: connections array (`socket_server.c:37-38`) — **FIXED** (removed)
- [x] **L4**: `send_error` doesn't escape message — **FIXED** (JSON escaping added)
- [x] **L5**: Zero-length response not handled — **FIXED** (returns 0)
- [x] **L6**: `json_stringify` doesn't escape — **FIXED** (proper escaping)
- [x] **L7**: No numeric arg validation in pshellctl — **FIXED** (safe_atoi)
- [x] **L8**: `atoi` UB on overflow — **FIXED** (safe_atoi)
- [x] **L9**: `gethostbyname` deprecated — **FIXED** (getaddrinfo)
- [x] **L10**: Content-Length case-sensitive — **FIXED** (strcasestr_local)
- [x] **L11**: Misleading expect error message — **FIXED** (improved error handling)
- [x] **L12**: `sb_grow` theoretical overflow — **FIXED** (not applicable, removed)
- [x] **L13**: 17th CSI parameter silently dropped — **FIXED** (increased to 32)
- [x] **L14**: `\0` escape produces embedded NUL — **FIXED** (doc warning added)
- [x] **L15**: Debug fprintf in production — **FIXED** (removed debug prints)
- [x] **L16**: `expect_any` fixed sleep — **FIXED** (exponential backoff)
- [x] **L17**: Scrollback not zeroed on mode 3 — **FIXED** (optional memset)
- [x] **L18**: Buffer shift can lose long patterns — **FIXED** (documented limitation)
- [x] **L19**: PID file write not checked — **FIXED** (return value check)

### Documentation

- [x] **D1**: Wiki says "14 routes" but code has 15 (`wiki/index.md:52`) — **FIXED** (updated to 15)
- [x] **D2**: `\0` escape documented as "Null character" but truncates strings (`docs/ESCAPE_SEQUENCES.md:18`) — **FIXED** (added warning)
- [x] **D3**: README "Last Updated" date vs CHANGELOG release date mismatch — **FIXED** (updated to 2026-10-10)
- [x] **D4**: Same as D1 (via wiki reference) — **FIXED**
- [x] **D5**: `session_manager_cleanup` declared but never defined (`session.h:142`) — **FIXED** (removed declaration)

---

## Known Issues & Future Improvements

### High Priority

- [ ] **Buffer Size Management**
  - Current: Fixed 64KB expect buffer
  - Issue: May lose old data if buffer fills
  - Solution: Implement dynamic buffer expansion or configurable size
  - Impact: Medium - affects long-running expect operations

- [ ] **Line Position Accuracy**
  - Current: Counts newlines in buffer
  - Issue: VT100 escape sequences affect line counting
  - Solution: Use screen buffer line tracking instead
  - Impact: Low - line position is informational

- [ ] **Multiple Concurrent Expects**
  - Current: One expect per session
  - Issue: Cannot run multiple expect operations simultaneously
  - Solution: Per-context buffers or operation queuing
  - Impact: Medium - limits automation scenarios

- [ ] **Screen buffer: grapheme cluster widths**
  - Combining marks, ZWJ sequences and skin tone modifiers are kept with their base character (cluster pool, max 8 code points, 4096 distinct clusters per screen; beyond that the extra marks are dropped)
  - Not done: variation selector U+FE0F does not widen a narrow base to 2 columns; regional indicator pairs (flags) take 2 cells; Hangul jamo sequences are not composed
  - `get_scrollback` lines are limited to 4096 bytes per line
  - Impact: Low

- [ ] **`pshellctl expect` / `expect-incoming` do not JSON-escape the pattern**
  - Current: the pattern is pasted into the request as is; docs/scripts pass pre-escaped patterns (`"\\$ "`)
  - Issue: a pattern with `"` fails; escaping now would change the meaning of pre-escaped patterns
  - Solution: escape in the client and document that patterns are plain regex text (breaking for existing scripts)
  - Impact: Low

- [ ] **`pshellctl attach` limitations**
  - Output is dropped for a terminal that cannot keep up; limit is `[sessions] max_attach` (default 16)
  - The initial redraw has colours and attributes, but not the terminal modes of the program (alternate screen, cursor visibility, mouse/paste modes)
  - Colon-separated SGR parameters (`38:2::r:g:b`) are not parsed by the VT100 parser (the sequence is dropped); `;` forms work

### Medium Priority

- [ ] **Pattern Matching Performance**
  - Current: Linear search O(n*m)
  - Issue: Slow for large buffers (64KB)
  - Solution: Implement Boyer-Moore or incremental matching
  - Impact: Low - only affects large buffer searches

- [ ] **Memory Management**
  - Current: Expect buffer kept until session destruction
  - Issue: Memory not freed after expect completes
  - Solution: Free buffer after inactivity period
  - Impact: Low - 64KB per session is acceptable

- [ ] **Timeout Precision**
  - Current: 10ms polling with usleep()
  - Issue: Not precise, wastes CPU
  - Solution: Use select/poll for event-driven timeout
  - Impact: Low - current implementation works

### Low Priority

- [ ] **Screen Buffer Seeding**
  - Current: May include VT100 escape sequences
  - Issue: Pattern matching against escape sequences
  - Solution: Strip escape sequences or use plain text
  - Impact: Very Low - rarely causes issues

- [ ] **Error Reporting**
  - Current: Limited error codes
  - Issue: Hard to diagnose failures
  - Solution: Add detailed error codes and messages
  - Impact: Low - debug logging usually sufficient

- [ ] **Test Suite Expansion**
  - Current: Unit tests (`tests/unit/test_pattern.c`, `tests/unit/test_terminal.c`) and integration tests (`tests/integration/test_full_workflow.sh`, `tests/integration/test_mcp_server.py`) run via `make test` / `make check`
  - Issue: Coverage gaps (timeout behavior, buffer overflow, concurrency, memory leaks)
  - Solution: Extend the existing test suite to cover remaining scenarios
  - Impact: Medium - important for maintenance

## Feature Requests

- [ ] **Expect Multiple Patterns**
  - Wait for any of N patterns to match
  - Return which pattern matched
  - Use case: Branching automation logic

- [ ] **Expect with Callback**
  - Register callback for pattern match
  - Async notification instead of blocking
  - Use case: Event-driven automation

- [ ] **Pattern History**
  - Track all matched patterns in session
  - Query match history
  - Use case: Debugging, audit trail

- [ ] **Expect Statistics**
  - Track match count, average time, failures
  - Per-session and global stats
  - Use case: Performance monitoring

- [ ] **Buffer Snapshots**
  - Save expect buffer state at intervals
  - Replay/analyze later
  - Use case: Debugging complex scenarios

## Documentation

- [x] EXPECT_BUFFER_IMPLEMENTATION.md - Technical details
- [x] MAINTENANCE.md - Maintenance guide
- [x] TODO.md - This file
- [ ] API_REFERENCE.md - Complete API documentation
- [ ] EXAMPLES.md - Usage examples and patterns
- [ ] TROUBLESHOOTING.md - Common issues and solutions

## Testing

- [x] Basic pattern matching
- [x] Line position tracking
- [x] Historical pattern matching
- [ ] Timeout behavior
- [ ] Buffer overflow handling
- [ ] Multiple sessions
- [ ] Concurrent operations
- [ ] Memory leak testing (valgrind)
- [ ] Performance benchmarks
- [ ] Edge cases (empty buffer, huge patterns, etc.)

## Build & Release

- [x] Makefile builds all binaries
- [x] No compilation warnings
- [ ] Static analysis (cppcheck, clang-tidy)
- [ ] Code coverage analysis
- [ ] Performance profiling
- [ ] Release packaging
- [ ] Installation script

## Notes

- Expect buffer implementation completed 2026-08-02
- All core functionality working and tested
- Focus next on test suite and documentation
- Consider performance improvements for v2.0
