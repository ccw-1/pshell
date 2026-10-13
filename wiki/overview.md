# pshell Project Overview

## What is pshell?

**pshell** (Programmable Shell) is a lightweight, portable daemon that provides programmatic control over interactive terminal applications through a REST API. Think of it as "expect/tcl meets tmux with a REST API" - it combines the automation capabilities of expect with the session management of tmux, all controllable via HTTP.

## The Problem

Interactive command-line applications (SSH, telnet, FTP, database CLIs, etc.) are difficult to automate because they:
- Require human interaction (prompts, passwords, confirmations)
- Don't have APIs
- Need pattern matching to detect state
- Require terminal emulation for proper display

Traditional solutions:
- **expect/tcl**: Powerful but requires learning Tcl, not REST-friendly
- **pexpect (Python)**: Language-specific, not a daemon
- **tmux/screen**: Great for multiplexing but no automation or API

## The Solution

pshell provides:

1. **REST API**: Control terminal sessions via HTTP from any language
2. **Pattern Matching**: Wait for specific output patterns (like expect)
3. **Session Management**: Multiple concurrent sessions (like tmux)
4. **Terminal Emulation**: Full VT100/xterm support
5. **Web Interface**: Browser-based debugging and control
6. **Portability**: Pure POSIX - runs everywhere

## Core Concepts

### Sessions

A **session** is a pseudo-terminal (PTY) running a command:

```
Session = PTY + Process + Terminal Emulator + Pattern Matcher
```

Each session:
- Has a unique ID (UUID)
- Runs one command (e.g., `ssh user@host`)
- Maintains screen state (24x80 by default)
- Has a scrollback buffer
- Can have registered patterns

### Pattern Matching

**Pattern matching** allows you to wait for specific output:

```json
{
  "pattern": "password:",
  "timeout": 5000
}
```

When the pattern matches, you get:
- The matched text
- Position in buffer

This enables expect-like automation:

```
1. spawn ssh user@host
2. expect "password:"
3. send "mypassword\n"
4. expect "$ "
5. send "ls -la\n"
```

### Terminal Emulation

**Terminal emulation** provides:
- Screen buffer (rows × columns)
- Cursor position tracking
- ANSI escape sequence parsing
- Scrollback buffer
- Title tracking

This allows you to:
- Get current screen state
- See what the user would see
- Capture output properly

### REST API

**REST API** provides HTTP endpoints:

```
POST   /api/sessions              # Create session
POST   /api/sessions/{id}/send    # Send text
POST   /api/sessions/{id}/expect  # Wait for pattern
GET    /api/sessions/{id}/screen  # Get screen state
```

All operations return JSON responses.

## Architecture

```
┌─────────────────────────────────────────┐
│         External Clients                │
│  (Python, Bash, curl, Web Browser)     │
└─────────────┬───────────────────────────┘
              │ HTTP/REST
              ▼
┌─────────────────────────────────────────┐
│         pshell Daemon                   │
│  ┌───────────────────────────────────┐ │
│  │  HTTP Server (custom, no deps)    │ │
│  └───────────────┬───────────────────┘ │
│                  │                      │
│  ┌───────────────▼───────────────────┐ │
│  │     Session Manager               │ │
│  │  ┌─────────┐  ┌─────────┐        │ │
│  │  │Session 1│  │Session 2│  ...   │ │
│  │  └────┬────┘  └────┬────┘        │ │
│  └───────┼────────────┼─────────────┘ │
│          │            │                │
│  ┌───────▼────────────▼─────────────┐ │
│  │   PTY Manager (POSIX)            │ │
│  └───────┬────────────┬─────────────┘ │
│          │            │                │
│  ┌───────▼────────────▼─────────────┐ │
│  │  Terminal Emulator (VT100)       │ │
│  └───────┬────────────┬─────────────┘ │
│          │            │                │
│  ┌───────▼────────────▼─────────────┐ │
│  │  Pattern Matcher (POSIX regex)   │ │
│  └──────────────────────────────────┘ │
└─────────────────────────────────────────┘
              │
              ▼
┌─────────────────────────────────────────┐
│      Child Processes                    │
│  (ssh, bash, vim, etc.)                 │
└─────────────────────────────────────────┘
```

## Design Principles

### 1. Portability First

- **Pure POSIX**: No platform-specific code
- **Standard APIs**: posix_openpt, regex.h, sockets
- **Minimal Dependencies**: No external libraries
- **Tested Platforms**: Linux (verified); macOS, BSD, Solaris, AIX, z/OS (planned)

### 2. Simplicity

- **Simple HTTP**: Custom server, no libmicrohttpd
- **Simple JSON**: Custom parser, no cJSON
- **Simple Auth**: API keys, no JWT
- **Simple Config**: INI format, no YAML/TOML

### 3. Reliability

- **No Memory Leaks**: Careful resource management
- **Clean Shutdown**: Proper cleanup on exit
- **Error Handling**: Graceful degradation
- **Signal Safety**: Proper signal handling

### 4. Performance

- **Efficient I/O**: select/poll-based multiplexing
- **Low Latency**: <100ms API response time
- **Scalable**: 50+ concurrent sessions
- **Small Footprint**: ~15MB for 50 sessions

## Use Cases

### 1. Automated Testing

Test interactive CLI applications:

```python
# Create session
session = client.create_session("myapp")

# Interact
client.send(session, "login\n")
client.expect(session, "Username:")
client.send(session, "admin\n")
client.expect(session, "Password:")
client.send(session, "secret\n")
client.expect(session, "Welcome")

# Verify
screen = client.get_screen(session)
assert "Dashboard" in screen
```

### 2. Remote Automation

Automate SSH sessions:

```bash
# Create SSH session
SESSION=$(curl -X POST http://localhost:8080/api/sessions \
  -H "X-API-Key: mykey" \
  -d '{"command":"ssh user@host"}' | jq -r .id)

# Wait for password
curl -X POST http://localhost:8080/api/sessions/$SESSION/expect \
  -H "X-API-Key: mykey" \
  -d '{"pattern":"password:","timeout":5000}'

# Send password
curl -X POST http://localhost:8080/api/sessions/$SESSION/send \
  -H "X-API-Key: mykey" \
  -d '{"text":"mypassword\n"}'
```

### 3. CI/CD Integration

Integrate with build pipelines:

```yaml
# .gitlab-ci.yml
test:
  script:
    - pshell-client create --command "./myapp"
    - pshell-client expect $SESSION_ID --pattern "Ready"
    - pshell-client send $SESSION_ID "test\n"
    - pshell-client expect $SESSION_ID --pattern "PASS"
```

### 4. System Administration

Batch operations on multiple systems:

```python
# Create sessions for multiple hosts
sessions = []
for host in hosts:
    session = client.create_session(f"ssh admin@{host}")
    sessions.append(session)

# Execute command on all
for session in sessions:
    client.expect(session, "password:")
    client.send(session, f"{password}\n")
    client.expect(session, "$ ")
    client.send(session, "uptime\n")
    
# Collect results
for session in sessions:
    screen = client.get_screen(session)
    print(screen)
```

### 5. Interactive Debugging

Debug applications with pattern matching:

```python
# Start application
session = client.create_session("./myapp --debug")

# Set breakpoint pattern
client.expect(session, "Breakpoint hit")

# When hit, inspect state
screen = client.get_screen(session)
buffer = client.get_buffer(session)

# Continue execution
client.send(session, "continue\n")
```

## Comparison with Alternatives

| Feature | pshell | expect | pexpect | tmux | screen |
|---------|--------|--------|---------|------|--------|
| REST API | ✅ | ❌ | ❌ | ❌ | ❌ |
| Web Interface | ✅ | ❌ | ❌ | ❌ | ❌ |
| Pattern Matching | ✅ | ✅ | ✅ | ❌ | ❌ |
| Multiple Sessions | ✅ | ❌ | ❌ | ✅ | ✅ |
| Language Agnostic | ✅ | ❌ | ❌ | ✅ | ✅ |
| Daemon Mode | ✅ | ❌ | ❌ | ✅ | ✅ |
| No Dependencies | ✅ | ❌ | ❌ | ❌ | ❌ |
| POSIX Portable | ✅ | ✅ | ❌ | ✅ | ✅ |

### When to Use pshell

Use pshell when you need:
- REST API control of terminal sessions
- Language-agnostic automation
- Multiple concurrent sessions
- Web-based debugging
- Portable solution (multiple platforms)

### When to Use Alternatives

Use **expect** when:
- You're comfortable with Tcl
- You need a mature, battle-tested solution
- You don't need REST API

Use **pexpect** when:
- You're working in Python only
- You don't need a daemon
- You want a simple library

Use **tmux/screen** when:
- You need terminal multiplexing for humans
- You don't need automation
- You want keyboard-driven interface

## Technology Stack

### Core Technologies

- **Language**: C99
- **Standards**: POSIX.1-2008
- **Threading**: POSIX threads (pthreads)
- **Networking**: POSIX sockets
- **PTY**: POSIX pseudo-terminals
- **Regex**: POSIX extended regex

### No External Dependencies

- HTTP server: Custom implementation
- JSON parser: Custom implementation
- UUID generation: Custom implementation
- Authentication: Simple API keys

### Optional Dependencies

- **libutil** (Linux/macOS): Convenience functions
- **zoslib** (z/OS): ASCII/EBCDIC conversion

## Project Status

**Current Phase**: Phase 10 Complete (v1.1.0)

All phases through 10 are implemented: core infrastructure, HTTP server, Unix
socket server, session management, terminal emulation, REST API, pattern
matching, web interface, CLI clients (pshell-client + pshellctl), and testing
(unit + integration tests via `make test` / `make check`).

See [Progress](../PROGRESS.md) for detailed implementation status and
[Implementation Plan](../IMPLEMENTATION_PLAN.md) for the original roadmap.

### Completed Features (summary)

**Phase 1-4**:
- Core infrastructure (PTY, process spawning, event loop, config, logging)
- HTTP server and REST API foundation
- Unix socket server and JSON protocol
- Session management

**Phase 5-7**:
- Terminal emulation (VT100/xterm, screen buffer, scrollback)
- REST API integration (send, output, screen, resize, expect)
- Pattern matching (POSIX regex, expect engine)

**Phase 8-10**:
- Web interface (browser-based debugging)
- CLI clients (pshell-client, pshellctl, pshell-mcp)
- Testing (unit + integration tests)

Remaining: Phase 11 (Documentation & Examples) and the improvement roadmap in
[TODO](../TODO.md).

## Getting Involved

### For Users

- Try the [README](../README.md) quick start
- Read the [Escape Sequences](../docs/ESCAPE_SEQUENCES.md) reference
- Explore the [Scrollback API](../docs/API_SCROLLBACK.md)

### For Developers

- Review [Architecture](architecture.md)
- Read [Progress](../PROGRESS.md) and [TODO](../TODO.md)
- Open an issue or pull request on GitHub

### For Operators

- Read the [Maintenance Guide](../docs/MAINTENANCE.md)
- Configure using the configuration reference in the Maintenance Guide
- Review [Security](../docs/SECURITY.md) notes

## Community

- **GitHub**: https://github.com/ccw-1/pshell
- **Issues**: https://github.com/ccw-1/pshell/issues

## License

License: TBD (no license file is present yet).

---

**Last Updated**: 2026-10-13  
**Version**: 1.1.0
