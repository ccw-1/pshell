# Architecture

## Overview

pshell is built on a modular, layered architecture designed for portability and maintainability. The system is divided into distinct layers, each with clear responsibilities.

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     External Clients                         │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │ REST Client  │  │ Web Browser  │  │ pshellctl    │      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
└─────────┼──────────────────┼──────────────────┼─────────────┘
          │                  │                  │
          │ HTTP             │ HTTP             │ Unix Socket
          │                  │                  │
┌─────────▼──────────────────▼──────────────────▼─────────────┐
│                    pshell Daemon                             │
│  ┌────────────────────────────────────────────────────────┐ │
│  │         Simple HTTP Server (custom, no deps)           │ │
│  │  ┌──────────────┐  ┌──────────────┐  ┌─────────────┐ │ │
│  │  │ REST API     │  │ Long Polling │  │ Static Web  │ │ │
│  │  │ Handler      │  │ (events)     │  │ Files       │ │ │
│  │  └──────┬───────┘  └──────┬───────┘  └─────────────┘ │ │
│  └─────────┼──────────────────┼────────────────────────────┘ │
│            │                  │                              │
│  ┌─────────▼──────────────────▼────────────────────────────┐ │
│  │      Unix Domain Socket Server                         │ │
│  │  - /var/run/pshell.sock                                │ │
│  │  - Same API as HTTP (JSON protocol)                   │ │
│  │  - No authentication (file permissions)               │ │
│  └─────────┬──────────────────────────────────────────────┘ │
│            │                                                  │
│  ┌─────────▼──────────────────────────────────────────────┐ │
│  │            Session Manager                              │ │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐             │ │
│  │  │ Session 1│  │ Session 2│  │ Session N│             │ │
│  │  └────┬─────┘  └────┬─────┘  └────┬─────┘             │ │
│  └───────┼─────────────┼─────────────┼───────────────────┘ │
│          │             │             │                      │
│  ┌───────▼─────────────▼─────────────▼───────────────────┐ │
│  │              PTY Manager (POSIX)                       │ │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐            │ │
│  │  │  PTY 1   │  │  PTY 2   │  │  PTY N   │            │ │
│  │  └────┬─────┘  └────┬─────┘  └────┬─────┘            │ │
│  └───────┼─────────────┼─────────────┼────────────────────┘ │
│          │             │             │                      │
│  ┌───────▼─────────────▼─────────────▼───────────────────┐ │
│  │         Terminal Emulator (VT100/xterm)               │ │
│  │  - Screen buffer (lines × cols)                       │ │
│  │  - Cursor position tracking                           │ │
│  │  - Escape sequence parsing                            │ │
│  │  - Scrollback buffer                                  │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │         Pattern Matcher (POSIX regex)                 │ │
│  │  - Regex pattern matching                             │ │
│  │  - Expect-like automation                             │ │
│  │  - Timeout handling (select/poll)                     │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │              Simple Authentication                     │ │
│  │  - API key validation (HTTP only)                     │ │
│  │  - Socket: file permissions                           │ │
│  └────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────┘
          │             │             │
          ▼             ▼             ▼
    ┌─────────┐   ┌─────────┐   ┌─────────┐
    │ Process │   │ Process │   │ Process │
    │   (ssh) │   │  (bash) │   │  (vim)  │
    └─────────┘   └─────────┘   └─────────┘
```

## Layer Descriptions

### 1. Client Layer

**Purpose**: External interfaces for controlling pshell

**Components**:
- **REST Clients**: HTTP-based clients (curl, Python, etc.)
- **Web Browser**: JavaScript-based web interface
- **pshellctl**: Command-line socket control utility

**Protocols**:
- HTTP/REST for remote access
- Unix domain socket for local control

### 2. Network Layer

**Purpose**: Handle network communication

**Components**:
- **HTTP Server**: Custom implementation, no external dependencies
  - Request parsing
  - Response building
  - Static file serving
  - Long polling for events
- **Unix Socket Server**: Local IPC
  - JSON-based protocol
  - File permission-based security

**Design Decisions**:
- No external HTTP libraries (libmicrohttpd, etc.)
- Simple, single-threaded design
- Select-based I/O multiplexing

### 3. API Layer

**Purpose**: Provide programmatic interface

**Components**:
- **REST API Handler**: Process HTTP requests
- **Socket Protocol Handler**: Process socket requests
- **Authentication**: API key validation (HTTP only)

**Endpoints**:
- Session management (create, list, destroy)
- Session control (send, expect, resize)
- System status and control

### 4. Session Management Layer

**Purpose**: Manage multiple concurrent sessions

**Components**:
- **Session Manager**: Lifecycle management
- **Session List**: Active session tracking
- **Resource Cleanup**: Automatic cleanup on exit

**Features**:
- Multiple concurrent sessions
- Session isolation
- Resource limits
- Idle timeout

### 5. PTY Layer

**Purpose**: Pseudo-terminal management

**Components**:
- **PTY Manager**: POSIX PTY operations
  - Create/destroy PTY pairs
  - Resize terminals
  - Read/write operations
- **Process Spawner**: Fork/exec in PTY
  - Command parsing
  - Environment setup
  - Process monitoring

**Implementation**: ✅ **COMPLETE** (Phase 1)
- `src/pty/pty_posix.c` - PTY operations
- `src/pty/process_spawn.c` - Process spawning
- `src/pty/io_multiplexer.c` - Event loop

### 6. Terminal Emulation Layer

**Purpose**: VT100/xterm terminal emulation

**Components**:
- **Escape Parser**: ANSI escape sequence parsing
- **Screen Buffer**: Terminal state (rows × cols)
- **Scrollback Buffer**: History buffer
- **Cursor Tracker**: Cursor position management

**Features**:
- VT100 compatibility
- xterm extensions
- Color support (256 colors)
- Unicode support

### 7. Pattern Matching Layer

**Purpose**: Expect-like automation

**Components**:
- **Pattern Matcher**: POSIX regex matching
- **Expect Engine**: Wait for patterns
- **Timeout Manager**: Handle timeouts

**Features**:
- Regular expression matching
- Multiple pattern matching
- Timeout support
- Callback-based API

### 8. Utility Layer

**Purpose**: Common utilities and helpers

**Components**: ✅ **COMPLETE** (Phase 1)
- **Logger**: Thread-safe logging (syslog + file)
- **Config Parser**: INI-style configuration
- **Buffer**: Circular buffer for I/O
- **JSON**: Simple JSON parser/builder
- **UUID**: UUID v4 generator
- **String Utils**: String manipulation

**Implementation**:
- `src/utils/logger.c` - Logging
- `src/utils/buffer.c` - Circular buffer
- `src/utils/json_simple.c` - JSON
- `src/utils/uuid_simple.c` - UUID
- `src/utils/string_utils.c` - String utilities
- `src/daemon/config.c` - Configuration

## Data Flow

### Session Creation Flow

```
1. Client → HTTP/Socket Request
2. API Handler → Validate request
3. Session Manager → Create session
4. PTY Manager → Create PTY pair
5. Process Spawner → Fork/exec command
6. Session Manager → Register session
7. API Handler → Return session ID
8. Client ← Response with session ID
```

### Data I/O Flow

```
1. Client → Send text to session
2. API Handler → Validate session
3. Session Manager → Find session
4. PTY Manager → Write to PTY master
5. Process ← Receives input on stdin
6. Process → Writes output to stdout/stderr
7. PTY Manager → Read from PTY master
8. Terminal Emulator → Parse escape sequences
9. Screen Buffer → Update state
10. Event System → Notify listeners
11. Client ← Long poll returns output
```

### Pattern Matching Flow

```
1. Client → Expect pattern request
2. API Handler → Validate request
3. Pattern Matcher → Register pattern
4. Event Loop → Monitor PTY output
5. Terminal Emulator → Parse output
6. Pattern Matcher → Check pattern
7. Match Found → Trigger callback
8. API Handler → Return match result
9. Client ← Response with match data
```

## Threading Model

**Single-threaded design** with select-based I/O multiplexing:

- Main thread runs event loop
- All I/O is non-blocking
- No thread synchronization needed
- Simpler, more portable

**Advantages**:
- No race conditions
- No deadlocks
- Easier to debug
- More portable

**Trade-offs**:
- Limited to single CPU core
- Blocking operations must be avoided
- Sufficient for typical workloads (50+ sessions)

## Security Model

### HTTP API Security

- **API Key Authentication**: Simple key-based auth
- **No JWT**: Simpler implementation
- **HTTPS**: Not implemented (use reverse proxy)

### Unix Socket Security

- **File Permissions**: 0600 (owner only)
- **No Authentication**: Trust file system
- **Local Only**: Cannot be accessed remotely

### Process Isolation

- **Separate PTYs**: Each session isolated
- **Process Groups**: Clean termination
- **Resource Limits**: Configurable limits

## Configuration

Configuration is loaded from INI-style file:

```ini
[daemon]
pid_file = /var/run/pshell.pid
log_file = /var/log/pshell.log
log_level = info

[http]
port = 8080
bind_address = 127.0.0.1

[socket]
enabled = true
socket_path = /var/run/pshell.sock

[sessions]
max_sessions = 50
default_rows = 24
default_cols = 80
```

## Error Handling

- **Logging**: All errors logged
- **Graceful Degradation**: Continue on non-fatal errors
- **Resource Cleanup**: Automatic cleanup on errors
- **Client Feedback**: Clear error messages

## Performance Considerations

- **Non-blocking I/O**: All I/O operations non-blocking
- **Efficient Buffers**: Circular buffers for I/O
- **Minimal Copying**: Zero-copy where possible
- **Event-driven**: React to events, don't poll

## Portability

**POSIX-only dependencies**:
- No external libraries
- Standard C99
- POSIX.1-2008 APIs
- Platform-specific code isolated

**Tested Platforms**:
- ✅ Linux (x86_64)
- ⏳ macOS
- ⏳ FreeBSD
- ⏳ Solaris
- ⏳ AIX
- ⏳ z/OS

## Implementation Status

### ✅ Phase 1: Core Infrastructure (COMPLETE)
- PTY operations
- Process spawning
- I/O multiplexing
- Configuration
- Logging
- Utilities

### 🚧 Phase 2: HTTP Server (IN PROGRESS)
- HTTP server
- Request parsing
- Response building
- Static files
- Authentication

### ⏳ Remaining Phases
- Phase 3: Unix Socket Server
- Phase 4: Session Management
- Phase 5: Terminal Emulation
- Phase 6: REST API
- Phase 7: Pattern Matching
- Phase 8: Web Interface
- Phase 9: CLI Client
- Phase 10: Testing
- Phase 11: Documentation

See [PROGRESS.md](../PROGRESS.md) for detailed status.
