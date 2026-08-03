# Incoming-Only Expect Implementation Status

## Current State (2026-08-03)

### Implementation Complete
- ✅ EXPECT_FLAG_SKIP_SEEDING flag added to expect_engine.h
- ✅ expect_context_create accepts flags parameter
- ✅ session_start_expect accepts skip_seeding parameter
- ✅ Socket protocol handler for expect_pattern_incoming
- ✅ pshellctl expect-incoming command
- ✅ session_clear_expect_buffer API function
- ✅ Backward compatibility maintained (all existing calls use EXPECT_FLAG_NONE)

### Root Cause Identified

**The expect loop times out even when new data arrives because:**

1. **Buffer cleared correctly**: When skip_seeding=true, session_start_expect clears expect_buffer
2. **PTY callback writes correctly**: New data from PTY is written to expect_buffer asynchronously
3. **Expect loop polls too fast**: The loop reads, finds empty buffer, sleeps 10ms, repeats
4. **Race condition**: Expect loop may complete timeout before PTY callback writes enough data

### The Real Problem

```c
// In expect_pattern() loop:
ssize_t n = session_read_output(ctx->session, read_buf, sizeof(read_buf) - 1);

if (n == 0) {
    // No data but session alive, sleep briefly
    usleep(10000);  /* 10ms - TOO SHORT! */
    continue;
}
```

The 10ms sleep is insufficient for:
- Shell command execution time
- PTY buffering delays
- Scheduler latency

### Solution Options

**Option A: Increase sleep time (SIMPLE)**
```c
usleep(50000);  /* 50ms instead of 10ms */
```
Pros: Simple, works for most cases
Cons: Still a race condition, just less likely

**Option B: Exponential backoff (BETTER)**
```c
int sleep_ms = 10;
while (no_data && !timeout) {
    usleep(sleep_ms * 1000);
    sleep_ms = min(sleep_ms * 2, 100);  // Cap at 100ms
}
```
Pros: Responsive for fast data, efficient for slow data
Cons: More complex

**Option C: Condition variable (BEST)**
Add pthread condition variable to signal when expect_buffer has new data.
Pros: No polling, immediate response
Cons: Requires threading changes

### Recommended Fix

Implement **Option B (Exponential Backoff)** as it balances simplicity and effectiveness without requiring threading infrastructure changes.

### Test Results

```bash
# Test 1: Historical data correctly times out ✅
echo BEFORE
expect-incoming "BEFORE" → timeout (correct)

# Test 2: New data times out ❌ (BUG)
expect-incoming "AFTER" (start)
echo AFTER
→ timeout (incorrect - should match)
```

### Next Steps

1. Implement exponential backoff in expect_pattern loop
2. Test with various timeout values
3. Verify both historical timeout and new data matching work
4. Update documentation with working implementation
