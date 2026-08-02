# pshell Maintenance Guide

## Quick Reference

### Build System
```bash
make              # Build all binaries
make clean        # Clean build artifacts
make deploy       # Install to ~/local/bin and run daemon in foreground
make test         # Run test suite (if available)
```

### Binaries
- `bin/pshell` - Main daemon (465KB)
- `bin/pshellctl` - Control client (159KB)
- `bin/pshell-client` - Interactive client (136KB)

### Configuration
- Default: `config/pshell.conf`
- Deployed: `~/local/etc/pshell.conf`
- Socket: `/tmp/pshell.sock` (default)
- PID file: `/tmp/pshell.pid`
- Log file: `/tmp/pshell.log`

## Architecture Overview

### Core Components

1. **Session Management** (`src/session/`)
   - `session.c` - Session lifecycle, PTY management, expect buffer
   - Manages terminal sessions with PTY integration
   - Handles expect buffer dual-write architecture

2. **PTY Layer** (`src/pty/`)
   - `pty_posix.c` - POSIX PTY operations
   - `process_spawn.c` - Process spawning
   - `io_multiplexer.c` - Event-driven I/O

3. **Terminal Emulation** (`src/terminal/`)
   - `screen_buffer.c` - Virtual screen buffer
   - `vt100_parser.c` - VT100 escape sequence parsing

4. **Pattern Matching** (`src/pattern/`)
   - `expect_engine.c` - Expect pattern matching
   - `pattern_matcher.c` - Regex/literal pattern matching
   - `timeout_manager.c` - Timeout handling

5. **Protocol Handlers** (`src/socket/`, `src/http/`)
   - `socket_protocol.c` - Unix socket JSON protocol
   - `rest_api.c` - HTTP REST API (optional)

6. **Clients** (`src/ctl/`, `src/cli/`)
   - `pshellctl.c` - Command-line control client
   - `pshell_client.c` - Interactive terminal client

### Data Flow

```
PTY Output → session_pty_callback()
              ├─→ Raw Circular Buffer (always)
              │   └─→ Screen Buffer (VT100 parsing)
              │       └─→ Scrollback
              ├─→ Expect Buffer (when active)
              │   └─→ Pattern Matching
              └─→ Output Log File (if enabled)
```

## Expect Buffer System

See `EXPECT_BUFFER_IMPLEMENTATION.md` for detailed documentation.

**Key Points:**
- Dual-write architecture: PTY output goes to both raw buffer and expect buffer
- Expect buffer is seeded from screen content on activation
- Pattern matching reads from expect buffer (non-destructive)
- Line position tracking relative to cursor

**Common Issues:**
1. **Pattern not found**: Check if expect buffer was seeded correctly
2. **Wrong line position**: VT100 sequences may affect line counting
3. **Buffer overflow**: Expect buffer is 64KB, may lose old data

## Debugging

### Enable Debug Logging

Edit `pshell.conf`:
```ini
[daemon]
log_level = debug
```

### View Logs
```bash
tail -f /tmp/pshell.log
```

### Key Log Messages

**Session Creation:**
- `Created session: id=<uuid>, command=<cmd>, pid=<pid>`
- `Added PTY FD <fd> to multiplexer for session <uuid>`

**Expect Operations:**
- `Seeded expect buffer with N bytes from screen buffer`
- `Loaded N bytes of seeded data into expect context`
- `Trying to match pattern 'X' in buffer (size=N): '<preview>'`
- `Pattern matched: 'X'`
- `Expect timeout reached`

**Errors:**
- `Failed to create expect context`
- `Failed to start expect buffer`
- `Error reading session output: <error>`

### Common Debug Scenarios

**1. Expect not finding pattern:**
```bash
# Check screen content
pshellctl screen <session-id>

# Check if pattern is in output
pshellctl screen <session-id> | grep "PATTERN"

# Enable debug logging and retry
# Check /tmp/pshell.log for "Trying to match" messages
```

**2. Session not responding:**
```bash
# Check if session exists
pshellctl list

# Check if process is alive
ps aux | grep <pid>

# Check PTY file descriptor
lsof -p <daemon-pid> | grep pts
```

**3. Socket connection issues:**
```bash
# Check if socket exists
ls -la /tmp/pshell.sock

# Check socket permissions
stat /tmp/pshell.sock

# Check if daemon is running
ps aux | grep pshell

# Check daemon log
tail -50 /tmp/pshell.log
```

## Testing

### Manual Testing

**Basic Session Test:**
```bash
# Start daemon
./bin/pshell -f -c pshell.conf &

# Create session
SESSION_ID=$(./bin/pshellctl -s /tmp/pshell.sock create --command bash)

# Send command
./bin/pshellctl -s /tmp/pshell.sock send "$SESSION_ID" "echo TEST"

# Get output
./bin/pshellctl -s /tmp/pshell.sock screen "$SESSION_ID"

# Destroy session
./bin/pshellctl -s /tmp/pshell.sock destroy "$SESSION_ID"
```

**Expect Test:**
```bash
SESSION_ID=$(./bin/pshellctl -s /tmp/pshell.sock create --command bash)
./bin/pshellctl -s /tmp/pshell.sock send "$SESSION_ID" "echo PATTERN_TEST"
sleep 2
./bin/pshellctl -s /tmp/pshell.sock expect "$SESSION_ID" "PATTERN" 5000
# Expected: Matched: "PATTERN" at -N
```

### Automated Testing

Create test script `tests/test_expect.sh`:
```bash
#!/bin/bash
set -e

# Start daemon
./bin/pshell -f -c pshell.conf > /tmp/pshell_test.log 2>&1 &
DAEMON_PID=$!
sleep 3

# Test 1: Basic expect
SESSION_ID=$(./bin/pshellctl -s /tmp/pshell.sock create --command bash)
./bin/pshellctl -s /tmp/pshell.sock send "$SESSION_ID" "echo TEST_PATTERN"
sleep 2
RESULT=$(./bin/pshellctl -s /tmp/pshell.sock expect "$SESSION_ID" "TEST_PATTERN" 5000)
if echo "$RESULT" | grep -q "Matched"; then
    echo "✓ Test 1 passed"
else
    echo "✗ Test 1 failed"
    exit 1
fi

# Cleanup
./bin/pshellctl -s /tmp/pshell.sock destroy "$SESSION_ID"
kill $DAEMON_PID
```

## Known Issues

### 1. Expect Buffer Limitations
- **Issue**: Buffer is 64KB, may lose old data
- **Workaround**: Use shorter expect timeouts, match recent output
- **Fix**: TODO - Implement dynamic buffer expansion

### 2. Line Position Accuracy
- **Issue**: VT100 escape sequences affect line counting
- **Workaround**: Use pattern matching, not line numbers
- **Fix**: TODO - Use screen buffer line tracking

### 3. Multiple Concurrent Expects
- **Issue**: Only one expect per session supported
- **Workaround**: Serialize expect operations
- **Fix**: TODO - Per-context buffers or queuing

### 4. HTTP Port Conflict
- **Issue**: Default port 8080 may conflict
- **Workaround**: Disable HTTP in config: `enabled = false`
- **Fix**: Use socket-only mode (recommended)

### 5. Memory Leaks
- **Issue**: Expect buffer not freed until session destruction
- **Workaround**: Destroy sessions when done
- **Fix**: TODO - Free buffer after inactivity

## Development Workflow

### Adding New Features

1. **Plan**: Document in `FEATURE_NAME.md`
2. **Implement**: Follow existing patterns
3. **Test**: Manual + automated tests
4. **Document**: Update this file and relevant docs
5. **Commit**: Clear commit message

### Code Style

- **Indentation**: 4 spaces
- **Braces**: K&R style (opening brace on same line)
- **Naming**: `snake_case` for functions/variables
- **Comments**: Doxygen-style for public APIs
- **Error handling**: Check return values, log errors

### Adding New Protocol Methods

1. Add handler function in `src/socket/socket_protocol.c`:
```c
static void handle_new_method(socket_connection_t *conn, 
                              json_value_t *params, 
                              session_manager_t *mgr) {
    // Implementation
}
```

2. Add dispatch in `handle_request()`:
```c
} else if (strcmp(method, "new_method") == 0) {
    handle_new_method(conn, params, mgr);
```

3. Add client command in `src/ctl/pshellctl.c`:
```c
static int cmd_new_method(socket_client_t *client, int argc, char **argv) {
    // Implementation
}
```

4. Update help text and dispatch table

### Memory Debugging

```bash
# Build with debug symbols
make clean
CFLAGS="-g -O0" make

# Run with valgrind
valgrind --leak-check=full --show-leak-kinds=all \
         ./bin/pshell -f -c pshell.conf

# Run client commands
./bin/pshellctl create --command bash
# ... test operations ...

# Check for leaks in valgrind output
```

## Performance Tuning

### Buffer Sizes
- Raw buffer: 64KB (RAW_BUFFER_SIZE in session.c)
- Expect buffer: 64KB (same as raw buffer)
- Screen buffer: Configurable via scrollback_lines

### I/O Multiplexer
- Uses epoll on Linux
- Timeout: 100ms default
- Max events: 64

### Pattern Matching
- Linear search: O(n*m)
- Consider Boyer-Moore for large buffers

## Troubleshooting Guide

### Daemon Won't Start
1. Check if already running: `ps aux | grep pshell`
2. Check socket file: `ls -la /tmp/pshell.sock`
3. Check permissions: `stat /tmp/pshell.sock`
4. Check log: `tail -50 /tmp/pshell.log`
5. Try foreground mode: `./bin/pshell -f -c pshell.conf`

### Session Creation Fails
1. Check max_sessions limit in config
2. Check available PTYs: `ls /dev/pts/`
3. Check process limits: `ulimit -a`
4. Check daemon log for errors

### Expect Not Working
1. Verify pattern exists: `pshellctl screen <id> | grep "PATTERN"`
2. Check expect buffer seeding: Look for "Seeded expect buffer" in log
3. Enable debug logging
4. Check timeout value (too short?)
5. Verify session is alive: `pshellctl list`

### High Memory Usage
1. Check number of active sessions: `pshellctl list`
2. Check scrollback size in config
3. Check for memory leaks with valgrind
4. Destroy unused sessions

### High CPU Usage
1. Check for tight loops in pattern matching
2. Reduce polling frequency (increase sleep in expect_pattern)
3. Check for runaway processes in sessions
4. Profile with perf or gprof

## Release Checklist

- [ ] All tests pass
- [ ] No memory leaks (valgrind clean)
- [ ] Documentation updated
- [ ] Version number bumped
- [ ] Changelog updated
- [ ] Build on clean system
- [ ] Test on target platforms
- [ ] Tag release in git

## Contact & Support

- Project: pshell terminal session manager
- Location: /home/knoppix/go-dev/pshell
- Documentation: README.md, EXPECT_BUFFER_IMPLEMENTATION.md
- Issues: Check logs first, then review known issues

## Version History

- **1.0** (2026-08-02): Initial release with expect buffer feature
  - Dedicated expect buffer architecture
  - Line position tracking
  - Socket protocol implementation
  - Pattern matching with timeout
