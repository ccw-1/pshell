# pshell Wiki

Welcome to the pshell project wiki. This documentation covers the design, implementation, and usage of pshell - a portable programmable shell with REST API and Unix socket control.

## Quick Links

- [Architecture](architecture.md) - System architecture and design
- [Implementation Plan](../IMPLEMENTATION_PLAN.md) - Detailed implementation roadmap
- [Progress](../PROGRESS.md) - Current implementation status
- [README](../README.md) - Project overview and quick start

## Project Status

**Current Phase**: Phase 2 Complete ✅

- ✅ **Phase 1**: Core Infrastructure (PTY, I/O, Config, Logging)
- ✅ **Phase 2**: HTTP Server (Custom HTTP server, REST API)
- 🚧 **Phase 3**: Unix Socket Server (Next)
- ⏳ **Phase 4**: Session Management
- ⏳ **Phase 5**: Terminal Emulation
- ⏳ **Phase 6**: REST API Integration
- ⏳ **Phase 7**: Pattern Matching
- ⏳ **Phase 8**: Web Interface
- ⏳ **Phase 9**: CLI Client
- ⏳ **Phase 10**: Testing
- ⏳ **Phase 11**: Documentation

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

**REST API Endpoints**:
- `GET /api/health` - Health check
- `GET /api/status` - Server status
- `GET /api/sessions` - List sessions
- `POST /api/sessions` - Create session (supports `scrollback_lines` parameter)
- `GET /api/sessions/:id` - Get session info
- `DELETE /api/sessions/:id` - Destroy session
- `POST /api/sessions/:id/send` - Send input
- `GET /api/sessions/:id/scrollback` - Get scrollback buffer (query params: `start_line`, `count`)
- `POST /api/sessions/:id/expect` - Wait for pattern
- `POST /api/sessions/:id/resize` - Resize terminal
- `GET /api/sessions/:id/output` - Get output (long poll)

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
- [Configuration](configuration.md) - Configuration options (TODO)
- [API Reference](api-reference.md) - REST API documentation (TODO)
- [Examples](examples.md) - Usage examples (TODO)

### For Developers
- [Architecture](architecture.md) - System design and components
- [Implementation Plan](../IMPLEMENTATION_PLAN.md) - Development roadmap
- [Progress](../PROGRESS.md) - Current status and next steps
- [Contributing](contributing.md) - How to contribute (TODO)
- [Porting Guide](porting.md) - Platform porting guide (TODO)

## Key Design Decisions

1. **No External Dependencies**: Everything implemented from scratch for maximum portability
2. **POSIX-Only APIs**: Ensures compatibility across Unix-like systems
3. **Single-Threaded**: Event-driven architecture using select()
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

1. **Unix Socket Server** (Phase 3)
   - Implement Unix domain socket server
   - Create socket protocol (JSON-based)
   - Build pshellctl utility

2. **Session Management** (Phase 4)
   - Implement session lifecycle
   - Integrate with PTY layer
   - Add resource management

3. **Terminal Emulation** (Phase 5)
   - VT100 escape sequence parser
   - Screen buffer management
   - Scrollback support

See [PROGRESS.md](../PROGRESS.md) for detailed status and [IMPLEMENTATION_PLAN.md](../IMPLEMENTATION_PLAN.md) for complete roadmap.

## Contributing

This project is under active development. Contributions are welcome once the core implementation is complete.

## License

See [LICENSE](../LICENSE) file for details.