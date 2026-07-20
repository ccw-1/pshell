# pshell JSON Parsing Bug Fix - Status Report

## Date: 2026-07-20

## Problem Identified
The `/send` API endpoint was not properly executing commands because escape sequences in JSON strings were being miscounted, causing backslashes to appear in the output instead of properly unescaped characters.

### Symptoms
- Sending `{"text":"ssh pok56\n"}` resulted in `ssh pok56\` appearing on screen
- The backslash indicated the shell was treating it as a line continuation
- Commands were not executing because the newline was not being sent to the PTY

## Root Cause
In `src/utils/json_simple.c`, the `parse_string_value()` function had a bug in its length calculation logic:

1. When counting string length, it would increment `len++` for both the backslash AND the escaped character
2. This caused the allocated buffer to be too large
3. During the unescape phase, the loop would run past the actual escape sequences
4. Result: backslashes were copied literally instead of being converted to their escape codes

## Fix Applied
**File:** `src/utils/json_simple.c`
**Function:** `parse_string_value()`

### Changes Made:
1. Replaced the length-counting logic with a proper end-position finder
2. Calculate `raw_len = end - start` instead of incrementing a counter
3. Fixed the unescape loop to properly handle escape sequences
4. Added proper position update: `p->pos = end + 1` to move past closing quote

### Code Diff:
```c
// OLD: Buggy length calculation
size_t len = 0;
while (p->pos < p->len && p->json[p->pos] != '"') {
    if (p->json[p->pos] == '\\') {
        p->pos++; // Skip escape char
        if (p->pos >= p->len) return NULL;
    }
    p->pos++;
    len++;  // BUG: This counts both \ and the escaped char
}

// NEW: Correct length calculation
size_t end = start;
while (end < p->len && p->json[end] != '"') {
    if (p->json[end] == '\\') {
        end++; // Skip escape char
        if (end >= p->len) return NULL;
    }
    end++;
}
size_t raw_len = end - start;
```

## Testing Performed
1. Created test program to reproduce the bug - confirmed backslash in output
2. Applied fix to `json_simple.c`
3. Rebuilt pshell successfully with `make clean && make`
4. Binary created: `bin/pshell`

## Status: READY FOR TESTING
- ✅ Bug identified and root cause found
- ✅ Fix implemented in source code
- ✅ Code compiled successfully
- ⏸️ **PAUSED** - Waiting for pok56 SSH issue to be resolved before final testing

## Next Steps (After System Restart)
1. Start pshell daemon: `./bin/pshell -c config/pshell-dev.conf -f`
2. Create session via REST API
3. Test with: `{"text":"ssh pok56\n"}`
4. Verify SSH connection establishes properly
5. Confirm no backslash appears in output
6. Verify newline is properly sent to PTY

## Files Modified
- `/home/knoppix/go-dev/pshell/src/utils/json_simple.c` (restore point 0 available)

## Build Artifacts
- `/home/knoppix/go-dev/pshell/bin/pshell` (rebuilt with fix)
- `/home/knoppix/go-dev/pshell/bin/pshell-client`
- `/home/knoppix/go-dev/pshell/bin/pshellctl`

## Notes
- The fix is minimal and surgical - only affects string parsing
- No other functionality should be impacted
- All compiler warnings are pre-existing (not introduced by this fix)
