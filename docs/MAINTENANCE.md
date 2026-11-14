# pshell Maintenance Guide

## Table of Contents

1. [Overview](#overview)
2. [Installation](#installation)
3. [Configuration](#configuration)
4. [Starting and Stopping](#starting-and-stopping)
5. [Monitoring](#monitoring)
6. [Logging](#logging)
7. [Backup and Recovery](#backup-and-recovery)
8. [Performance Tuning](#performance-tuning)
9. [Security](#security)
10. [Troubleshooting](#troubleshooting)
11. [Upgrades](#upgrades)
12. [Common Maintenance Tasks](#common-maintenance-tasks)

## Overview

This guide covers the operational aspects of maintaining a pshell installation, including installation, configuration, monitoring, troubleshooting, and routine maintenance tasks.

### System Requirements

- POSIX-compliant operating system (Linux, macOS, BSD, Solaris, AIX, z/OS)
- C99 compiler (gcc, clang, xlc)
- POSIX threads support
- Minimum 512MB RAM
- 100MB disk space

### Architecture Overview

```
pshell daemon
├── HTTP Server (port 8080)
├── Session Manager (max 50 sessions)
├── PTY Manager (POSIX pseudo-terminals)
├── Terminal Emulator (VT100/xterm)
└── Pattern Matcher (POSIX regex)
```

## Installation

### From Source

```bash
# Clone repository
git clone https://github.com/ccw-1/pshell.git
cd pshell

# Build
make clean
make

# Run tests
make test

# Install (requires root)
sudo make install
```

### Installation Locations

`sudo make install` creates:

```
/usr/local/bin/pshell              # Daemon binary
/usr/local/bin/pshell-client       # HTTP CLI client
/usr/local/bin/pshellctl           # Unix socket control utility
/usr/local/bin/pshell-mcp          # MCP server (AI agents)
/usr/local/share/pshell/web/       # Web interface files
/etc/pshell/pshell.conf            # Configuration file
```

Runtime paths are created by the daemon itself according to the configuration
(defaults in the shipped `config/pshell.conf`):

```
/tmp/pshell.sock                   # Unix domain socket
/tmp/pshell.pid                    # PID file
/tmp/pshell.log                    # Log file
```

### Post-Installation

```bash
# Create config directory (make install already does this)
sudo mkdir -p /etc/pshell
sudo cp config/pshell.conf /etc/pshell/

# Edit configuration
sudo vi /etc/pshell/pshell.conf
```

## Configuration

### Configuration File Format

The configuration file uses simple INI format. Only the keys below are read by
the daemon; any other keys are silently ignored.

```ini
# /etc/pshell/pshell.conf

[daemon]
pid_file = /tmp/pshell.pid
log_file = /tmp/pshell.log
log_level = info                    # debug, info, warn, error
use_syslog = false

[http]
enabled = false                     # HTTP server off by default (see SECURITY.md)
port = 8080
bind_address = 127.0.0.1            # Bind to localhost only
static_dir = /usr/local/share/pshell/web
# api_key = legacy-shared-key       # Legacy single-key auth (see Auth below)

[socket]
enabled = true
socket_path = /tmp/pshell.sock
socket_permissions = 0600

[auth]
enabled = true
api_keys = key1,key2,key3          # Comma-separated API keys

[sessions]
max_sessions = 50
max_attach = 16
default_shell = /bin/bash
```

### Configuration Options

#### Daemon Section

| Option | Default | Description |
|--------|---------|-------------|
| `pid_file` | `/tmp/pshell.pid` | PID file location |
| `log_file` | `/tmp/pshell.log` | Log file location |
| `log_level` | `info` | Logging level (debug, info, warn, error) |
| `use_syslog` | `false` | Also log to syslog |

#### HTTP Section

| Option | Default | Description |
|--------|---------|-------------|
| `enabled` | `false` | Enable the HTTP/REST server |
| `port` | `8080` | HTTP server port |
| `bind_address` | `127.0.0.1` | Bind address (use 0.0.0.0 for all interfaces) |
| `static_dir` | - | Directory for web interface static files |
| `api_key` | - | Legacy single shared API key (prefer `[auth] api_keys`) |

#### Socket Section

| Option | Default | Description |
|--------|---------|-------------|
| `enabled` | `true` | Enable the Unix socket server |
| `socket_path` | `/tmp/pshell.sock` | Unix domain socket path |
| `socket_permissions` | `0600` | Socket file permissions (octal) |

#### Auth Section

| Option | Default | Description |
|--------|---------|-------------|
| `enabled` | `false` | Enable API key authentication |
| `api_keys` | - | Comma-separated list of valid API keys |

Clients may authenticate with either `X-API-Key: <key>` or `Authorization: Bearer <key>`.
With `enabled = true` and no `api_keys`, the daemon refuses to start. Static
files and `/api/health` are always unauthenticated.

#### Sessions Section

| Option | Default | Description |
|--------|---------|-------------|
| `max_sessions` | `50` | Maximum concurrent sessions |
| `max_attach` | `16` | Terminals that can attach to one session at once (1-256); read at startup |
| `default_shell` | `/bin/sh` | Shell used when a request omits `command` |

**Note**: Scrollback size, terminal size, and timeouts are per-session request
parameters (e.g. `scrollback_lines` on `POST /api/sessions`, `--scrollback` on
`pshellctl create`), not configuration file keys.

### Reloading Configuration

There is no configuration reload: the SIGHUP handler is currently a no-op
(TODO) and neither `pshell-client reload` nor `pshellctl reload` exists. To
apply configuration changes, restart the daemon:

```bash
sudo kill $(cat /tmp/pshell.pid)   # graceful shutdown
sudo pshell -c /etc/pshell/pshell.conf
```

## Starting and Stopping

### Manual Start/Stop

```bash
# Start daemon
sudo pshell -c /etc/pshell/pshell.conf

# Stop daemon (via signal; there is no shutdown command)
sudo kill $(cat /tmp/pshell.pid)

# Graceful shutdown (default SIGTERM)
sudo kill -TERM $(cat /tmp/pshell.pid)

# Force shutdown
sudo kill -KILL $(cat /tmp/pshell.pid)

# Or stop by name
pkill pshell
```

### Control Interfaces

pshell provides two control interfaces:

**1. HTTP Client (pshell-client)**
- Remote control via HTTP REST API
- Requires API key authentication
- Works over network

```bash
# Check daemon status
pshell-client status

# List sessions
pshell-client list

# Create session
pshell-client create --command "bash"

# Send input
pshell-client send <session-id> "echo hello\n"

# Read output
pshell-client output <session-id>
```

**2. Socket Control (pshellctl)**
- Local control via Unix domain socket
- No authentication (file permissions)
- Faster, lower overhead

```bash
# Check daemon status
pshellctl status

# List sessions
pshellctl list

# Create session
pshellctl create --command "bash"

# Send input
pshellctl send <session-id> "echo hello\n"

# Read output
pshellctl output <session-id>

# Get screen state
pshellctl screen <session-id>

# Resize terminal
pshellctl resize <session-id> 30 100

# Interactive terminal on a session (Ctrl-] detaches)
pshellctl attach <session-id>

# Destroy session
pshellctl destroy <session-id>
```

### Using Scripts

No helper start/stop scripts are shipped; use the commands above or a service
manager (systemd/launchd).

### systemd Service (Linux)

No systemd unit file is shipped; create `/etc/systemd/system/pshell.service`:

```ini
[Unit]
Description=pshell - Programmable Shell Daemon
After=network.target

[Service]
Type=forking
PIDFile=/tmp/pshell.pid
ExecStart=/usr/local/bin/pshell -c /etc/pshell/pshell.conf
ExecStop=/bin/kill -TERM $MAINPID
Restart=on-failure
RestartSec=5s

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable pshell
sudo systemctl start pshell
sudo systemctl status pshell
```

### launchd Service (macOS)

Create `/Library/LaunchDaemons/com.pshell.daemon.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.pshell.daemon</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/local/bin/pshell</string>
        <string>-c</string>
        <string>/etc/pshell/pshell.conf</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
</dict>
</plist>
```

Load and start:

```bash
sudo launchctl load /Library/LaunchDaemons/com.pshell.daemon.plist
sudo launchctl start com.pshell.daemon
```

## Monitoring

### Health Check

```bash
# Check if daemon is running
pshell-client status

# Or use curl
curl http://localhost:8080/api/status
```

Expected response (note: values are currently hardcoded placeholders — real
session/uptime reporting is a TODO in the code):

```json
{
  "status": "running",
  "sessions": 0,
  "uptime": 0
}
```

### Monitoring Active Sessions

```bash
# List all sessions
pshell-client list

# Or use curl
curl -H "X-API-Key: <your-generated-key>" http://localhost:8080/api/sessions
```

### Process Monitoring

```bash
# Check process status
ps aux | grep pshell

# Check resource usage
top -p $(cat /tmp/pshell.pid)

# Check open file descriptors
lsof -p $(cat /tmp/pshell.pid)

# Check network connections
netstat -anp | grep $(cat /tmp/pshell.pid)
```

### Metrics Collection

There is no `/api/metrics` endpoint. For process-level metrics, use OS tools
(`ps`, `top`, `lsof` above). For service health, poll `/api/status` (see the
note above about its placeholder values) or `/api/health`.

### Integration with Monitoring Tools

#### Nagios/Icinga

Create check script `/usr/local/libexec/check_pshell.sh`:

```bash
#!/bin/bash
STATUS=$(curl -s http://localhost:8080/api/status | jq -r .status)
if [ "$STATUS" = "running" ]; then
    echo "OK - pshell is running"
    exit 0
else
    echo "CRITICAL - pshell is not running"
    exit 2
fi
```

## Logging

### Log Levels

- **debug**: Detailed debugging information
- **info**: General informational messages
- **warn**: Warning messages
- **error**: Error messages

### Log Format

```
[2026-07-17 15:00:00] [INFO] [main] pshell daemon started (pid=12345)
[2026-07-17 15:00:01] [INFO] [http] HTTP server listening on 127.0.0.1:8080
[2026-07-17 15:00:05] [INFO] [session] Session created: abc123 (ssh user@host)
[2026-07-17 15:00:15] [ERROR] [pty] Failed to allocate PTY: Too many open files
```

### Log Rotation

The shipped configuration logs to `/tmp/pshell.log`; adjust the paths below to
match your `log_file` setting. The daemon's SIGHUP handler is currently a
no-op, so rotation must not rely on signals to reopen the log — use
`copytruncate` (or restart the daemon after rotating).

#### Using logrotate (Linux)

Create `/etc/logrotate.d/pshell`:

```
/tmp/pshell.log {
    daily
    rotate 7
    compress
    delaycompress
    missingok
    notifempty
    copytruncate
}
```

#### Manual Rotation

```bash
# Truncate in place (the daemon keeps writing to the same file)
sudo truncate -s 0 /tmp/pshell.log
```

### Viewing Logs

```bash
# Tail log file
tail -f /tmp/pshell.log

# View last 100 lines
tail -n 100 /tmp/pshell.log

# Search for errors
grep ERROR /tmp/pshell.log

# View logs for specific session
grep "abc123" /tmp/pshell.log
```

## Backup and Recovery

### What to Backup

1. **Configuration**: `/etc/pshell/pshell.conf`
2. **Web files**: `/usr/local/share/pshell/web/` (if customized)
3. **Logs**: `/tmp/pshell.log` (optional)

### Backup Script

```bash
#!/bin/bash
# backup_pshell.sh

BACKUP_DIR="/backup/pshell"
DATE=$(date +%Y%m%d_%H%M%S)
BACKUP_FILE="$BACKUP_DIR/pshell_backup_$DATE.tar.gz"

mkdir -p "$BACKUP_DIR"

tar czf "$BACKUP_FILE" \
    /etc/pshell/pshell.conf \
    /usr/local/share/pshell/web/

echo "Backup created: $BACKUP_FILE"
```

### Recovery

```bash
# Extract backup
tar xzf pshell_backup_20260717_150000.tar.gz -C /

# Restart daemon
sudo systemctl restart pshell
```

### Session State

**Note**: pshell does not persist session state. Active sessions are lost on daemon restart. This is by design for security and simplicity.

## Performance Tuning

### System Limits

Check and adjust system limits:

```bash
# Check current limits
ulimit -a

# Increase file descriptor limit
ulimit -n 4096

# Make permanent (add to /etc/security/limits.conf)
* soft nofile 4096
* hard nofile 8192
```

### Configuration Tuning

```ini
[sessions]
max_sessions = 100             # Increase for more concurrent sessions

[socket]
socket_permissions = 0600      # Keep restricted to the daemon owner
```

Scrollback memory is controlled per session: pass a smaller `--scrollback` /
`scrollback_lines` when creating sessions. To clean up idle sessions, destroy
them via `pshellctl destroy` / `DELETE /api/sessions/:id` from your automation
(there is no idle timeout).

### Memory Usage

Approximate memory per session:
- Base: 100KB
- Screen buffer (24x80): 2KB
- Scrollback (1000 lines): 80KB
- Total: ~200KB per session

For 50 sessions: ~10MB + daemon overhead (~5MB) = ~15MB total

### CPU Usage

CPU usage is minimal during idle periods. Pattern matching and terminal emulation are the most CPU-intensive operations.

## Security

### Network Security

```ini
[http]
bind_address = 127.0.0.1       # Bind to localhost only
```

For remote access, use SSH tunneling:

```bash
ssh -L 8080:localhost:8080 user@remote-host
```

### API Key Management

```bash
# Generate random API key
openssl rand -hex 32

# Add to configuration
[auth]
api_keys = abc123...,def456...
```

**Best Practices:**
- Use long, random API keys (64+ characters)
- Rotate keys regularly
- Use different keys for different clients
- Never commit keys to version control

### File Permissions

```bash
# Secure configuration file
sudo chmod 600 /etc/pshell/pshell.conf
sudo chown root:root /etc/pshell/pshell.conf

# Secure log file (path matches log_file in config)
sudo chmod 644 /tmp/pshell.log
sudo chown root:root /tmp/pshell.log
```

### Process Isolation

Run pshell as dedicated user:

```bash
# Create user
sudo useradd -r -s /bin/false pshell

# Change ownership
sudo chown pshell:pshell /tmp/pshell.log
sudo chown pshell:pshell /tmp/pshell.pid

# Update systemd service
[Service]
User=pshell
Group=pshell
```

## Troubleshooting

### Daemon Won't Start

**Symptom**: Daemon fails to start

**Possible Causes:**
1. Port already in use
2. Permission denied
3. Configuration error

**Solutions:**

```bash
# Check if port is in use
netstat -an | grep 8080
lsof -i :8080

# Check permissions
ls -l /tmp/pshell.pid
ls -l /tmp/pshell.log

# Run in foreground to see startup errors
pshell -c /etc/pshell/pshell.conf -f

# Check logs
tail -f /tmp/pshell.log
```

### Cannot Create Sessions

**Symptom**: Session creation fails

**Possible Causes:**
1. Max sessions reached
2. PTY allocation failure
3. Process spawn failure

**Solutions:**

```bash
# Check active sessions
pshell-client list

# Check system limits
ulimit -n
cat /proc/sys/fs/file-max

# Check available PTYs
ls /dev/pts/

# Increase limits
ulimit -n 4096
```

### High Memory Usage

**Symptom**: Daemon using excessive memory

**Possible Causes:**
1. Too many sessions
2. Large scrollback buffers
3. Memory leak

**Solutions:**

```bash
# Check memory usage
ps aux | grep pshell
top -p $(cat /tmp/pshell.pid)

# Reduce scrollback for new sessions (per-session setting, not config)
pshellctl create --command bash --scrollback 500

# Clean up long-lived sessions from your automation
pshellctl destroy <session-id>
```

### Socket Control Connection Failed

**Symptom**: pshellctl cannot connect to daemon

**Possible Causes:**
1. Daemon not running
2. Socket file missing or wrong permissions
3. Wrong socket path

**Solutions:**

```bash
# Check daemon status
ps aux | grep pshell

# Check socket file
ls -l /tmp/pshell.sock

# Verify socket path in config
grep socket_path /etc/pshell/pshell.conf

# Check socket permissions
[socket]
socket_permissions = 0600

# Try with custom socket path
pshellctl -s /tmp/pshell.sock status
```

### HTTP Client Connection Failed

**Symptom**: pshell-client cannot connect to daemon

**Possible Causes:**
1. Daemon not running
2. Wrong port or bind address
3. Firewall blocking connection
4. Invalid API key

**Solutions:**

```bash
# Check daemon is listening
netstat -an | grep 8080
lsof -i :8080

# Test connection
curl http://127.0.0.1:8080/api/health

# Check API key
curl -H "X-API-Key: your-key" http://127.0.0.1:8080/api/status

# Check firewall
sudo iptables -L | grep 8080
```

### Pattern Matching Not Working

**Symptom**: Expect patterns not matching

**Possible Causes:**
1. Incorrect regex syntax
2. Timeout too short
3. Pattern not in buffer

**Solutions:**

```bash
# Test regex
echo "password:" | grep -E "password:"

# Increase timeout
pshellctl expect <session-id> "password:" 10000

# Check buffer contents
pshellctl output <session-id>
curl -H "X-API-Key: <your-generated-key>" \
  http://localhost:8080/api/sessions/abc123/scrollback
```

### Web Interface Not Loading

**Symptom**: Web interface returns 404

**Possible Causes:**
1. Static files not installed
2. Incorrect path configuration
3. Permission denied

**Solutions:**

```bash
# Check static files
ls -l /usr/local/share/pshell/web/

# Check configuration
grep static_dir /etc/pshell/pshell.conf

# Check permissions
sudo chmod -R 755 /usr/local/share/pshell/web/
```

## Upgrades

### Upgrade Process

```bash
# 1. Backup configuration
sudo cp /etc/pshell/pshell.conf /etc/pshell/pshell.conf.backup

# 2. Stop daemon
sudo systemctl stop pshell

# 3. Build new version
cd pshell
git pull
make clean
make

# 4. Run tests
make test

# 5. Install
sudo make install

# 6. Compare configurations
diff /etc/pshell/pshell.conf /etc/pshell/pshell.conf.backup

# 7. Start daemon
sudo systemctl start pshell

# 8. Verify
pshell-client status
```

### Rollback

```bash
# 1. Stop daemon
sudo systemctl stop pshell

# 2. Restore old binaries
sudo cp /backup/pshell/pshell /usr/local/bin/
sudo cp /backup/pshell/pshell-client /usr/local/bin/

# 3. Restore configuration
sudo cp /etc/pshell/pshell.conf.backup /etc/pshell/pshell.conf

# 4. Start daemon
sudo systemctl start pshell
```

## Common Maintenance Tasks

### Weekly Tasks

- Review logs for errors
- Check disk space
- Verify backup completion
- Review active sessions

### Monthly Tasks

- Rotate logs
- Update API keys
- Review security settings
- Check for updates

### Quarterly Tasks

- Performance review
- Capacity planning
- Security audit
- Documentation review

### Maintenance Checklist

```bash
#!/bin/bash
# maintenance_check.sh

echo "=== pshell Maintenance Check ==="
echo ""

# Check daemon status
echo "1. Daemon Status:"
systemctl status pshell | grep Active

# Check disk space
echo ""
echo "2. Disk Space:"
df -h /tmp

# Check log size
echo ""
echo "3. Log Size:"
ls -lh /tmp/pshell.log

# Check active sessions
echo ""
echo "4. Active Sessions:"
pshell-client list | wc -l

# Check memory usage
echo ""
echo "5. Memory Usage:"
ps aux | grep pshell | grep -v grep | awk '{print $6}'

# Check for errors in logs
echo ""
echo "6. Recent Errors:"
grep ERROR /tmp/pshell.log | tail -n 5

echo ""
echo "=== Check Complete ==="
```

## Support and Resources

### Documentation

- Architecture: `wiki/architecture.md`
- Implementation Plan: `IMPLEMENTATION_PLAN.md`
- Progress: `PROGRESS.md`
- Scrollback API: `docs/API_SCROLLBACK.md`
- Escape Sequences: `docs/ESCAPE_SEQUENCES.md`
- MCP Server: `docs/MCP_SERVER.md`

### Getting Help

- GitHub Issues: https://github.com/ccw-1/pshell/issues

### Reporting Bugs

Include the following information:
1. pshell version (`pshell -v`)
2. Operating system and version
3. Configuration file
4. Relevant log entries
5. Steps to reproduce

---

**Last Updated**: 2026-10-13
**Version**: 1.1.0
