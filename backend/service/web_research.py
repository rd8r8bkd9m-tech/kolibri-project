"""
web_research.py — Полноценный Web Research для Kolibri

Фаза: Глубокий веб-поиск и обучение
- Поиск через DuckDuckGo, Google, Bing
- Парсинг любых сайтов (HTML → текст)
- Автоматическое обучение на найденных материалах
- Генерация статей из множества источников
"""
from __future__ import annotations

import html
import json
import logging
import re
import time
from dataclasses import dataclass, field
from typing import Optional
from urllib.parse import quote, urlparse

import requests

log = logging.getLogger("kolibri.web_research")

# ============================================================================
# Search Engines
# ============================================================================

@dataclass
class SearchResult:
    """Результат поиска."""
    title: str
    url: str
    snippet: str
    source: str  # "duckduckgo", "google", "bing"
    relevance: float = 0.0


@dataclass
class ScrapedContent:
    """Извлечённый контент со страницы."""
    url: str
    title: str
    text: str
    word_count: int
    language: str
    scrape_time: float


class DuckDuckGoSearch:
    """Поиск через DuckDuckGo (без API ключа)."""

    @staticmethod
    def search(query: str, max_results: int = 10, lang: str = "ru") -> list[SearchResult]:
        """Поиск через DuckDuckGo HTML."""
        results = []
        try:
            url = f"https://html.duckduckgo.com/html/?q={quote(query)}"
            headers = {
                "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
                "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
                "Accept-Language": f"{lang},en;q=0.8",
            }
            resp = requests.get(url, headers=headers, timeout=10)
            resp.raise_for_status()

            html_content = resp.text

            # Парсим результаты через regex
            # DuckDuckGo использует pattern: <a rel="nofollow" class="result__a" href="...">Title</a>
            title_pattern = re.compile(r'<a[^>]+class="result__a"[^>]*>(.*?)</a>', re.DOTALL)
            url_pattern = re.compile(r'<a[^>]+class="result__a"[^>]+href="([^"]+)"', re.DOTALL)
            snippet_pattern = re.compile(r'<a[^>]+class="result__snippet"[^>]*>(.*?)</a>', re.DOTALL)

            titles = title_pattern.findall(html_content)
            urls = url_pattern.findall(html_content)
            snippets = snippet_pattern.findall(html_content)

            # Очистка HTML из заголовков и сниппетов
            def clean_html(text):
                text = re.sub(r'<[^>]+>', '', text)
                text = html.unescape(text)
                return text.strip()

            for i in range(min(len(titles), len(urls), max_results)):
                title = clean_html(titles[i])
                url = urls[i]
                snippet = clean_html(snippets[i]) if i < len(snippets) else ""

                if title and url:
                    results.append(SearchResult(
                        title=title,
                        url=url,
                        snippet=snippet[:200],
                        source="duckduckgo",
                        relevance=0.8,
                    ))

        except Exception as e:
            log.warning("DuckDuckGo search failed: %s", e)

        return results


class GoogleSearch:
    """Поиск через Google (через scraping)."""

    @staticmethod
    def search(query: str, max_results: int = 10, lang: str = "ru") -> list[SearchResult]:
        """Поиск через Google."""
        results = []
        try:
            url = f"https://www.google.com/search?q={quote(query)}&hl={lang}&num={max_results}"
            headers = {
                "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
                "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
                "Accept-Language": f"{lang},en;q=0.8",
            }
            resp = requests.get(url, headers=headers, timeout=10)
            resp.raise_for_status()

            # Упрощённый парсинг Google
            from html.parser import HTMLParser

            class GoogleParser(HTMLParser):
                def __init__(self):
                    super().__init__()
                    self.results = []
                    self.in_title = False
                    self.in_snippet = False
                    self.current_url = ""
                    self.current_title = ""
                    self.current_snippet = ""
                    self.collect_data = False

                def handle_starttag(self, tag, attrs):
                    attrs_dict = dict(attrs)
                    if tag == "a" and "href" in attrs_dict:
                        href = attrs_dict["href"]
                        if href.startswith("/url?q="):
                            self.current_url = href.split("/url?q=")[1].split("&")[0]
                            self.collect_data = True
                    if tag == "h3" and self.collect_data:
                        self.in_title = True
                        self.current_title = ""
                    if tag == "span" and "class" in attrs_dict and any("snippet" in c for c in attrs_dict.get("class", "").split()):
                        self.in_snippet = True
                        self.current_snippet = ""

                def handle_data(self, data):
                    if self.in_title:
                        self.current_title += data
                    if self.in_snippet:
                        self.current_snippet += data

                def handle_endtag(self, tag):
                    if tag == "h3" and self.in_title:
                        self.in_title = False
                    if tag == "span" and self.in_snippet:
                        self.in_snippet = False
                    if tag == "div" and self.collect_data and self.current_url and self.current_title:
                        self.results.append({
                            "url": self.current_url,
                            "title": self.current_title.strip(),
                            "snippet": self.current_snippet.strip()[:200],
                        })
                        self.current_url = ""
                        self.current_title = ""
                        self.current_snippet = ""
                        self.collect_data = False

            parser = GoogleParser()
            parser.feed(resp.text)

            for r in parser.results[:max_results]:
                results.append(SearchResult(
                    title=r["title"],
                    url=r["url"],
                    snippet=r["snippet"],
                    source="google",
                    relevance=0.85,
                ))

        except Exception as e:
            log.warning("Google search failed: %s", e)

        return results


# ============================================================================
# Web Scraper
# ============================================================================

class WebScraper:
    """Парсинг любых веб-страниц — извлечение основного контента."""

    @staticmethod
    def scrape(url: str, timeout: float = 15.0) -> ScrapedContent | None:
        """Скачать страницу и извлечь основной текст."""
        try:
            headers = {
                "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
                "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
                "Accept-Language": "ru,en;q=0.8",
            }
            resp = requests.get(url, headers=headers, timeout=timeout)
            resp.raise_for_status()

            return WebScraper._extract_content(resp.text, url)
        except Exception as e:
            log.warning("Failed to scrape %s: %s", url, e)
            return None

    @staticmethod
    def _extract_content(html_content: str, url: str) -> ScrapedContent:
        """Извлечь основной текст из HTML."""
        from html.parser import HTMLParser

        class ContentExtractor(HTMLParser):
            def __init__(self):
                super().__init__()
                self.title = ""
                self.text_parts = []
                self.in_title = False
                self.in_script = False
                self.in_style = False
                self.in_nav = False
                self.current_text = ""
                self.skip_tags = {"script", "style", "nav", "footer", "header", "aside", "noscript"}
                self.current_skip = None

            def handle_starttag(self, tag, attrs):
                if tag == "title":
                    self.in_title = True
                if tag in self.skip_tags:
                    if self.current_skip is None:
                        self.current_skip = tag
                if tag in {"p", "h1", "h2", "h3", "h4", "h5", "h6", "li", "td", "blockquote", "article", "section", "div"}:
                    if self.current_skip is None:
                        if self.current_text.strip():
                            self.text_parts.append(self.current_text.strip())
                        self.current_text = ""

            def handle_endtag(self, tag):
                if tag == "title":
                    self.in_title = False
                if tag == self.current_skip:
                    self.current_skip = None
                if tag in {"p", "h1", "h2", "h3", "h4", "h5", "h6", "li", "td", "blockquote", "article", "section"}:
                    if self.current_text.strip() and self.current_skip is None:
                        self.text_parts.append(self.current_text.strip())
                    self.current_text = ""

            def handle_data(self, data):
                if self.in_title:
                    self.title += data
                elif self.current_skip is None:
                    self.current_text += data

            def get_text(self):
                if self.current_text.strip():
                    self.text_parts.append(self.current_text.strip())
                return "\n\n".join(self.text_parts)

        extractor = ContentExtractor()
        extractor.feed(html_content)

        text = extractor.get_text()
        title = extractor.title.strip()

        # Очистка текста
        text = re.sub(r'\n{3,}', '\n\n', text)
        text = re.sub(r' {2,}', ' ', text)

        # Определяем язык
        lang = "ru" if re.search(r'[а-яё]', text, re.IGNORECASE) else "en"

        # Разбиваем на предложения для подсчёта слов
        words = len(re.findall(r'\b\w+\b', text))

        return ScrapedContent(
            url=url,
            title=title or urlparse(url).path.strip("/").replace("-", " ").title(),
            text=text[:10000],  # Ограничиваем длину
            word_count=words,
            language=lang,
            scrape_time=time.time(),
        )


# ============================================================================
# Web Research Pipeline
# ============================================================================

class WebResearchPipeline:
    """Полный pipeline веб-исследования."""

    def __init__(self) -> None:
        self.ddg = DuckDuckGoSearch()
        self.google = GoogleSearch()
        self.scraper = WebScraper()
        self._cache: dict[str, ScrapedContent] = {}

    def research(self, topic: str, max_sources: int = 10,
                 timeout: float = 30.0) -> dict:
        """Провести веб-исследование по теме."""
        t0 = time.time()

        # 1. Поиск через несколько поисковиков
        all_results: list[SearchResult] = []

        # DuckDuckGo
        ddg_results = self.ddg.search(topic, max_results=max_sources)
        all_results.extend(ddg_results)

        # Google
        google_results = self.google.search(topic, max_results=max_sources)
        all_results.extend(google_results)

        # Уникальные URL
        seen_urls = set()
        unique_results = []
        for r in all_results:
            if r.url not in seen_urls:
                seen_urls.add(r.url)
                unique_results.append(r)

        # 2. Парсинг найденных страниц
        scraped_contents: list[ScrapedContent] = []
        for result in unique_results[:max_sources]:
            if result.url in self._cache:
                scraped_contents.append(self._cache[result.url])
                continue

            content = self.scraper.scrape(result.url, timeout=timeout / max(1, len(unique_results)))
            if content and content.word_count > 50:
                self._cache[result.url] = content
                scraped_contents.append(content)

        # 3. Комбинируем контент
        combined_text = self._combine_content(scraped_contents, topic)

        duration_ms = (time.time() - t0) * 1000

        return {
            "topic": topic,
            "sources_found": len(unique_results),
            "sources_scraped": len(scraped_contents),
            "total_words": sum(c.word_count for c in scraped_contents),
            "combined_text": combined_text,
            "sources": [
                {"title": r.title, "url": r.url, "source": r.source}
                for r in unique_results[:max_sources]
            ],
            "duration_ms": round(duration_ms, 1),
        }

    def _combine_content(self, contents: list[ScrapedContent], topic: str) -> str:
        """Комбинировать контент из нескольких источников в связный текст."""
        if not contents:
            return ""

        # Сортируем по длине (самые информативные первые)
        contents.sort(key=lambda c: c.word_count, reverse=True)

        parts = []
        seen_sentences = set()

        for content in contents:
            # Разбиваем на предложения
            sentences = re.split(r'(?<=[.!?])\s+', content.text)

            for sentence in sentences:
                sentence = sentence.strip()
                if not sentence or len(sentence) < 20:
                    continue

                # Убираем дубликаты
                sentence_key = sentence[:100].lower()
                if sentence_key in seen_sentences:
                    continue
                seen_sentences.add(sentence_key)

                parts.append(sentence)

        return "\n\n".join(parts[:50])  # Максимум 50 уникальных предложений

    def research_and_learn(self, topic: str, ai_engine=None,
                           max_sources: int = 10, timeout: float = 30.0) -> dict:
        """Провести исследование и автоматически обучиться."""
        result = self.research(topic, max_sources=max_sources, timeout=timeout)

        if result["combined_text"] and ai_engine:
            try:
                # Автоматическое обучение на найденных материалах
                for content in self._cache.values():
                    if content.word_count > 100:
                        try:
                            ai_engine._train_queue.put_nowait(("user_text", content.text[:20000]))
                        except Exception:
                            pass
            except Exception as e:
                log.warning("Failed to queue training: %s", e)

        return result
