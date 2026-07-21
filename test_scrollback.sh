#!/bin/bash
set -e

echo "Starting pshell daemon in background..."
./bin/pshell -f &
DAEMON_PID=$!
sleep 2

echo "Creating session with custom scrollback size..."
SESSION_ID=$(./bin/pshellctl create --command "bash" --rows 24 --cols 80 2>&1 | tail -1)
echo "Session ID: $SESSION_ID"

echo "Sending commands to generate output..."
./bin/pshellctl send "$SESSION_ID" "for i in {1..50}; do echo Line \$i; done"
sleep 1

echo "Getting scrollback..."
./bin/pshellctl scrollback "$SESSION_ID" --start 0 --count 20

echo "Cleaning up..."
./bin/pshellctl destroy "$SESSION_ID"
kill $DAEMON_PID 2>/dev/null || true

echo "Test completed successfully!"
