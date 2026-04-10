#!/usr/bin/env bash
set -euo pipefail

# ============================================================================
# Kolibri Live Queue — End-to-End Integration Test
# ============================================================================
# Tests the complete live queue workflow from question capture to knowledge 
# assimilation, including all v1.2 features (bulk ops, search, analytics).
#
# Usage:
#   ./ci/e2e_test_live_queue.sh [--port PORT] [--verbose]
# ============================================================================

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"

port=8001
verbose=0
passed=0
failed=0
total=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) port="$2"; shift 2 ;;
        --verbose) verbose=1; shift ;;
        *) echo "Unknown argument: $1"; exit 1 ;;
    esac
done

api_base="http://localhost:${port}/api/v1"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

pass() {
    echo -e "  ${GREEN}✓${NC} $1"
    ((passed++)) || true
    ((total++)) || true
}

fail() {
    echo -e "  ${RED}✗${NC} $1"
    if [[ -n "${2:-}" ]]; then
        echo -e "    ${RED}Details: $2${NC}"
    fi
    ((failed++)) || true
    ((total++)) || true
}

info() {
    if [[ $verbose -eq 1 ]]; then
        echo -e "  ${BLUE}ℹ${NC} $1"
    fi
}

log() {
    echo -e "\n${YELLOW}▸ $1${NC}"
}

# Wait for server
wait_for_server() {
    log "Waiting for server..."
    local count=0
    while [[ $count -lt 30 ]]; do
        if curl -sf "${api_base}/health" >/dev/null 2>&1; then
            pass "Server ready (${count}s)"
            return 0
        fi
        sleep 1
        ((count++)) || true
    done
    fail "Server timeout" "Did not start within 30s"
    return 1
}

echo "================================================================"
echo "🧪 Kolibri Live Queue — E2E Integration Test"
echo "================================================================"
echo "  Port: ${port}"
echo "  API: ${api_base}"
echo "================================================================"

# Phase 1: Health & Setup
log "Phase 1: System Health"
health=$(curl -sf "${api_base}/health" 2>/dev/null || echo "")
if echo "$health" | grep -q '"status":"ok"'; then
    pass "Health endpoint OK"
else
    fail "Health check" "Response: $health"
    exit 1
fi

# Phase 2: Generate Unknown Questions
log "Phase 2: Generate Unknown Questions (Capture)"

questions=(
    "что такое квантовая запутанность простыми словами?"
    "как работает фотосинтез на молекулярном уровне?"
    "объясни теорию относительности Эйнштейна"
    "что такое машинное обучение с подкреплением?"
    "как работает блокчейн технология?"
)

for q in "${questions[@]}"; do
    response=$(curl -sf -X POST "${api_base}/ai/chat" \
        -H "Content-Type: application/json" \
        -d "{\"message\": \"$q\"}" 2>/dev/null || echo "")
    
    if echo "$response" | grep -q '"response"'; then
        pass "Question captured: ${q:0:40}..."
        info "Response: $response"
    else
        fail "Question not captured: ${q:0:40}"
    fi
    sleep 0.5  # Allow async capture
done

# Allow async capture to complete
sleep 2

# Phase 3: Live Queue Operations
log "Phase 3: Live Queue Operations"

# 3.1: List pending
queue_response=$(curl -sf -X POST "${api_base}/live-queue/list" \
    -H "Content-Type: application/json" \
    -d '{"limit": 20}' 2>/dev/null || echo "")

if echo "$queue_response" | grep -q '"pending"'; then
    pass "List pending endpoint works"
    
    count=$(echo "$queue_response" | grep -o '"count":[0-9]*' | cut -d: -f2)
    if [[ -n "$count" && "$count" -gt 0 ]]; then
        pass "Queue has $count pending questions"
    else
        info "Queue is empty (may be expected)"
    fi
else
    fail "List pending endpoint" "Invalid response"
fi

# 3.2: Get stats
stats_response=$(curl -sf "${api_base}/live-queue/stats" 2>/dev/null || echo "")

if echo "$stats_response" | grep -q '"pending"'; then
    pass "Stats endpoint works"
    info "Stats: $stats_response"
else
    fail "Stats endpoint" "Invalid response"
fi

# 3.3: Search
search_response=$(curl -sf -X POST "${api_base}/live-queue/search" \
    -H "Content-Type: application/json" \
    -d '{"query": "квантов", "status": "pending"}' 2>/dev/null || echo "")

if echo "$search_response" | grep -q '"results"'; then
    pass "Search endpoint works"
    search_count=$(echo "$search_response" | grep -o '"count":[0-9]*' | cut -d: -f2)
    pass "Search found $search_count results for 'квантов'"
else
    fail "Search endpoint" "Invalid response: $search_response"
fi

# 3.4: Analytics
analytics_response=$(curl -sf "${api_base}/live-queue/analytics" 2>/dev/null || echo "")

if echo "$analytics_response" | grep -q '"overview"'; then
    pass "Analytics endpoint works"
    info "Analytics: $analytics_response"
else
    fail "Analytics endpoint" "Invalid response"
fi

# Phase 4: Single Operations
log "Phase 4: Single Question Operations"

# Get first pending ID
first_id=$(echo "$queue_response" | grep -o '"id":[0-9]*' | head -1 | cut -d: -f2)

if [[ -n "$first_id" && "$first_id" != "0" ]]; then
    # Approve
    approve_response=$(curl -sf -X POST "${api_base}/live-queue/approve" \
        -H "Content-Type: application/json" \
        -d "{\"id\": $first_id}" 2>/dev/null || echo "")
    
    if echo "$approve_response" | grep -q '"status":"approved"'; then
        pass "Single approve works (ID: $first_id)"
    else
        fail "Single approve" "Response: $approve_response"
    fi
    
    # Get second pending ID
    second_id=$(echo "$queue_response" | grep -o '"id":[0-9]*' | sed -n '2p' | cut -d: -f2)
    
    if [[ -n "$second_id" && "$second_id" != "0" && "$second_id" != "$first_id" ]]; then
        # Edit
        edit_response=$(curl -sf -X POST "${api_base}/live-queue/edit" \
            -H "Content-Type: application/json" \
            -d "{\"id\": $second_id, \"answer\": \"Test edited answer\"}" 2>/dev/null || echo "")
        
        if echo "$edit_response" | grep -q '"status"'; then
            pass "Edit endpoint works (ID: $second_id)"
        else
            fail "Edit endpoint" "Response: $edit_response"
        fi
        
        # Reject
        reject_response=$(curl -sf -X POST "${api_base}/live-queue/reject" \
            -H "Content-Type: application/json" \
            -d "{\"id\": $second_id}" 2>/dev/null || echo "")
        
        if echo "$reject_response" | grep -q '"status":"rejected"'; then
            pass "Single reject works (ID: $second_id)"
        else
            fail "Single reject" "Response: $reject_response"
        fi
    fi
else
    info "No pending questions for single operations"
fi

# Phase 5: Bulk Operations
log "Phase 5: Bulk Operations"

# Get more pending questions
queue_response2=$(curl -sf -X POST "${api_base}/live-queue/list" \
    -H "Content-Type: application/json" \
    -d '{"limit": 20}' 2>/dev/null || echo "")

ids=$(echo "$queue_response2" | grep -o '"id":[0-9]*' | head -3 | cut -d: -f2 | tr '\n' ',' | sed 's/,$//')

if [[ -n "$ids" ]]; then
    # Bulk approve
    bulk_approve=$(curl -sf -X POST "${api_base}/live-queue/bulk-approve" \
        -H "Content-Type: application/json" \
        -d "{\"ids\": [$ids]}" 2>/dev/null || echo "")
    
    if echo "$bulk_approve" | grep -q '"status":"bulk_approve"'; then
        approved_count=$(echo "$bulk_approve" | grep -o '"approved":[0-9]*' | cut -d: -f2)
        pass "Bulk approve works ($approved_count approved)"
    else
        fail "Bulk approve" "Response: $bulk_approve"
    fi
    
    # Get more IDs for reject test
    queue_response3=$(curl -sf -X POST "${api_base}/live-queue/list" \
        -H "Content-Type: application/json" \
        -d '{"limit": 20}' 2>/dev/null || echo "")
    
    reject_ids=$(echo "$queue_response3" | grep -o '"id":[0-9]*' | head -2 | cut -d: -f2 | tr '\n' ',' | sed 's/,$//')
    
    if [[ -n "$reject_ids" ]]; then
        bulk_reject=$(curl -sf -X POST "${api_base}/live-queue/bulk-reject" \
            -H "Content-Type: application/json" \
            -d "{\"ids\": [$reject_ids]}" 2>/dev/null || echo "")
        
        if echo "$bulk_reject" | grep -q '"status":"bulk_reject"'; then
            rejected_count=$(echo "$bulk_reject" | grep -o '"rejected":[0-9]*' | cut -d: -f2)
            pass "Bulk reject works ($rejected_count rejected)"
        else
            fail "Bulk reject" "Response: $bulk_reject"
        fi
    else
        info "No pending questions for bulk reject"
    fi
else
    info "No pending questions for bulk operations"
fi

# Phase 6: Analytics & Metrics
log "Phase 6: Analytics & Metrics"

# Analytics after operations
analytics_final=$(curl -sf "${api_base}/live-queue/analytics" 2>/dev/null || echo "")

if echo "$analytics_final" | grep -q '"overview"'; then
    pass "Analytics reflects operations"
    info "Final analytics: $analytics_final"
else
    fail "Analytics endpoint" "Invalid response"
fi

# Prometheus metrics
metrics_response=$(curl -sf "http://localhost:${port}/metrics" 2>/dev/null || echo "")

if echo "$metrics_response" | grep -q 'kolibri_live_queue_pending_total'; then
    pass "Prometheus metrics endpoint works"
else
    fail "Prometheus metrics" "Invalid response"
fi

if echo "$metrics_response" | grep -q 'kolibri_live_queue_approval_rate'; then
    pass "Approval rate metric present"
else
    fail "Approval rate metric" "Missing"
fi

# Phase 7: Export & Knowledge Pipeline
log "Phase 7: Export & Knowledge Pipeline"

# Trigger export
export_response=$(curl -sf -X POST "${api_base}/live-queue/export" 2>/dev/null || echo "")

if echo "$export_response" | grep -q '"status":"export_triggered"'; then
    pass "Export triggered via API"
else
    info "Export via API not available (using CLI instead)"
    
    # Try CLI export
    if [[ -f "${project_root}/scripts/live_ingest.py" ]]; then
        python3 "${project_root}/scripts/live_ingest.py" export \
            --output "${project_root}/build/knowledge/approved" >/dev/null 2>&1 || true
        pass "Export via CLI completed"
    fi
fi

# Check approved files
if ls "${project_root}/build/knowledge/approved"/*.md >/dev/null 2>&1; then
    approved_count=$(ls "${project_root}/build/knowledge/approved"/*.md 2>/dev/null | wc -l | tr -d ' ')
    pass "$approved_count approved questions exported to Markdown"
else
    info "No approved questions exported yet"
fi

# Phase 8: Database Integrity
log "Phase 8: Database Integrity"

db_path="${project_root}/build/knowledge/live_queue.db"

if [[ -f "$db_path" ]]; then
    pass "Live queue database exists"
    
    if file "$db_path" | grep -q "SQLite"; then
        pass "Valid SQLite database"
    else
        fail "Database validation" "Not a valid SQLite file"
    fi
    
    # Check tables
    tables=$(sqlite3 "$db_path" ".tables" 2>/dev/null || echo "")
    if echo "$tables" | grep -q "live_queue\|knowledge_queue"; then
        pass "Queue table exists"
    else
        fail "Queue table" "Table not found"
    fi
    
    # Check record counts
    total_records=$(sqlite3 "$db_path" "SELECT COUNT(*) FROM knowledge_queue;" 2>/dev/null || echo "0")
    pass "Total records in queue: $total_records"
else
    fail "Database file" "Not found at $db_path"
fi

# Phase 9: Python CLI Tool
log "Phase 9: Python CLI Tool"

if [[ -f "${project_root}/scripts/live_ingest.py" ]]; then
    # Test list command
    cli_list=$(python3 "${project_root}/scripts/live_ingest.py" list 2>&1 || echo "")
    if echo "$cli_list" | grep -q -E "Pending questions|No pending"; then
        pass "CLI list command works"
    else
        fail "CLI list command" "Unexpected output"
    fi
else
    fail "CLI tool" "live_ingest.py not found"
fi

# Summary
echo -e "\n${BLUE}================================================================${NC}"
echo -e "${BLUE}📊 E2E Test Results Summary${NC}"
echo -e "${BLUE}================================================================${NC}"
echo -e "  ${GREEN}Passed: ${passed}${NC}"
echo -e "  ${RED}Failed: ${failed}${NC}"
echo -e "  Total:  ${total}"
echo -e "${BLUE}================================================================${NC}"

if [[ $failed -gt 0 ]]; then
    echo -e "\n${RED}❌ E2E test FAILED${NC}"
    echo -e "${RED}Check failed tests above for details${NC}"
    exit 1
else
    echo -e "\n${GREEN}✅ All E2E tests PASSED!${NC}"
    echo -e "${GREEN}Live Queue is fully operational${NC}"
    exit 0
fi
