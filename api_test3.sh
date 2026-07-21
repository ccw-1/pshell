#!/bin/bash
# Socket API Test Script for pshell - Screen Display Test
# Tests screen buffer rendering by displaying exactly what's on screen

set -e

PSHELLCTL="/home/knoppix/go-dev/pshell/bin/pshellctl"
SESSION_ID=""

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Create a new session
create_session() {
    log_info "Creating new bash session via pshellctl..."
    
    SESSION_ID=$("$PSHELLCTL" create --command /bin/bash 2>&1)
    
    if [ -z "$SESSION_ID" ]; then
        log_error "Failed to create session"
        exit 1
    fi
    
    log_info "Session created: $SESSION_ID"
}

# Send text to session
send_text() {
    local text="$1"
    log_info "Sending: $(echo "$text" | sed 's/\\n/<newline>/g')"
    
    "$PSHELLCTL" send "$SESSION_ID" "$text" > /dev/null 2>&1
}

# Get screen state
get_screen() {
    "$PSHELLCTL" screen "$SESSION_ID" 2>/dev/null
}

# Cleanup session
cleanup() {
    if [ -n "$SESSION_ID" ]; then
        log_info "Cleaning up session: $SESSION_ID"
        "$PSHELLCTL" destroy "$SESSION_ID" > /dev/null 2>&1
    fi
}

trap cleanup EXIT

# Main test flow
main() {
    echo "=========================================="
    echo "  pshell Screen Display Test"
    echo "=========================================="
    echo ""
    
    # Check if pshellctl exists
    if [ ! -x "$PSHELLCTL" ]; then
        log_error "pshellctl not found at $PSHELLCTL"
        exit 1
    fi
    
    # Step 1: Create session
    create_session
    sleep 2
    
    # Step 2: Send 'who' command
    log_info "Sending 'who' command..."
    send_text "who\n"
    
    # Wait for command to complete
    sleep 2
    
    # Step 3: Get screen state
    log_info "Retrieving screen state (24 lines x 80 cols)..."
    SCREEN=$(get_screen)
    
    echo ""
    echo "=========================================="
    echo "  Screen Content (as displayed)"
    echo "=========================================="
    echo "$SCREEN"
    echo "=========================================="
    echo ""
    
    log_info "✓ Screen display test complete"
}

main "$@"
