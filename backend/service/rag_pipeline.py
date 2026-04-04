"""
rag_pipeline.py — Retrieval-Augmented Generation для Kolibri

Фаза A1: Увеличение базы знаний через RAG
- Загрузка документов из Wikipedia, учебников, документации
- Автоматическая индексация при ingest
- Self-correction: проверка ответов на противоречия
"""
from __future__ import annotations

import hashlib
import json
import logging
import re
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

log = logging.getLogger("kolibri.rag")

# ============================================================================
# Document Chunk
# ============================================================================

@dataclass
class DocumentChunk:
    """Фрагмент документа для retrieval."""
    text: str
    source: str
    title: str
    category: str
    embedding: list[float] = field(default_factory=list)
    chunk_id: str = ""
    timestamp: float = 0.0

    def __post_init__(self) -> None:
        if not self.chunk_id:
            self.chunk_id = hashlib.sha256(
                f"{self.source}:{self.title}:{self.text[:100]}".encode()
            ).hexdigest()[:16]
        if not self.timestamp:
            self.timestamp = time.time()


# ============================================================================
# RAG Index
# ============================================================================

class RAGIndex:
    """Индекс для быстрого поиска по документам."""

    def __init__(self, index_path: Path | None = None) -> None:
        self.index_path = index_path or Path("data/rag_index.json")
        self.chunks: list[DocumentChunk] = []
        self.inverted_index: dict[str, list[int]] = {}  # term → chunk indices
        self.category_index: dict[str, list[int]] = {}  # category → chunk indices
        self._load()

    def add_chunk(self, chunk: DocumentChunk) -> None:
        """Добавить чанк в индекс."""
        idx = len(self.chunks)
        self.chunks.append(chunk)

        # Inverted index
        terms = self._tokenize(chunk.text)
        for term in terms:
            if term not in self.inverted_index:
                self.inverted_index[term] = []
            self.inverted_index[term].append(idx)

        # Category index
        if chunk.category not in self.category_index:
            self.category_index[chunk.category] = []
        self.category_index[chunk.category].append(idx)

    def add_document(self, text: str, source: str, title: str,
                     category: str = "general", chunk_size: int = 500,
                     overlap: int = 100) -> int:
        """Разбить документ на чанки и добавить в индекс."""
        chunks = self._split_text(text, chunk_size, overlap)
        count = 0
        for chunk_text in chunks:
            chunk = DocumentChunk(
                text=chunk_text,
                source=source,
                title=title,
                category=category,
            )
            self.add_chunk(chunk)
            count += 1
        return count

    def search(self, query: str, top_k: int = 5,
               category: str | None = None) -> list[DocumentChunk]:
        """Поиск релевантных чанков."""
        query_terms = self._tokenize(query)
        if not query_terms:
            return []

        # TF-IDF scoring
        scores: dict[int, float] = {}
        n_docs = len(self.chunks)

        for term in query_terms:
            if term in self.inverted_index:
                doc_freq = len(self.inverted_index[term])
                idf = max(1.0, (n_docs / (doc_freq + 1e-10)))

                for idx in self.inverted_index[term]:
                    # TF
                    chunk_text = self.chunks[idx].text.lower()
                    tf = chunk_text.count(term) / max(1, len(chunk_text.split()))

                    # BM25-like scoring
                    score = tf * idf
                    scores[idx] = scores.get(idx, 0.0) + score

        # Filter by category if specified
        if category and category in self.category_index:
            allowed = set(self.category_index[category])
            scores = {k: v for k, v in scores.items() if k in allowed}

        # Sort by score
        sorted_indices = sorted(scores.keys(), key=lambda i: scores[i], reverse=True)
        return [self.chunks[i] for i in sorted_indices[:top_k]]

    def self_correct(self, query: str, answer: str, top_k: int = 3) -> tuple[bool, str]:
        """#Фаза A2: Self-correction — проверить ответ на противоречия."""
        relevant_chunks = self.search(query, top_k=top_k)
        if not relevant_chunks:
            return True, answer  # Нет данных для проверки

        # Проверяем что ответ содержит ключевые факты из чанков
        answer_lower = answer.lower()
        contradictions = []

        for chunk in relevant_chunks:
            chunk_text = chunk.text.lower()
            # Извлекаем ключевые факты (предложения с числами/датами)
            facts = self._extract_facts(chunk_text)
            for fact in facts:
                if fact not in answer_lower and len(fact) > 10:
                    contradictions.append(fact)

        if contradictions:
            # Ответ противоречит знаниям — предлагаем исправление
            correction = self._generate_correction(query, relevant_chunks, contradictions)
            return False, correction

        return True, answer

    def save(self) -> None:
        """Сохранить индекс."""
        self.index_path.parent.mkdir(parents=True, exist_ok=True)
        data = {
            "chunks": [
                {
                    "text": c.text,
                    "source": c.source,
                    "title": c.title,
                    "category": c.category,
                    "chunk_id": c.chunk_id,
                    "timestamp": c.timestamp,
                }
                for c in self.chunks
            ],
            "inverted_index": self.inverted_index,
            "category_index": self.category_index,
        }
        self.index_path.write_text(json.dumps(data, ensure_ascii=False, indent=2))
        log.info("RAG index saved: %d chunks", len(self.chunks))

    def _load(self) -> None:
        """Загрузить индекс."""
        if self.index_path.exists():
            try:
                data = json.loads(self.index_path.read_text())
                self.inverted_index = data.get("inverted_index", {})
                self.category_index = data.get("category_index", {})
                for c_data in data.get("chunks", []):
                    chunk = DocumentChunk(
                        text=c_data["text"],
                        source=c_data["source"],
                        title=c_data["title"],
                        category=c_data["category"],
                        chunk_id=c_data.get("chunk_id", ""),
                        timestamp=c_data.get("timestamp", 0.0),
                    )
                    self.chunks.append(chunk)
                log.info("RAG index loaded: %d chunks", len(self.chunks))
            except Exception as e:
                log.error("Failed to load RAG index: %s", e)

    @staticmethod
    def _tokenize(text: str) -> list[str]:
        """Простая токенизация."""
        text = text.lower()
        text = re.sub(r'[^\w\sа-яёa-z0-9]', ' ', text)
        tokens = text.split()
        # Убираем стоп-слова
        stop_words = {
            "и", "в", "на", "с", "по", "к", "у", "о", "из", "за", "для",
            "the", "a", "an", "is", "are", "was", "were", "of", "to", "in",
            "что", "это", "как", "так", "не", "но", "или", "если", "то",
        }
        return [t for t in tokens if t not in stop_words and len(t) > 2]

    @staticmethod
    def _split_text(text: str, chunk_size: int, overlap: int) -> list[str]:
        """Разбить текст на перекрывающиеся чанки по предложениям."""
        sentences = re.split(r'(?<=[.!?])\s+', text)
        chunks = []
        current_chunk = []
        current_len = 0

        for sentence in sentences:
            sentence_len = len(sentence)
            if current_len + sentence_len > chunk_size and current_chunk:
                chunks.append(" ".join(current_chunk))
                # Overlap: сохраняем последние предложения
                overlap_sentences = []
                overlap_len = 0
                for s in reversed(current_chunk):
                    if overlap_len + len(s) > overlap:
                        break
                    overlap_sentences.insert(0, s)
                    overlap_len += len(s)
                current_chunk = overlap_sentences
                current_len = overlap_len

            current_chunk.append(sentence)
            current_len += sentence_len

        if current_chunk:
            chunks.append(" ".join(current_chunk))

        return chunks

    @staticmethod
    def _extract_facts(text: str) -> list[str]:
        """Извлечь факты из текста (предложения с числами/датами)."""
        facts = []
        sentences = re.split(r'(?<=[.!?])\s+', text)
        for sentence in sentences:
            # Предложения с числами или датами
            if re.search(r'\d{4}|\d+[\s,]*\d+|%\d+', sentence):
                facts.append(sentence.lower().strip())
        return facts

    @staticmethod
    def _generate_correction(query: str, chunks: list[DocumentChunk],
                              contradictions: list[str]) -> str:
        """Сгенерировать исправленный ответ."""
        # Берём наиболее релевантный чанк как основу
        if chunks:
            best_chunk = chunks[0]
            return (
                f"Уточнённый ответ на вопрос «{query}»: "
                f"{best_chunk.text[:500]}..."
            )
        return "Не удалось проверить ответ."


# ============================================================================
# Wikipedia Ingest
# ============================================================================

class WikipediaIngest:
    """#Фаза A1: Загрузка знаний из Wikipedia."""

    def __init__(self, rag_index: RAGIndex) -> None:
        self.rag_index = rag_index
        self._cache: dict[str, str] = {}

    def ingest_topic(self, topic: str, language: str = "ru",
                     max_chars: int = 10000) -> int:
        """Загрузить статью из Wikipedia и добавить в индекс."""
        import urllib.request
        import urllib.parse

        cache_key = f"{language}:{topic}"
        if cache_key in self._cache:
            text = self._cache[cache_key]
        else:
            # Wikipedia API
            title = urllib.parse.quote(topic.replace(" ", "_"))
            url = (
                f"https://{language}.wikipedia.org/api/rest_v1/page/summary/{title}"
            )
            try:
                req = urllib.request.Request(url, headers={
                    "User-Agent": "KolibriAI/1.0"
                })
                with urllib.request.urlopen(req, timeout=10) as resp:
                    data = json.loads(resp.read().decode("utf-8"))
                    text = data.get("extract", "")
                    self._cache[cache_key] = text
            except Exception as e:
                log.error("Failed to fetch Wikipedia article '%s': %s", topic, e)
                return 0

        if not text:
            return 0

        # Определяем категорию
        category = self._detect_category(topic, text)

        # Добавляем в индекс
        count = self.rag_index.add_document(
            text=text[:max_chars],
            source=f"wikipedia:{language}",
            title=topic,
            category=category,
        )

        log.info("Ingested Wikipedia '%s': %d chunks, category=%s", topic, count, category)
        return count

    def ingest_batch(self, topics: list[str], language: str = "ru") -> int:
        """Загрузить несколько тем."""
        total = 0
        for topic in topics:
            total += self.ingest_topic(topic, language)
        return total

    @staticmethod
    def _detect_category(topic: str, text: str) -> str:
        """Определить категорию по ключевым словам."""
        text_lower = (topic + " " + text).lower()

        categories = {
            "math": ["математик", "алгебр", "геометр", "числ", "уравнен", "матриц"],
            "physics": ["физик", "энерги", "сил", "скорост", "гравитац", "квантов"],
            "biology": ["биолог", "клетк", "организм", "ген", "эволюц", "днк"],
            "chemistry": ["химич", "реакци", "элемент", "молекул", "атом", "веществ"],
            "history": ["истор", "войн", "импер", "революц", "древн", "царств"],
            "medicine": ["медицин", "болезн", "лечен", "симптом", "диагноз", "иммун"],
            "it": ["программ", "алгоритм", "компьютер", "код", "функци", "данны"],
            "economics": ["эконом", "рынок", "валют", "инфляц", "бюджет", "финанс"],
            "law": ["закон", "прав", "суд", "стать", "конституц", "юрисдикц"],
        }

        for category, keywords in categories.items():
            for kw in keywords:
                if kw in text_lower:
                    return category

        return "general"


# ============================================================================
# RAG Pipeline
# ============================================================================

class RAGPipeline:
    """Полный RAG pipeline: retrieval → generation → self-correction."""

    def __init__(self, index_path: Path | None = None) -> None:
        self.index = RAGIndex(index_path)
        self.ingest = WikipediaIngest(self.index)

    def query(self, question: str, top_k: int = 5,
              category: str | None = None) -> dict:
        """Ответить на вопрос через RAG."""
        t0 = time.time()

        # Retrieval
        chunks = self.index.search(question, top_k=top_k, category=category)

        if not chunks:
            return {
                "response": "",
                "sources": [],
                "confidence": 0.0,
                "duration_ms": (time.time() - t0) * 1000,
                "method": "rag-no-results",
            }

        # Generation: объединяем чанки в ответ
        response_parts = []
        sources = []
        for chunk in chunks[:3]:
            response_parts.append(chunk.text)
            sources.append({
                "title": chunk.title,
                "source": chunk.source,
                "category": chunk.category,
            })

        raw_response = " ".join(response_parts)[:2000]

        # Self-correction
        is_correct, corrected_response = self.index.self_correct(
            question, raw_response, top_k=3
        )

        confidence = 0.8 if is_correct else 0.5

        return {
            "response": corrected_response,
            "sources": sources,
            "confidence": confidence,
            "duration_ms": (time.time() - t0) * 1000,
            "method": "rag-self-corrected" if not is_correct else "rag",
            "corrected": not is_correct,
        }

    def ingest_topics(self, topics: list[str], language: str = "ru") -> int:
        """Загрузить темы из Wikipedia."""
        total = self.ingest.ingest_batch(topics, language)
        self.index.save()
        return total
