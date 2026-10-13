# Scrollback Buffer API Reference

## Overview

The scrollback buffer feature allows you to retrieve terminal history from pshell sessions. Each session maintains a configurable scrollback buffer that stores terminal output lines.

## Configuration

### Session Creation

When creating a session, you can specify the scrollback buffer size:

**Default**: 1000 lines (when `scrollback_lines` is 0 or omitted)

#### REST API

```bash
POST /api/sessions
Content-Type: application/json

{
  "command": "bash",
  "rows": 24,
  "cols": 80,
  "scrollback_lines": 5000
}
```

#### pshellctl

```bash
# Uses default 1000 lines
pshellctl create --command bash --rows 24 --cols 80

# Explicit scrollback size
pshellctl create --command bash --scrollback 5000
```

**Note**: The buffer size is set per session at creation time — via the `scrollback_lines` JSON parameter (REST) or the `--scrollback` flag (pshellctl).

## Retrieving Scrollback

### REST API

**Endpoint**: `GET /api/sessions/:id/scrollback`

**Query Parameters**:
- `start_line` (optional, default: 0) - Starting line index (0-based)
- `count` (optional, default: 100) - Number of lines to retrieve

**Example**:
```bash
curl -X GET "http://localhost:8080/api/sessions/abc123/scrollback?start_line=0&count=50" \
-H "X-API-Key: <your-generated-key>"
```

**Response**:
```json
{
  "scrollback_size": 5000,
  "scrollback_used": 150,
  "start_line": 0,
  "count": 50,
  "lines": [
    "Line 1 content",
    "Line 2 content",
    "..."
  ]
}
```

**Response Fields**:
- `scrollback_size` - Total configured buffer size
- `scrollback_used` - Number of lines currently in buffer
- `start_line` - Starting line index of returned data
- `count` - Number of lines returned
- `lines` - Array of line strings

### pshellctl

**Command**: `pshellctl scrollback <session-id> [--start N] [--count N]`

**Options**:
- `--start N` - Starting line index (default: 0)
- `--count N` - Number of lines to retrieve (default: 100)

**Example**:
```bash
# Get first 50 lines
pshellctl scrollback abc123 --start 0 --count 50

# Get lines 100-149
pshellctl scrollback abc123 --start 100 --count 50
```

**Output**:
```
Scrollback: 150/5000 lines used
Showing lines 0-49:

Line 1 content
Line 2 content
...
```

## Use Cases

### 1. Debugging

Retrieve command history and output for troubleshooting:

```bash
# Get last 100 lines
pshellctl scrollback $SESSION_ID --start 0 --count 100
```

### 2. Log Analysis

Extract specific portions of terminal output:

```bash
# Get lines 500-599
curl "http://localhost:8080/api/sessions/$SESSION_ID/scrollback?start_line=500&count=100"
```

### 3. Session Replay

Retrieve entire session history:

```bash
# Get all available lines
USED=$(curl -s "http://localhost:8080/api/sessions/$SESSION_ID/scrollback?count=1" | jq -r .scrollback_used)
curl "http://localhost:8080/api/sessions/$SESSION_ID/scrollback?start_line=0&count=$USED"
```

## Limitations

1. **Buffer Size**: Once the buffer is full, oldest lines are discarded (FIFO)
2. **Line Indexing**: Lines are indexed from 0 (oldest) to scrollback_used-1 (newest)
3. **Invalid Range**: Requesting lines outside the valid range returns an error
4. **Memory**: Large scrollback buffers consume more memory per session

## Error Handling

### Invalid Session ID

```json
{
  "status": "error",
  "error": {
    "message": "Session not found"
  }
}
```

### Invalid Range

```json
{
  "status": "error",
  "error": {
    "message": "Invalid start_line"
  }
}
```

### Invalid Count

```json
{
  "status": "error",
  "error": {
    "message": "Invalid count"
  }
}
```

## Implementation Details

### Internal API

The following C functions are available for internal use:

```c
// Get configured scrollback buffer size
int session_get_scrollback_size(const session_t *session);

// Get number of lines currently in buffer
int session_get_scrollback_used(const session_t *session);

// Get specific line from scrollback buffer
ssize_t session_get_scrollback_line(const session_t *session, 
                                    int line_index, 
                                    char *buffer, 
                                    size_t size);
```

### Buffer Management

- Scrollback buffer is implemented as a circular buffer in `screen_buffer.c`
- Lines are stored as character cells with attributes
- When buffer is full, oldest lines are overwritten
- Line retrieval converts character cells to plain text

## Best Practices

1. **Choose Appropriate Buffer Size**: Balance memory usage vs. history needs
   - Small scripts: 1000 lines (default)
   - Interactive sessions: 5000-10000 lines
   - Long-running processes: 10000+ lines

2. **Pagination**: Retrieve scrollback in chunks for large buffers
   ```bash
   # Get 100 lines at a time
   for i in {0..900..100}; do
     pshellctl scrollback $SESSION_ID --start $i --count 100
   done
   ```

3. **Monitor Buffer Usage**: Check `scrollback_used` to know when buffer is full
   ```bash
   curl "http://localhost:8080/api/sessions/$SESSION_ID/scrollback?count=1" | \
     jq '{size: .scrollback_size, used: .scrollback_used}'
   ```

4. **Error Handling**: Always check response status before processing lines
   ```python
   resp = requests.get(f'http://localhost:8080/api/sessions/{sid}/scrollback')
   if resp.json().get('status') == 'success':
       lines = resp.json()['result']['lines']
   ```
