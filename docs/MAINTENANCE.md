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
git clone https://github.com/yourusername/pshell.git
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

```
/usr/local/bin/pshell              # Daemon binary
/usr/local/bin/pshell-client       # HTTP CLI client
/usr/local/bin/pshellctl           # Unix socket control utility
/usr/local/share/pshell/web/       # Web interface files
/tmp/pshell.sock                   # Unix domain socket (runtime)
/etc/pshell/pshell.conf            # Configuration file
/var/run/pshell.pid                # PID file
/var/log/pshell.log                # Log file
```

### Post-Installation

```bash
# Create log directory
sudo mkdir -p /var/log
sudo touch /var/log/pshell.log
sudo chmod 644 /var/log/pshell.log

# Create config directory
sudo mkdir -p /etc/pshell
sudo cp config/pshell.conf /etc/pshell/

# Edit configuration
sudo vi /etc/pshell/pshell.conf
```

## Configuration

### Configuration File Format

The configuration file uses simple INI format:

```ini
# /etc/pshell/pshell.conf

[daemon]
pid_file = /var/run/pshell.pid
log_file = /var/log/pshell.log
log_level = info                    # debug, info, warn, error
daemonize = true                    # Run as daemon

[http]
port = 8080
bind_address = 127.0.0.1           # Bind to localhost only
max_connections = 50
request_timeout = 30               # Seconds

[auth]
enabled = true
api_keys = key1,key2,key3          # Comma-separated API keys

[sessions]
max_sessions = 50
default_rows = 24
default_cols = 80
scrollback_lines = 1000
idle_timeout = 3600                # Seconds (1 hour)
cleanup_interval = 300             # Seconds (5 minutes)

[terminal]
term_type = xterm-256color
```

### Configuration Options

#### Daemon Section

| Option | Default | Description |
|--------|---------|-------------|
| `pid_file` | `/var/run/pshell.pid` | PID file location |
| `log_file` | `/var/log/pshell.log` | Log file location |
| `log_level` | `info` | Logging level (debug, info, warn, error) |
| `daemonize` | `true` | Run as background daemon |

#### HTTP Section

| Option | Default | Description |
|--------|---------|-------------|
| `port` | `8080` | HTTP server port |
| `bind_address` | `127.0.0.1` | Bind address (use 0.0.0.0 for all interfaces) |
| `max_connections` | `50` | Maximum concurrent HTTP connections |
| `request_timeout` | `30` | HTTP request timeout in seconds |

#### Auth Section

| Option | Default | Description |
|--------|---------|-------------|
| `enabled` | `true` | Enable API key authentication |
| `api_keys` | - | Comma-separated list of valid API keys |

#### Sessions Section

| Option | Default | Description |
|--------|---------|-------------|
| `max_sessions` | `50` | Maximum concurrent sessions |
| `default_rows` | `24` | Default terminal rows |
| `default_cols` | `80` | Default terminal columns |
| `scrollback_lines` | `1000` | Scrollback buffer size |
| `idle_timeout` | `3600` | Idle session timeout (seconds) |
| `cleanup_interval` | `300` | Session cleanup interval (seconds) |

#### Terminal Section

| Option | Default | Description |
|--------|---------|-------------|
| `term_type` | `xterm-256color` | TERM environment variable value |

### Reloading Configuration

```bash
# Send SIGHUP to reload configuration
sudo kill -HUP $(cat /var/run/pshell.pid)

# Or use the client
pshell-client reload
```

## Starting and Stopping

### Manual Start/Stop

```bash
# Start daemon
sudo pshell -c /etc/pshell/pshell.conf

# Stop daemon (via HTTP client)
pshell-client shutdown

# Stop daemon (via socket control)
pshellctl shutdown

# Stop daemon (via signal)
sudo kill $(cat /var/run/pshell.pid)

# Graceful shutdown (wait for sessions to close)
sudo kill -TERM $(cat /var/run/pshell.pid)

# Force shutdown
sudo kill -KILL $(cat /var/run/pshell.pid)
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

# Destroy session
pshellctl destroy <session-id>
```

### Using Scripts

```bash
# Start
sudo /usr/local/share/pshell/scripts/start_daemon.sh

# Stop
sudo /usr/local/share/pshell/scripts/stop_daemon.sh

# Restart
sudo /usr/local/share/pshell/scripts/stop_daemon.sh
sudo /usr/local/share/pshell/scripts/start_daemon.sh
```

### systemd Service (Linux)

Create `/etc/systemd/system/pshell.service`:

```ini
[Unit]
Description=pshell - Programmable Shell Daemon
After=network.target

[Service]
Type=forking
PIDFile=/var/run/pshell.pid
ExecStart=/usr/local/bin/pshell -c /etc/pshell/pshell.conf
ExecReload=/bin/kill -HUP $MAINPID
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

Expected response:
```json
{
  "status": "running",
  "uptime": 3600,
  "sessions": {
    "active": 5,
    "max": 50
  },
  "memory": {
    "used": 52428800,
    "limit": 536870912
  }
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
top -p $(cat /var/run/pshell.pid)

# Check open file descriptors
lsof -p $(cat /var/run/pshell.pid)

# Check network connections
netstat -anp | grep $(cat /var/run/pshell.pid)
```

### Metrics Collection

pshell exposes metrics at `/api/metrics`:

```bash
curl http://localhost:8080/api/metrics
```

Example output:
```
# HELP pshell_sessions_active Number of active sessions
# TYPE pshell_sessions_active gauge
pshell_sessions_active 5

# HELP pshell_sessions_total Total sessions created
# TYPE pshell_sessions_total counter
pshell_sessions_total 127

# HELP pshell_http_requests_total Total HTTP requests
# TYPE pshell_http_requests_total counter
pshell_http_requests_total 1543

# HELP pshell_memory_bytes Memory usage in bytes
# TYPE pshell_memory_bytes gauge
pshell_memory_bytes 52428800
```

### Integration with Monitoring Tools

#### Prometheus

Add to `prometheus.yml`:

```yaml
scrape_configs:
  - job_name: 'pshell'
    static_configs:
      - targets: ['localhost:8080']
    metrics_path: '/api/metrics'
```

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
[2026-07-17 15:00:10] [WARN] [session] Session abc123 idle for 3600 seconds
[2026-07-17 15:00:15] [ERROR] [pty] Failed to allocate PTY: Too many open files
```

### Log Rotation

#### Using logrotate (Linux)

Create `/etc/logrotate.d/pshell`:

```
/var/log/pshell.log {
    daily
    rotate 7
    compress
    delaycompress
    missingok
    notifempty
    postrotate
        kill -HUP $(cat /var/run/pshell.pid) 2>/dev/null || true
    endscript
}
```

#### Manual Rotation

```bash
# Rotate log file
sudo mv /var/log/pshell.log /var/log/pshell.log.1
sudo touch /var/log/pshell.log
sudo chmod 644 /var/log/pshell.log
sudo kill -HUP $(cat /var/run/pshell.pid)
```

### Viewing Logs

```bash
# Tail log file
tail -f /var/log/pshell.log

# View last 100 lines
tail -n 100 /var/log/pshell.log

# Search for errors
grep ERROR /var/log/pshell.log

# View logs for specific session
grep "abc123" /var/log/pshell.log
```

## Backup and Recovery

### What to Backup

1. **Configuration**: `/etc/pshell/pshell.conf`
2. **Web files**: `/usr/local/share/pshell/web/` (if customized)
3. **Logs**: `/var/log/pshell.log` (optional)

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
[http]
max_connections = 100          # Increase for more concurrent clients

[sessions]
max_sessions = 100             # Increase for more concurrent sessions
scrollback_lines = 500         # Decrease to reduce memory usage
idle_timeout = 1800            # Decrease to clean up idle sessions faster
```

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

# Secure log file
sudo chmod 644 /var/log/pshell.log
sudo chown root:root /var/log/pshell.log
```

### Process Isolation

Run pshell as dedicated user:

```bash
# Create user
sudo useradd -r -s /bin/false pshell

# Change ownership
sudo chown pshell:pshell /var/log/pshell.log
sudo chown pshell:pshell /var/run/pshell.pid

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
ls -l /var/run/pshell.pid
ls -l /var/log/pshell.log

# Validate configuration
pshell -c /etc/pshell/pshell.conf -t

# Check logs
tail -f /var/log/pshell.log
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
top -p $(cat /var/run/pshell.pid)

# Reduce scrollback
[sessions]
scrollback_lines = 500

# Clean up idle sessions
[sessions]
idle_timeout = 1800
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
  http://localhost:8080/api/sessions/abc123/buffer
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
df -h /var/log

# Check log size
echo ""
echo "3. Log Size:"
ls -lh /var/log/pshell.log

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
grep ERROR /var/log/pshell.log | tail -n 5

echo ""
echo "=== Check Complete ==="
```

## Support and Resources

### Documentation

- API Reference: `docs/API_REFERENCE.md`
- Architecture: `docs/ARCHITECTURE.md`
- Porting Guide: `docs/PORTING.md`
- Examples: `docs/EXAMPLES.md`

### Getting Help

- GitHub Issues: https://github.com/yourusername/pshell/issues
- Mailing List: pshell-users@example.com
- IRC: #pshell on irc.freenode.net

### Reporting Bugs

Include the following information:
1. pshell version (`pshell -v`)
2. Operating system and version
3. Configuration file
4. Relevant log entries
5. Steps to reproduce

---

**Last Updated**: 2026-07-17
**Version**: 1.0.0
