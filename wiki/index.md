# pshell Wiki

Welcome to the pshell project wiki. This documentation covers the design, implementation, and usage of pshell - a portable programmable shell with REST API and Unix socket control.

## Quick Links

- [Architecture](architecture.md) - System architecture and design
- [Implementation Plan](../IMPLEMENTATION_PLAN.md) - Detailed implementation roadmap
- [Progress](../PROGRESS.md) - Current implementation status
- [README](../README.md) - Project overview and quick start

## Project Status

**Current Phase**: Phase 10 Complete ✅

- ✅ **Phase 1**: Core Infrastructure (PTY, I/O, Config, Logging)
- ✅ **Phase 2**: HTTP Server (Custom HTTP server, REST API)
- ✅ **Phase 3**: Unix Socket Server (JSON protocol, pshellctl)
- ✅ **Phase 4**: Session Management
- ✅ **Phase 5**: Terminal Emulation
- ✅ **Phase 6**: REST API Integration
- ✅ **Phase 7**: Pattern Matching
- ✅ **Phase 8**: Web Interface
- ✅ **Phase 9**: CLI Clients (pshell-client + pshellctl)
- ✅ **Phase 10**: Testing (unit + integration tests)
- ⏳ **Phase 11**: Documentation & Examples

## What's Working

### HTTP Server (Phase 2)
The HTTP server is fully functional and integrated with the daemon:

```bash
# Start daemon
./bin/pshell -f -c config/pshell-dev.conf

# Test endpoints
curl http://127.0.0.1:8080/api/health
curl http://127.0.0.1:8080/api/status
curl http://127.0.0.1:8080/api/sessions
```

**Features**:
- Custom HTTP server (no external dependencies)
- Request parsing (GET, POST, PUT, DELETE)
- Response building (JSON, files, errors)
- Route registration and matching
- API key authentication (optional)
- Non-blocking I/O
- Event loop integration

**REST API Endpoints** (14 routes):
- `GET /api/health` - Health check (unauthenticated)
- `GET /api/status` - Server status
- `POST /api/sessions` - Create session (supports `scrollback_lines` parameter)
- `GET /api/sessions` - List sessions
- `GET /api/sessions/:id` - Get session info (raw JSON, includes `total_lines`)
- `DELETE /api/sessions/:id` - Destroy session
- `POST /api/sessions/:id/send` - Send input (text sent verbatim; escape sequences are client-side only)
- `POST /api/sessions/:id/expect` - Wait for pattern
- `POST /api/sessions/:id/resize` - Resize terminal
- `GET /api/sessions/:id/output` - Read available output (single non-blocking read)
- `GET /api/sessions/:id/screen` - Get current screen state
- `GET /api/sessions/:id/raw` - Get raw output
- `GET /api/sessions/:id/scrollback` - Get scrollback buffer (query params: `start_line`, `count`)
- `GET /api/sessions/:id/lines` - Get line range from output log (query params: `start`, `end` - both required)

### Unix Socket Control (Phase 3)
`pshellctl` provides local control over the Unix domain socket with the same
operations plus CLI conveniences (`lines --head/--tail`, `scrollback`,
`screen`, `generate-key`). The socket server shares the daemon's JSON protocol
and event loop.

### Core Infrastructure (Phase 1)
All core components are implemented and working:

- **PTY Management**: POSIX-compliant pseudo-terminal operations
- **Process Spawning**: Fork/exec with PTY integration
- **I/O Multiplexer**: Select-based event loop
- **Configuration**: INI-style config parser
- **Logging**: File and syslog support with log levels
- **Utilities**: JSON, UUID, string manipulation, circular buffers

## Documentation

### For Users
- [README](../README.md) - Getting started guide
- [Escape Sequences](../docs/ESCAPE_SEQUENCES.md) - Complete escape sequence reference
- [Scrollback API](../docs/API_SCROLLBACK.md) - Scrollback buffer API documentation
- [Output Logging](../docs/OUTPUT_LOGGING.md) - Session output logging and rotation
- [Default Shell](../docs/DEFAULT_SHELL.md) - Configurable default shell
- [MCP Server](../docs/MCP_SERVER.md) - MCP server for AI agents

### For Operators
- [Maintenance Guide](../docs/MAINTENANCE.md) - Installation, configuration, operations, troubleshooting
- [Security](../docs/SECURITY.md) - Multi-user security considerations
- [Environment Variables](../docs/ENVIRONMENT_VARIABLES.md) - Environment variable configuration

### For Developers
- [Architecture](architecture.md) - System design and components
- [Project Overview](overview.md) - High-level project description
- [Implementation Plan](../IMPLEMENTATION_PLAN.md) - Development roadmap
- [Progress](../PROGRESS.md) - Current status and next steps
- [Expect Buffer Implementation](../EXPECT_BUFFER_IMPLEMENTATION.md) - Expect buffer internals
- [TODO](../TODO.md) - Known issues and improvement roadmap

## Key Design Decisions

1. **No External Dependencies**: Everything implemented from scratch for maximum portability
2. **POSIX-Only APIs**: Ensures compatibility across Unix-like systems
3. **Event Loop + Worker Threads**: A single-threaded select() event loop handles all I/O; expect operations run on detached worker threads guarded by a per-session mutex
4. **Custom HTTP Server**: Simple, lightweight, no library dependencies
5. **JSON Protocol**: Both HTTP and Unix socket use JSON for consistency
6. **Modular Design**: Clear separation between layers

## Getting Started

### Build
```bash
make clean && make
```

### Run (Development)
```bash
./bin/pshell -f -c config/pshell-dev.conf
```

### Test
```bash
# Health check
curl http://127.0.0.1:8080/api/health

# Create session (mock)
curl -X POST http://127.0.0.1:8080/api/sessions \
  -H "Content-Type: application/json" \
  -d '{"command":"bash","rows":24,"cols":80}'
```

## Next Steps

1. **Documentation & Examples** (Phase 11)
   - Consolidate API reference material
   - Expand usage examples
   - Keep wiki pages in sync with the code

2. **Improvement Roadmap** (see [TODO.md](../TODO.md))
   - Expand test coverage
   - Expect buffer sizing and precision improvements
   - Config reload support (currently not implemented)

See [PROGRESS.md](../PROGRESS.md) for detailed status and [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) for complete roadmap.

## Contributing

This project is under active development. Contributions are welcome: open an issue or pull request at https://github.com/ccw-1/pshell.

## License

License: TBD (no license file is present yet).