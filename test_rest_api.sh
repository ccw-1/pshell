#!/bin/bash
# Test REST API output directly

set -e

# Start pshell
pkill -9 pshell 2>/dev/null || true
sleep 1
cd /home/knoppix/go-dev/pshell
./bin/pshell -c config/pshell.conf -f > /tmp/pshell_rest_test.log 2>&1 &
PSHELL_PID=$!
sleep 3

echo "Creating session..."
SESSION_RESPONSE=$(curl -s -X POST http://localhost:8888/api/sessions \
  -H "Content-Type: application/json" \
  -d '{"command":"bash","rows":24,"cols":80}')

SESSION_ID=$(echo "$SESSION_RESPONSE" | grep -o '"id":"[^"]*"' | cut -d'"' -f4)
echo "Session ID: $SESSION_ID"

sleep 2

echo "Sending 'who' command..."
curl -s -X POST "http://localhost:8888/api/sessions/$SESSION_ID/input" \
  -H "Content-Type: application/json" \
  -d '{"text":"who\n"}' > /dev/null

sleep 2

echo ""
echo "=== REST API Output (raw JSON) ==="
OUTPUT=$(curl -s "http://localhost:8888/api/sessions/$SESSION_ID/output")
echo "$OUTPUT" | head -20

echo ""
echo "=== Checking for CRLF (\\r\\n) in output ==="
if echo "$OUTPUT" | grep -q '\\r\\n'; then
    echo "✓ Found CRLF sequences (\\r\\n)"
else
    echo "✗ No CRLF found - only LF (\\n)"
fi

echo ""
echo "=== Character analysis of first line ==="
FIRST_LINE=$(echo "$OUTPUT" | grep -o '"output":"[^"]*"' | head -1 | cut -d'"' -f4)
echo "First 100 chars: $FIRST_LINE" | head -c 100
echo ""
echo "Escape sequences: $(echo "$FIRST_LINE" | grep -o '\\[rn]' | head -10 | tr '\n' ' ')"

# Cleanup
curl -s -X DELETE "http://localhost:8888/api/sessions/$SESSION_ID" > /dev/null
kill -9 $PSHELL_PID 2>/dev/null || true
