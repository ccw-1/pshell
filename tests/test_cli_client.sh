#!/bin/bash
# Test script for pshell CLI client

set -e

echo "=== Testing pshell CLI Client ==="
echo

# Test 1: Status
echo "Test 1: Check daemon status"
./bin/pshell-client status
echo "✓ Status check passed"
echo

# Test 2: Create session
echo "Test 2: Create session"
SESSION_ID=$(./bin/pshell-client create --command "bash" --rows 24 --cols 80)
echo "Created session: $SESSION_ID"
echo "✓ Session creation passed"
echo

# Test 3: List sessions
echo "Test 3: List sessions"
./bin/pshell-client list | grep -q "$SESSION_ID"
echo "✓ Session listing passed"
echo

# Test 4: Send input
echo "Test 4: Send input"
./bin/pshell-client send "$SESSION_ID" "echo 'Hello from pshell'\n"
echo "✓ Send input passed"
echo

# Test 5: Read output (with delay)
echo "Test 5: Read output"
sleep 3
OUTPUT=$(./bin/pshell-client output "$SESSION_ID")
echo "Output: $OUTPUT"
echo "✓ Read output passed"
echo

# Test 6: Get session info
echo "Test 6: Get session info"
./bin/pshell-client info "$SESSION_ID"
echo "✓ Session info passed"
echo

# Test 7: Destroy session
echo "Test 7: Destroy session"
./bin/pshell-client destroy "$SESSION_ID"
echo "✓ Session destruction passed"
echo

echo "=== All CLI client tests passed ==="
