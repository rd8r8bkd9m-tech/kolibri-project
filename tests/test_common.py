"""
Тесты для common.py — Settings, InferenceRequest, extract_text.
"""
from __future__ import annotations

import os

import pytest

from backend.service.common import Settings, InferenceRequest, extract_text


class TestSettings:
    def test_default_values(self) -> None:
        s = Settings()
        assert s.response_mode == "script"
        assert s.local_only is True
        assert s.llm_endpoint is None
        assert s.llm_timeout == 30.0

    def test_load_from_env(self, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.setenv("KOLIBRI_RESPONSE_MODE", "llm")
        monkeypatch.setenv("KOLIBRI_LLM_TIMEOUT", "60")
        monkeypatch.setenv("KOLIBRI_LOCAL_ONLY", "0")
        s = Settings.load()
        assert s.response_mode == "llm"
        assert s.local_only is False
        assert s.llm_timeout == 60.0

    def test_load_invalid_timeout(self, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.setenv("KOLIBRI_LLM_TIMEOUT", "не_число")
        with pytest.raises(RuntimeError, match="numeric"):
            Settings.load()

    def test_default_mode(self) -> None:
        s = Settings.load()
        assert s.response_mode in ("script", "llm")


class TestExtractText:
    def test_extract_from_response_key(self) -> None:
        assert extract_text({"response": "hello"}) == "hello"

    def test_extract_from_choices(self) -> None:
        payload = {
            "choices": [{"message": {"content": "answer"}}]
        }
        assert extract_text(payload) == "answer"

    def test_extract_from_content_key(self) -> None:
        assert extract_text({"content": "text"}) == "text"

    def test_extract_raises_on_invalid(self) -> None:
        with pytest.raises(ValueError, match="text output"):
            extract_text({"unknown": 123})

    def test_extract_strips_whitespace(self) -> None:
        assert extract_text({"response": "  hello  "}) == "hello"


class TestInferenceRequest:
    def test_valid_request(self) -> None:
        req = InferenceRequest(prompt="test prompt")
        assert req.prompt == "test prompt"
        assert req.mode is None
        assert req.temperature is None

    def test_temperature_bounds(self) -> None:
        req = InferenceRequest(prompt="test", temperature=1.5)
        assert req.temperature == 1.5

    def test_prompt_min_length(self) -> None:
        with pytest.raises(Exception):  # pydantic validation
            InferenceRequest(prompt="")
