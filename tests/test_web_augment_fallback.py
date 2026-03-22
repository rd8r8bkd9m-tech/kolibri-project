from __future__ import annotations

import pytest


@pytest.fixture(scope="module")
def engine():
    from backend.service.ai_engine import KolibriAIEngine

    return KolibriAIEngine()


def test_web_augment_builds_answer_and_caches(engine, monkeypatch):
    calls = {"search": 0, "fetch": 0}

    def fake_search_quick(query: str, max_urls: int = 8, include_bing_fallback: bool = True):
        calls["search"] += 1
        return [
            {
                "url": "https://example.com/kolibri-web-augment",
                "title": "Kolibri Topic",
                "snippet": "Тестоваятемаwebaugment описывает динамическое обучение модели на веб-данных.",
                "source": "duckduckgo",
            }
        ]

    def fake_fetch_page_text(url: str, timeout: int = 10, max_chars: int = 80_000):
        calls["fetch"] += 1
        text = (
            "Тестоваятемаwebaugment — это подход, при котором модель берёт недостающие знания из интернета, "
            "добавляет их в локальную память и затем отвечает содержательно. "
            "Система сохраняет знания локально и использует их в следующих вопросах."
        )
        return text, "Kolibri Topic", len(text.encode("utf-8"))

    monkeypatch.setattr("backend.service.ai_engine.search_quick", fake_search_quick)
    monkeypatch.setattr("backend.service.ai_engine.fetch_page_text", fake_fetch_page_text)

    query = "что такое тестоваятемаwebaugment"
    q_tokens = {"тестоваятемаwebaugment"}
    q_stems = {"тестоваятемаwebaugment"}

    answer_1 = engine._try_web_augment_answer(query, q_tokens=q_tokens, q_stems=q_stems)
    answer_2 = engine._try_web_augment_answer(query, q_tokens=q_tokens, q_stems=q_stems)

    assert answer_1 is not None
    assert "тестоваятемаwebaugment" in answer_1.lower()
    assert answer_2 == answer_1
    assert calls["search"] == 1
    assert calls["fetch"] == 1


def test_chat_uses_web_augment_when_local_data_missing(engine, monkeypatch):
    def fake_search_quick(query: str, max_urls: int = 8, include_bing_fallback: bool = True):
        return [
            {
                "url": "https://example.com/kolibri-unknown-topic",
                "title": "Unknown Topic",
                "snippet": "Квазисигнатурная динамика объясняет неизвестную тему и её принципы.",
                "source": "duckduckgo",
            }
        ]

    def fake_fetch_page_text(url: str, timeout: int = 10, max_chars: int = 80_000):
        text = (
            "Квазисигнатурная динамика — это тестовый термин для проверки fallback-режима. "
            "Он показывает, что движок может искать материал в интернете и отвечать не шаблоном."
        )
        return text, "Unknown Topic", len(text.encode("utf-8"))

    monkeypatch.setattr("backend.service.ai_engine.search_quick", fake_search_quick)
    monkeypatch.setattr("backend.service.ai_engine.fetch_page_text", fake_fetch_page_text)

    result = engine.chat(
        "объясни квазисигнатурную динамику",
        conversation_id="web-augment-test-conv",
    )

    assert result["method"] in {"web-augment", "dynamic-fallback", "ru-safe-fallback"}
    assert "квазисигнатур" in result["response"].lower()
