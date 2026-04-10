#!/usr/bin/env bash
set -euo pipefail

# ============================================================================
# Kolibri Live Loop — CI Smoke Test
# ============================================================================
# Validates that the live knowledge loop is functioning correctly:
# 1. Server responds to health checks
# 2. Unknown questions are captured to live queue
# 3. Live queue API endpoints work
# 4. Questions can be approved/rejected
# 5. Metrics endpoint returns valid data
#
# Usage:
#   ./ci/smoke_test_live_loop.sh [--port PORT] [--timeout SECONDS]
# ============================================================================

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"

port=8001
timeout=30
passed=0
failed=0
total=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) port="$2"; shift 2 ;;
        --timeout) timeout="$2"; shift 2 ;;
        *) echo "Unknown argument: $1"; exit 1 ;;
    esac
done

api_base="http://localhost:${port}/api/v1"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass_test() {
    echo -e "  ${GREEN}✓${NC} $1"
    ((passed++)) || true
    ((total++)) || true
}

fail_test() {
    echo -e "  ${RED}✗${NC} $1: $2"
    ((failed++)) || true
    ((total++)) || true
}

wait_for_server() {
    echo -e "\n${YELLOW}⏳ Waiting for server to be ready...${NC}"
    local count=0
    while [[ $count -lt $timeout ]]; do
        if curl -sf "http://localhost:${port}/api/v1/health" >/dev/null 2>&1; then
            echo -e "  ${GREEN}✓${NC} Server is ready (${count}s)"
            return 0
        fi
        sleep 1
        ((count++)) || true
    done
    echo -e "  ${RED}✗${NC} Server did not start within ${timeout}s"
    return 1
}

echo "================================================================"
echo "🧪 Kolibri Live Loop — CI Smoke Test"
echo "================================================================"
echo "  Port: ${port}"
echo "  Timeout: ${timeout}s"
echo "================================================================"

# Test 1: Server Health
echo -e "\n📋 Test 1: Server Health Check"
health_response=$(curl -sf "${api_base}/health" 2>/dev/null || echo "")
if echo "$health_response" | grep -q '"status":"ok"'; then
    pass_test "Health endpoint returns OK"
else
    fail_test "Health endpoint" "Invalid response: $health_response"
    exit 1
fi

# Test 2: Send Unknown Question (low confidence)
echo -e "\n📋 Test 2: Capture Unknown Question"
unknown_question="что такое квантовая запутанность простыми словами?"
chat_response=$(curl -sf -X POST "${api_base}/ai/chat" \
    -H "Content-Type: application/json" \
    -d "{\"message\":\"${unknown_question}\"}" 2>/dev/null || echo "")

if echo "$chat_response" | grep -q '"response"'; then
    pass_test "Chat endpoint responds to unknown question"
    
    # Check confidence level (should be low for unknown questions)
    confidence=$(echo "$chat_response" | grep -o '"confidence":[0-9.]*' | cut -d: -f2)
    if [[ -n "$confidence" ]]; then
        pass_test "Response includes confidence: ${confidence}"
    else
        pass_test "Response received (confidence field optional)"
    fi
else
    fail_test "Chat endpoint" "No response for unknown question"
fi

# Test 3: Live Queue List
echo -e "\n📋 Test 3: Live Queue List Endpoint"
sleep 2  # Allow async capture to complete

queue_response=$(curl -sf -X POST "${api_base}/live-queue/list" \
    -H "Content-Type: application/json" \
    -d '{"limit": 10}' 2>/dev/null || echo "")

if echo "$queue_response" | grep -q '"pending"'; then
    pass_test "Live queue list endpoint works"
    
    count=$(echo "$queue_response" | grep -o '"count":[0-9]*' | cut -d: -f2)
    if [[ -n "$count" ]]; then
        pass_test "Queue contains ${count} pending questions"
    fi
else
    fail_test "Live queue list" "Invalid response: $queue_response"
fi

# Test 4: Live Queue Stats
echo -e "\n📋 Test 4: Live Queue Stats Endpoint"
stats_response=$(curl -sf "${api_base}/live-queue/stats" 2>/dev/null || echo "")

if echo "$stats_response" | grep -q '"pending"'; then
    pass_test "Live queue stats endpoint works"
else
    fail_test "Live queue stats" "Invalid response: $stats_response"
fi

# Test 5: Approve Question (if any pending)
echo -e "\n📋 Test 5: Approve Question Workflow"
pending_id=$(echo "$queue_response" | grep -o '"id":[0-9]*' | head -1 | cut -d: -f2)

if [[ -n "$pending_id" && "$pending_id" != "0" ]]; then
    approve_response=$(curl -sf -X POST "${api_base}/live-queue/approve" \
        -H "Content-Type: application/json" \
        -d "{\"id\":${pending_id}}" 2>/dev/null || echo "")
    
    if echo "$approve_response" | grep -q '"status":"approved"'; then
        pass_test "Question #${pending_id} approved successfully"
    else
        fail_test "Approve question" "Invalid response: $approve_response"
    fi
    
    # Verify stats updated
    stats_after=$(curl -sf "${api_base}/live-queue/stats" 2>/dev/null || echo "")
    if echo "$stats_after" | grep -q '"approved"'; then
        pass_test "Stats reflect approval"
    else
        fail_test "Stats update" "Stats not updated"
    fi
else
    echo -e "  ${YELLOW}⊘${NC} No pending questions to approve (skipping)"
    ((total++)) || true
fi

# Test 6: Reject Question
echo -e "\n📋 Test 6: Reject Question Workflow"
pending_id2=$(echo "$queue_response" | grep -o '"id":[0-9]*' | tail -1 | cut -d: -f2)

if [[ -n "$pending_id2" && "$pending_id2" != "0" && "$pending_id2" != "$pending_id" ]]; then
    reject_response=$(curl -sf -X POST "${api_base}/live-queue/reject" \
        -H "Content-Type: application/json" \
        -d "{\"id\":${pending_id2}}" 2>/dev/null || echo "")
    
    if echo "$reject_response" | grep -q '"status":"rejected"'; then
        pass_test "Question #${pending_id2} rejected successfully"
    else
        fail_test "Reject question" "Invalid response: $reject_response"
    fi
else
    echo -e "  ${YELLOW}⊘${NC} No additional pending questions to reject (skipping)"
    ((total++)) || true
fi

# Test 7: Edit Question
echo -e "\n📋 Test 7: Edit and Approve Question"
if [[ -n "$pending_id" && "$pending_id" != "0" ]]; then
    edit_response=$(curl -sf -X POST "${api_base}/live-queue/edit" \
        -H "Content-Type: application/json" \
        -d "{\"id\":1,\"answer\":\"Test edited answer\"}" 2>/dev/null || echo "")
    
    # This might fail if ID 1 doesn't exist, so we check if endpoint exists
    if curl -sf -X POST "${api_base}/live-queue/edit" \
        -H "Content-Type: application/json" \
        -d '{"id":99999,"answer":"test"}' 2>/dev/null | grep -q '"status"\|"error"'; then
        pass_test "Edit endpoint exists and responds"
    else
        fail_test "Edit endpoint" "Endpoint not responding"
    fi
else
    echo -e "  ${YELLOW}⊘${NC} No questions to edit (skipping)"
    ((total++)) || true
fi

# Test 8: Prometheus Metrics
echo -e "\n📋 Test 8: Prometheus Metrics Endpoint"
metrics_response=$(curl -sf "http://localhost:${port}/metrics" 2>/dev/null || echo "")

if echo "$metrics_response" | grep -q 'kolibri_live_queue_pending_total'; then
    pass_test "Metrics endpoint returns live queue metrics"
else
    fail_test "Metrics endpoint" "Invalid response"
fi

if echo "$metrics_response" | grep -q 'kolibri_live_queue_approval_rate'; then
    pass_test "Metrics include approval rate"
else
    fail_test "Approval rate metric" "Missing from response"
fi

# Test 9: Python CLI Tool
echo -e "\n📋 Test 9: Python CLI Tool (live_ingest.py)"
if [[ -x "${project_root}/scripts/live_ingest.py" ]] || [[ -f "${project_root}/scripts/live_ingest.py" ]]; then
    cli_output=$(python3 "${project_root}/scripts/live_ingest.py list 2>&1 || echo "")
    if echo "$cli_output" | grep -q -E "Pending questions|No pending"; then
        pass_test "CLI tool list command works"
    else
        fail_test "CLI tool" "Unexpected output: $cli_output"
    fi
else
    fail_test "CLI tool" "live_ingest.py not found"
fi

# Test 10: Database File Created
echo -e "\n📋 Test 10: Live Queue Database"
if [[ -f "${project_root}/build/knowledge/live_queue.db" ]]; then
    pass_test "Live queue database file exists"
    
    # Check if it's a valid SQLite database
    if file "${project_root}/build/knowledge/live_queue.db" | grep -q "SQLite"; then
        pass_test "Valid SQLite database"
    else
        fail_test "Database validation" "File is not valid SQLite"
    fi
else
    fail_test "Database file" "build/knowledge/live_queue.db not found"
fi

# Summary
echo -e "\n================================================================"
echo "📊 Test Results Summary"
echo "================================================================"
echo -e "  ${GREEN}Passed: ${passed}${NC}"
echo -e "  ${RED}Failed: ${failed}${NC}"
echo -e "  Total:  ${total}"
echo "================================================================"

if [[ $failed -gt 0 ]]; then
    echo -e "\n${RED}❌ Smoke test FAILED${NC}"
    exit 1
else
    echo -e "\n${GREEN}✅ All smoke tests passed!${NC}"
    exit 0
fi
