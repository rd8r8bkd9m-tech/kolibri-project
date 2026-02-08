# Content Factory — Схема данных (RU)

## Объекты и связи

### 1) Campaign
- **id** (UUID, PK)
- **name** (string)
- **niche** (string)
- **status** (enum: draft/active/paused/archived)
- **created_at**, **updated_at**

**Связи:** Campaign → ContentItem (1:M)

---

### 2) ContentItem
- **id** (UUID, PK)
- **campaign_id** (FK → Campaign)
- **topic** (string)
- **status** (enum: analysis/idea_approval/production/content_approval/publishing/analytics)
- **analysis_report** (text)
- **content_body** (text)
- **platform** (enum: youtube/tiktok/telegram/blog/vk)
- **risk_score** (int)
- **created_at**, **updated_at**

**Индексы:** (campaign_id, status), (platform, status)

---

### 3) Approval
- **id** (UUID, PK)
- **content_item_id** (FK → ContentItem)
- **type** (enum: idea/content)
- **status** (enum: approved/changes/rejected)
- **notes** (text)
- **approved_by** (string)
- **approved_at** (datetime)

**Связи:** ContentItem → Approval (1:M)

---

### 4) Asset
- **id** (UUID, PK)
- **content_item_id** (FK → ContentItem)
- **type** (enum: script/storyboard/thumbnail/video/subtitles)
- **uri** (string)
- **metadata** (json)
- **created_at**

---

### 5) PublishJob
- **id** (UUID, PK)
- **content_item_id** (FK → ContentItem)
- **platform** (string)
- **scheduled_at** (datetime)
- **published_at** (datetime)
- **status** (enum: queued/published/failed)
- **external_url** (string)
- **utm** (json)

---

### 6) AnalyticsSnapshot
- **id** (UUID, PK)
- **content_item_id** (FK → ContentItem)
- **views** (int)
- **engagement_rate** (float)
- **ctr** (float)
- **retention** (float)
- **leads** (int)
- **revenue** (float)
- **cost** (float)
- **romi** (float)
- **captured_at** (datetime)

**Индексы:** (content_item_id, captured_at DESC)

---

## Рекомендации
- Для MVP достаточно Campaign + ContentItem + AnalyticsSnapshot.
- Approval можно хранить в ContentItem (status + notes) и вынести позже.
- Asset и PublishJob добавить после MVP.
