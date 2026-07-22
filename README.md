# pshell - Portable Programmable Shell

A lightweight, portable C-based programmable shell daemon with virtual terminal capabilities, inspired by expect/tcl but designed for modern REST API-based automation.

## 📚 Key Documentation

- **[Implementation Plan](IMPLEMENTATION_PLAN.md)** - Complete technical implementation plan with architecture, phases, and specifications
- **[Wiki Home](wiki/index.md)** - Complete documentation index organized by audience
- **[Maintenance Guide](docs/MAINTENANCE.md)** - Operations, monitoring, troubleshooting, and maintenance procedures

## Features

- **Multiple PTY Sessions**: Manage multiple concurrent pseudo-terminal sessions (like tmux)
- **VT100/xterm Emulation**: Full terminal emulation with screen buffer and configurable scrollback
- **Scrollback Buffer**: Configurable scrollback buffer size (default 1000 lines) with API access to retrieve terminal history
- **REST API**: Simple HTTP/REST API for external control (no external dependencies)
- **Unix Socket Control**: Local control via Unix domain socket with `pshellctl` utility
- **Pattern Matching**: POSIX regex-based expect-like automation
- **Web Interface**: Browser-based debugging and control interface
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

### Control via REST API

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
git clone https://github.com/yourusername/pshell.git
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

- **Issues**: https://github.com/yourusername/pshell/issues
- **Documentation**: [Wiki Home](wiki/index.md)
- **Mailing List**: pshell-users@example.com

## Acknowledgments

Inspired by:
- **expect** - Don Libes
- **tmux** - Nicholas Marriott
- **screen** - GNU Project

## Status

**Current Status**: Phase 9 Complete - Core Implementation Done ✅

**Completed Phases**:
- ✅ Phase 1-4: Core Infrastructure, HTTP Server, Unix Socket, Session Management
- ✅ Phase 5: Terminal Emulation (VT100 parser, screen buffer)
- ✅ Phase 6: REST API (all endpoints functional)
- ✅ Phase 7: Pattern Matching (expect engine, timeout manager)
- ✅ Phase 8: Web Interface (HTML/CSS/JS frontend)
- ✅ Phase 9: HTTP CLI Client (pshell-client)
- ✅ Phase 10: Socket Control Utility (pshellctl)

**Remaining Phases**:
- ⏳ Phase 11: Additional Testing & Integration Tests
- ⏳ Phase 12: Final Documentation & Examples

**Build Status**: All binaries compile and run successfully
- `bin/pshell` - Main daemon (fully functional)
- `bin/pshell-client` - HTTP CLI client (complete)
- `bin/pshellctl` - Socket control utility (complete)

See **[PROGRESS.md](PROGRESS.md)** for detailed implementation status.

---

**Version**: 0.9.0 (beta)  
**Last Updated**: 2026-07-17