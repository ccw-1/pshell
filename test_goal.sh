#!/bin/bash
# Test script to get goal number from pok2b using pshell

set -e

PSHELL_API="http://127.0.0.1:8888/api"

echo "Starting pshell daemon..."
cd /home/knoppix/go-dev/pshell
killall -9 pshell 2>/dev/null || true
./bin/pshell -f -c config/pshell.conf > /tmp/pshell_test.log 2>&1 &
DAEMON_PID=$!
sleep 3

if ! ps -p $DAEMON_PID > /dev/null; then
    echo "ERROR: Daemon failed to start"
    cat /tmp/pshell_test.log
    exit 1
fi

echo "Daemon running (PID: $DAEMON_PID)"

# Create SSH session to pok2b
echo "Creating SSH session to pok2b..."
SID=$(curl -s -X POST "$PSHELL_API/sessions" \
    -H "Content-Type: application/json" \
    -d '{"command":"ssh -t pok2b","rows":24,"cols":80}' | jq -r '.id')

if [ -z "$SID" ] || [ "$SID" = "null" ]; then
    echo "ERROR: Failed to create session"
    kill $DAEMON_PID
    exit 1
fi

echo "Session created: $SID"

# Wait for SSH connection and profile to load
echo "Waiting for SSH connection (12 seconds)..."
sleep 12

# Press Enter multiple times to get past profile prompts
echo "Clearing profile prompts..."
for i in {1..5}; do
    curl -s -X POST "$PSHELL_API/sessions/$SID/send" \
        -H "Content-Type: application/json" \
        -d '{"text":"\n"}' > /dev/null
    sleep 1
done

# Execute bash goal command
echo "Executing: bash goal"
curl -s -X POST "$PSHELL_API/sessions/$SID/send" \
    -H "Content-Type: application/json" \
    -d '{"text":"bash goal\n"}' > /dev/null

# Wait for command to execute
echo "Waiting for command execution (5 seconds)..."
sleep 5

# Read output
echo "Reading output..."
OUTPUT=$(curl -s "$PSHELL_API/sessions/$SID/output" | jq -r '.output')

echo ""
echo "=== SESSION OUTPUT ==="
echo "$OUTPUT"
echo "======================"
echo ""

# Extract goal number
GOAL=$(echo "$OUTPUT" | grep -oP 'goal is \K\d+' | tail -1)

if [ -z "$GOAL" ]; then
    # Try alternative patterns
    GOAL=$(echo "$OUTPUT" | grep -oP '\d{5}' | tail -1)
fi

if [ -n "$GOAL" ]; then
    echo "✓ SUCCESS: Goal number is: $GOAL"
    EXIT_CODE=0
else
    echo "✗ FAILED: Could not extract goal number"
    echo ""
    echo "Checking for 'goal' keyword in output..."
    echo "$OUTPUT" | grep -i "goal" || echo "No 'goal' found in output"
    EXIT_CODE=1
fi

# Cleanup
echo ""
echo "Cleaning up..."
curl -s -X DELETE "$PSHELL_API/sessions/$SID" > /dev/null
kill $DAEMON_PID 2>/dev/null

exit $EXIT_CODE
