#!/bin/bash
# Socket API Test Script for pshell - Goal Challenge Automation
# Tests JSON parsing fix using pshellctl (Unix domain socket client)

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

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
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

# Get session output
get_output() {
    "$PSHELLCTL" output "$SESSION_ID" 2>/dev/null
}

# Wait for specific text in output
wait_for_text() {
    local search_text="$1"
    local max_attempts="${2:-10}"
    local attempt=0
    
    log_info "Waiting for: '$search_text'"
    
    while [ $attempt -lt $max_attempts ]; do
        OUTPUT=$(get_output)
        if echo "$OUTPUT" | grep -q "$search_text"; then
            log_info "Found: '$search_text'"
            return 0
        fi
        attempt=$((attempt + 1))
        sleep 1
    done
    
    log_error "Timeout waiting for: '$search_text'"
    return 1
}

# Extract number from goal output
extract_number() {
    local output="$1"
    echo "$output" | grep -oP "Remember this number: \K\d+" | tail -1
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
    echo "  pshell Socket API Test - Goal Challenge"
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
    
    # Step 2: SSH to pok56
    log_info "Connecting to pok56..."
    send_text "ssh pok56\n"
    
    if ! wait_for_text "CCW@ZOSCAN56" 30; then
        log_error "Failed to connect to pok56"
        exit 1
    fi
    
    log_info "Connected to ZOSCAN56"
    sleep 1
    
    # Step 3: Run ./goal
    log_info "Running ./goal program..."
    send_text "./goal\n"
    
    if ! wait_for_text "Remember this number:" 10; then
        log_error "Failed to start ./goal"
        exit 1
    fi
    
    sleep 1
    
    # Step 4: Extract the number
    OUTPUT=$(get_output)
    NUMBER=$(extract_number "$OUTPUT")
    
    if [ -z "$NUMBER" ]; then
        log_error "Failed to extract number from output"
        echo "$OUTPUT"
        exit 1
    fi
    
    log_info "Number to square: $NUMBER"
    
    # Step 5: Calculate square
    ANSWER=$((NUMBER * NUMBER))
    log_info "Calculated answer: $ANSWER"
    
    # Step 6: Send answer
    send_text "${ANSWER}\n"
    
    if ! wait_for_text "SUCC\|FAIL" 10; then
        log_error "No response from ./goal"
        exit 1
    fi
    
    sleep 1
    
    # Step 7: Verify result
    FINAL_OUTPUT=$(get_output)
    
    if echo "$FINAL_OUTPUT" | grep -q "SUCC"; then
        echo ""
        log_info "✓ TEST PASSED - Got SUCC response"
        echo ""
        echo "Final output:"
        echo "$FINAL_OUTPUT" | tail -10
        exit 0
    else
        echo ""
        log_error "✗ TEST FAILED - Got FAIL response"
        echo ""
        echo "Final output:"
        echo "$FINAL_OUTPUT" | tail -10
        exit 1
    fi
}

main "$@"