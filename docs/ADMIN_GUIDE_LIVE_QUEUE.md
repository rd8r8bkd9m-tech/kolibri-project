# Live Queue Moderation — Admin Guide

## Quick Start for Moderators

This guide teaches you how to effectively moderate the Kolibri Live Knowledge Queue.

## 1. Daily Workflow

### Step 1: Check Queue Status
```bash
# Quick overview
python3 scripts/live_ingest.py list

# Or check stats via API
curl http://localhost:8001/api/v1/live-queue/stats
```

### Step 2: Review Pending Questions

**Option A: Web UI (Recommended)**
1. Click workspace icon (bottom-left)
2. Select "Live Queue" tab
3. Browse pending questions
4. Click any question to see details

**Option B: CLI**
```bash
# List with full details
python3 scripts/live_ingest.py list
```

**Option C: API**
```bash
curl -X POST http://localhost:8001/api/v1/live-queue/list \
  -H "Content-Type: application/json" \
  -d '{"limit": 20}'
```

### Step 3: Take Action

For each question, you have three choices:

#### ✅ Approve (Keep Draft Answer)
Use when the draft answer is acceptable as-is.

**Web UI:** Click green ✓ button  
**CLI:** 
```bash
python3 scripts/live_ingest.py approve <ID>
```
**API:**
```bash
curl -X POST http://localhost:8001/api/v1/live-queue/approve \
  -H "Content-Type: application/json" \
  -d '{"id": <ID>}'
```

#### ✏️ Edit & Approve (Improve Answer)
Use when the draft needs corrections or improvements.

**Web UI:**
1. Click question to open details
2. Click "✏️ Редактировать"
3. Edit the answer in the textarea
4. Click "Сохранить и одобрить"

**CLI:**
```bash
python3 scripts/live_ingest.py approve <ID> --answer "Your improved answer"
```

**API:**
```bash
curl -X POST http://localhost:8001/api/v1/live-queue/edit \
  -H "Content-Type: application/json" \
  -d '{"id": <ID>, "answer": "Your improved answer"}'
```

#### ❌ Reject (Discard Question)
Use for spam, duplicates, or out-of-scope questions.

**Web UI:** Click red ✕ button  
**CLI:**
```bash
python3 scripts/live_ingest.py reject <ID>
```
**API:**
```bash
curl -X POST http://localhost:8001/api/v1/live-queue/reject \
  -H "Content-Type: application/json" \
  -d '{"id": <ID>}'
```

### Step 4: Export & Retrain

After reviewing questions:

```bash
# 1. Export approved to Markdown
python3 scripts/live_ingest.py export

# 2. Rebuild knowledge index
scripts/knowledge_pipeline.sh

# 3. Retrain with new knowledge
scripts/auto_train.sh
```

Now Kolibri will answer these questions correctly!

## 2. Moderation Guidelines

### 2.1 When to Approve

**Green light (approve as-is):**
- ✅ Factual, verifiable answer
- ✅ Clear and concise (1-3 sentences)
- ✅ Correct terminology
- ✅ No obvious errors
- ✅ Appropriate for Kolibri's knowledge base

**Example:**
```
Q: What is the capital of France?
A: Столица Франции — Париж.
✅ Approve
```

### 2.2 When to Edit

**Yellow light (edit before approving):**
- ⚠️ Minor factual errors
- ⚠️ Missing important details
- ⚠️ Unclear or ambiguous wording
- ⚠️ Outdated information
- ⚠️ Poor formatting

**Editing Examples:**

**Before:**
```
Q: What is photosynthesis?
A: Фотосинтез: 6CO₂ + 6H₂O → C₆H₁₂O₆ + 6O₂.
```

**After (improved):**
```
Q: What is photosynthesis?
A: Фотосинтез — процесс преобразования энергии света в химическую энергию. 
Формула: 6CO₂ + 6H₂O → C₆H₁₂O₆ + 6O₂. 
Происходит в хлоропластах растений с участием хлорофилла.
```

### 2.3 When to Reject

**Red light (reject):**
- ❌ Spam or gibberish
- ❌ Duplicate of existing knowledge
- ❌ Outside Kolibri's scope (e.g., personal advice)
- ❌ Cannot verify the answer
- ❌ Too controversial or subjective

**Examples:**
```
Q: asdfghjkl?
A: Нет ответа
❌ Reject (gibberish)

Q: What is the meaning of life?
A: 42
❌ Reject (subjective/philosophical)
```

## 3. Quality Standards

### 3.1 Answer Format

**Good answer structure:**
1. **Direct answer** (1 sentence)
2. **Supporting details** (1-2 sentences, optional)
3. **Example or formula** (if applicable)

**Examples:**

**Physics:**
```
Q: What is Newton's second law?
A: Второй закон Ньютона: F = m * a. 
Сила, действующая на тело, равна произведению массы на ускорение.
```

**Geography:**
```
Q: What is the highest mountain?
A: Высочайшая гора — Эверест (8849 м). 
Расположен на границе Непала и Тибета (Китай).
```

**Chemistry:**
```
Q: What is water's chemical formula?
A: Формула воды — H₂O. 
Два атома водорода и один атом кислорода.
```

### 3.2 Language Standards

- **Language**: Russian (unless question is in English)
- **Tone**: Informative, neutral
- **Style**: Concise, encyclopedic
- **Terminology**: Use standard scientific terms

### 3.3 Length Guidelines

- **Minimum**: 1 complete sentence
- **Optimal**: 2-3 sentences
- **Maximum**: 5 sentences (split complex topics)

## 4. Efficiency Tips

### 4.1 Batch Processing

**Don't review one-by-one. Batch your work:**

```bash
# Review 20 questions at once
python3 scripts/live_ingest.py list | head -40

# Approve multiple questions
for id in 1 2 3 4 5; do
    python3 scripts/live_ingest.py approve $id
done
```

### 4.2 Keyboard Shortcuts (Web UI)

While in the Live Queue tab:
- `Enter`: Open selected question
- `A`: Approve current question
- `R`: Reject current question
- `E`: Edit current question
- `Esc`: Close detail panel

### 4.3 Use Filters

**Focus on high-impact questions:**

```bash
# Get questions with highest confidence drafts first
sqlite3 build/knowledge/live_queue.db <<EOF
SELECT id, title, draft_confidence 
FROM live_queue 
WHERE status = 'pending' 
ORDER BY draft_confidence DESC 
LIMIT 20;
EOF
```

### 4.4 Time Management

**Recommended schedule:**
- **Morning** (15 min): Review overnight queue
- **Afternoon** (10 min): Quick check and approve
- **Evening** (15 min): Final review and export

**Target metrics:**
- Review time: < 30 sec per question
- Approval rate: 60-80%
- Queue size: < 20 pending

## 5. Advanced Operations

### 5.1 Search Pending Questions

```bash
# Search by keyword
sqlite3 build/knowledge/live_queue.db <<EOF
SELECT id, title, created_at 
FROM live_queue 
WHERE status = 'pending' 
  AND (title LIKE '%физика%' OR title LIKE '%химия%')
ORDER BY created_at DESC;
EOF
```

### 5.2 Bulk Export by Date

```bash
# Export questions approved today
python3 -c "
import sqlite3
import os

conn = sqlite3.connect('build/knowledge/live_queue.db')
cursor = conn.execute('''
    SELECT title, content 
    FROM live_queue 
    WHERE status IN (\"approved\", \"edited\")
      AND date(reviewed_at) = date(\"now\")
''')

os.makedirs('build/knowledge/approved_today', exist_ok=True)

for i, (title, content) in enumerate(cursor.fetchall(), 1):
    safe_title = title[:50].replace('/', '_').replace('?', '')
    filepath = f'build/knowledge/approved_today/{safe_title}.md'
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(f'### Q: {title}\\n{content}\\n---\\n')
    print(f'Exported: {filepath}')

conn.close()
"
```

### 5.3 Statistics Report

```bash
# Generate daily report
python3 -c "
import sqlite3

conn = sqlite3.connect('build/knowledge/live_queue.db')

# Overall stats
cursor = conn.execute('''
    SELECT 
        status,
        COUNT(*) as count,
        MIN(created_at) as first,
        MAX(created_at) as last
    FROM live_queue
    GROUP BY status
''')

print('=== Live Queue Daily Report ===')
print()
for status, count, first, last in cursor.fetchall():
    print(f'{status.upper()}: {count} questions')
    print(f'  First: {first}')
    print(f'  Last:  {last}')
    print()

# Approval rate
cursor = conn.execute('''
    SELECT 
        ROUND(
            CAST(SUM(CASE WHEN status IN (\"approved\", \"edited\") THEN 1 ELSE 0 END) AS FLOAT) /
            CAST(COUNT(*) AS FLOAT) * 100, 
            2
        ) as approval_rate
    FROM live_queue
    WHERE status != \"pending\"
''')

rate = cursor.fetchone()[0]
print(f'Approval Rate: {rate}%')

conn.close()
"
```

### 5.4 Merge Similar Questions

If you find duplicates:

```bash
# Find potential duplicates (same first 30 chars)
sqlite3 build/knowledge/live_queue.db <<EOF
SELECT 
    SUBSTR(title, 1, 30) as prefix,
    COUNT(*) as duplicates,
    GROUP_CONCAT(id) as ids
FROM live_queue
WHERE status = 'pending'
GROUP BY prefix
HAVING duplicates > 1;
EOF

# Keep best answer, reject others
python3 scripts/live_ingest.py approve <BEST_ID> --answer "Merged answer"
python3 scripts/live_ingest.py reject <OTHER_ID_1>
python3 scripts/live_ingest.py reject <OTHER_ID_2>
```

## 6. Quality Assurance

### 6.1 Post-Approval Testing

After approving and retraining, test that Kolibri learned:

```bash
# Ask the same question
curl -X POST http://localhost:8001/api/v1/ai/chat \
  -H "Content-Type: application/json" \
  -d '{"message": "your question here"}'

# Check response includes your approved answer
```

### 6.2 Review Approved Answers

Periodically review approved answers:

```bash
# List approved answers from last week
sqlite3 build/knowledge/live_queue.db <<EOF
SELECT id, title, content, reviewed_at
FROM live_queue
WHERE status IN ('approved', 'edited')
  AND reviewed_at >= datetime('now', '-7 days')
ORDER BY reviewed_at DESC;
EOF
```

### 6.3 Spot Check Retraining

```bash
# Check if approved questions made it to knowledge base
ls -l build/knowledge/approved/*.md | head -10

# Verify they're indexed
grep -l "your approved answer" build/knowledge/index.json
```

## 7. Troubleshooting for Moderators

### 7.1 Cannot Approve

**Problem**: "failed to approve" error

**Solutions:**
```bash
# Check if question exists
sqlite3 build/knowledge/live_queue.db "SELECT id, status FROM live_queue WHERE id = <ID>;"

# Check database is not locked
lsof build/knowledge/live_queue.db

# Retry with CLI
python3 scripts/live_ingest.py approve <ID>
```

### 7.2 Answer Not Saving

**Problem**: Edited answer reverts to draft

**Solution:**
- Use the API edit endpoint instead of UI
- Check for special characters (escape if needed)
- Verify answer length < 4096 chars

### 7.3 Missing Questions

**Problem**: Questions not showing in pending list

**Possible causes:**
1. Already reviewed (check approved/rejected)
2. Database corruption
3. Server not capturing (check logs)

**Debug:**
```bash
# Check all statuses
sqlite3 build/knowledge/live_queue.db <<EOF
SELECT status, COUNT(*) FROM live_queue GROUP BY status;
EOF

# Check recent captures
tail -f kolibri_http.log | grep "Live queue"
```

## 8. Best Practices Checklist

### Daily
- [ ] Review all pending questions
- [ ] Approve high-quality answers
- [ ] Edit and approve when needed
- [ ] Reject spam/duplicates
- [ ] Export approved questions
- [ ] Run knowledge pipeline
- [ ] Run auto-training

### Weekly
- [ ] Review approval rate (target: >60%)
- [ ] Check queue size trend
- [ ] Update confidence threshold if needed
- [ ] Backup database
- [ ] Test retrained knowledge

### Monthly
- [ ] Generate statistics report
- [ ] Clean old rejected questions (>30 days)
- [ ] Review and update moderation guidelines
- [ ] Archive old approved questions
- [ ] Plan capacity (queue growth rate)

## 9. Common Scenarios

### Scenario 1: Flood of Similar Questions

**Situation**: Many variations of the same question

**Action:**
1. Identify the core question
2. Create one comprehensive answer
3. Approve best example with edited answer
4. Reject duplicates with note

```bash
# Example
python3 scripts/live_ingest.py approve 100 --answer "Квантовая запутанность — физическое явление, при котором квантовые состояния двух или более частиц оказываются взаимосвязанными."

python3 scripts/live_ingest.py reject 101
python3 scripts/live_ingest.py reject 102
python3 scripts/live_ingest.py reject 103
```

### Scenario 2: Technical Terms Need Correction

**Situation**: Draft has wrong terminology

**Action:**
1. Edit with correct terminology
2. Add brief explanation
3. Approve

```bash
python3 scripts/live_ingest.py approve 200 --answer "Фотосинтез происходит в хлоропластах (не в митохондриях). Хлорофилл поглощает световую энергию."
```

### Scenario 3: Incomplete Answer

**Situation**: Draft is too brief

**Action:**
1. Add missing details
2. Include examples
3. Approve

```bash
python3 scripts/live_ingest.py approve 300 --answer "Теорема Пифагора: В прямоугольном треугольнике квадрат гипотенузы равен сумме квадратов катетов (c² = a² + b²). Пример: если a=3, b=4, то c=5."
```

## 10. Metrics & KPIs

### Key Metrics to Track

| Metric | Target | Current |
|--------|--------|---------|
| Pending queue size | < 20 | Check: `curl /api/v1/live-queue/stats` |
| Approval rate | 60-80% | Calculate from stats |
| Review time | < 30 sec/question | Track manually |
| Export frequency | Daily | Check approved/ count |
| Knowledge growth | +10-20 questions/week | Monitor index size |

### Success Indicators

✅ **Healthy queue:**
- Pending count stays low
- High approval rate (>60%)
- Consistent daily exports
- Positive user feedback

⚠️ **Needs attention:**
- Pending count growing
- Low approval rate (<40%)
- Infrequent reviews
- User reports wrong answers

## 11. Resources

### Documentation
- Architecture: `docs/live_learning.md`
- Deployment: `docs/DEPLOYMENT_LIVE_QUEUE.md`
- Quick Reference: `docs/LIVE_QUEUE_QUICKREF.md`

### Tools
- CLI: `scripts/live_ingest.py`
- Smoke Tests: `ci/smoke_test_live_loop.sh`
- Database: `build/knowledge/live_queue.db`

### Support
- Check logs: `tail -f kolibri_http.log`
- Test API: `curl http://localhost:8001/api/v1/live-queue/stats`
- Verify DB: `sqlite3 build/knowledge/live_queue.db "PRAGMA integrity_check;"`

---

**Last Updated**: April 7, 2026  
**Version**: 1.0.0  
**For**: Kolibri Moderators & Knowledge Engineers
