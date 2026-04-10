# Live Knowledge Loop — Quick Reference

## 🚀 Quick Start (One Command)
```bash
./start.sh
```

## 📋 Check Pending Questions
```bash
python3 scripts/live_ingest.py list
```

## ✅ Approve a Question
```bash
# With default draft answer
python3 scripts/live_ingest.py approve 1

# With edited answer
python3 scripts/live_ingest.py approve 1 --answer "Correct answer text"
```

## ❌ Reject a Question
```bash
python3 scripts/live_ingest.py reject 1
```

## 📦 Export & Retrain
```bash
# Export approved questions and rebuild index
scripts/knowledge_pipeline.sh

# Run auto-training with new knowledge
scripts/auto_train.sh
```

## 🔍 API Endpoints

### List Pending Questions
```bash
curl -X POST http://localhost:8001/api/v1/live-queue/list \
  -H "Content-Type: application/json" \
  -d '{"limit": 10}'
```

### Approve Question
```bash
curl -X POST http://localhost:8001/api/v1/live-queue/approve \
  -H "Content-Type: application/json" \
  -d '{"id": 1}'
```

### Get Queue Stats
```bash
curl http://localhost:8001/api/v1/live-queue/stats
# Returns: {"pending": 5, "approved": 20, "rejected": 2}
```

## 🎯 How It Works

1. **User asks question** → C server checks confidence
2. **Low confidence (< 0.4)** → Question captured to live queue
3. **Draft answer generated** → Based on similar documents
4. **Moderator reviews** → Via UI, CLI, or API
5. **Approved** → Exported to `build/knowledge/approved/`
6. **Re-indexed** → Included in knowledge pipeline
7. **Auto-trained** → Genome evolves with new knowledge
8. **Future questions** → Answered correctly! ✅

## 📁 Key Files

| File | Purpose |
|------|---------|
| `build/knowledge/live_queue.db` | SQLite database with queue |
| `build/knowledge/approved/*.md` | Exported approved questions |
| `scripts/live_ingest.py` | CLI management tool |
| `frontend/src/components/LiveQueueDrawer.tsx` | Moderation UI |
| `docs/live_learning.md` | Full documentation |

## ⚙️ Configuration

### Change Confidence Threshold
Edit `backend/src/kolibri_http_server.c`:
```c
// In capture_to_live_queue() function
if (confidence >= 0.4) return;  // Change 0.4 to your threshold
```

### Change Auto-Refresh Interval
Edit `frontend/src/components/LiveQueueDrawer.tsx`:
```typescript
const interval = setInterval(loadData, 30000); // Change 30000 to ms
```

## 🐛 Troubleshooting

### Live Queue Not Initializing
```bash
# Check if directory exists
ls -la build/knowledge/

# Manually create if needed
mkdir -p build/knowledge
```

### SQLite Missing
```bash
# macOS
brew install sqlite

# Ubuntu/Debian
sudo apt install libsqlite3-dev
```

### Queue Database Corrupted
```bash
# Reset the queue
rm build/knowledge/live_queue.db
# Restart server — it will recreate
./start.sh
```

## 📊 Metrics to Watch

- **Pending Count**: Should stay low (< 20)
- **Approval Rate**: Target > 60%
- **Queue Growth**: Monitor for spikes
- **Response Time**: Should not increase (async operation)

## 🎓 Moderator Best Practices

1. **Review Daily**: Don't let queue grow
2. **Edit Answers**: Improve draft quality
3. **Reject Irrelevant**: Keep KB clean
4. **Be Consistent**: Similar questions → similar answers
5. **Test After Training**: Verify new knowledge works

---

**Full Documentation**: [docs/live_learning.md](live_learning.md)  
**Implementation Details**: [docs/IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md)
