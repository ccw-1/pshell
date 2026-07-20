# pshell Session Restart Guide

## Quick Start After System Restart

### 1. Start pshell Daemon
```bash
cd /home/knoppix/go-dev/pshell
./bin/pshell -c /home/knoppix/go-dev/pshell.conf -f &
```

**Configuration:**
- HTTP API: `http://127.0.0.1:8888`
- Socket: `/tmp/pshell.sock`
- Logs: `/tmp/pshell.log`

### 2. Verify Daemon is Running
```bash
# Check process
ps aux | grep pshell | grep -v grep

# Check HTTP port
lsof -i :8888

# Check socket
ls -la /tmp/pshell.sock

# View logs
tail -f /tmp/pshell.log
```

### 3. Run Automated Tests
```bash
# REST API test
/home/knoppix/go-dev/api_test1.sh

# Socket API test (using pshellctl)
/home/knoppix/go-dev/api_test2.sh
```

Both tests should output `✓ TEST PASSED - Got SUCC response`

## What Was Fixed

### JSON Parsing Bug (Commit 94de1d3)
**Problem:** Escape sequences in JSON strings were miscounted, causing backslashes to appear in output instead of properly unescaped characters.

**Example:**
- Before: `{"text":"ssh pok56\n"}` → Terminal shows `ssh pok56\`
- After: `{"text":"ssh pok56\n"}` → Terminal properly sends newline

**Root Cause:** The `parse_string_value()` function in `src/utils/json_simple.c` counted both the backslash AND the escaped character when calculating string length.

**Fix:** Changed from counter-based to position-based length calculation (`raw_len = end - start`).

## Test Scripts

### api_test1.sh - REST API Test
Automated test using HTTP REST API:
1. Creates session via POST `/api/sessions`
2. Connects to pok56 via SSH
3. Runs `./goal` challenge program
4. Extracts number and calculates square
5. Submits answer and verifies SUCC response
6. Cleans up session

### api_test2.sh - Socket API Test
Automated test using pshellctl (Unix socket):
1. Creates session via `pshellctl create`
2. Connects to pok56 via SSH
3. Runs `./goal` challenge program
4. Extracts number and calculates square
5. Submits answer and verifies SUCC response
6. Cleans up session

## Manual Testing

### Create Session
```bash
# REST API
curl -X POST http://127.0.0.1:8888/api/sessions \
  -H "Content-Type: application/json" \
  -d '{"command":"/bin/bash"}'

# Socket API
./bin/pshellctl create --command /bin/bash
```

### Send Command
```bash
# REST API (replace SESSION_ID)
curl -X POST http://127.0.0.1:8888/api/sessions/SESSION_ID/send \
  -H "Content-Type: application/json" \
  -d '{"text":"echo hello\n"}'

# Socket API
./bin/pshellctl send SESSION_ID "echo hello\n"
```

### Get Output
```bash
# REST API
curl http://127.0.0.1:8888/api/sessions/SESSION_ID/output

# Socket API
./bin/pshellctl output SESSION_ID
```

### Delete Session
```bash
# REST API
curl -X DELETE http://127.0.0.1:8888/api/sessions/SESSION_ID

# Socket API
./bin/pshellctl destroy SESSION_ID
```

## Troubleshooting

### Port 8080 Already in Use
The default config uses port 8888 to avoid conflicts. If you need to change it:
```bash
# Edit config file
vim /home/knoppix/go-dev/pshell.conf

# Change port under [http] section
port = 9999
```

### Socket Not Found
```bash
# Check if daemon is running
ps aux | grep pshell

# Check socket configuration
grep socket_path /home/knoppix/go-dev/pshell.conf

# Restart daemon if needed
pkill -f bin/pshell
./bin/pshell -c /home/knoppix/go-dev/pshell.conf -f &
```

### Tests Failing
```bash
# Check daemon logs
tail -50 /tmp/pshell.log

# Verify SSH access to pok56
ssh pok56 echo "test"

# Run tests with verbose output
bash -x /home/knoppix/go-dev/api_test1.sh
```

## Build Information

### Rebuild from Source
```bash
cd /home/knoppix/go-dev/pshell
make clean
make
```

### Binaries
- `bin/pshell` - Main daemon
- `bin/pshell-client` - HTTP client
- `bin/pshellctl` - Socket client

### Source Files Modified
- `src/utils/json_simple.c` - JSON parsing fix applied

## Documentation

- **BUGFIX_STATUS.md** - Detailed bug analysis and fix documentation
- **SESSION_RESTART.md** - This file
- **README.md** - Project overview
- **config/pshell-dev.conf** - Development configuration (port 8080)
- **/home/knoppix/go-dev/pshell.conf** - Production configuration (port 8888)

## Git Status

**Latest Commit:** 94de1d3 - "Fix JSON string parsing bug causing escape sequences to be miscounted"

**Modified Files:**
- src/utils/json_simple.c
- BUGFIX_STATUS.md

**Branch:** main

**Remote:** github.com:ccw-1/pshell.git

## Next Steps

After verifying the daemon is running and tests pass:
1. Use the REST API or Socket API to create sessions
2. Send commands with proper escape sequences (`\n`, `\t`, etc.)
3. Verify output shows no backslash artifacts
4. Confirm interactive programs work correctly

## Support

For issues or questions:
1. Check `/tmp/pshell.log` for error messages
2. Review BUGFIX_STATUS.md for detailed fix information
3. Run automated tests to verify functionality
4. Check process status and port bindings
