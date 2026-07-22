# Output Logging Reference

## Overview

The output logging feature allows you to capture all terminal output from pshell sessions to a file. This is useful for:
- Recording complete session history beyond scrollback buffer limits
- Audit trails and compliance requirements
- Debugging and troubleshooting
- Post-session analysis and replay

## Key Features

- **Append Mode**: Logs are appended to existing files, preserving previous content
- **Raw PTY Data**: Captures raw terminal output including all escape sequences
- **Buffered Writes**: Uses 8KB buffer for efficient I/O performance
- **Automatic Flush**: Buffer is flushed on session destroy
- **No Size Limit**: Unlike scrollback buffer, log files can grow indefinitely

## Configuration

### Session Creation

Output logging is configured when creating a session via the `output_file` parameter.

#### REST API

```bash
POST /api/sessions
Content-Type: application/json

{
  "command": "bash",
  "rows": 24,
  "cols": 80,
  "output_file": "/path/to/logfile.log"
}
```

**Parameters:**
- `output_file` (optional, string): Absolute path to log file
  - If omitted or null, no logging is performed
  - File is created if it doesn't exist (mode 0644)
  - Existing files are appended to (not truncated)

**Example:**
```bash
curl -X POST http://localhost:8080/api/sessions \
  -H "X-API-Key: mykey" \
  -H "Content-Type: application/json" \
  -d '{
    "command": "bash",
    "rows": 24,
    "cols": 80,
    "scrollback_lines": 5000,
    "output_file": "/var/log/pshell/session.log"
  }'
```

#### pshellctl

```bash
pshellctl create --command bash --output /path/to/logfile.log
```

**Options:**
- `--output <file>`: Path to output log file
- Can be combined with other options (`--scrollback`, `--rows`, `--cols`)

**Examples:**
```bash
# Basic output logging
pshellctl create --command bash --output /tmp/session.log

# With custom scrollback
pshellctl create --command bash --scrollback 5000 --output /tmp/session.log

# SSH session with logging
pshellctl create --command "ssh user@host" --output /var/log/ssh-sessions.log
```

#### Unix Socket Protocol

```json
{
  "method": "create_session",
  "params": {
    "command": "bash",
    "rows": 24,
    "cols": 80,
    "output_file": "/path/to/logfile.log"
  }
}
```

## Log Rotation

### Overview

Log rotation automatically manages log file size by creating multiple numbered files and rotating them when a line limit is reached. This prevents individual log files from growing too large while maintaining complete history.

### Configuration

Rotation is configured via two parameters:
- `rotate_count` (integer): Number of rotation files to maintain (e.g., 4 creates .0, .1, .2, .3)
- `rotate_lines` (integer): Maximum lines per file before rotation

**File Naming:**
- Active file: `output.log` (specified path)
- Rotated files: `output.log.0`, `output.log.1`, `output.log.2`, `output.log.3`
- Oldest file has highest number (e.g., `.3` is oldest when rotate_count=4)

### REST API

```bash
curl -X POST http://localhost:8080/api/sessions \
  -H "X-API-Key: mykey" \
  -H "Content-Type: application/json" \
  -d '{
    "command": "bash",
    "output_file": "/var/log/session.log",
    "rotate_count": 4,
    "rotate_lines": 1000
  }'
```

**Parameters:**
- `rotate_count` (optional, integer): Number of rotation files (0 = no rotation)
- `rotate_lines` (optional, integer): Lines per file before rotation (0 = no rotation)

### pshellctl

```bash
pshellctl create --command bash \
  --output /var/log/session.log \
  --rotate 4:1000
```

**Format:** `--rotate n:m`
- `n`: Number of rotation files (rotate_count)
- `m`: Lines per file (rotate_lines)

**Examples:**
```bash
# Rotate with 4 files, 1000 lines each
pshellctl create --command bash --output /tmp/session.log --rotate 4:1000

# Rotate with 10 files, 5000 lines each
pshellctl create --command bash --output /var/log/app.log --rotate 10:5000

# Combined with scrollback
pshellctl create --command bash --scrollback 2000 \
  --output /tmp/session.log --rotate 5:2000
```

### Rotation Behavior

**When Rotation Occurs:**
1. Line counter increments on each newline (`\n`) character
2. When `current_line_count >= rotate_lines`:
   - Current buffer is flushed to disk
   - Files are rotated (oldest deleted, others shifted)
   - New file is created
   - Line counter resets to 0

**Rotation Process:**
```
Before rotation (rotate_count=4, at 1000 lines):
  session.log      (1000 lines, active)
  session.log.0    (1000 lines)
  session.log.1    (1000 lines)
  session.log.2    (1000 lines)
  session.log.3    (1000 lines) <- oldest

After rotation:
  session.log      (0 lines, new file)
  session.log.0    (1000 lines, was session.log)
  session.log.1    (1000 lines, was session.log.0)
  session.log.2    (1000 lines, was session.log.1)
  session.log.3    (1000 lines, was session.log.2)
  [old .3 deleted]
```

**Total Capacity:**
- With `rotate_count=4` and `rotate_lines=1000`:
  - Active file: up to 1000 lines
  - 4 rotated files: 4000 lines
  - **Total: ~5000 lines** across all files

### Use Cases

**1. Long-Running Sessions**
```bash
# Keep last 50,000 lines (10 files × 5000 lines)
pshellctl create --command "tail -f /var/log/syslog" \
  --output /var/log/pshell/syslog-monitor.log \
  --rotate 10:5000
```

**2. High-Volume Output**
```bash
# Rotate frequently for verbose applications
pshellctl create --command "./verbose-app" \
  --output /tmp/app.log \
  --rotate 20:500
```

**3. Disk Space Management**
```bash
# Limit total disk usage (4 files × 1000 lines ≈ 4MB)
pshellctl create --command bash \
  --output /var/log/session.log \
  --rotate 4:1000
```

**4. Audit Trail with Retention**
```bash
# Keep last 100,000 lines of audit history
pshellctl create --command "ssh admin@prod" \
  --output /var/log/audit/admin-session.log \
  --rotate 20:5000
```

### Performance Impact

- **Line Counting**: Minimal overhead (single comparison per newline)
- **Rotation**: Brief pause during file operations (~10-50ms)
- **Disk I/O**: Rotation involves file renames (fast) and one file deletion

### Limitations

- Rotation is based on **line count**, not file size
- Lines with no newline character don't trigger rotation
- Very long lines (no `\n`) can exceed expected file size
- Rotation occurs during write, may cause brief output delay

## Log File Format

### Raw PTY Output

Log files contain **raw PTY output** including:
- All printable characters
- Control characters (Ctrl-C, Ctrl-D, etc.)
- ANSI/VT100 escape sequences
- Terminal control codes

**Example log content:**
```
$ ls -la
total 48
drwxr-xr-x  6 user user 4096 Jan 15 10:30 .
drwxr-xr-x 20 user user 4096 Jan 15 10:25 ..
-rw-r--r--  1 user user  220 Jan 15 10:25 .bash_logout
^[[H^[[2J$ clear
$ echo "Hello World"
Hello World
$ ^C
```

### Escape Sequences

The log preserves all terminal escape sequences:
- `^[[H^[[2J` - Clear screen
- `^[[1;31m` - Color codes
- `^[[K` - Clear to end of line
- `\r\n` - Carriage return + newline

To view raw escape sequences:
```bash
cat -v /path/to/logfile.log
```

To strip escape sequences:
```bash
sed 's/\x1b\[[0-9;]*m//g' /path/to/logfile.log
```

## Use Cases

### 1. Audit Trail

Record all commands and output for compliance:

```bash
# Create session with timestamped log
SESSION_ID=$(pshellctl create --command bash \
  --output "/var/log/audit/session-$(date +%Y%m%d-%H%M%S).log")

# All activity is logged
pshellctl send $SESSION_ID "whoami\n"
pshellctl send $SESSION_ID "sudo systemctl restart nginx\n"

# Log file contains complete history
```

### 2. Debugging

Capture output for troubleshooting:

```bash
# Start application with logging
SESSION_ID=$(pshellctl create --command "./myapp" \
  --output "/tmp/myapp-debug.log")

# Reproduce issue
pshellctl send $SESSION_ID "test command\n"

# Analyze log file
grep ERROR /tmp/myapp-debug.log
```

### 3. Long-Running Sessions

Capture output beyond scrollback buffer:

```bash
# Create session with both scrollback and logging
pshellctl create --command "tail -f /var/log/syslog" \
  --scrollback 1000 \
  --output "/tmp/syslog-capture.log"

# Scrollback buffer: last 1000 lines (in memory)
# Log file: complete history (on disk)
```

### 4. Session Replay

Record and replay terminal sessions:

```bash
# Record session
SESSION_ID=$(pshellctl create --command bash \
  --output "/tmp/session-recording.log")

# Perform actions
pshellctl send $SESSION_ID "ls -la\n"
pshellctl send $SESSION_ID "cat file.txt\n"

# Replay with timing (requires scriptreplay or similar)
cat /tmp/session-recording.log
```

### 5. Multiple Sessions to One Log

Append multiple sessions to a single log file:

```bash
# Session 1
pshellctl create --command "ssh server1" \
  --output "/var/log/all-sessions.log"

# Session 2 (appends to same file)
pshellctl create --command "ssh server2" \
  --output "/var/log/all-sessions.log"

# Both sessions logged to same file
```

## Performance Considerations

### Buffering

- **Buffer Size**: 8KB (8192 bytes)
- **Write Strategy**: Buffered writes for performance
- **Flush Triggers**:
  - Buffer full (8KB accumulated)
  - Session destroy
  - Process exit

### Disk I/O

Output logging adds minimal overhead:
- Buffered writes reduce syscall frequency
- Asynchronous to terminal emulation
- No impact on interactive responsiveness

### Disk Space

Monitor disk usage for long-running sessions:

```bash
# Check log file size
du -h /path/to/logfile.log

# Rotate logs periodically
logrotate /etc/logrotate.d/pshell-sessions
```

## Error Handling

### File Creation Failure

If the output file cannot be created or opened:
- Session creation **continues** without logging
- Warning is logged to daemon log
- No error returned to client

**Example:**
```bash
# Invalid path - session created, logging disabled
pshellctl create --command bash --output "/nonexistent/dir/file.log"
# Session ID returned, but no logging occurs
```

### Write Errors

If writes fail during session:
- Errors logged to daemon log
- Session continues normally
- Subsequent writes attempted

### Permissions

Ensure daemon has write permissions:

```bash
# Create log directory
sudo mkdir -p /var/log/pshell
sudo chown pshell:pshell /var/log/pshell
sudo chmod 755 /var/log/pshell

# Create session
pshellctl create --command bash \
  --output "/var/log/pshell/session.log"
```

## Best Practices

### 1. Use Absolute Paths

Always specify absolute paths for log files:

```bash
# Good
pshellctl create --command bash --output "/var/log/session.log"

# Bad (relative to daemon's working directory)
pshellctl create --command bash --output "session.log"
```

### 2. Organize by Date/User

Structure log files for easy management:

```bash
# By date
LOG_DIR="/var/log/pshell/$(date +%Y-%m-%d)"
mkdir -p "$LOG_DIR"
pshellctl create --command bash --output "$LOG_DIR/session-$USER.log"

# By user
pshellctl create --command bash \
  --output "/var/log/pshell/users/$USER/session-$(date +%H%M%S).log"
```

### 3. Implement Log Rotation

Use logrotate to manage log file size:

```bash
# /etc/logrotate.d/pshell-sessions
/var/log/pshell/*.log {
    daily
    rotate 7
    compress
    delaycompress
    missingok
    notifempty
    create 0644 pshell pshell
}
```

### 4. Combine with Scrollback

Use both features for different purposes:

```bash
# Scrollback: recent history (fast, in-memory)
# Output log: complete history (persistent, on-disk)
pshellctl create --command bash \
  --scrollback 1000 \
  --output "/var/log/pshell/session.log"
```

### 5. Secure Sensitive Data

Be cautious with sensitive information:

```bash
# Restrict log file permissions
pshellctl create --command "ssh admin@prod" \
  --output "/var/log/secure/admin-session.log"

chmod 600 /var/log/secure/admin-session.log
```

## Comparison: Output Logging vs Scrollback

| Feature | Output Logging | Scrollback Buffer |
|---------|---------------|-------------------|
| **Storage** | Disk (file) | Memory (RAM) |
| **Size Limit** | Unlimited (disk space) | Fixed (e.g., 1000 lines) |
| **Persistence** | Survives daemon restart | Lost on daemon restart |
| **Performance** | Buffered disk I/O | In-memory (faster) |
| **Access** | File system | API/pshellctl |
| **Format** | Raw PTY data | Processed lines |
| **Use Case** | Long-term storage | Recent history |

## Troubleshooting

### Log File Not Created

**Problem**: Session created but log file doesn't exist

**Solutions**:
1. Check file path is absolute
2. Verify directory exists and is writable
3. Check daemon logs for errors:
   ```bash
   journalctl -u pshell -f
   ```

### Incomplete Logs

**Problem**: Log file missing recent output

**Cause**: Buffer not flushed yet (8KB buffer)

**Solution**: Destroy session to force flush:
```bash
pshellctl destroy <session-id>
```

### Permission Denied

**Problem**: Cannot write to log file

**Solutions**:
1. Check daemon user permissions
2. Create directory with correct ownership:
   ```bash
   sudo mkdir -p /var/log/pshell
   sudo chown pshell:pshell /var/log/pshell
   ```

### Disk Space Issues

**Problem**: Disk full from large log files

**Solutions**:
1. Implement log rotation
2. Monitor disk usage:
   ```bash
   df -h /var/log
   ```
3. Compress old logs:
   ```bash
   gzip /var/log/pshell/*.log.1
   ```

## API Reference

### Session Creation Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `command` | string | No | "bash" | Command to execute |
| `rows` | integer | No | 24 | Terminal rows |
| `cols` | integer | No | 80 | Terminal columns |
| `scrollback_lines` | integer | No | 1000 | Scrollback buffer size |
| `output_file` | string | No | null | Output log file path |

### Internal Implementation

**C Structure:**
```c
struct session {
    // ... other fields ...
    
    /* Output logging */
    char output_file[512];      // Log file path
    int output_fd;              // File descriptor (-1 if disabled)
    char *output_buffer;        // Write buffer (8KB)
    size_t output_buffer_used;  // Bytes in buffer
    size_t output_buffer_size;  // Buffer size (8192)
};
```

**Functions:**
- `session_open_output_file()` - Open log file in append mode
- `session_write_output_log()` - Write data to buffer
- `session_flush_output_log()` - Flush buffer to disk
- `session_close_output_file()` - Close file and free buffer

## Examples

### Python Script

```python
import requests
import time

# Create session with logging
response = requests.post('http://localhost:8080/api/sessions',
    headers={'X-API-Key': 'mykey'},
    json={
        'command': 'bash',
        'output_file': '/tmp/python-session.log'
    })

session_id = response.json()['id']

# Send commands
requests.post(f'http://localhost:8080/api/sessions/{session_id}/send',
    headers={'X-API-Key': 'mykey'},
    json={'text': 'echo "Hello from Python"\n'})

time.sleep(1)

# Destroy session (flushes log)
requests.delete(f'http://localhost:8080/api/sessions/{session_id}',
    headers={'X-API-Key': 'mykey'})

# Read log file
with open('/tmp/python-session.log', 'r') as f:
    print(f.read())
```

### Shell Script

```bash
#!/bin/bash

# Create session with logging
SESSION_ID=$(pshellctl create --command bash \
    --output "/tmp/script-session.log")

# Run commands
pshellctl send "$SESSION_ID" "date\n"
sleep 1
pshellctl send "$SESSION_ID" "whoami\n"
sleep 1
pshellctl send "$SESSION_ID" "pwd\n"
sleep 1

# Destroy session
pshellctl destroy "$SESSION_ID"

# Display log
echo "=== Session Log ==="
cat /tmp/script-session.log
```

## See Also

- [Scrollback Buffer API](API_SCROLLBACK.md) - In-memory terminal history
- [Escape Sequences](ESCAPE_SEQUENCES.md) - Terminal control codes
- [REST API Reference](../wiki/api-reference.md) - Complete API documentation
