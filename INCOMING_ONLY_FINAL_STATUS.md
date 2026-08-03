# Incoming-Only Expect - Final Status Report

## Implementation Summary (2026-08-03)

### What Was Implemented ✅
1. **EXPECT_FLAG_SKIP_SEEDING** flag in expect_engine.h
2. **expect_context_create()** accepts flags parameter
3. **session_start_expect()** accepts skip_seeding parameter
4. **Socket protocol handler** for expect_pattern_incoming method
5. **pshellctl expect-incoming** command
6. **session_clear_expect_buffer()** API function
7. **Exponential backoff** in expect loop (10ms→20ms→40ms→80ms→100ms)
8. **Baseline tracking** via session_read_pos in expect_context
9. **Backward compatibility** maintained (all existing calls use EXPECT_FLAG_NONE)

### Current Status ❌ NOT WORKING

**Test Results:**
- ✅ Test 1: Historical data correctly times out
- ❌ Test 2: New data times out (should match)
- ✅ Test 3: Regular expect still works

**Root Cause:** The baseline tracking approach has a fundamental flaw:

1. When `skip_seeding=true`, session_start_expect() keeps existing buffer content
2. expect_context_create() reads the buffer to get baseline size
3. **BUT**: session_read_output() is NON-DESTRUCTIVE (peek operation)
4. So baseline = current buffer size (e.g., 50 bytes)
5. PTY callback writes new data, buffer grows to 60 bytes
6. Expect loop reads buffer: total=60, baseline=50, new_data=10 ✓
7. **PROBLEM**: Next iteration reads buffer again: total=60, baseline=60 (we updated it!)
8. No new data detected even though more arrives

### The Real Problem

The baseline tracking logic has a race condition:
```c
// First read: total=60, baseline=50, new=10 ✓
ctx->session_read_pos += n;  // Now baseline=60

// Second read: total=70, baseline=60, new=10 ✓
ctx->session_read_pos += n;  // Now baseline=70

// But if PTY writes between reads:
// Read: total=70, baseline=70, new=0 ❌ (missed the new data!)
```

### Why It Fails

The PTY callback writes asynchronously. The expect loop polls with exponential backoff. Between polls, new data arrives but we've already updated our baseline, so we miss it.

### Correct Solution

**Option 1: Destructive Read (RECOMMENDED)**
Make session_read_expect_output() CONSUME data:
```c
ssize_t session_read_expect_output(session_t *session, char *buffer, size_t size) {
    size_t to_copy = min(size, session->expect_buffer_used);
    memcpy(buffer, session->expect_buffer, to_copy);
    
    // CONSUME: shift remaining data
    memmove(session->expect_buffer, session->expect_buffer + to_copy,
            session->expect_buffer_used - to_copy);
    session->expect_buffer_used -= to_copy;
    
    return to_copy;
}
```

Then in incoming-only mode:
1. Clear buffer at start
2. PTY callback accumulates new data
3. Each read CONSUMES data
4. Only NEW data accumulates

**Option 2: Separate Incoming Buffer**
Create a second buffer specifically for incoming-only mode that starts empty.

**Option 3: Timestamp-Based**
Track when expect started, only match data with timestamps after that.

### Recommendation

Implement **Option 1 (Destructive Read)** because:
- Simplest to implement
- No additional memory overhead
- Natural semantics (read = consume)
- Works for both regular and incoming-only modes

### Files Modified
- `include/pshell/expect_engine.h` - Added EXPECT_FLAG_SKIP_SEEDING, session_read_pos
- `src/pattern/expect_engine.c` - Flags support, baseline tracking, exponential backoff
- `include/pshell/session.h` - Added skip_seeding parameter, session_clear_expect_buffer()
- `src/session/session.c` - Conditional seeding, session_clear_expect_buffer() implementation
- `src/socket/socket_protocol.c` - Added handle_expect_pattern_incoming handler
- `src/ctl/pshellctl.c` - Added cmd_expect_incoming command
- `src/http/rest_api.c` - Updated expect_context_create call

### Next Steps

1. Revert baseline tracking approach
2. Implement destructive read in session_read_expect_output()
3. Simplify expect loop (remove baseline logic)
4. Test thoroughly
5. Update documentation

### Lessons Learned

- Non-destructive peek + baseline tracking doesn't work with asynchronous writes
- Polling with backoff isn't sufficient for race-free operation
- Destructive read (consume) is the correct semantic for stream processing
