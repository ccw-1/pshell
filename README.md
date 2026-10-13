# pshell - Portable Programmable Shell

A lightweight, portable C-based programmable shell daemon with virtual terminal capabilities, inspired by expect/tcl but designed for modern REST API-based automation.

## 📚 Key Documentation

- **[Implementation Plan](IMPLEMENTATION_PLAN.md)** - Complete technical implementation plan with architecture, phases, and specifications
- **[MCP Server](docs/MCP_SERVER.md)** - Model Context Protocol server for AI agents (auto-starts the daemon)
- **[Wiki Home](wiki/index.md)** - Complete documentation index organized by audience
- **[Maintenance Guide](docs/MAINTENANCE.md)** - Operations, monitoring, troubleshooting, and maintenance procedures

## Features

- **Multiple PTY Sessions**: Manage multiple concurrent pseudo-terminal sessions (like tmux)
- **VT100/xterm Emulation**: Full terminal emulation with screen buffer and configurable scrollback
- **Scrollback Buffer**: Configurable scrollback buffer size (default 1000 lines) with API access to retrieve terminal history
- **Expect Pattern Matching**: Dedicated expect buffer with line position tracking for reliable automation
- **REST API**: Simple HTTP/REST API for external control (no external dependencies)
- **Unix Socket Control**: Local control via Unix domain socket with `pshellctl` utility
- **Pattern Matching**: POSIX regex-based expect-like automation with dual-write architecture
- **Web Interface**: Browser-based debugging and control interface
- **MCP Server**: Model Context Protocol (stdio) server for AI agents; starts the daemon automatically
- **Privilege Separation**: Sessions can run as specific users when daemon runs as root
- **Configurable Default Shell**: Optional command parameter with configurable default shell
- **Portable**: Pure POSIX implementation - runs on Linux, macOS, BSD, Solaris, AIX, z/OS
- **Minimal Dependencies**: No external libraries required (except POSIX standard)

## Quick Start

### Build

```bash
make
```

### Install

```bash
sudo make install
```

### Start Daemon

```bash
# Start daemon
sudo pshell -c /etc/pshell/pshell.conf

# Or use systemd (Linux)
sudo systemctl start pshell

# Start in foreground (for debugging)
pshell -c /etc/pshell/pshell.conf -f
```

### Stop Daemon

```bash
# Graceful shutdown using PID file
kill $(cat /tmp/pshell.pid)

# Or find and stop by name
pkill pshell

# Or use systemd (Linux)
sudo systemctl stop pshell

# If running in foreground mode (-f)
# Press Ctrl+C to stop
```

### Control via Socket (Recommended for Local Use)

```bash
# Create session with custom scrollback buffer
pshellctl create --command bash --scrollback 5000

# Create session with output logging (captures all terminal output)
pshellctl create --command bash --output /tmp/session.log

# Create session with output logging and rotation (4 files, 1000 lines each)
pshellctl create --command bash --output /tmp/session.log --rotate 4:1000

# Create session with both scrollback and output logging
pshellctl create --command bash --scrollback 5000 --output /tmp/session.log

# List sessions
pshellctl list

# Send text with escape sequences (use single quotes!)
pshellctl send <session-id> 'ls -la\n'

# Send control characters
pshellctl send <session-id> '\cC'           # Ctrl-C
pshellctl send <session-id> '\cD'           # Ctrl-D

# Send function keys
pshellctl send <session-id> '\F1'           # F1 key
pshellctl send <session-id> '\F12'          # F12 key

# Send arrow keys
pshellctl send <session-id> '\UP'           # Up arrow
pshellctl send <session-id> '\DOWN'         # Down arrow

# Send special keys
pshellctl send <session-id> '\HOME'         # Home key
pshellctl send <session-id> '\END'          # End key
pshellctl send <session-id> '\DEL'          # Delete key

# Send Shift+keys (for text selection)
pshellctl send <session-id> '\SHIFT_UP'     # Shift+Up arrow
pshellctl send <session-id> '\SHIFT_TAB'    # Shift+Tab (backtab)
pshellctl send <session-id> '\SHIFT_F1'     # Shift+F1 (F13)

# Get scrollback buffer (retrieve terminal history)
pshellctl scrollback <session-id> --start 0 --count 50

# Expect pattern
pshellctl expect <session-id> "password:" --timeout 5000

# Attach interactively
pshellctl attach <session-id>

# Destroy session
pshellctl destroy <session-id>

# Get session info (includes total_lines count)
pshellctl info <session-id>

# Get line range from output log (1-based line numbers)
pshellctl lines <session-id> --start 1 --end 100      # Lines 1-100
pshellctl lines <session-id> --head 50                # First 50 lines
pshellctl lines <session-id> --tail 100               # Last 100 lines


```

#### Supported Escape Sequences

The `send` command supports comprehensive escape sequence processing:

**Standard Escapes:**
- `\n` - Newline
- `\r` - Carriage return
- `\t` - Tab
- `\e` - Escape key (ESC)
- `\\` - Literal backslash

**Control Characters:**
- `\cX` - Control+X (e.g., `\cC` = Ctrl-C, `\cD` = Ctrl-D)

**Function Keys:**
- `\F1` through `\F12` - Function keys F1-F12

**Arrow Keys:**
- `\UP` `\DOWN` `\LEFT` `\RIGHT` - Arrow keys

**Special Keys:**
- `\HOME` `\END` - Home and End keys
- `\INS` `\DEL` - Insert and Delete keys
- `\PGUP` `\PGDN` - Page Up and Page Down

**Shift+Keys:**
- `\SHIFT_UP` `\SHIFT_DOWN` `\SHIFT_LEFT` `\SHIFT_RIGHT` - Shift+Arrow (text selection)
- `\SHIFT_TAB` - Shift+Tab (backtab/reverse tab)
- `\SHIFT_F1` through `\SHIFT_F12` - Shift+Function keys (F13-F24)

**Numeric:**
- `\xHH` - Hexadecimal (e.g., `\x03` = Ctrl-C)
- `\NNN` - Octal (e.g., `\033` = ESC)
```

### MCP Server (AI Agents)

`bin/pshell-mcp` is a [Model Context Protocol](https://modelcontextprotocol.io) server (stdio, JSON-RPC 2.0) that exposes pshell sessions as tools. It talks to the daemon over the Unix socket and **starts the daemon automatically** if it is not running.

```bash
make mcp   # builds bin/pshell-mcp
```

MCP client configuration (opencode example):

```json
{"mcp": {"pshell": {"type": "local", "command": ["/usr/local/bin/pshell-mcp"], "enabled": true}}}
```

Exposed tools: `pshell_create_session`, `pshell_list_sessions`, `pshell_send`, `pshell_read_output`, `pshell_screen`, `pshell_scrollback`, `pshell_read_lines`, `pshell_expect`, `pshell_expect_incoming`, `pshell_resize`, `pshell_destroy_session`, `pshell_status`.

Full documentation: **[docs/MCP_SERVER.md](docs/MCP_SERVER.md)**. Test it with `python3 tests/integration/test_mcp_server.py`.

## Control via REST API

⚠️ **Security Warning**: The REST API requires authentication for production use. Generate a secure API key:

```bash
# Generate a cryptographically secure API key
pshellctl generate-key

# Add to /etc/pshell/pshell.conf:
[auth]
enabled = true
api_keys = <generated-key>

# Restart daemon to apply
sudo systemctl restart pshell
```

**Examples** (replace `mykey` with your generated key):

```bash
# Create session with custom scrollback buffer
curl -X POST http://localhost:8080/api/sessions \
  -H "X-API-Key: <your-generated-key>" \
  -H "Content-Type: application/json" \
  -d '{"command":"ssh user@host","rows":24,"cols":80,"scrollback_lines":5000}'

# Create session with output logging
curl -X POST http://localhost:8080/api/sessions \
  -H "X-API-Key: <your-generated-key>" \
  -H "Content-Type: application/json" \
  -d '{"command":"bash","rows":24,"cols":80,"output_file":"/tmp/session.log"}'

# Create session with output logging and rotation (4 files, 1000 lines each)
curl -X POST http://localhost:8080/api/sessions \
  -H "X-API-Key: <your-generated-key>" \
  -H "Content-Type: application/json" \
  -d '{"command":"bash","rows":24,"cols":80,"output_file":"/tmp/session.log","rotate_count":4,"rotate_lines":1000}'

# Create session with both scrollback and output logging
curl -X POST http://localhost:8080/api/sessions \
  -H "X-API-Key: <your-generated-key>" \
  -H "Content-Type: application/json" \
  -d '{"command":"bash","rows":24,"cols":80,"scrollback_lines":5000,"output_file":"/tmp/session.log"}'

# Send text
curl -X POST http://localhost:8080/api/sessions/<session-id>/send \
  -H "X-API-Key: <your-generated-key>" \
  -H "Content-Type: application/json" \
  -d '{"text":"ls -la\n"}'

# Get scrollback buffer
curl -X GET "http://localhost:8080/api/sessions/<session-id>/scrollback?start_line=0&count=50" \
  -H "X-API-Key: <your-generated-key>"

# Expect pattern
curl -X POST http://localhost:8080/api/sessions/<session-id>/expect \
  -H "X-API-Key: <your-generated-key>" \
  -H "Content-Type: application/json" \

# Get session info (includes total_lines count)
curl -X GET http://localhost:8080/api/sessions/<session-id> \
  -H "X-API-Key: <your-generated-key>"

# Get line range from output log (1-based line numbers)
curl -X GET "http://localhost:8080/api/sessions/<session-id>/lines?start=1&end=100" \
  -H "X-API-Key: <your-generated-key>"

# Get first 50 lines
curl -X GET "http://localhost:8080/api/sessions/<session-id>/lines?head=50" \
  -H "X-API-Key: <your-generated-key>"

# Get last 100 lines
curl -X GET "http://localhost:8080/api/sessions/<session-id>/lines?tail=100" \
  -H "X-API-Key: <your-generated-key>"


  -d '{"pattern":"password:","timeout":5000}'
```

## Architecture

```
External Clients (REST/Web/pshellctl)
         ↓
    HTTP Server + Unix Socket Server
         ↓
   Session Manager
         ↓
    PTY Manager (POSIX)
         ↓
  Terminal Emulator (VT100)
         ↓
   Pattern Matcher (POSIX regex)
```

**Detailed Architecture**: See [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) for complete technical specifications, diagrams, and implementation details.

## Documentation

### 📖 Complete Documentation Index

**[Wiki Home](wiki/index.md)** - Complete documentation organized by audience (Users, Operators, Developers, Maintainers)

### 🔒 Security Documentation

- **[SECURITY.md](docs/SECURITY.md)** - Multi-user security considerations and best practices
- **[ENVIRONMENT_VARIABLES.md](docs/ENVIRONMENT_VARIABLES.md)** - Environment variable configuration

### 🔧 Technical Documentation

- **[EXPECT_BUFFER_IMPLEMENTATION.md](EXPECT_BUFFER_IMPLEMENTATION.md)** - Expect buffer architecture and implementation details
- **[MCP_SERVER.md](docs/MCP_SERVER.md)** - MCP server for AI agents (tools, auto-start, client config)
- **[MAINTENANCE.md](MAINTENANCE.md)** - Complete maintenance guide with debugging and troubleshooting
- **[TODO.md](TODO.md)** - Known issues, future improvements, and feature roadmap

### Quick Links by Role

#### For Users
- [Getting Started](wiki/getting-started.md) - Quick start guide
- [User Guide](wiki/user-guide.md) - Complete user documentation
- [API Reference](wiki/api-reference.md) - REST API documentation
- [Socket Protocol](docs/SOCKET_PROTOCOL.md) - Unix socket protocol
- [Examples](wiki/examples.md) - Usage examples

#### For Operators
- [Installation](wiki/installation.md) - Installation guide
- [Configuration](wiki/configuration.md) - Configuration reference
- [Operations](wiki/operations.md) - Day-to-day operations
- **[Maintenance Guide](docs/MAINTENANCE.md)** - Complete maintenance procedures
- [Troubleshooting](wiki/troubleshooting.md) - Common issues

#### For Developers
- [Project Overview](wiki/overview.md) - High-level project description
- **[Implementation Plan](IMPLEMENTATION_PLAN.md)** - Complete technical plan
- [Architecture](wiki/architecture.md) - Technical architecture deep dive
- [Development Setup](wiki/development-setup.md) - Dev environment
- [Code Structure](wiki/code-structure.md) - Codebase organization
- [Contributing](wiki/contributing.md) - Contribution guidelines

#### For Maintainers
- **[Maintenance Guide](docs/MAINTENANCE.md)** - Operations and maintenance
- [Porting Guide](wiki/porting.md) - Platform porting
- [Design Decisions](wiki/design-decisions.md) - Design rationale

## Requirements

### Required (POSIX Standard)

- POSIX.1-2008 compliant OS
- C99 compiler (gcc, clang, xlc)
- POSIX threads (pthreads)
- POSIX regex (regex.h)
- POSIX sockets
- POSIX PTY (posix_openpt, grantpt, unlockpt, ptsname)

### Optional (Platform-specific)

- libutil (Linux/macOS) - for convenience functions
- zoslib (z/OS) - for ASCII/EBCDIC conversion

### No External Dependencies

- ✅ No libmicrohttpd
- ✅ No cJSON
- ✅ No libuuid
- ✅ No JWT libraries
- ✅ No WebSocket libraries
- ✅ No SSL/TLS libraries

## Supported Platforms

- Linux (all distributions)
- macOS (10.x+)
- FreeBSD/OpenBSD/NetBSD
- Solaris/illumos
- AIX
- z/OS

## Configuration

Edit `/etc/pshell/pshell.conf`:

```ini
[daemon]
pid_file = /var/run/pshell.pid
log_file = /var/log/pshell.log
log_level = info

[http]
enabled = false  # Disabled by default for security (see SECURITY.md)
port = 8080
bind_address = 127.0.0.1
max_connections = 50

[socket]
enabled = true
socket_path = /var/run/pshell.sock
socket_permissions = 0600

[auth]
enabled = true
api_keys = your-api-key-here

[sessions]
max_sessions = 50
default_rows = 24
default_cols = 80
scrollback_lines = 1000
idle_timeout = 3600

[terminal]
term_type = xterm-256color
```

**Complete Configuration Reference**: See [Maintenance Guide](docs/MAINTENANCE.md#configuration-reference)

## Control Methods

### 1. pshellctl (Unix Socket - Recommended for Local)

Fast, local control via Unix domain socket:

```bash
pshellctl create bash
pshellctl list
pshellctl send <id> "echo hello\n"
pshellctl attach <id>  # Interactive mode
```

**Advantages**:
- No authentication needed (file permissions)
- Lower latency
- Simpler for local automation

### 2. REST API (HTTP - For Remote Access)

Remote control via HTTP:

```bash
curl -H "X-API-Key: key" http://host:8080/api/sessions
```

**Advantages**:
- Remote access
- Language-agnostic
- Web-friendly

### 3. Web Interface

Browser-based control:

```
http://localhost:8080/
```

**Features**:
- Session management
- Terminal display
- Pattern matching controls
- Debug panel

## Use Cases

### 1. Automated Testing

```bash
# Create test session
SESSION=$(pshellctl create ./myapp)

# Interact
pshellctl send $SESSION 'login\n'
pshellctl expect $SESSION "Username:"
pshellctl send $SESSION 'admin\n'
pshellctl expect $SESSION "Password:"
pshellctl send $SESSION 'secret\n'
pshellctl expect $SESSION "Welcome"

# Verify
pshellctl screen $SESSION | grep "Dashboard"
```

### 2. Remote Automation

```bash
# SSH automation
SESSION=$(pshellctl create "ssh user@host")
pshellctl expect $SESSION "password:"
pshellctl send $SESSION 'mypassword\n'
pshellctl expect $SESSION "\\$ "
pshellctl send $SESSION 'uptime\n'
```

### 3. CI/CD Integration

```yaml
# .gitlab-ci.yml
test:
  script:
    - SESSION=$(pshellctl create ./myapp)
    - pshellctl expect $SESSION "Ready"
    - pshellctl send $SESSION "test\n"
    - pshellctl expect $SESSION "PASS"


## Error Handling and Exit Codes

### Expect Command Exit Codes

The `expect` and `expect-incoming` commands return exit codes for proper error handling in shell scripts:

- **Exit 0**: Pattern matched successfully
- **Exit 1**: Pattern not found (timeout or no match)

**Example with error handling:**
```bash
#!/bin/bash
SESSION=$(pshellctl create bash)

# Send command
pshellctl send $SESSION "login\n"

# Wait for prompt with error handling
if pshellctl expect $SESSION "Username:" 5000; then
    echo "Login prompt appeared"
    pshellctl send $SESSION "admin\n"
else
    echo "ERROR: Login prompt timeout" >&2
    exit 1
fi

# Check for success or error
if pshellctl expect $SESSION "Welcome" 5000; then
    echo "Login successful"
elif pshellctl expect $SESSION "Invalid" 1000; then
    echo "ERROR: Login failed" >&2
    exit 1
fi
```

### Client Disconnect Handling

The server is resilient to client disconnections during expect operations:

- **Ctrl-C on pshellctl**: Server detects disconnect and cancels the expect operation cleanly
- **Network interruption**: Server automatically cleans up and continues normally
- **No server restart needed**: The server will not get stuck waiting for disconnected clients

This ensures the server remains responsive even when clients disconnect unexpectedly during long-running expect operations.




## Line-Range API

The line-range API provides efficient access to specific ranges of output from session logs. All line numbers are **1-based** (first line = line 1).

### Features

- **Global Line Tracking**: Each session maintains a `total_lines` counter that never resets
- **File Rotation Support**: Seamlessly reads across rotated log files
- **Flexible Queries**: Support for specific ranges, head, and tail operations
- **Three Access Methods**: CLI (`pshellctl`), REST API, and Socket protocol

### CLI Usage

```bash
# Get current line count
pshellctl info <session-id>
# Returns: {..., "total_lines": 12345}

# Get specific line range (lines 100-200)
pshellctl lines <session-id> --start 100 --end 200

# Get first 50 lines
pshellctl lines <session-id> --head 50

# Get last 100 lines
pshellctl lines <session-id> --tail 100
```

### REST API

```bash
# Get session info (includes total_lines)
curl -X GET http://localhost:8080/api/sessions/<session-id> \
  -H "X-API-Key: <your-key>"

# Get line range
curl -X GET "http://localhost:8080/api/sessions/<session-id>/lines?start=100&end=200" \
  -H "X-API-Key: <your-key>"

# Get first/last N lines
curl -X GET "http://localhost:8080/api/sessions/<session-id>/lines?head=50" \
  -H "X-API-Key: <your-key>"

curl -X GET "http://localhost:8080/api/sessions/<session-id>/lines?tail=100" \
  -H "X-API-Key: <your-key>"
```

### Use Cases

**Monitor New Output:**
```bash
# Save current position
LAST_LINE=$(pshellctl info <session-id> | jq .total_lines)

# Later, get only new lines
CURRENT=$(pshellctl info <session-id> | jq .total_lines)
pshellctl lines <session-id> --start $((LAST_LINE + 1)) --end $CURRENT
```

**Pagination:**
```bash
# Get total lines
TOTAL=$(pshellctl info <session-id> | jq .total_lines)

# Page through output (100 lines per page)
PAGE_SIZE=100
for ((page=0; page<(TOTAL/PAGE_SIZE); page++)); do
    START=$((page * PAGE_SIZE + 1))
    END=$((START + PAGE_SIZE - 1))
    pshellctl lines <session-id> --start $START --end $END
done
```

**Log Analysis:**
```bash
# Get last 1000 lines for analysis
pshellctl lines <session-id> --tail 1000 | grep ERROR

# Get specific error context (lines 5000-5100)
pshellctl lines <session-id> --start 5000 --end 5100
```


```

### 4. Batch Operations

```bash
# Execute on multiple hosts
for host in host1 host2 host3; do
  SESSION=$(pshellctl create "ssh admin@$host")
  pshellctl expect $SESSION "password:"
  pshellctl send $SESSION "$PASSWORD"'\n'
  pshellctl expect $SESSION "\\$ "
  pshellctl send $SESSION 'uptime\n'
  pshellctl screen $SESSION
done
```

## Python Client Example

```python
import requests

# Create session
resp = requests.post('http://localhost:8080/api/sessions',
    headers={'X-API-Key': '<your-generated-key>'},
    json={'command': 'ssh user@host', 'rows': 24, 'cols': 80})
session_id = resp.json()['id']

# Wait for password prompt
resp = requests.post(f'http://localhost:8080/api/sessions/{session_id}/expect',
    headers={'X-API-Key': '<your-generated-key>'},
    json={'pattern': 'password:', 'timeout': 5000})

# Send password
requests.post(f'http://localhost:8080/api/sessions/{session_id}/send',
    headers={'X-API-Key': '<your-generated-key>'},
    json={'text': 'mypassword\n'})
```

## Comparison with Similar Tools

| Feature | pshell | expect | tmux | screen |
|---------|--------|--------|------|--------|
| REST API | ✅ | ❌ | ❌ | ❌ |
| Unix Socket Control | ✅ | ❌ | ✅ | ❌ |
| Web Interface | ✅ | ❌ | ❌ | ❌ |
| Pattern Matching | ✅ | ✅ | ❌ | ❌ |
| Multiple Sessions | ✅ | ❌ | ✅ | ✅ |
| POSIX Portable | ✅ | ✅ | ✅ | ✅ |
| No Dependencies | ✅ | ❌ | ❌ | ❌ |
| Scriptable | ✅ | ✅ | ✅ | ✅ |

## Development

### Build from Source

```bash
git clone https://github.com/ccw-1/pshell.git
cd pshell
make
```

### Run Tests

```bash
make test
```

### Development Roadmap

See **[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)** for detailed 14-week development plan:

1. Core Infrastructure (Weeks 1-2)
2. HTTP Server (Weeks 3-4)
3. Unix Socket Server (Week 5)
4. Session Management (Week 6)
5. Terminal Emulation (Weeks 7-8)
6. REST API (Week 9)
7. Pattern Matching (Week 10)
8. Web Interface (Week 11)
9. CLI Client (Week 12)
10. Testing (Week 13)
11. Documentation (Week 14)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines.

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Support

- **Issues**: https://github.com/ccw-1/pshell/issues
- **Documentation**: [Wiki Home](wiki/index.md)
- **Mailing List**: pshell-users@example.com

## Acknowledgments

Inspired by:
- **expect** - Don Libes
- **tmux** - Nicholas Marriott
- **screen** - GNU Project

## Status

**Current Status**: Phase 10 Complete - Expect Buffer Feature Implemented ✅

**Completed Phases**:
- ✅ Phase 1-4: Core Infrastructure, HTTP Server, Unix Socket, Session Management
- ✅ Phase 5: Terminal Emulation (VT100 parser, screen buffer)
- ✅ Phase 6: REST API (all endpoints functional)
- ✅ Phase 7: Pattern Matching (expect engine, timeout manager)
- ✅ Phase 8: Web Interface (HTML/CSS/JS frontend)
- ✅ Phase 9: HTTP CLI Client (pshell-client)
- ✅ Phase 10: Socket Control Utility (pshellctl)
- ✅ **Expect Buffer Feature**: Dedicated buffer with dual-write architecture and line position tracking

**Recent Updates**:
- ✅ Expect now runs asynchronously on a worker thread, so output produced while waiting is matched (previously the daemon's event loop stalled and the call timed out)
- ✅ Fixed output logging / line-range API: per-file line counter is now maintained without rotation, and cumulative line counters reset per session
- ✅ Fixed `pshellctl lines --tail` SIGPIPE (reconnects between daemon round-trips)
- ✅ Added `pshell-mcp`, a Model Context Protocol server for AI agents that auto-starts the daemon (see [docs/MCP_SERVER.md](docs/MCP_SERVER.md))
- ✅ Implemented dedicated expect buffer to prevent data loss
- ✅ Added dual-write system (raw buffer + expect buffer)
- ✅ Implemented line position tracking (relative to cursor)
- ✅ Fixed pattern matching reliability
- ✅ Added comprehensive documentation (EXPECT_BUFFER_IMPLEMENTATION.md, MAINTENANCE.md, TODO.md)

**Remaining Work**:
- ⏳ Phase 11: Additional Testing & Integration Tests
- ⏳ Phase 12: Final Documentation & Examples
- ⏳ Known Issues: See [TODO.md](TODO.md) for improvement roadmap

**Build Status**: All binaries compile and run successfully ✅
- `bin/pshell` - Main daemon (fully functional with expect buffer)
- `bin/pshell-client` - HTTP CLI client (complete)
- `bin/pshellctl` - Socket control utility (complete with line position display)
- `bin/pshell-mcp` - MCP server for AI agents (complete; auto-starts the daemon)

See **[PROGRESS.md](PROGRESS.md)** for detailed implementation status.

---

**Version**: 1.1.0 (MCP server + async expect)  
**Last Updated**: 2026-10-10

See [CHANGELOG.md](CHANGELOG.md) for release history and [VERSION](VERSION) for the current version.