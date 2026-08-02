# Expect Buffer Implementation

## Overview

The expect buffer is a dedicated accumulation buffer that solves the data loss problem when expect operations compete with other consumers of PTY output.

## Problem Statement

**Original Issue:** PTY output was only written to a raw circular buffer. When expect operations tried to read from this buffer, data could be lost if:
- Other consumers (screen buffer, output logging) read from the same buffer
- The circular buffer wrapped around before expect could read
- Multiple expect operations competed for the same data

## Solution Architecture

### 1. Dual-Write System

PTY callback (`session_pty_callback`) now writes to TWO buffers:
- **Raw circular buffer** - For streaming output, screen updates, logging
- **Expect buffer** - Dedicated accumulation buffer (only when expect is active)

```c
/* In session_pty_callback() */
if (n > 0) {
    /* Write to raw circular buffer (always) */
    for (ssize_t i = 0; i < n; i++) {
        session->raw_buffer[session->raw_write_pos] = buffer[i];
        // ... circular buffer management
    }
    
    /* Also write to expect buffer if expect is active */
    if (session->expect_active && session->expect_buffer) {
        for (ssize_t i = 0; i < n; i++) {
            if (session->expect_buffer_used < session->expect_buffer_size) {
                session->expect_buffer[session->expect_buffer_used++] = buffer[i];
            }
        }
    }
}
```

### 2. Expect Buffer Lifecycle

**Activation (session_start_expect):**
1. Allocate expect buffer (64KB, same as raw buffer)
2. Seed buffer with current screen content (not raw buffer, which may be empty)
3. Set `expect_active` flag to true
4. Future PTY output is dual-written

**Deactivation (session_stop_expect):**
1. Clear `expect_active` flag
2. Reset buffer used counter
3. Keep buffer allocated for reuse

**Buffer Seeding:**
```c
/* Seed from screen buffer (has all visible content) */
if (session->screen) {
    char screen_text[65536];
    size_t screen_len = screen_buffer_get_text(session->screen, screen_text, sizeof(screen_text) - 1);
    if (screen_len > 0) {
        memcpy(session->expect_buffer, screen_text, copy_size);
        session->expect_buffer_used = copy_size;
    }
}
```

### 3. Expect Engine Integration

**Context Creation (expect_context_create):**
1. Calls `session_start_expect()` to activate buffer
2. Reads seeded data into context buffer via `session_read_output()`
3. Context buffer now contains historical screen content + future PTY output

**Pattern Matching (expect_pattern):**
1. Tries to match pattern in context buffer
2. If no match, reads more output via `session_read_output()`
3. Appends new data to context buffer
4. Repeats until match or timeout

**Context Destruction (expect_context_destroy):**
1. Calls `session_stop_expect()` to deactivate buffer
2. Frees context resources

### 4. Output Routing

**session_read_output()** routes reads based on expect state:
```c
ssize_t session_read_output(session_t *session, char *buffer, size_t size) {
    if (session->expect_active) {
        return session_read_expect_output(session, buffer, size);
    }
    return session_read_raw_output(session, buffer, size);
}
```

**session_read_expect_output()** returns accumulated data (non-destructive peek):
```c
ssize_t session_read_expect_output(session_t *session, char *buffer, size_t size) {
    size_t to_copy = (size < session->expect_buffer_used) ? size : session->expect_buffer_used;
    if (to_copy > 0) {
        memcpy(buffer, session->expect_buffer, to_copy);
    }
    return (ssize_t)to_copy;
}
```

## Line Position Tracking

Matches include line position relative to cursor:
- **0**: Pattern on current cursor line
- **Negative**: Pattern above cursor (historical/scrolled output)
- **Positive**: Pattern below cursor (rare)

Calculation:
```c
/* Count newlines before match position */
int matched_line = 0;
for (size_t i = 0; i < match.start && buffer[i]; i++) {
    if (buffer[i] == '\n') matched_line++;
}

/* Get cursor position and calculate relative line */
int cursor_row, cursor_col;
session_get_cursor_position(session, &cursor_row, &cursor_col);
int relative_line = matched_line - cursor_row;
```

## Files Modified

### Core Implementation
- **src/session/session.c**
  - Added expect buffer fields to `struct session`
  - Implemented `session_start_expect()`, `session_stop_expect()`, `session_read_expect_output()`
  - Modified `session_pty_callback()` for dual-write
  - Updated `session_read_output()` for routing

- **src/pattern/expect_engine.c**
  - Modified `expect_context_create()` to activate buffer and load seeded data
  - Modified `expect_context_destroy()` to deactivate buffer
  - Added debug logging for pattern matching

- **include/pshell/session.h**
  - Added public API declarations for expect buffer management

### Protocol/Client
- **src/socket/socket_protocol.c**
  - Implemented `handle_expect_pattern()` handler
  - Added `#include "pshell/expect_engine.h"`
  - Added line position calculation
  - Returns JSON: `{"found":true/false, "matched_text":"...", "line":N, "reason":"..."}`

- **src/ctl/pshellctl.c**
  - Fixed JSON parsing to use "found" field (not "matched")
  - Display line position: `Matched: "pattern" at -5`

## Known Issues and Future Work

### 1. Buffer Size Limits
- Expect buffer is 64KB (same as raw buffer)
- If screen content + new output exceeds 64KB, oldest data is lost
- **TODO**: Consider dynamic buffer expansion or configurable size

### 2. Multiple Concurrent Expects
- Current implementation supports one expect operation per session
- Multiple concurrent expects would share the same buffer
- **TODO**: Add per-context buffers or expect operation queuing

### 3. Line Position Accuracy
- Line position is calculated from buffer content, not actual terminal state
- VT100 escape sequences may affect line counting
- **TODO**: Use screen buffer line tracking instead of newline counting

### 4. Memory Management
- Expect buffer is allocated on first use and kept until session destruction
- **TODO**: Consider freeing buffer after inactivity period

### 5. Pattern Matching Performance
- Large buffers (64KB) are searched linearly on each iteration
- **TODO**: Implement incremental matching or Boyer-Moore algorithm

### 6. Timeout Precision
- Timeout uses `usleep(10000)` (10ms) polling
- **TODO**: Use select/poll for more precise timeout handling

### 7. Screen Buffer Seeding
- Seeding from screen buffer may include VT100 escape sequences
- **TODO**: Strip escape sequences or use plain text extraction

### 8. Error Handling
- Limited error reporting when buffer allocation fails
- **TODO**: Add detailed error codes and messages

## Testing

### Basic Test
```bash
SESSION_ID=$(pshellctl create --command bash)
pshellctl send "$SESSION_ID" "echo PATTERN_TEST"
pshellctl expect "$SESSION_ID" "PATTERN" 5000
# Expected: Matched: "PATTERN" at -N
```

### Historical Match Test
```bash
pshellctl send "$SESSION_ID" "echo LINE1"
pshellctl send "$SESSION_ID" "echo LINE2"
pshellctl send "$SESSION_ID" "echo LINE3"
pshellctl expect "$SESSION_ID" "LINE1" 5000
# Expected: Matched: "LINE1" at -N (negative, scrolled up)
```

### Timeout Test
```bash
pshellctl expect "$SESSION_ID" "NONEXISTENT" 1000
# Expected: No match (timeout)
```

## Debugging

Enable debug logging in `pshell.conf`:
```ini
[daemon]
log_level = debug
```

Key log messages:
- `Seeded expect buffer with N bytes from screen buffer`
- `Loaded N bytes of seeded data into expect context`
- `Trying to match pattern 'X' in buffer (size=N)`
- `Pattern matched: 'X'`
- `Expect timeout reached`

## Performance Considerations

1. **Dual-write overhead**: Minimal, simple memory copy
2. **Buffer seeding**: One-time cost on expect start (screen buffer copy)
3. **Pattern matching**: Linear search, O(n*m) worst case
4. **Memory usage**: +64KB per session when expect is active

## Maintenance Checklist

When modifying expect functionality:
- [ ] Test with multiple concurrent sessions
- [ ] Test with large output (>64KB)
- [ ] Test timeout behavior
- [ ] Test pattern matching edge cases (start/end of buffer)
- [ ] Verify line position calculation
- [ ] Check memory leaks (valgrind)
- [ ] Test with different terminal sizes
- [ ] Verify VT100 escape sequence handling

## References

- Original issue: PTY output data loss in expect operations
- Solution: Dedicated expect buffer with dual-write architecture
- Implementation date: 2026-08-02
- Version: pshell 1.0 (expect buffer feature)
