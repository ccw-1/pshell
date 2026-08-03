# Expect Incoming-Only Pattern Matching Design

## Requirement

Add API to match patterns ONLY against incoming/new data, ignoring data already on screen or in buffer.

## Current Behavior

The existing `expect_pattern()` API:
1. Seeds expect buffer with current screen content (historical data)
2. Accumulates new PTY output
3. Matches pattern against: **historical + new data**

Example:
```
Screen already shows: "Welcome\nUsername: "
User calls: expect_pattern("Username:")
Result: IMMEDIATE match (from historical data)
```

## Desired Behavior (Incoming-Only)

New API should:
1. **Skip seeding** - Don't load historical screen content
2. Start with empty buffer
3. Match pattern against: **only new data arriving after expect starts**

Example:
```
Screen already shows: "Welcome\nUsername: "
User calls: expect_pattern_incoming("Username:")
Result: NO match (ignores historical)
         Waits for NEW "Username:" to appear
```

## Use Cases

### 1. Waiting for Next Prompt
```bash
# Already at prompt, run command, wait for NEXT prompt
pshellctl send "$SESSION" "long_running_command\n"
pshellctl expect-incoming "$SESSION" "\\$ " 30000
# Ignores current prompt, waits for new one after command completes
```

### 2. Detecting New Events
```bash
# Monitor for new log entries, ignore existing logs
pshellctl send "$SESSION" "tail -f /var/log/app.log\n"
pshellctl expect-incoming "$SESSION" "ERROR" 60000
# Only matches ERROR that appears AFTER tail starts
```

### 3. Avoiding False Positives
```bash
# Screen shows: "Password: " from previous failed attempt
pshellctl send "$SESSION" "ssh user@host\n"
pshellctl expect-incoming "$SESSION" "Password:" 5000
# Waits for NEW password prompt, ignores old one
```

## Implementation Options

### Option 1: Add Flag to expect_context_create (Recommended)

**Pros:**
- Minimal API changes
- Backward compatible
- Flexible (can be extended with more flags)

**Cons:**
- Slightly more complex API

**API Changes:**
```c
/* Add flags enum */
typedef enum {
    EXPECT_FLAG_NONE = 0,
    EXPECT_FLAG_SKIP_SEEDING = 1 << 0,  /* Don't seed from screen */
    EXPECT_FLAG_CASE_INSENSITIVE = 1 << 1,  /* Future: case-insensitive */
} expect_flags_t;

/* Modify expect_context_create */
expect_context_t *expect_context_create(session_t *session, int timeout_ms, expect_flags_t flags);

/* Usage */
ctx = expect_context_create(session, 5000, EXPECT_FLAG_SKIP_SEEDING);
```

### Option 2: New Function expect_pattern_incoming

**Pros:**
- Clear, explicit API
- No changes to existing functions
- Easy to understand

**Cons:**
- Code duplication
- More functions to maintain

**API Changes:**
```c
/* New function */
expect_result_t expect_pattern_incoming(expect_context_t *ctx, const char *pattern,
                                        bool case_sensitive, match_result_t *result);

/* Implementation: Same as expect_pattern but skip seeding */
```

### Option 3: Add Parameter to expect_pattern

**Pros:**
- Single function handles both cases
- No new functions

**Cons:**
- Changes existing API (breaking change)
- Less clear semantics

**API Changes:**
```c
/* Modify expect_pattern */
expect_result_t expect_pattern(expect_context_t *ctx, const char *pattern,
                               bool case_sensitive, bool incoming_only,
                               match_result_t *result);
```

## Recommended Solution: Option 1 (Flags)

### Implementation Steps

1. **Add flags enum to expect_engine.h:**
```c
typedef enum {
    EXPECT_FLAG_NONE = 0,
    EXPECT_FLAG_SKIP_SEEDING = 1 << 0,
} expect_flags_t;
```

2. **Modify expect_context_create:**
```c
expect_context_t *expect_context_create(session_t *session, int timeout_ms, expect_flags_t flags) {
    // ... existing code ...
    
    ctx->flags = flags;
    
    /* Activate expect buffer in session */
    if (session_start_expect(session) < 0) {
        // ... error handling ...
    }
    
    /* Conditionally seed buffer based on flags */
    if (!(flags & EXPECT_FLAG_SKIP_SEEDING)) {
        ssize_t seeded = session_read_output(session, ctx->buffer, ctx->buffer_size - 1);
        if (seeded > 0) {
            ctx->buffer[seeded] = '\0';
            ctx->buffer_used = seeded;
            log_debug("Loaded %zd bytes of seeded data into expect context", seeded);
        }
    } else {
        log_debug("Skipped seeding (incoming-only mode)");
    }
    
    return ctx;
}
```

3. **Update expect_context_t struct:**
```c
typedef struct {
    session_t *session;
    char *buffer;
    size_t buffer_size;
    size_t buffer_used;
    time_t start_time;
    int timeout_ms;
    expect_flags_t flags;  /* NEW */
} expect_context_t;
```

4. **Add socket protocol handler:**
```c
static void handle_expect_pattern_incoming(socket_connection_t *conn, 
                                           json_value_t *params, 
                                           session_manager_t *mgr) {
    // Same as handle_expect_pattern but pass EXPECT_FLAG_SKIP_SEEDING
    expect_context_t *ctx = expect_context_create(session, timeout, EXPECT_FLAG_SKIP_SEEDING);
    // ... rest of implementation ...
}
```

5. **Add pshellctl command:**
```bash
pshellctl expect-incoming <session-id> <pattern> <timeout>
```

6. **Add REST API endpoint:**
```
POST /api/sessions/{id}/expect-incoming
{
  "pattern": "regex",
  "timeout": 5000,
  "case_sensitive": true
}
```

## Testing

### Test 1: Basic Incoming-Only
```bash
SESSION=$(pshellctl create bash)
pshellctl send "$SESSION" "echo ALREADY_HERE"
sleep 2
# Should NOT match (historical)
pshellctl expect-incoming "$SESSION" "ALREADY_HERE" 2000
# Expected: timeout

pshellctl send "$SESSION" "echo NEW_DATA"
sleep 1
# Should match (new)
pshellctl expect-incoming "$SESSION" "NEW_DATA" 2000
# Expected: Matched: "NEW_DATA" at 0
```

### Test 2: Prompt Detection
```bash
SESSION=$(pshellctl create bash)
# Already at prompt
pshellctl send "$SESSION" "sleep 5"
# Should NOT match current prompt, wait for next one
pshellctl expect-incoming "$SESSION" "\\$ " 10000
# Expected: Matched after 5 seconds
```

### Test 3: Comparison with Regular Expect
```bash
SESSION=$(pshellctl create bash)
pshellctl send "$SESSION" "echo TEST_PATTERN"
sleep 2

# Regular expect: immediate match (historical)
pshellctl expect "$SESSION" "TEST_PATTERN" 2000
# Expected: Matched: "TEST_PATTERN" at -N

# Incoming-only: timeout (ignores historical)
pshellctl expect-incoming "$SESSION" "TEST_PATTERN" 2000
# Expected: No match (timeout)
```

## Backward Compatibility

- Existing API unchanged: `expect_context_create(session, timeout, EXPECT_FLAG_NONE)`
- Default behavior preserved (seeding enabled)
- New functionality opt-in via flags

## Future Extensions

The flags approach allows future enhancements:
- `EXPECT_FLAG_CASE_INSENSITIVE` - Case-insensitive matching
- `EXPECT_FLAG_MULTILINE` - Multiline regex mode
- `EXPECT_FLAG_NO_TIMEOUT` - Infinite timeout
- `EXPECT_FLAG_CLEAR_ON_MATCH` - Clear buffer after match

## Documentation Updates

Files to update:
- `EXPECT_BUFFER_IMPLEMENTATION.md` - Add incoming-only section
- `MAINTENANCE.md` - Add testing procedures
- `TODO.md` - Mark as completed
- `README.md` - Add examples
- API documentation - New flags and usage

## Summary

**Answer to User's Question:**
Yes, it's possible with the existing architecture! We need to:
1. Add a flag parameter to `expect_context_create()`
2. Skip the seeding step when flag is set
3. Add new protocol handler and client command

The implementation is straightforward and maintains backward compatibility.
