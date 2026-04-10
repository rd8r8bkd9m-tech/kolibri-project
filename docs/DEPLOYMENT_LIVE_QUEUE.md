# Kolibri Live Queue — Deployment & Admin Guide

## 1. Deployment Overview

The Live Knowledge Loop is automatically deployed when starting Kolibri via `./start.sh`. This guide covers configuration, monitoring, and administration.

## 2. Architecture Components

### Backend Services
- **C HTTP Server** (`kolibri_http`): Port 8001
  - Captures low-confidence questions automatically
  - Provides REST API for queue management
  - Exports Prometheus metrics
  
- **Python CLI** (`scripts/live_ingest.py`):
  - Command-line tool for queue management
  - Export approved questions to Markdown
  - Similarity-based draft generation

### Frontend Components
- **LiveQueueDrawer**: React component in workspace drawer
- **API Client**: `frontend/src/api/liveQueue.ts`
- **Types**: `frontend/src/types/liveQueue.ts`

### Data Storage
- **SQLite Database**: `build/knowledge/live_queue.db`
- **Approved Export**: `build/knowledge/approved/*.md`

## 3. Installation & Setup

### Prerequisites
```bash
# Ensure SQLite3 is available
# macOS:
brew install sqlite

# Ubuntu/Debian:
sudo apt install libsqlite3-dev
```

### Build C Server with Live Queue Support
```bash
cd /Users/kolibri/Desktop/kolibri-project

# Compile with SQLite support
cc -O2 -I backend/include -I backend/include/kolibri \
   -I/opt/homebrew/opt/openssl@3/include \
   -DSQLITE_CORE \
   -o kolibri_http backend/src/kolibri_http_server.c \
   build/libkolibri_core.a \
   -L/opt/homebrew/opt/openssl@3/lib -lssl -lcrypto -lm -lpthread -lsqlite3
```

### Start the System
```bash
./start.sh
```

The live queue will be automatically initialized at `build/knowledge/live_queue.db`.

## 4. Configuration

### 4.1 Confidence Threshold

Controls which questions are captured to the live queue.

**Location**: `backend/src/kolibri_http_server.c`

```c
static void capture_to_live_queue(...) {
    if (!g_live_queue) return;
    
    /* Only capture low-confidence responses */
    if (confidence >= 0.4) return;  // ← Change this value
    
    // ...
}
```

**Recommended values:**
- `0.3`: Only very uncertain questions
- `0.4`: Default (balanced)
- `0.5`: More aggressive capture

### 4.2 Database Location

**Location**: `backend/src/kolibri_http_server.c` in `main()`

```c
g_live_queue_db = "build/knowledge/live_queue.db";
```

**Change to absolute path for production:**
```c
g_live_queue_db = "/var/lib/kolibri/live_queue.db";
```

### 4.3 Auto-Refresh Interval (Frontend)

**Location**: `frontend/src/components/LiveQueueDrawer.tsx`

```typescript
const interval = setInterval(loadData, 30000); // 30 seconds
```

### 4.4 Knowledge Pipeline Integration

**Location**: `scripts/knowledge_pipeline.sh`

The pipeline automatically exports approved questions before re-indexing:

```bash
# Export approved live queue questions
queue_db="$output_dir/live_queue.db"
approved_dir="$output_dir/approved"

if [[ -x "$project_root/build/kolibri_queue" ]]; then
    "$project_root/build/kolibri_queue" export --db "$queue_db" --status approved --output "$approved_dir"
fi
```

## 5. API Reference

### 5.1 Live Queue Endpoints

Base URL: `http://localhost:8001/api/v1`

#### List Pending Questions
```http
POST /live-queue/list
Content-Type: application/json

{
  "limit": 50
}
```

**Response:**
```json
{
  "pending": [
    {
      "id": 1,
      "title": "что такое квантовая запутанность?",
      "content": "Нет точных знаний по этому вопросу...",
      "source": "live_capture",
      "created_at": "2026-04-07 15:30:00"
    }
  ],
  "count": 1
}
```

#### Approve Question
```http
POST /live-queue/approve
Content-Type: application/json

{
  "id": 1
}
```

**Response:**
```json
{
  "status": "approved",
  "id": 1
}
```

#### Edit and Approve Question
```http
POST /live-queue/edit
Content-Type: application/json

{
  "id": 1,
  "answer": "Квантовая запутанность — это физическое явление..."
}
```

**Response:**
```json
{
  "status": "edited",
  "id": 1
}
```

#### Reject Question
```http
POST /live-queue/reject
Content-Type: application/json

{
  "id": 1
}
```

**Response:**
```json
{
  "status": "rejected",
  "id": 1
}
```

#### Get Queue Statistics
```http
GET /live-queue/stats
```

**Response:**
```json
{
  "pending": 5,
  "approved": 23,
  "rejected": 2
}
```

#### Trigger Export
```http
POST /live-queue/export
```

**Response:**
```json
{
  "status": "export_triggered",
  "exit_code": 0
}
```

### 5.2 Prometheus Metrics

**Endpoint**: `http://localhost:8001/metrics`

**Available metrics:**
```
# HELP kolibri_live_queue_pending_total Total pending questions in live queue
# TYPE kolibri_live_queue_pending_total gauge
kolibri_live_queue_pending_total 5

# HELP kolibri_live_queue_approved_total Total approved questions
# TYPE kolibri_live_queue_approved_total gauge
kolibri_live_queue_approved_total 23

# HELP kolibri_live_queue_rejected_total Total rejected questions
# TYPE kolibri_live_queue_rejected_total gauge
kolibri_live_queue_rejected_total 2

# HELP kolibri_live_queue_backlog_seconds Age of oldest pending question in seconds
# TYPE kolibri_live_queue_backlog_seconds gauge
kolibri_live_queue_backlog_seconds 120

# HELP kolibri_live_queue_approval_rate Ratio of approved to total processed
# TYPE kolibri_live_queue_approval_rate gauge
kolibri_live_queue_approval_rate 0.92
```

## 6. Monitoring

### 6.1 Prometheus Configuration

Add to your `prometheus.yml`:

```yaml
scrape_configs:
  - job_name: 'kolibri'
    static_configs:
      - targets: ['localhost:8001']
    metrics_path: '/metrics'
    scrape_interval: 15s
```

### 6.2 Grafana Dashboard

Create a dashboard with these panels:

1. **Pending Questions** (Gauge)
   - Query: `kolibri_live_queue_pending_total`
   - Warning threshold: > 20
   - Critical threshold: > 50

2. **Approval Rate** (Gauge)
   - Query: `kolibri_live_queue_approval_rate`
   - Target: > 0.6 (60%)

3. **Queue Growth Rate** (Graph)
   - Query: `rate(kolibri_live_queue_pending_total[5m])`

4. **Processed Questions** (Counter)
   - Query: `kolibri_live_queue_approved_total + kolibri_live_queue_rejected_total`

### 6.3 Alert Rules

Add to your Prometheus alerting rules:

```yaml
groups:
  - name: kolibri-live-queue
    rules:
      - alert: HighPendingQueue
        expr: kolibri_live_queue_pending_total > 50
        for: 10m
        labels:
          severity: warning
        annotations:
          summary: "High pending question queue"
          description: "{{ $value }} questions awaiting moderation"

      - alert: LowApprovalRate
        expr: kolibri_live_queue_approval_rate < 0.4
        for: 1h
        labels:
          severity: warning
        annotations:
          summary: "Low approval rate"
          description: "Only {{ $value | humanizePercentage }} of questions are being approved"
```

## 7. Administration

### 7.1 Daily Workflow

#### Morning Review
```bash
# Check queue status
python3 scripts/live_ingest.py list

# Review and approve high-quality questions
python3 scripts/live_ingest.py approve 1 --answer "Edited answer text"

# Reject spam or irrelevant questions
python3 scripts/live_ingest.py reject 2

# Export approved questions
python3 scripts/live_ingest.py export

# Rebuild knowledge index
scripts/knowledge_pipeline.sh

# Retrain with new knowledge
scripts/auto_train.sh
```

#### Using the Web UI
1. Open workspace drawer (click workspace icon)
2. Click "Live Queue" tab
3. Review pending questions
4. Click question to see details
5. Use "Редактировать" to edit answer
6. Click "Одобрить" or "Отклонить"

### 7.2 Batch Operations

#### Approve All Pending (Careful!)
```bash
# Get all pending IDs
python3 -c "
import sqlite3
conn = sqlite3.connect('build/knowledge/live_queue.db')
cursor = conn.execute('SELECT id FROM live_queue WHERE status = \"pending\"')
for row in cursor.fetchall():
    print(row[0])
conn.close()
" | while read id; do
    curl -s -X POST http://localhost:8001/api/v1/live-queue/approve \
      -H "Content-Type: application/json" \
      -d "{\"id\":$id}"
    echo "Approved #$id"
done
```

#### Export to External KB
```bash
# Export approved questions
python3 scripts/live_ingest.py export --output /tmp/kolibri_approved

# Copy to knowledge base
cp /tmp/kolibri_approved/*.md knowledge/

# Re-index
scripts/knowledge_pipeline.sh
```

### 7.3 Database Maintenance

#### Backup Queue Database
```bash
cp build/knowledge/live_queue.db build/knowledge/live_queue.db.backup.$(date +%Y%m%d)
```

#### Clean Old Rejected Questions
```bash
sqlite3 build/knowledge/live_queue.db <<EOF
DELETE FROM live_queue 
WHERE status = 'rejected' 
  AND reviewed_at < datetime('now', '-30 days');
VACUUM;
EOF
```

#### Check Database Integrity
```bash
sqlite3 build/knowledge/live_queue.db "PRAGMA integrity_check;"
```

### 7.4 Performance Tuning

#### SQLite WAL Mode (Already Enabled)
The database uses WAL (Write-Ahead Logging) for better concurrency:
```sql
PRAGMA journal_mode=WAL;
```

#### Index Optimization
```bash
sqlite3 build/knowledge/live_queue.db <<EOF
CREATE INDEX IF NOT EXISTS idx_live_queue_status 
ON live_queue(status, created_at);
ANALYZE;
EOF
```

## 8. Troubleshooting

### 8.1 Common Issues

#### Live Queue Not Initializing
**Symptoms:** Server logs show "⚠️ Live Queue: failed to initialize"

**Solution:**
```bash
# Check directory permissions
ls -la build/knowledge/

# Create manually if needed
mkdir -p build/knowledge
chmod 755 build/knowledge

# Restart server
pkill kolibri_http
./start.sh
```

#### Database Locked
**Symptoms:** "database is locked" errors

**Solution:**
```bash
# Check for stuck processes
lsof build/knowledge/live_queue.db

# Kill stuck processes if needed
kill -9 <PID>

# Remove journal files
rm -f build/knowledge/live_queue.db-wal
rm -f build/knowledge/live_queue.db-shm
```

#### Queue Growing Unbounded
**Symptoms:** Pending count > 100

**Solutions:**
1. Increase moderator review frequency
2. Lower confidence threshold to capture fewer questions
3. Batch approve/reject using scripts
4. Check for spam/flood attacks

### 8.2 Log Analysis

#### Server Logs
```bash
# Watch for live queue captures
tail -f kolibri_http.log | grep "Live queue"

# Example output:
# 📝 Live queue: captured Q#123 (confidence=0.30): "что такое квантовая запутанность?"
```

#### Python CLI Verbose Mode
```bash
# Add debugging to live_ingest.py
python3 -c "
import sqlite3
conn = sqlite3.connect('build/knowledge/live_queue.db')
cursor = conn.execute('SELECT COUNT(*) FROM live_queue WHERE status = ?', ('pending',))
print(f'Pending questions: {cursor.fetchone()[0]}')
conn.close()
"
```

### 8.3 Recovery Procedures

#### Reset Live Queue
```bash
# WARNING: This will delete all queue data
rm build/knowledge/live_queue.db

# Restart server to recreate
./start.sh
```

#### Migrate to New Version
```bash
# Export current queue
sqlite3 build/knowledge/live_queue.db ".dump" > queue_backup.sql

# After updating code
sqlite3 build/knowledge/live_queue.db < queue_backup.sql
```

## 9. Security Considerations

### 9.1 Access Control

The live queue API currently has **no authentication**. For production:

1. **Add API key authentication**
2. **Restrict to localhost only**
3. **Implement role-based access**

Example nginx configuration:
```nginx
location /api/v1/live-queue/ {
    allow 127.0.0.1;
    deny all;
    
    proxy_pass http://localhost:8001;
}
```

### 9.2 Input Validation

All user input is sanitized via:
- C server: `json_escape()` function
- Python: Parameterized SQL queries
- Frontend: React auto-escaping

### 9.3 Audit Trail

The database tracks:
- `created_at`: When question was captured
- `reviewed_at`: When moderator reviewed it
- `reviewed_by`: Moderator identifier
- `moderation_note`: Review comments

## 10. Scaling

### 10.1 Single Node (Current)
- Suitable for: < 1000 questions/day
- Storage: ~1MB per 1000 questions
- Performance: < 10ms per operation

### 10.2 Multi-Node (Future)
Planned features:
- Swarm sync of approved questions
- Distributed moderation
- Centralized queue server

### 10.3 High Volume Optimizations
If queue grows beyond 10,000 questions:
1. Partition database by month
2. Archive old approved questions
3. Implement pagination in API
4. Add full-text search

## 11. Best Practices

### 11.1 Moderation Guidelines

**Approve when:**
- Question is legitimate knowledge gap
- Draft answer is mostly correct
- Answer can be verified

**Edit when:**
- Draft answer has minor errors
- Answer needs more detail
- Terminology needs correction

**Reject when:**
- Question is spam/nonsense
- Duplicate of existing knowledge
- Outside Kolibri's scope

### 11.2 Answer Quality

**Good answers:**
- Concise (1-3 sentences)
- Factual and verifiable
- Use proper terminology
- Include examples when helpful

**Bad answers:**
- Vague or ambiguous
- Speculative without marking
- Contains errors
- Too verbose or too brief

### 11.3 Maintenance Schedule

**Daily:**
- Review pending queue
- Approve/reject questions

**Weekly:**
- Export approved questions
- Rebuild knowledge index
- Run auto-training

**Monthly:**
- Review queue statistics
- Clean old rejected questions
- Backup database
- Review confidence threshold

## 12. Support

### 12.1 Documentation
- Live Learning Architecture: `docs/live_learning.md`
- Implementation Summary: `docs/IMPLEMENTATION_SUMMARY.md`
- Quick Reference: `docs/LIVE_QUEUE_QUICKREF.md`

### 12.2 Testing
```bash
# Run smoke tests
./ci/smoke_test_live_loop.sh

# Test CLI tool
python3 scripts/live_ingest.py list

# Test API
curl http://localhost:8001/api/v1/live-queue/stats
```

### 12.3 Contact
For issues or questions:
- Check documentation in `docs/`
- Review implementation in `backend/src/kolibri_http_server.c`
- Check frontend component: `frontend/src/components/LiveQueueDrawer.tsx`

---

**Last Updated**: April 7, 2026  
**Version**: 1.0.0  
**Status**: Production Ready
