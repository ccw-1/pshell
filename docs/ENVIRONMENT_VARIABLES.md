# Environment Variables

## PSHELL_SOCKET_PATH

**Purpose**: Override the default Unix socket path for `pshellctl` without using command-line flags.

**Default**: `/tmp/pshell.sock`

**Priority Order**:
1. Command-line flag `-s` (highest priority)
2. Environment variable `PSHELL_SOCKET_PATH`
3. Compiled-in default `/tmp/pshell.sock` (lowest priority)

### Usage Examples

**Set for current session:**
```bash
export PSHELL_SOCKET_PATH=/var/run/pshell.sock
pshellctl status
pshellctl list
```

**Set for single command:**
```bash
PSHELL_SOCKET_PATH=/custom/path.sock pshellctl status
```

**Override environment variable with flag:**
```bash
export PSHELL_SOCKET_PATH=/var/run/pshell.sock
pshellctl -s /tmp/pshell.sock status  # Uses /tmp/pshell.sock
```

**Add to shell profile:**
```bash
# ~/.bashrc or ~/.zshrc
export PSHELL_SOCKET_PATH=/var/run/pshell.sock
```

### Use Cases

1. **System-wide configuration**: Set in `/etc/environment` or systemd service
2. **Per-user configuration**: Set in `~/.bashrc` or `~/.profile`
3. **Container environments**: Set via Docker/Kubernetes environment
4. **Testing**: Easily switch between different daemon instances

### Example: Multiple Daemon Instances

```bash
# Terminal 1: Development daemon
export PSHELL_SOCKET_PATH=/tmp/pshell-dev.sock
pshell -f -c config/pshell-dev.conf

# Terminal 2: Development client
export PSHELL_SOCKET_PATH=/tmp/pshell-dev.sock
pshellctl status

# Terminal 3: Production daemon
export PSHELL_SOCKET_PATH=/var/run/pshell.sock
pshell -c config/pshell-production.conf

# Terminal 4: Production client
export PSHELL_SOCKET_PATH=/var/run/pshell.sock
pshellctl status
```

### Systemd Service Example

```ini
[Unit]
Description=pshell daemon
After=network.target

[Service]
Type=forking
Environment="PSHELL_SOCKET_PATH=/var/run/pshell.sock"
ExecStart=/usr/local/bin/pshell -c /etc/pshell/pshell.conf
PIDFile=/var/run/pshell.pid
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

### Docker Compose Example

```yaml
version: '3'
services:
  pshell:
    image: pshell:latest
    environment:
      - PSHELL_SOCKET_PATH=/var/run/pshell.sock
    volumes:
      - /var/run/pshell.sock:/var/run/pshell.sock
```

## Future Environment Variables

Planned for future releases:

- `PSHELL_CONFIG_PATH`: Override default config file location
- `PSHELL_LOG_LEVEL`: Override log level without config file
- `PSHELL_API_KEY`: Set API key for HTTP authentication
