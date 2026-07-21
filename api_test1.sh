#!/bin/bash
# API Test Script for pshell - Goal Challenge Automation
# Tests JSON parsing fix and automated interaction with ./goal program

set -e

API_BASE="http://127.0.0.1:8888/api"
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
    log_info "Creating new bash session..."
    RESPONSE=$(curl -s -X POST "$API_BASE/sessions" \
        -H "Content-Type: application/json" \
        -d '{"command":"/bin/bash"}')
    
    SESSION_ID=$(echo "$RESPONSE" | jq -r '.id')
    
    if [ -z "$SESSION_ID" ] || [ "$SESSION_ID" = "null" ]; then
        log_error "Failed to create session"
        exit 1
    fi
    
    log_info "Session created: $SESSION_ID"
}

# Send text to session
send_text() {
    local text="$1"
    log_info "Sending: $(echo "$text" | sed 's/\\n/<newline>/g')"
    
    curl -s -X POST "$API_BASE/sessions/$SESSION_ID/send" \
        -H "Content-Type: application/json" \
        -d "{\"text\":\"$text\"}" > /dev/null
}

# Get session output
get_output() {
    curl -s -X GET "$API_BASE/sessions/$SESSION_ID/output" | jq -r '.output'
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
        curl -s -X DELETE "$API_BASE/sessions/$SESSION_ID" > /dev/null
    fi
}

trap cleanup EXIT

# Main test flow
main() {
    echo "=========================================="
    echo "  pshell API Test - Goal Challenge"
    echo "=========================================="
    echo ""
    
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
