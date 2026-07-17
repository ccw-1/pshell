# pshell - Portable Programmable Shell with REST API

## Project Overview

**pshell** is a lightweight, portable C-based programmable shell daemon with virtual terminal capabilities, inspired by expect/tcl but designed for modern REST API-based automation. Built on POSIX standards for maximum portability.

**Design Principles:**
- Minimal dependencies (POSIX only)
- Maximum portability (Linux, macOS, BSD, z/OS, Solaris, AIX)
- Simple HTTP server (no external libraries)
- Unix domain socket for local control
- Basic authentication (API keys, no JWT)
- Clean, maintainable code

## Core Features

- Multiple concurrent PTY sessions (like tmux)
- VT100/xterm terminal emulation
- Simple HTTP/REST API for external control
- Unix domain socket for local control
- Control utility program (pshellctl)
- Web-based debugging interface (static HTML/JS)
- Pattern matching and automation (POSIX regex)
- Session management

## Architecture Design

### High-Level Architecture

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

### Directory Structure

```
pshell/
├── src/
│   ├── daemon/
│   │   ├── main.c                    # Daemon entry point
│   │   ├── config.c/h                # Configuration (simple key=value)
│   │   ├── signal_handler.c/h        # Signal handling
│   │   └── pid_file.c/h              # PID file management
│   ├── http/
│   │   ├── http_server.c/h           # Simple HTTP server (custom)
│   │   ├── http_parser.c/h           # HTTP request parser
│   │   ├── http_response.c/h         # HTTP response builder
│   │   ├── rest_api.c/h              # REST API handlers
│   │   ├── long_poll.c/h             # Long polling for events
│   │   └── static_files.c/h          # Static file serving
│   ├── socket/
│   │   ├── socket_server.c/h         # Unix domain socket server
│   │   ├── socket_protocol.c/h       # Socket protocol (JSON-based)
│   │   └── socket_client.c/h         # Socket client library
│   ├── session/
│   │   ├── session_manager.c/h       # Session lifecycle
│   │   ├── session.c/h               # Individual session
│   │   └── session_list.c/h          # Session list management
│   ├── pty/
│   │   ├── pty_posix.c/h             # POSIX PTY operations
│   │   ├── process_spawn.c/h         # Process spawning
│   │   └── io_multiplexer.c/h        # select/poll event loop
│   ├── terminal/
│   │   ├── vt100_emulator.c/h        # VT100 emulation
│   │   ├── screen_buffer.c/h         # Screen state
│   │   ├── escape_parser.c/h         # ANSI escape sequences
│   │   └── scrollback.c/h            # Scrollback buffer
│   ├── pattern/
│   │   ├── pattern_matcher.c/h       # POSIX regex matching
│   │   ├── expect_engine.c/h         # Expect-like automation
│   │   └── timeout_manager.c/h       # Timeout handling
│   ├── utils/
│   │   ├── logger.c/h                # Logging (syslog + file)
│   │   ├── buffer.c/h                # Circular buffer
│   │   ├── json_simple.c/h           # Simple JSON (no deps)
│   │   ├── uuid_simple.c/h           # Simple UUID generation
│   │   └── string_utils.c/h          # String utilities
│   ├── cli/
│   │   ├── pshell_client.c           # HTTP CLI client tool
│   │   └── commands.c/h              # Client commands
│   └── ctl/
│       ├── pshellctl.c               # Socket control utility
│       └── ctl_commands.c/h          # Control commands
├── web/
│   ├── index.html                    # Main web interface
│   ├── terminal.html                 # Terminal view
│   ├── css/
│   │   └── style.css                 # Styling
│   └── js/
│       ├── app.js                    # Main application
│       ├── terminal.js               # Terminal rendering (vanilla JS)
│       ├── api.js                    # REST API client
│       └── poll.js                   # Long polling client
├── include/
│   └── pshell/                       # Public headers
├── tests/
│   ├── unit/
│   │   ├── test_session.c
│   │   ├── test_vt100.c
│   │   ├── test_pattern.c
│   │   ├── test_http.c
│   │   ├── test_socket.c
│   │   └── test_json.c
│   └── integration/
│       ├── test_ssh_automation.sh
│       ├── test_multi_session.sh
│       ├── test_socket_control.sh
│       └── test_web_interface.sh
├── examples/
│   ├── python/
│   │   ├── ssh_login.py              # Python REST client
│   │   └── batch_commands.py
│   ├── bash/
│   │   ├── create_session.sh
│   │   ├── send_keys.sh
│   │   └── socket_control.sh         # Socket control examples
│   └── curl/
│       └── api_examples.sh           # curl examples
├── scripts/
│   ├── build.sh                      # Build script
│   ├── run_tests.sh                  # Test runner
│   ├── start_daemon.sh               # Start daemon
│   └── stop_daemon.sh                # Stop daemon
├── docs/
│   ├── API_REFERENCE.md              # REST API documentation
│   ├── SOCKET_PROTOCOL.md            # Socket protocol documentation
│   ├── WEB_INTERFACE.md              # Web UI guide
│   ├── ARCHITECTURE.md               # Architecture details
│   ├── TERMINAL_EMULATION.md         # VT100 implementation
│   ├── PORTING.md                    # Porting guide
│   ├── MAINTENANCE.md                # Maintenance guide
│   ├── TROUBLESHOOTING.md            # Common issues
│   ├── DEPLOYMENT.md                 # Deployment guide
│   └── EXAMPLES.md                   # Usage examples
├── wiki/
│   ├── index.md                      # Wiki index
│   ├── overview.md                   # Project overview
│   ├── architecture.md               # Architecture deep dive
│   └── ...                           # Other wiki pages
├── config/
│   └── pshell.conf                   # Default configuration
├── Makefile
├── README.md
├── CONTRIBUTING.md
├── CHANGELOG.md
├── LICENSE
└── .gitignore
```

## Technical Specifications

### 1. Unix Domain Socket Server

**Purpose**: Provide local control interface without HTTP overhead

**Implementation**:
```c
// socket_server.c
typedef struct {
    int listen_fd;              // Listening socket
    char socket_path[256];      // Socket file path
    session_manager_t *sess_mgr; // Session manager reference
} socket_server_t;

int socket_server_init(socket_server_t *server, const char *path) {
    // Create Unix domain socket
    server->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    
    // Bind to path
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    
    // Remove old socket file
    unlink(path);
    
    bind(server->listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server->listen_fd, 10);
    
    // Set permissions (0600 - owner only)
    chmod(path, 0600);
    
    return 0;
}
```

**Socket Protocol**:
- JSON-based (same as HTTP API)
- Request/response format
- No authentication (file permissions)

**Example Protocol**:
```json
// Request
{
  "method": "create_session",
  "params": {
    "command": "ssh user@host",
    "rows": 24,
    "cols": 80
  }
}

// Response
{
  "status": "success",
  "result": {
    "id": "550e8400-e29b-41d4-a716-446655440000",
    "pid": 12345
  }
}
```

### 2. pshellctl Control Utility

**Purpose**: Command-line tool for socket-based control

**Usage Examples**:
```bash
# Create session
pshellctl create ssh user@host

# List sessions
pshellctl list

# Send text
pshellctl send <session-id> "ls -la\n"

# Expect pattern
pshellctl expect <session-id> "password:" --timeout 5000

# Get screen
pshellctl screen <session-id>

# Attach to session (interactive)
pshellctl attach <session-id>

# Destroy session
pshellctl destroy <session-id>

# Daemon control
pshellctl status
pshellctl reload
pshellctl shutdown
```

**Implementation**:
```c
// pshellctl.c
int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    const char *command = argv[1];
    
    // Connect to socket
    int sock_fd = socket_client_connect("/var/run/pshell.sock");
    if (sock_fd < 0) {
        fprintf(stderr, "Failed to connect to pshell daemon\n");
        return 1;
    }
    
    // Dispatch command
    if (strcmp(command, "create") == 0) {
        return cmd_create(sock_fd, argc - 2, argv + 2);
    } else if (strcmp(command, "list") == 0) {
        return cmd_list(sock_fd);
    } else if (strcmp(command, "send") == 0) {
        return cmd_send(sock_fd, argc - 2, argv + 2);
    } else if (strcmp(command, "expect") == 0) {
        return cmd_expect(sock_fd, argc - 2, argv + 2);
    } else if (strcmp(command, "screen") == 0) {
        return cmd_screen(sock_fd, argc - 2, argv + 2);
    } else if (strcmp(command, "attach") == 0) {
        return cmd_attach(sock_fd, argc - 2, argv + 2);
    } else if (strcmp(command, "destroy") == 0) {
        return cmd_destroy(sock_fd, argc - 2, argv + 2);
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        return 1;
    }
}
```

**Interactive Attach Mode**:
```c
int cmd_attach(int sock_fd, int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: pshellctl attach <session-id>\n");
        return 1;
    }
    
    const char *session_id = argv[0];
    
    // Set terminal to raw mode
    struct termios orig_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    
    // Main loop: forward stdin to session, display output
    fd_set read_fds;
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock_fd, &read_fds);
        
        select(sock_fd + 1, &read_fds, NULL, NULL, NULL);
        
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            // Read from stdin, send to session
            char buf[256];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n > 0) {
                socket_send_text(sock_fd, session_id, buf, n);
            }
        }
        
        if (FD_ISSET(sock_fd, &read_fds)) {
            // Read from socket, display output
            char buf[4096];
            ssize_t n = socket_read_output(sock_fd, session_id, buf, sizeof(buf));
            if (n > 0) {
                write(STDOUT_FILENO, buf, n);
            }
        }
    }
    
    // Restore terminal
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    return 0;
}
```

### 3. Socket Protocol Specification

**Message Format**:
```
<length><json>
```

- `<length>`: 4-byte network byte order integer (message length)
- `<json>`: JSON-encoded message

**Request Format**:
```json
{
  "method": "method_name",
  "params": {
    // method-specific parameters
  },
  "id": 123  // optional request ID
}
```

**Response Format**:
```json
{
  "status": "success" | "error",
  "result": {
    // method-specific result
  },
  "error": {
    "code": 123,
    "message": "error message"
  },
  "id": 123  // matches request ID
}
```

**Methods**:
- `create_session` - Create new session
- `list_sessions` - List all sessions
- `get_session` - Get session info
- `destroy_session` - Destroy session
- `send_text` - Send text to session
- `expect_pattern` - Wait for pattern
- `get_screen` - Get screen state
- `get_buffer` - Get output buffer
- `resize_session` - Resize terminal
- `daemon_status` - Get daemon status
- `daemon_reload` - Reload configuration
- `daemon_shutdown` - Shutdown daemon

## REST API Design

### Core Endpoints (Minimal Set)

```
# Session Management
POST   /api/sessions                      # Create session
GET    /api/sessions                      # List sessions
GET    /api/sessions/{id}                 # Get session info
DELETE /api/sessions/{id}                 # Destroy session

# Session Control
POST   /api/sessions/{id}/send            # Send text
POST   /api/sessions/{id}/expect          # Wait for pattern
GET    /api/sessions/{id}/screen          # Get screen state
GET    /api/sessions/{id}/buffer          # Get output buffer
POST   /api/sessions/{id}/resize          # Resize terminal

# Events (Long Polling)
GET    /api/sessions/{id}/events          # Long poll for events

# System
GET    /api/status                        # Daemon status
GET    /api/version                       # Version info
```

### Authentication

**HTTP API**: Simple API key in header:
```
X-API-Key: <api-key>
```

**Socket API**: No authentication (file permissions)

API keys stored in config file:
```ini
[auth]
api_keys = key1,key2,key3
```

## Configuration File (Simple INI format)

```ini
# pshell.conf
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
api_keys = default-key-change-me

[sessions]
max_sessions = 50
default_rows = 24
default_cols = 80
scrollback_lines = 1000
idle_timeout = 3600

[terminal]
term_type = xterm-256color
```

## Build System (Portable Makefile)

```makefile
# Makefile - POSIX-compliant, portable
CC = cc
CFLAGS = -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L -g -O2
LDFLAGS = -lpthread

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -lutil
endif
ifeq ($(UNAME_S),Darwin)
    LDFLAGS += -lutil
endif
ifeq ($(UNAME_S),SunOS)
    LDFLAGS += -lsocket -lnsl
endif

# Directories
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin

# Source files
DAEMON_SOURCES = $(shell find $(SRC_DIR) -name '*.c' ! -path '*/cli/*' ! -path '*/ctl/*')
CLIENT_SOURCES = $(shell find $(SRC_DIR)/cli -name '*.c') \
                 $(SRC_DIR)/utils/logger.c \
                 $(SRC_DIR)/utils/json_simple.c
CTL_SOURCES = $(shell find $(SRC_DIR)/ctl -name '*.c') \
              $(SRC_DIR)/socket/socket_client.c \
              $(SRC_DIR)/utils/json_simple.c

DAEMON_OBJECTS = $(DAEMON_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
CLIENT_OBJECTS = $(CLIENT_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
CTL_OBJECTS = $(CTL_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Targets
DAEMON = $(BIN_DIR)/pshell
CLIENT = $(BIN_DIR)/pshell-client
CTL = $(BIN_DIR)/pshellctl

all: $(DAEMON) $(CLIENT) $(CTL)

$(DAEMON): $(DAEMON_OBJECTS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(DAEMON_OBJECTS) -o $@ $(LDFLAGS)

$(CLIENT): $(CLIENT_OBJECTS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CLIENT_OBJECTS) -o $@ $(LDFLAGS)

$(CTL): $(CTL_OBJECTS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CTL_OBJECTS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

install: all
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(DAEMON) $(DESTDIR)/usr/local/bin/
	install -m 755 $(CLIENT) $(DESTDIR)/usr/local/bin/
	install -m 755 $(CTL) $(DESTDIR)/usr/local/bin/
	install -d $(DESTDIR)/usr/local/share/pshell/web
	cp -r web/* $(DESTDIR)/usr/local/share/pshell/web/

.PHONY: all clean install
```

## Development Phases

### Phase 1: Core Infrastructure (Weeks 1-2)
- [ ] Project structure
- [ ] POSIX PTY implementation
- [ ] Process spawning
- [ ] Basic I/O multiplexing (select)
- [ ] Configuration parser
- [ ] Logging system

### Phase 2: HTTP Server (Weeks 3-4)
- [ ] Simple HTTP server (sockets)
- [ ] HTTP request parser
- [ ] HTTP response builder
- [ ] Static file serving
- [ ] API key authentication

### Phase 3: Unix Socket Server (Week 5)
- [ ] Unix domain socket server
- [ ] Socket protocol implementation
- [ ] Socket client library
- [ ] pshellctl utility

### Phase 4: Session Management (Week 6)
- [ ] Session lifecycle
- [ ] Session list management
- [ ] Resource cleanup

### Phase 5: Terminal Emulation (Weeks 7-8)
- [ ] VT100 escape parser
- [ ] Screen buffer
- [ ] Scrollback buffer
- [ ] Cursor tracking

### Phase 6: REST API (Week 9)
- [ ] Session endpoints
- [ ] Control endpoints
- [ ] Long polling for events
- [ ] JSON request/response

### Phase 7: Pattern Matching (Week 10)
- [ ] POSIX regex matcher
- [ ] Expect engine
- [ ] Timeout handling

### Phase 8: Web Interface (Week 11)
- [ ] HTML/CSS layout
- [ ] JavaScript API client
- [ ] Terminal renderer
- [ ] Long polling client

### Phase 9: CLI Client (Week 12)
- [ ] HTTP command-line tool
- [ ] All API operations
- [ ] Interactive mode

### Phase 10: Testing (Week 13)
- [ ] Unit tests
- [ ] Integration tests
- [ ] Socket control tests
- [ ] Platform testing

### Phase 11: Documentation (Week 14)
- [ ] API documentation
- [ ] Socket protocol documentation
- [ ] User guide
- [ ] Porting guide
- [ ] Maintenance guide
- [ ] Examples

## Success Criteria

1. **Portability**
   - Builds on Linux, macOS, BSD
   - Minimal platform-specific code
   - No external dependencies (except POSIX)

2. **Functionality**
   - Multiple concurrent sessions
   - VT100 emulation
   - Pattern matching
   - REST API complete
   - Socket control working
   - Web interface working

3. **Performance**
   - Handle 50+ sessions
   - Low latency (<100ms)
   - Efficient memory usage

4. **Reliability**
   - No memory leaks
   - Proper error handling
   - Clean resource cleanup

5. **Maintainability**
   - Clean, readable code
   - Good documentation
   - Easy to port

## References

- **POSIX.1-2008**: https://pubs.opengroup.org/onlinepubs/9699919799/
- **expect**: http://expect.sourceforge.net/
- **tmux**: https://github.com/tmux/tmux
- **VT100 User Guide**: https://vt100.net/docs/vt100-ug/
- **Advanced Programming in the UNIX Environment** (Stevens & Rago)

---

**Next Steps:**
1. Review and approve this plan with socket control
2. Set up git repository
3. Create initial project structure
4. Create maintenance documentation
5. Begin Phase 1 implementation