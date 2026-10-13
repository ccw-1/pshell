# Default Shell Configuration

## Overview

The pshell daemon now supports configurable default shell for sessions. When creating a session without specifying a command, the daemon will use the configured default shell.

## Configuration

Add to `pshell.conf`:

```ini
[sessions]
max_sessions = 100
default_shell = /bin/bash
```

**Default value**: `/bin/sh` (if not configured)

## Usage

### REST API

**Without command (uses default shell):**
```bash
curl -X POST http://localhost:8080/api/sessions \
  -H "Content-Type: application/json" \
  -d '{"rows":24,"cols":80}'
```

**With explicit command (overrides default):**
```bash
curl -X POST http://localhost:8080/api/sessions \
  -H "Content-Type: application/json" \
  -d '{"command":"zsh","rows":24,"cols":80}'
```

### Socket Protocol (pshellctl)

**Important**: `pshellctl` always sends an explicit command (defaulting to
`bash`), so the daemon-side `default_shell` fallback is not reachable via
pshellctl — it applies to REST requests that omit `command`:

```bash
# Runs bash (pshellctl's own default), not the daemon's default_shell
pshellctl create

# Explicit command
pshellctl create --command zsh
```

## Implementation Details

### Architecture

1. **Configuration Loading** (`src/daemon/main.c`):
   - Reads `default_shell` from config file
   - Stores in static buffer (256 bytes)
   - Provides `get_default_shell()` function

2. **REST API** (`src/http/rest_api.c`):
   - Checks if `command` parameter is provided
   - Falls back to `get_default_shell()` if omitted
   - Logs default shell usage

3. **Socket Protocol** (`src/socket/socket_protocol.c`):
   - Same fallback logic as REST API
   - Consistent behavior across interfaces

### Code Changes

**Files Modified:**
- `include/pshell/process_spawn.h` - Added credential support
- `src/daemon/main.c` - Added default shell configuration
- `src/http/rest_api.c` - Added default shell fallback
- `src/socket/socket_protocol.c` - Added default shell fallback
- `config/pshell.conf` - Added default_shell setting
- `config/pshell-dev.conf` - Added default_shell setting

## Platform Considerations

**Recommended defaults by platform:**
- Linux: `/bin/bash`
- macOS: `/bin/zsh` (macOS 10.15+) or `/bin/bash`
- BSD: `/bin/sh` or `/usr/local/bin/bash`
- Solaris: `/bin/bash` or `/bin/ksh`
- AIX: `/bin/bash` or `/bin/ksh`
- z/OS: `/bin/sh`

## Security Notes

- Default shell runs with daemon's privileges (unless privilege separation is configured)
- Validate shell path exists and is executable
- Consider restricting to approved shells via configuration validation

## Future Enhancements

1. **Shell Validation**: Verify shell exists and is executable at startup
2. **Per-User Shells**: Support different default shells per user (with privilege separation)
3. **Shell Whitelist**: Configuration option to restrict allowed shells
4. **Environment Variables**: Support for shell-specific environment setup
