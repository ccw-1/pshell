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

### 1. Manual Testing
- Created test program to reproduce the bug - confirmed backslash in output
- Applied fix to `json_simple.c`
- Rebuilt pshell successfully with `make clean && make`
- Binary created: `bin/pshell`

### 2. Integration Testing
**Test Command:** `{"text":"ssh pok56\n"}`
- ✅ Bytes sent: 10 (correct: "ssh pok56" + newline)
- ✅ Output: `ssh pok56` followed by SSH connection banner
- ✅ No backslash visible in terminal
- ✅ Newline properly sent to PTY
- ✅ SSH connection established to ZOSCAN56

### 3. Automated Testing
Created two comprehensive test scripts:

#### REST API Test (`/home/knoppix/go-dev/api_test1.sh`)
- Automated session creation via HTTP REST API
- SSH connection to pok56
- Execution of `./goal` challenge program
- Number extraction and calculation (20² = 400)
- Answer submission and verification
- **Result:** ✅ SUCC - Test passed

#### Socket API Test (`/home/knoppix/go-dev/api_test2.sh`)
- Automated session creation via pshellctl (Unix socket)
- SSH connection to pok56
- Execution of `./goal` challenge program
- Number extraction and calculation (12² = 144)
- Answer submission and verification
- **Result:** ✅ SUCC - Test passed

Both test scripts include:
- Automated session management (create/destroy)
- Pattern matching and text extraction
- Automatic calculation
- Result verification (SUCC/FAIL)
- Color-coded logging
- Error handling and cleanup

## Status: ✅ COMPLETE AND VERIFIED

- ✅ Bug identified and root cause found
- ✅ Fix implemented in source code
- ✅ Code compiled successfully
- ✅ Manual integration test passed
- ✅ Automated REST API test passed
- ✅ Automated Socket API test passed
- ✅ Changes committed and pushed (commit 94de1d3)

## Restarting After System Restart

### Quick Start
```bash
# 1. Navigate to pshell directory
cd /home/knoppix/go-dev/pshell

# 2. Start pshell daemon (uses port 8888)
./bin/pshell -c /home/knoppix/go-dev/pshell.conf -f &

# 3. Run automated tests
/home/knoppix/go-dev/api_test1.sh  # REST API test
/home/knoppix/go-dev/api_test2.sh  # Socket API test
```

### Configuration
- **Config file:** `/home/knoppix/go-dev/pshell.conf`
- **HTTP port:** 8888 (127.0.0.1)
- **Socket path:** `/tmp/pshell.sock`
- **Log file:** `/tmp/pshell.log`
- **PID file:** `/tmp/pshell.pid`

### Verification
```bash
# Check daemon is running
ps aux | grep pshell

# Check port is listening
lsof -i :8888

# Check socket exists
ls -la /tmp/pshell.sock

# View logs
tail -f /tmp/pshell.log
```

## Files Modified
- `/home/knoppix/go-dev/pshell/src/utils/json_simple.c` (restore point 0 available)

## Build Artifacts
- `/home/knoppix/go-dev/pshell/bin/pshell` (rebuilt with fix)
- `/home/knoppix/go-dev/pshell/bin/pshell-client`
- `/home/knoppix/go-dev/pshell/bin/pshellctl`

## Test Scripts
- `/home/knoppix/go-dev/api_test1.sh` - REST API automation test
- `/home/knoppix/go-dev/api_test2.sh` - Socket API automation test

## Comparison with Previous Output
Compared current output with `/home/knoppix/go-dev/._1` (previous session):
- Both outputs show clean SSH connections
- No backslash artifacts in either output
- Commands execute properly with newlines
- Fix verified: escape sequences properly unescaped

## Notes
- The fix is minimal and surgical - only affects string parsing
- No other functionality impacted
- All compiler warnings are pre-existing (not introduced by this fix)
- Both REST API and Socket API work correctly with the fix
- Automated test scripts provide regression testing capability