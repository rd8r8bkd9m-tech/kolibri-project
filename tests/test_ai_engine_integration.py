"""Интеграционные тесты: AIEngine + CoT + ContextWindow + FormulaLM."""

from __future__ import annotations

import sys
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))


@pytest.fixture(scope="module")
def engine():
    """Создаём KolibriAIEngine один раз для модуля."""
    from backend.service.ai_engine import KolibriAIEngine

    eng = KolibriAIEngine()
    eng._persist_conversations = False
    return eng


@pytest.fixture(scope="module")
def greeting_result(engine):
    """Один вызов chat() для приветствия — переиспользуем."""
    return engine.chat("Привет, как дела?")


@pytest.fixture(scope="module")
def knowledge_result(engine):
    """Один вызов chat() для обычного запроса."""
    return engine.chat("Что такое нейронная сеть")


# ---- Chat: базовый вызов возвращает новые поля ----

def test_chat_returns_thinking(greeting_result):
    """chat() должен вернуть поле thinking (строка)."""
    assert "thinking" in greeting_result
    assert isinstance(greeting_result["thinking"], str)


def test_chat_returns_thinking_steps(greeting_result):
    """chat() должен вернуть список thinking_steps."""
    assert "thinking_steps" in greeting_result
    assert isinstance(greeting_result["thinking_steps"], list)


def test_chat_returns_generation_used(greeting_result):
    """chat() должен вернуть generation_used (bool)."""
    assert "generation_used" in greeting_result
    assert isinstance(greeting_result["generation_used"], bool)


def test_chat_returns_context_stats(greeting_result):
    """chat() должен вернуть context_stats (dict)."""
    assert "context_stats" in greeting_result
    stats = greeting_result["context_stats"]
    assert "working_count" in stats
    assert stats["working_count"] >= 1


# ---- Обычный запрос возвращает thinking_steps ----

def test_knowledge_returns_thinking_steps(knowledge_result):
    """Обычный запрос → thinking_steps с type и content."""
    assert "thinking_steps" in knowledge_result
    steps = knowledge_result["thinking_steps"]
    assert isinstance(steps, list)
    if steps:
        step = steps[0]
        assert "type" in step
        assert "content" in step


# ---- Основная структура ответа сохранена ----

def test_chat_preserves_existing_fields(knowledge_result):
    """Новые поля не ломают старые."""
    for key in ("response", "confidence", "sources", "method",
                "duration_ms", "formula_data", "graph_stats"):
        assert key in knowledge_result, f"Отсутствует ключ: {key}"


def test_chat_confidence_is_float(knowledge_result):
    """confidence — число от 0 до 1."""
    assert 0.0 <= knowledge_result["confidence"] <= 1.0


def test_chat_duration_positive(knowledge_result):
    """duration_ms > 0."""
    assert knowledge_result["duration_ms"] > 0


# ---- FormulaLM: атрибуты на месте ----

def test_lm_attributes_exist(engine):
    """LM-атрибуты должны быть инициализированы."""
    assert hasattr(engine, '_bpe_tokenizer')
    assert hasattr(engine, '_formula_lm')
    assert hasattr(engine, '_lm_trained')
    assert hasattr(engine, '_lm_generation')
    assert hasattr(engine, '_chain_of_thought')
    assert hasattr(engine, '_context_window')


def test_generate_text_returns_string(engine):
    """_generate_text() возвращает строку (пусть пустую)."""
    result = engine._generate_text("тест")
    assert isinstance(result, str)


def test_cache_is_scoped_by_conversation(engine):
    """Одинаковый вопрос в разных диалогах не должен подменять conversation_id."""
    prompt = "что такое распределенные вычисления?"
    res_a = engine.chat(prompt, conversation_id="cache-scope-a")
    res_b = engine.chat(prompt, conversation_id="cache-scope-b")

    assert res_a["conversation_id"].endswith("cache-scope-a")
    assert res_b["conversation_id"].endswith("cache-scope-b")
    assert any("cache-scope-a" in key for key in engine.conversations)
    assert any("cache-scope-b" in key for key in engine.conversations)


def test_cache_does_not_freeze_after_context_change(engine):
    """Одинаковый текст не должен возвращать устаревший cached-ответ после новых фактов."""
    conv = f"cache-context-update-{time.time_ns()}"
    first = engine.chat("а как насчет порда исползуетса в праекте?", conversation_id=conv)
    engine.chat("В проекте используется порт 8001 для API.", conversation_id=conv)
    second = engine.chat("а как насчет порда исползуетса в праекте?", conversation_id=conv)

    assert first["method"] != "dialog-context"
    assert second["method"] in {"dialog-context", "dialog-fact-ack", "dynamic-fallback"}
    assert "8001" in second["response"] or second["method"] != "dynamic-fallback"


def test_stage_budget_plan_is_ordered(engine):
    """План stage-budget должен укладываться в общий бюджет и иметь валидные этапы."""
    budgets, deadlines = engine._plan_chat_stage_deadlines(
        start_time=1000.0,
        global_deadline=1012.0,
        budget_ms=12000,
        fast_mode=True,
    )
    assert set(("retrieval", "synthesis", "repair", "cognition", "reserve")).issubset(set(budgets.keys()))
    assert budgets["retrieval"] > 0
    assert budgets["synthesis"] > 0
    assert deadlines["retrieval"] <= deadlines["synthesis"] <= deadlines["repair"] <= deadlines["cognition"] <= 1012.0


def test_context_window_is_isolated_per_conversation(engine):
    """Контекст разных conversation_id не должен смешиваться."""
    conv_a = "ctx-iso-a"
    conv_b = "ctx-iso-b"
    token_a = "уникальныймаркеральфа"
    token_b = "уникальныймаркербета"

    engine.chat(f"{token_a} объясни подробно", conversation_id=conv_a)
    engine.chat(f"{token_b} объясни подробно", conversation_id=conv_b)

    key_a = next(key for key in engine._context_windows if conv_a in key)
    key_b = next(key for key in engine._context_windows if conv_b in key)
    window_a = engine._context_windows[key_a]
    window_b = engine._context_windows[key_b]
    text_a = " ".join(msg.content.lower() for msg in window_a.working_memory)
    text_b = " ".join(msg.content.lower() for msg in window_b.working_memory)

    assert token_a in text_a
    assert token_b in text_b
    assert token_a not in text_b


def test_profile_memory_commands(engine):
    """Колибри должен стабильно запоминать имя и факты пользователя."""
    conv = "profile-memory-test-001"

    identity = engine.chat("Как тебя зовут?", conversation_id=conv)
    assert identity["method"] == "identity"
    assert "Колибри AI" in identity["response"]

    remember_name = engine.chat("Меня зовут Алексей", conversation_id=conv)
    assert remember_name["method"] == "remember-name"
    assert "Алексей" in remember_name["response"]

    who_am_i = engine.chat("Как меня зовут?", conversation_id=conv)
    assert who_am_i["method"] == "profile-memory"
    assert "Алексей" in who_am_i["response"]

    remember_fact = engine.chat(
        "Запомни, что я люблю практические тесты",
        conversation_id=conv,
    )
    assert remember_fact["method"] == "remember-fact"

    about_me = engine.chat("Что ты знаешь обо мне?", conversation_id=conv)
    assert about_me["method"] == "profile-memory"
    assert "Алексей" in about_me["response"]
    assert "люблю практические тесты" in about_me["response"]


def test_profile_memory_read_now_flows_through_canonical_synthesis(engine, monkeypatch):
    """Read-only profile memory query должен идти через canonical synthesis path."""
    conv = "profile-memory-canonical-001"
    engine.chat("Меня зовут Алексей", conversation_id=conv)

    called: dict[str, bool] = {}

    def fake_synthesize(*args, **kwargs):
        called["value"] = True
        return ("Я помню о вас:\n• Имя: Алексей", 0.95, "profile-memory")

    monkeypatch.setattr(engine, "_synthesize_response", fake_synthesize)

    result = engine.chat("Как меня зовут?", conversation_id=conv)

    assert called.get("value") is True
    assert result["method"] == "profile-memory"
    assert "Алексей" in result["response"]


def test_document_list_now_flows_through_canonical_synthesis(engine, monkeypatch):
    """Read-only список обученных текстов должен идти через canonical synthesis path."""
    conv = "document-list-canonical-001"
    learned = engine.chat("Научи: " + ("Сказка Колобок. " * 30), conversation_id=conv)
    assert learned["method"] == "train-command"

    called: dict[str, bool] = {}

    def fake_synthesize(*args, **kwargs):
        called["value"] = True
        return ("Я помню такие тексты:\n• Колобок", 0.95, "document-list")

    monkeypatch.setattr(engine, "_synthesize_response", fake_synthesize)

    result = engine.chat("Какие тексты ты знаешь?", conversation_id=conv)

    assert called.get("value") is True
    assert result["method"] == "document-list"
    assert "колобок" in result["response"].lower()


def test_retell_memory_now_flows_through_canonical_synthesis(engine, monkeypatch):
    """Read-only retell request должен идти через canonical synthesis path."""
    conv = "retell-canonical-001"
    learned = engine.chat("Научи: " + ("Сказка Колобок. " * 30), conversation_id=conv)
    assert learned["method"] == "train-command"

    called: dict[str, bool] = {}

    def fake_synthesize(*args, **kwargs):
        called["value"] = True
        return ("Пересказ: Колобок ушёл от деда и бабки.", 0.95, "retell-memory")

    monkeypatch.setattr(engine, "_synthesize_response", fake_synthesize)

    result = engine.chat("Перескажи колобок своими словами", conversation_id=conv)

    assert called.get("value") is True
    assert result["method"] == "retell-memory"
    assert "пересказ" in result["response"].lower()


def test_document_display_title_prefers_clean_summary_over_noisy_title(engine):
    doc = {
        "title": "Текст: представь, тебя, список",
        "summary": "Колобок ушёл от деда и бабки и встретил лису.",
        "text": "Колобок ушёл от деда и бабки и встретил лису.",
    }

    title = engine._document_display_title(doc)

    assert "колобок" in title.lower()
    assert not title.lower().startswith("текст:")


def test_document_list_hides_auto_message_documents(engine, monkeypatch):
    profile = engine._new_user_profile()
    profile["documents"] = [
        {
            "title": "Текст: представь, тебя, список",
            "summary": "Операторский автофрагмент, который не должен показываться пользователю.",
            "text": "Операторский автофрагмент, который не должен показываться пользователю.",
            "source": "auto-message",
        },
        {
            "title": "Сказка Колобок",
            "summary": "Колобок ушёл от деда и бабки и встретил зверей.",
            "text": "Колобок ушёл от деда и бабки и встретил зверей.",
            "source": "teach-command",
        },
    ]
    monkeypatch.setattr(engine, "_get_user_profile", lambda *args, **kwargs: profile)

    response, method = engine._build_profile_memory_read_response("Какие тексты ты знаешь?")

    assert method == "document-list"
    assert "колобок" in (response or "").lower()
    assert "представь, тебя, список" not in (response or "").lower()


def test_document_list_hides_noisy_operator_titles(engine, monkeypatch):
    profile = engine._new_user_profile()
    profile["documents"] = [
        {
            "title": "Вот это уже сильная, сбалансированная версия",
            "summary": "Вот это уже сильная, сбалансированная версия.",
            "text": "Вот это уже сильная, сбалансированная версия.",
            "source": "teach-command",
        },
        {
            "title": "Тест на код + безопасность",
            "summary": "Практический тест по коду и безопасности.",
            "text": "Практический тест по коду и безопасности.",
            "source": "teach-command",
        },
        {
            "title": "Сказка Колобок",
            "summary": "Колобок ушёл от деда и бабки и встретил зверей.",
            "text": "Колобок ушёл от деда и бабки и встретил зверей.",
            "source": "teach-command",
        },
    ]
    monkeypatch.setattr(engine, "_get_user_profile", lambda *args, **kwargs: profile)

    response, method = engine._build_profile_memory_read_response("Какие тексты ты знаешь?")

    assert method == "document-list"
    assert "колобок" in (response or "").lower()
    assert "вот это уже сильная" not in (response or "").lower()
    assert "тест на код + безопасность" in (response or "").lower()


def test_document_list_hides_devlog_like_entries_by_content(engine, monkeypatch):
    profile = engine._new_user_profile()
    profile["documents"] = [
        {
            "title": "Отчёт по исправлению",
            "summary": "Сделал. Локальный решатель логических задач внедрён и выкачен на сервер. Что изменил: backend/service/ai_engine.py",
            "text": "Сделал. Локальный решатель логических задач внедрён и выкачен на сервер. Что изменил: /Users/kolibri/kolibri-project/backend/service/ai_engine.py",
        },
        {
            "title": "История про теремок",
            "summary": "Теремок стоял в поле, и к нему приходили звери.",
            "text": "История про теремок. Теремок стоял в поле, и к нему приходили звери.",
        },
    ]
    monkeypatch.setattr(engine, "_get_user_profile", lambda *args, **kwargs: profile)

    response, method = engine._build_profile_memory_read_response("Какие тексты ты знаешь?")

    assert method == "document-list"
    assert "теремок" in (response or "").lower()
    assert "локальный решатель" not in (response or "").lower()
    assert "backend/service/ai_engine.py" not in (response or "")


def test_c_core_formula_bridge_short_circuits_definition_query(tmp_path):
    """Определительные запросы должны уметь приходить из C-core formula bridge."""
    from backend.service.ai_engine import KolibriAIEngine

    eng = KolibriAIEngine()
    eng._persist_conversations = False
    eng._enable_c_inference = True
    binary = tmp_path / "kolibri_infer_cli"
    binary.write_text("", encoding="utf-8")
    binary.chmod(0o755)
    memory_dir = tmp_path / "formula_memory"
    memory_dir.mkdir()
    eng.c_inference.binary_path = binary
    eng.c_inference.knowledge_path = memory_dir
    eng.c_inference.query = lambda text, strategy="formula": {
        "response": "Математика — точная формальная наука, изучающая структуры и отношения.",
        "confidence": 0.91,
        "knowledge_hits": 0,
        "formulas_applied": 1,
    }

    result = eng.chat("что такое математика", conversation_id=f"c-core-bridge-{time.time_ns()}")

    assert result["method"] == "c-core-formula"
    assert "точная формальная наука" in result["response"]


def test_c_inference_runner_parses_numeric_vote_summary():
    """Python bridge должен поднимать telemetry цифрового голосования из C CLI."""
    from backend.service.ai_engine import CInferenceRunner

    runner = CInferenceRunner()
    parsed = runner._parse_output(
        "\n".join(
            [
                "STATUS=ok",
                "CONFIDENCE=0.875",
                "KNOWLEDGE_HITS=2",
                "FORMULAS_APPLIED=1",
                "LOGIC_RULES=0",
                "DIGIT_WINNER=1",
                "DIGIT_WINNER_SCORE=8.400000",
                "DIGIT_RUNNER_UP_SCORE=4.200000",
                "DIGIT_CONSENSUS=0.510000",
                "QUERY_KIND=what_is",
                "CANONICAL_TOPIC=математика",
                "DEFINITION_ENTITY=математика",
                "TOPIC_TOKEN_COUNT=1",
                "DIGIT_VOTES=0:0.350000,1:8.400000,2:4.200000,3:0.000000,4:1.100000,5:3.500000,6:1.250000,7:0.000000,8:0.000000,9:0.450000",
                "RESPONSE_BEGIN",
                "Математика — точная формальная наука.",
                "RESPONSE_END",
            ]
        )
    )

    assert parsed is not None
    assert parsed["digit_winner"] == 1
    assert parsed["digit_consensus"] == pytest.approx(0.51)
    assert parsed["digit_votes"]["1"] == pytest.approx(8.4)
    assert parsed["digit_votes"]["5"] == pytest.approx(3.5)
    assert parsed["query_kind"] == "what_is"
    assert parsed["canonical_topic"] == "математика"
    assert parsed["definition_entity"] == "математика"
    assert parsed["topic_token_count"] == 1


def test_c_core_formula_bridge_handles_knowledge_query(tmp_path):
    """Запросы «что ты знаешь о ...» должны идти в C-core formula bridge."""
    from backend.service.ai_engine import KolibriAIEngine

    eng = KolibriAIEngine()
    eng._persist_conversations = False
    eng._enable_c_inference = True
    binary = tmp_path / "kolibri_infer_cli"
    binary.write_text("", encoding="utf-8")
    binary.chmod(0o755)
    memory_dir = tmp_path / "formula_memory"
    memory_dir.mkdir()
    eng.c_inference.binary_path = binary
    eng.c_inference.knowledge_path = memory_dir
    eng.c_inference.query = lambda text, strategy="formula": {
        "response": (
            "Астрономия — естественная наука о небесных телах и Вселенной. "
            "Она изучает звезды, планеты, галактики и космические процессы."
        ),
        "confidence": 0.9,
        "knowledge_hits": 0,
        "formulas_applied": 1,
    }

    result = eng.chat("что ты знаешь об астрономии", conversation_id=f"c-core-knowledge-{time.time_ns()}")

    assert result["method"] == "c-core-formula"
    assert "небесных телах" in result["response"]


def test_c_core_formula_bridge_surfaces_query_semantics(tmp_path):
    """Обычный c-core-formula runtime должен поднимать query semantics из C bridge."""
    from backend.service.ai_engine import KolibriAIEngine

    eng = KolibriAIEngine()
    eng._persist_conversations = False
    eng._enable_c_inference = True
    binary = tmp_path / "kolibri_infer_cli"
    binary.write_text("", encoding="utf-8")
    binary.chmod(0o755)
    memory_dir = tmp_path / "formula_memory"
    memory_dir.mkdir()
    eng.c_inference.binary_path = binary
    eng.c_inference.knowledge_path = memory_dir
    eng.c_inference.query = lambda text, strategy="formula": {
        "response": "Химия — естественная наука о веществах.",
        "confidence": 0.88,
        "knowledge_hits": 0,
        "formulas_applied": 1,
        "query_kind": "tell",
        "canonical_topic": "химия",
        "definition_entity": "химия",
        "topic_token_count": 1,
        "digit_winner": 1,
        "digit_consensus": 0.41,
        "digit_votes": {"1": 10.5, "5": 4.2},
    }

    result = eng.chat("расскажи о химии", conversation_id=f"c-core-semantics-{time.time_ns()}")

    assert result["method"] == "c-core-formula"
    fd = result["formula_data"]
    assert fd["c_query_kind"] == "tell"
    assert fd["c_canonical_topic"] == "химия"
    assert fd["c_definition_entity"] == "химия"
    assert fd["c_topic_token_count"] == 1
    assert fd["c_digit_winner"] == 1
    assert fd["c_digit_consensus"] == pytest.approx(0.41)


@pytest.mark.parametrize(
    ("query", "needle"),
    [
        ("объясни физику", "физик"),
        ("расскажи о химии", "хими"),
        ("что ты знаешь о праве", "прав"),
        ("как устроено право", "прав"),
        ("почему важно право", "прав"),
    ],
)
def test_projection_queries_use_unified_canonical_fallback(engine, monkeypatch: pytest.MonkeyPatch, query, needle):
    """Projection-запросы должны идти через единый fallback, а не в старые explain/topic-overview ветки."""
    monkeypatch.setattr(engine, "_enable_c_inference", False)
    monkeypatch.setattr(engine, "_try_web_augment_answer", lambda *args, **kwargs: None)

    result = engine.chat(query, conversation_id=f"projection-fallback-{abs(hash(query)) % 10000}")

    assert result["method"] == "canonical-topic-fallback"
    assert needle in result["response"].lower()


def test_runtime_digit_vote_present_for_math_eval(engine):
    result = engine.chat("45+678", conversation_id=f"runtime-vote-math-{time.time_ns()}")
    assert result["method"] == "math-eval"
    fd = result["formula_data"]
    assert fd["runtime_vote_origin"] == "runtime"
    assert fd["runtime_digit_winner"] == 4
    assert float(fd["runtime_digit_votes"]["4"]) > 0.0


def test_math_eval_now_flows_through_canonical_synthesis(engine, monkeypatch):
    """Math query должен идти через canonical synthesis path, а не early special branch."""
    called: dict[str, bool] = {}

    def fake_synthesize(*args, **kwargs):
        called["value"] = True
        return ("2+2 = **4**", 0.98, "math-eval")

    monkeypatch.setattr(engine, "_synthesize_response", fake_synthesize)

    result = engine.chat("2+2", conversation_id="math-canonical-001")

    assert called.get("value") is True
    assert result["method"] == "math-eval"


def test_runtime_digit_vote_present_for_dialog_context(engine):
    conv = f"runtime-vote-context-{time.time_ns()}"
    engine.chat("В проекте используется порт 8001 для API.", conversation_id=conv)
    result = engine.chat("А подробнее", conversation_id=conv)

    assert result["method"] == "dialog-context"
    fd = result["formula_data"]
    assert fd["runtime_vote_origin"] == "runtime"
    assert float(fd["runtime_digit_votes"]["6"]) > 0.0
    assert fd["runtime_query_kind"] == "followup"


def test_runtime_digit_vote_present_for_c_core_formula(engine, monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setattr(engine, "_enable_c_inference", True)
    monkeypatch.setattr(
        engine,
        "_is_valid_c_formula_answer",
        lambda query, payload: bool(payload and payload.get("response")),
    )

    class DummyCInference:
        available = True

        @staticmethod
        def query(text: str, strategy: str = "formula") -> dict[str, object]:
            return {
                "response": "Право — система общеобязательных норм.",
                "confidence": 0.91,
                "knowledge_hits": 0,
                "formulas_applied": 1,
                "query_kind": "knowledge",
                "canonical_topic": "право",
                "definition_entity": "право",
                "topic_token_count": 1,
                "digit_winner": 1,
                "digit_consensus": 0.44,
                "digit_votes": {"1": 8.5, "5": 3.2, "9": 2.1},
            }

    monkeypatch.setattr(engine, "c_inference", DummyCInference())

    result = engine.chat("что ты знаешь о праве", conversation_id=f"runtime-vote-c-core-{time.time_ns()}")

    assert result["method"] == "c-core-formula"
    fd = result["formula_data"]
    assert fd["c_digit_winner"] == 1
    assert fd["runtime_vote_origin"] == "c-core+runtime"
    assert float(fd["runtime_digit_votes"]["1"]) >= 8.5


def test_dialog_context_now_flows_through_canonical_synthesis(engine, monkeypatch: pytest.MonkeyPatch):
    """Follow-up context answer больше не должен short-circuit до _synthesize_response."""
    called = {"value": False}

    def fake_synthesize_response(**kwargs):
        called["value"] = True
        return ("По контексту текущего диалога: порт 8001 используется для API.", 0.56, "dialog-context")

    monkeypatch.setattr(engine, "_synthesize_response", fake_synthesize_response)

    conv = f"canonical-dialog-context-{time.time_ns()}"
    engine.chat("В проекте используется порт 8001 для API.", conversation_id=conv)
    result = engine.chat("А подробнее", conversation_id=conv)

    assert called["value"] is True
    assert result["method"] == "dialog-context"
    assert "8001" in result["response"]


def test_dialog_fact_ack_now_flows_through_canonical_synthesis(engine, monkeypatch: pytest.MonkeyPatch):
    """Фактическое утверждение пользователя должно идти через _synthesize_response, а не ранний short-circuit."""
    called = {"value": False}

    def fake_synthesize_response(**kwargs):
        called["value"] = True
        return ("Принял. Зафиксировал в контексте: В проекте используется порт 8001 для API.", 0.78, "dialog-fact-ack")

    monkeypatch.setattr(engine, "_maybe_auto_learn_from_message", lambda *_args, **_kwargs: None)
    monkeypatch.setattr(engine, "_synthesize_response", fake_synthesize_response)

    result = engine.chat(
        "В проекте используется порт 8001 для API.",
        conversation_id=f"canonical-dialog-fact-{time.time_ns()}",
    )

    assert called["value"] is True
    assert result["method"] == "dialog-fact-ack"
    assert "8001" in result["response"]


def test_c_core_formula_accepts_definition_with_etymology_noise():
    """Этимология и латиница в начале не должны ломать корректное определение."""
    from backend.service.ai_engine import KolibriAIEngine

    eng = KolibriAIEngine()
    payload = {
        "response": (
            "Медицина (от лат. medicina, ars medicina — лечебное искусство) — "
            "система научных знаний и практической деятельности, направленная "
            "на сохранение здоровья и лечение болезней."
        ),
        "confidence": 0.82,
    }

    assert eng._is_valid_c_formula_answer("что такое медицина", payload) is True
    assert "система научных знаний" in payload["response"]


def test_c_core_formula_rejects_domain_substitution_for_definition():
    """Bridge не должен принимать подмену темы вроде географии -> математика."""
    from backend.service.ai_engine import KolibriAIEngine

    eng = KolibriAIEngine()
    payload = {
        "response": "География — точная формальная наука, изучающая структуры и отношения.",
        "confidence": 0.84,
    }

    assert eng._is_valid_c_formula_answer("что такое география", payload) is False


def test_c_core_formula_requires_focus_term_for_definition():
    """Для definitional query ответ должен содержать сам предмет или его стем."""
    from backend.service.ai_engine import KolibriAIEngine

    eng = KolibriAIEngine()
    payload = {
        "response": "Это система научных знаний и практической деятельности, направленная на сохранение здоровья.",
        "confidence": 0.84,
    }

    assert eng._is_valid_c_formula_answer("что такое медицина", payload) is False


def test_c_core_formula_accepts_inflected_definition_support_terms():
    """Словоформы вроде «материи» и «движения» не должны ломать ответ по физике."""
    from backend.service.ai_engine import KolibriAIEngine

    eng = KolibriAIEngine()
    payload = {
        "response": (
            "Физика — естественная наука, изучающая наиболее общие свойства "
            "материи, движения, энергии, пространства и времени."
        ),
        "confidence": 0.88,
    }

    assert eng._is_valid_c_formula_answer("что такое физика", payload) is True


def test_c_core_formula_rejects_series_instead_of_medical_therapy():
    """Терапия как сериал не должна проходить как валидный медицинский ответ."""
    from backend.service.ai_engine import KolibriAIEngine

    eng = KolibriAIEngine()
    payload = {
        "response": "Терапия — американский комедийный сериал.",
        "confidence": 0.91,
    }

    assert eng._is_valid_c_formula_answer("что такое терапия", payload) is False


def test_definition_query_is_not_misclassified_as_greeting():
    """Definitional query вроде «что такое право» не должен попадать в greeting intent."""
    from backend.service.ai_engine import KolibriAIEngine

    eng = KolibriAIEngine()
    assert eng._is_greeting_intent("что такое право") is False


def test_capabilities_intent_handles_natural_russian_variants(engine):
    """Вопросы о возможностях не должны уходить в retrieval-корпус."""
    result = engine.chat("что ты уже умеешь", conversation_id="capabilities-001")
    assert result["method"] == "command"
    text = result["response"].lower()
    assert "умею" in text
    assert "c-ядро" in text or "локальной базы знаний" in text


def test_capabilities_now_flow_through_canonical_synthesis(engine, monkeypatch):
    """Read-only capabilities query должен идти через canonical synthesis path."""
    called: dict[str, bool] = {}

    def fake_synthesize(*args, **kwargs):
        called["value"] = True
        return ("Сейчас я уже умею считать точные выражения через C-ядро.", 1.0, "command")

    monkeypatch.setattr(engine, "_synthesize_response", fake_synthesize)

    result = engine.chat("что ты уже умеешь", conversation_id="capabilities-canonical-001")

    assert called.get("value") is True
    assert result["method"] == "command"


def test_architecture_query_returns_system_explanation(engine):
    """Запрос про архитектуру Kolibri должен объясняться системно, а не доменным мусором."""
    result = engine.chat("Объясни архитектуру Kolibri простым языком", conversation_id="arch-001")
    assert result["method"] == "kolibri-architecture"
    text = result["response"].lower()
    assert "c-ядро" in text or "ядро" in text
    assert "фронтенд" in text


def test_architecture_now_flows_through_canonical_synthesis(engine, monkeypatch):
    """Read-only architecture query должен идти через canonical synthesis path."""
    called: dict[str, bool] = {}

    def fake_synthesize(*args, **kwargs):
        called["value"] = True
        return ("Kolibri устроен слоями. В центре — C-ядро.", 0.98, "kolibri-architecture")

    monkeypatch.setattr(engine, "_synthesize_response", fake_synthesize)

    result = engine.chat("Объясни архитектуру Kolibri простым языком", conversation_id="arch-canonical-001")

    assert called.get("value") is True
    assert result["method"] == "kolibri-architecture"


@pytest.mark.parametrize(
    ("query", "method", "needle"),
    [
        ("ты умеешь говорить", "self-meta", "умею"),
        ("ты бог?", "self-meta", "не бог"),
        ("дебил", "abuse-deescalation", "что именно сломалось"),
        ("владислав кочуров", "clarify-entity", "уточните"),
    ],
)
def test_meta_and_boundary_intents_do_not_fall_into_retrieval(engine, query, method, needle):
    result = engine.chat(query, conversation_id=f"meta-{abs(hash(query)) % 10000}")
    assert result["method"] == method
    assert needle in result["response"].lower()


def test_self_meta_now_flows_through_canonical_synthesis(engine, monkeypatch):
    """Read-only self-meta query должен идти через canonical synthesis path."""
    called: dict[str, bool] = {}

    def fake_synthesize(*args, **kwargs):
        called["value"] = True
        return ("Да. Я умею общаться текстом и считать.", 0.98, "self-meta")

    monkeypatch.setattr(engine, "_synthesize_response", fake_synthesize)

    result = engine.chat("ты умеешь говорить", conversation_id="self-meta-canonical-001")

    assert called.get("value") is True
    assert result["method"] == "self-meta"


def test_system_stats_now_flow_through_canonical_synthesis(engine, monkeypatch):
    called: dict[str, bool] = {}

    def fake_synthesize(*args, **kwargs):
        called["value"] = True
        return ("📊 **Kolibri AI — Числовое Мышление**", 1.0, "command")

    monkeypatch.setattr(engine, "_synthesize_response", fake_synthesize)

    result = engine.chat("Покажи статистику модели", conversation_id="system-stats-canonical-001")

    assert called.get("value") is True
    assert result["method"] == "command"
    assert "kolibri" in result["response"].lower()


def test_formula_inspect_now_flows_through_canonical_synthesis(engine, monkeypatch):
    called: dict[str, bool] = {}

    def fake_synthesize(*args, **kwargs):
        called["value"] = True
        return ("⚡ **Формула Kolibri (лучшая из 16)**", 1.0, "formula-inspect")

    monkeypatch.setattr(engine, "_synthesize_response", fake_synthesize)

    result = engine.chat("Покажи формулу", conversation_id="formula-inspect-canonical-001")

    assert called.get("value") is True
    assert result["method"] == "formula-inspect"


def test_formula_inspect_is_not_overridden_by_ru_safe_fallback(engine, monkeypatch):
    """Canonical formula-inspect не должен сбиваться outer language-fallback'ом."""
    monkeypatch.setattr(
        engine,
        "_synthesize_response",
        lambda *args, **kwargs: ("⚡ **Формула Kolibri (лучшая из 16)**", 1.0, "formula-inspect"),
    )
    monkeypatch.setattr(engine, "_response_needs_language_fallback", lambda text: True)
    monkeypatch.setattr(engine, "_build_russian_fallback", lambda message, retrieved: "fallback")

    result = engine.chat("Покажи формулу", conversation_id="formula-inspect-outer-guard-001")

    assert result["method"] == "formula-inspect"
    assert "формула kolibri" in result["response"].lower()


def test_projection_queries_now_flow_through_canonical_synthesis(engine, monkeypatch):
    """Projection-запросы больше не должны short-circuit'иться до _synthesize_response()."""
    called: dict[str, bool] = {}

    def fake_synthesize(*args, **kwargs):
        called["value"] = True
        return ("Право — система общеобязательных норм.", 0.88, "c-core-formula")

    monkeypatch.setattr(engine, "_synthesize_response", fake_synthesize)

    result = engine.chat("что ты знаешь о праве", conversation_id=f"projection-synth-{time.time_ns()}")

    assert called.get("value") is True
    assert result["method"] == "c-core-formula"


def test_projection_synthesis_preserves_c_formula_payload(engine, monkeypatch):
    """Projection-ответ из canonical synthesis должен сохранить c-core telemetry."""
    monkeypatch.setattr(engine, "_match_c_projection_query", lambda *_args, **_kwargs: ("knowledge", "право"))
    monkeypatch.setattr(
        engine,
        "_try_c_core_topic_projection_response",
        lambda **_kwargs: {
            "response": "Право — система общеобязательных норм.",
            "confidence": 0.88,
            "sources": ["c-core-formula"],
            "method": "c-core-formula",
            "knowledge_hits": 2,
            "c_payload": {
                "query_kind": "knowledge",
                "canonical_topic": "право",
                "digit_winner": 1,
                "digit_consensus": 0.44,
                "digit_votes": {"1": 2.0, "9": 0.5},
                "formulas_applied": 3,
                "knowledge_hits": 2,
            },
            "formula_data": {
                "c_query_kind": "knowledge",
                "c_canonical_topic": "право",
                "c_digit_winner": 1,
                "c_digit_consensus": 0.44,
                "c_digit_votes": {"1": 2.0, "9": 0.5},
            },
            "graph_stats": {"patterns": 1},
        },
    )

    result = engine.chat("что ты знаешь о праве", conversation_id=f"projection-payload-{time.time_ns()}")

    assert result["method"] == "c-core-formula"
    assert result["sources"] == ["c-core-formula"]
    assert result["knowledge_hits"] == 2
    assert result["formula_data"]["c_query_kind"] == "knowledge"
    assert result["formula_data"]["c_canonical_topic"] == "право"
    assert result["formula_data"]["c_digit_winner"] == 1


def test_c_inference_warmup_uses_formula_query(tmp_path):
    """Warmup должен прогревать именно формульный C-контур."""
    from backend.service.ai_engine import CInferenceRunner

    binary = tmp_path / "kolibri_infer_cli"
    binary.write_text("", encoding="utf-8")
    binary.chmod(0o755)
    knowledge = tmp_path / "formula_memory"
    knowledge.mkdir()

    runner = CInferenceRunner(binary_path=binary, knowledge_path=knowledge)
    calls: list[tuple[str, str]] = []

    def _query(text: str, strategy: str = "formula"):
        calls.append((text, strategy))
        return {"response": "ok"}

    runner.query = _query  # type: ignore[method-assign]
    runner.warmup()

    assert calls == [("что такое математика", "formula")]


def test_c_inference_query_ignores_invalid_utf8(monkeypatch, tmp_path):
    """Bridge к C CLI не должен падать на битом UTF-8 в stdout."""
    from backend.service.ai_engine import CInferenceRunner

    binary = tmp_path / "kolibri_infer_cli"
    binary.write_text("", encoding="utf-8")
    binary.chmod(0o755)
    knowledge = tmp_path / "formula_memory"
    knowledge.mkdir()

    class _Result:
        returncode = 0
        stdout = b"STATUS=ok\nRESPONSE_BEGIN\n\xd0\x9c\xd0\xb0\xd1\x82\xd0\xb5\xd0\xbc\xd0\xb0\xd1\x82\xd0\xb8\xd0\xba\xd0\xb0 \xff\xd0\xb2\xd0\xb0\xd0\xb6\xd0\xbd\xd0\xb0\nRESPONSE_END\n"

    monkeypatch.setattr("backend.service.ai_engine.subprocess.run", lambda *args, **kwargs: _Result())

    runner = CInferenceRunner(binary_path=binary, knowledge_path=knowledge)
    payload = runner.query("почему важна математика", strategy="formula")

    assert payload is not None
    assert "Математика" in str(payload.get("response", ""))


@pytest.mark.parametrize(
    "query",
    [
        "привет колибри",
        "превет, колибри",
        "privet kolibri",
        "здарова",
    ],
)
def test_greeting_linguistic_variants(engine, query):
    """Приветствие должно срабатывать даже при транслите/опечатке."""
    result = engine.chat(query, conversation_id=f"greet-ling-{abs(hash(query)) % 10000}")
    assert result["method"] == "greeting"
    assert "колибри" in result["response"].lower()


@pytest.mark.parametrize(
    "query",
    [
        "как дела",
        "Как ты",
        "как дела колибри",
    ],
)
def test_smalltalk_checkin_does_not_fall_into_retrieval_noise(engine, query):
    """Короткий check-in должен давать диалоговый ответ, а не доменный мусор."""
    result = engine.chat(query, conversation_id=f"smalltalk-{abs(hash(query)) % 10000}")
    assert result["method"] == "smalltalk-checkin"
    assert "готов помочь" in result["response"].lower()


@pytest.mark.parametrize(
    "query",
    [
        "как тебя завут",
        "кто ты такой",
        "твое имя",
        "представся",
    ],
)
def test_identity_linguistic_variants(engine, query):
    """Идентификация ассистента должна быть устойчивой к шуму ввода."""
    result = engine.chat(query, conversation_id=f"identity-ling-{abs(hash(query)) % 10000}")
    assert result["method"] == "identity"
    assert "колибри" in result["response"].lower()


def test_greeting_does_not_override_real_question(engine):
    """Если после приветствия есть содержательный вопрос, не возвращаем greeting."""
    result = engine.chat(
        "привет, сколько будет один плюс один",
        conversation_id="greet-not-override-001",
    )
    assert result["method"] != "greeting"


def test_dialog_context_handles_noisy_followup(engine):
    """Контекст должен удерживаться даже при шумном follow-up с опечатками."""
    conv = "dialog-context-noise-001"
    engine.chat("В проекте используется порт 8001 для API.", conversation_id=conv)
    result = engine.chat("а как насчет порда исползуетса в праекте?", conversation_id=conv)
    assert result["method"] in {"dialog-context", "dynamic-fallback", "dialog-fact-ack"}
    assert (
        "8001" in result["response"]
        or "порт" in result["response"].lower()
        or "мало проверенных данных" in result["response"].lower()
        or "зафиксировал" in result["response"].lower()
    )


def test_linguistic_normalizes_project_typos(engine):
    """Шумный ввод по проектным словам должен нормализоваться в канонические токены."""
    terms = engine._extract_linguistic_terms(
        "а как насчет порда исползуетса в праекте?",
        min_len=2,
        drop_stop=True,
        drop_generic=False,
    )
    assert "порт" in terms
    assert "используется" in terms
    assert "проекте" in terms


def test_project_query_without_context_returns_guided_fallback(engine):
    """Шумный проектный вопрос без фактов должен давать управляемый fallback, а не случайный ответ."""
    conv = "project-guided-fallback-001"
    result = engine.chat("а как насчет порда исползуетса в праекте?", conversation_id=conv)
    assert result["method"] == "dynamic-fallback"
    assert "подтвержд" in result["response"].lower()
    assert "добавьте факт" in result["response"].lower()


def test_no_false_context_switch_after_unrelated_topic(engine):
    """После темы погоды шумный проектный вопрос не должен ошибочно тянуть погодный контекст."""
    conv = "project-after-weather-001"
    engine.chat("Какая погода в Лениногорске?", conversation_id=conv)
    engine.chat("А подробнее", conversation_id=conv)
    result = engine.chat("а как насчет порда исползуетса в праекте?", conversation_id=conv)
    assert result["method"] == "dynamic-fallback"
    assert "подтвержд" in result["response"].lower()


def test_weather_shape_rejects_irrelevant_noise(engine):
    """Погодный вопрос не должен проходить с нерелевантным «пробки утром» ответом."""
    ok = engine._answer_shape_is_valid(
        "Какая погода в Лениногорске?",
        "Так, утром может отображаться информация о пробках, а вечером — погода на завтра.",
    )
    assert not ok


def test_plain_fact_statement_is_acknowledged(engine):
    """Информативное утверждение без вопроса подтверждается и фиксируется в контексте."""
    conv = "dialog-fact-ack-001"
    result = engine.chat("В проекте используется порт 8001 для API.", conversation_id=conv)
    assert result["method"] == "dialog-fact-ack"
    assert "8001" in result["response"]


def test_plain_fact_statement_saved_in_profile_memory(engine):
    """Подтверждённый факт должен попадать в долговременную память клиента."""
    conv = "dialog-fact-ack-002"
    engine.chat("В проекте используется порт 8001 для API.", conversation_id=conv)
    memory = engine.chat("Что ты знаешь обо мне?", conversation_id=conv)
    assert memory["method"] == "profile-memory"
    assert "порт 8001" in memory["response"].lower()


def test_followup_only_uses_context(engine):
    """Фраза «А подробнее» должна продолжать контекст, а не уходить в случайный retrieval."""
    conv = "dialog-followup-only-001"
    engine.chat("В проекте используется порт 8001 для API.", conversation_id=conv)
    result = engine.chat("А подробнее", conversation_id=conv)
    assert result["method"] == "dialog-context"
    assert "8001" in result["response"] or "порт" in result["response"].lower()


def test_followup_only_strips_service_ack_wrapper(engine):
    """Follow-up по факту должен возвращать сам факт без служебного 'Принял. Зафиксировал...'."""
    conv = "dialog-followup-clean-001"
    engine.chat("В проекте используется порт 8001 для API.", conversation_id=conv)
    result = engine.chat("А подробнее", conversation_id=conv)
    assert result["method"] == "dialog-context"
    assert "зафиксировал" not in result["response"].lower()
    assert "порт 8001" in result["response"].lower()


def test_followup_only_without_context_is_safe(engine):
    """Если контекста нет, follow-up не должен генерировать мусорный контент."""
    conv = "dialog-followup-empty-001"
    result = engine.chat("А подробнее", conversation_id=conv)
    assert result["method"] == "dialog-context"
    assert "нет фактов" in result["response"].lower()


def test_conversation_recap_uses_current_thread(engine):
    """Вопрос о текущем диалоге должен сводить именно текущий тред, а не retrieval-корпус."""
    conv = "dialog-recap-001"
    engine.chat("Меня зовут Владислав", conversation_id=conv)
    engine.chat("Расскажи о праве", conversation_id=conv)

    result = engine.chat("О чем мы говорили до этого?", conversation_id=conv)

    assert result["method"] == "conversation-memory"
    text = result["response"].lower()
    assert "владислав" in text
    assert "прав" in text


def test_conversation_recap_now_flows_through_canonical_synthesis(engine, monkeypatch):
    """Read-only recap должен идти через canonical synthesis path."""
    conv = "dialog-recap-canonical-001"
    engine.chat("Меня зовут Владислав", conversation_id=conv)
    engine.chat("Расскажи о праве", conversation_id=conv)

    called: dict[str, bool] = {}

    def fake_synthesize(*args, **kwargs):
        called["value"] = True
        return (
            "Кратко по текущему диалогу:\n"
            "• Вы сказали: Меня зовут Владислав.\n"
            "• Я ответил: Право — это система норм.",
            0.96,
            "conversation-memory",
        )

    monkeypatch.setattr(engine, "_synthesize_response", fake_synthesize)

    result = engine.chat("О чем мы говорили до этого?", conversation_id=conv)

    assert called.get("value") is True
    assert result["method"] == "conversation-memory"
    assert "владислав" in result["response"].lower()


def test_conversation_recap_without_context_is_safe(engine):
    """Если содержательного контекста нет, recap должен честно об этом сказать."""
    conv = "dialog-recap-empty-001"
    result = engine.chat("О чем мы говорили до этого?", conversation_id=conv)
    assert result["method"] == "conversation-memory-empty"
    assert "нет содержатель" in result["response"].lower()


def test_followup_after_recap_returns_topic_not_recap_wrapper(engine):
    """После recap фраза «А подробнее» должна продолжать тему, а не пересказывать сам recap."""
    conv = "dialog-recap-followup-001"
    engine.chat("Расскажи о праве", conversation_id=conv)
    engine.chat("О чем мы говорили до этого?", conversation_id=conv)

    result = engine.chat("А подробнее", conversation_id=conv)

    assert result["method"] == "dialog-context"
    text = result["response"].lower()
    assert "прав" in text
    assert "кратко по текущему диалогу" not in text


def test_followup_simple_mode_keeps_current_topic(engine):
    """«А проще» должно продолжать текущую тему, а не уходить в случайный retrieval."""
    conv = "dialog-followup-simple-001"
    engine.chat("Расскажи о праве", conversation_id=conv)

    result = engine.chat("А проще", conversation_id=conv)

    assert result["method"] == "dialog-context"
    text = result["response"].lower()
    assert "прав" in text
    assert "если проще" in text or "по контексту текущего диалога" in text


def test_followup_bullets_mode_formats_current_topic(engine):
    """«По пунктам» должно структурировать предыдущий ответ по текущей теме."""
    conv = "dialog-followup-bullets-001"
    engine.chat("Расскажи о праве", conversation_id=conv)
    engine.chat("А проще", conversation_id=conv)

    result = engine.chat("По пунктам", conversation_id=conv)

    assert result["method"] == "dialog-context"
    assert "•" in result["response"]
    text = result["response"].lower()
    assert "прав" in text
    assert "а проще" not in text


def test_followup_more_mode_stays_on_same_topic(engine):
    """«Что ещё?» должно продолжать последнюю тему, а не открывать новую случайную ветку."""
    conv = "dialog-followup-more-001"
    engine.chat("Что такое химия?", conversation_id=conv)

    result = engine.chat("Что ещё?", conversation_id=conv)

    assert result["method"] == "dialog-context"
    assert "хим" in result["response"].lower()


def test_followup_more_mode_prefers_additive_information(engine, monkeypatch: pytest.MonkeyPatch):
    """«Что ещё?» должно пытаться добавить новый смысл, а не просто повторять прошлый ответ."""
    conv = "dialog-followup-more-additive-001"
    engine.chat("Расскажи о праве", conversation_id=conv)

    monkeypatch.setattr(engine, "_enable_c_inference", False)
    monkeypatch.setattr(
        engine,
        "_topic_summary_for_followup",
        lambda topic: "Право — система общеобязательных норм. Право защищает законные интересы и устанавливает ответственность.",
    )
    monkeypatch.setattr(
        engine,
        "_get_recent_assistant_answer",
        lambda context_window, current_query=None: "Право — система общеобязательных норм.",
    )

    result = engine.chat("Что ещё?", conversation_id=conv)

    assert result["method"] == "dialog-context"
    text = result["response"].lower()
    assert "защищает" in text or "ответствен" in text
    assert "система общеобязательных норм" not in text


def test_pick_additive_followup_text_drops_template_prefix(engine):
    """Additive-путь не должен возвращать одну служебную обвязку без полезного содержания."""
    result = engine._pick_additive_followup_text(
        "Например, если взять тему «право», то можно рассмотреть такой случай: Право защищает законные интересы и устанавливает ответственность.",
        "Право — система общеобязательных норм.",
        topic="право",
    )

    assert result is not None
    text = result.lower()
    assert "защищает" in text or "ответствен" in text
    assert "можно рассмотреть такой случай" not in text


def test_followup_more_mode_uses_secondary_c_query_for_new_aspect(engine, monkeypatch: pytest.MonkeyPatch):
    """Если первый c-core follow-up повторяет определение, engine должен пробовать следующий вопрос к ядру."""
    conv = "dialog-followup-more-c-core-001"
    engine.chat("Расскажи о праве", conversation_id=conv)
    engine.chat("Это точно?", conversation_id=conv)

    monkeypatch.setattr(engine, "_enable_c_inference", True)
    monkeypatch.setattr(engine, "_topic_summary_for_followup", lambda topic: None)
    monkeypatch.setattr(
        engine,
        "_is_valid_c_formula_answer",
        lambda query, payload: bool(payload and payload.get("response")),
    )

    class DummyCInference:
        available = True

        @staticmethod
        def query(text: str, strategy: str = "formula") -> dict[str, object]:
            normalized = text.lower()
            if normalized == "расскажи подробно о право":
                return {"response": "Право — система общеобязательных норм. Право определяет допустимое поведение."}
            if normalized == "как устроено право":
                return {"response": "Право регулирует обязанности, ответственность и способы защиты законных интересов."}
            return {"response": ""}

    monkeypatch.setattr(engine, "c_inference", DummyCInference())

    result = engine.chat("Что ещё?", conversation_id=conv)

    assert result["method"] == "dialog-context"
    text = result["response"].lower()
    assert "обязан" in text or "ответствен" in text or "защит" in text
    assert "основное уже обозначено" not in text


def test_followup_example_mode_uses_c_core_projection(engine, monkeypatch: pytest.MonkeyPatch):
    """«А пример?» должен уметь брать дополнительную проекцию темы из c-core, а не только повторять определение."""
    conv = "dialog-followup-example-c-core-001"
    engine.chat("Расскажи о праве", conversation_id=conv)

    monkeypatch.setattr(engine, "_enable_c_inference", True)
    monkeypatch.setattr(
        engine,
        "_is_valid_c_formula_answer",
        lambda query, payload: bool(payload and payload.get("response")),
    )

    class DummyCInference:
        available = True

        @staticmethod
        def query(text: str, strategy: str = "formula") -> dict[str, object]:
            normalized = text.lower()
            if normalized == "как устроено право":
                return {"response": "Право регулирует договоры, обязанности сторон и ответственность за нарушение правил."}
            if normalized == "зачем нужно право":
                return {"response": "Право нужно для защиты интересов людей и согласования общественных правил."}
            return {"response": ""}

    monkeypatch.setattr(engine, "c_inference", DummyCInference())

    result = engine.chat("А пример?", conversation_id=conv)

    assert result["method"] == "dialog-context"
    text = result["response"].lower()
    assert "например" in text
    assert "договор" in text or "ответствен" in text or "защит" in text


def test_followup_c_query_candidates_include_example_paths(engine):
    """Example/more follow-up должны формировать query-варианты под примеры и применения."""
    example_candidates = engine._followup_c_query_candidates("example", "право")
    more_candidates = engine._followup_c_query_candidates("more", "право")

    assert "пример из право" in example_candidates
    assert "пример применения право" in example_candidates
    assert "пример из практики право" in example_candidates
    assert "типичный случай право" in example_candidates
    assert "где используется право" in example_candidates
    assert "роль право" in more_candidates
    assert "функции право" in more_candidates
    assert "применение право" in more_candidates
    assert "пример из право" in more_candidates
    assert "типичный случай право" in more_candidates


def test_extract_explicit_example_text_prefers_example_clause(engine):
    """Если ответ содержит явный маркер примера, follow-up должен брать сам пример, а не определение целиком."""
    result = engine._extract_explicit_example_text(
        "Право — система норм. Пример из права: договор определяет обязанности сторон и последствия нарушения условий.",
        topic="право",
    )

    assert result is not None
    text = result.lower()
    assert "договор" in text
    assert "обязан" in text or "нарушени" in text
    assert "система норм" not in text


def test_extract_explicit_example_text_allows_short_example_tail(engine):
    """Короткий explicit-example хвост тоже должен засчитываться как новый пример."""
    result = engine._extract_explicit_example_text(
        "Право — система норм. Пример из права: договор.",
        topic="право",
    )

    assert result is not None
    assert "договор" in result.lower()


def test_extract_explicit_example_text_supports_natural_markers(engine):
    result = engine._extract_explicit_example_text(
        "Право помогает упорядочивать общественные отношения. Например: договор фиксирует обязанности сторон.",
        topic="право",
    )

    assert result is not None
    text = result.lower()
    assert "договор" in text
    assert "обязан" in text


def test_followup_example_mode_prefers_explicit_example_from_recent_answer(engine, monkeypatch: pytest.MonkeyPatch):
    """Если прошлый ответ уже содержит явный пример, «А пример?» должен вернуть его напрямую."""
    conv = "dialog-followup-example-recent-001"
    engine.chat("Расскажи о праве", conversation_id=conv)

    monkeypatch.setattr(
        engine,
        "_get_recent_assistant_answer",
        lambda context_window, current_query=None: (
            "Право — система норм. Пример из права: договор определяет обязанности сторон и последствия нарушения условий."
        ),
    )

    result = engine.chat("А пример?", conversation_id=conv)

    assert result["method"] == "dialog-context"
    text = result["response"].lower()
    assert "например" in text
    assert "договор" in text
    assert "обязан" in text or "нарушени" in text


def test_extract_followup_directive_mode_supports_natural_example_and_more_variants(engine):
    assert engine._extract_followup_directive_mode("Приведи пример") == "example"
    assert engine._extract_followup_directive_mode("Поясни на примере") == "example"
    assert engine._extract_followup_directive_mode("Что ещё важного?") == "more"
    assert engine._extract_followup_directive_mode("Что ещё ты знаешь?") == "more"


def test_followup_why_mode_stays_on_topic(engine):
    """«А почему?» должно продолжать текущую тему, а не уходить в новый поиск."""
    conv = "dialog-followup-why-001"
    engine.chat("Расскажи о праве", conversation_id=conv)

    result = engine.chat("А почему?", conversation_id=conv)

    assert result["method"] == "dialog-context"
    text = result["response"].lower()
    assert "прав" in text
    assert "смысл" in text or "потому" in text or "важ" in text


def test_followup_example_mode_stays_on_topic(engine):
    """«А пример?» должен давать пример по последней теме, а не терять контекст."""
    conv = "dialog-followup-example-001"
    engine.chat("Что такое химия?", conversation_id=conv)

    result = engine.chat("А пример?", conversation_id=conv)

    assert result["method"] == "dialog-context"
    text = result["response"].lower()
    assert "хим" in text
    assert "например" in text or "пример" in text


def test_followup_compare_without_target_asks_to_clarify_same_topic(engine):
    """Голое «Сравни» не должно уходить в retrieval: нужно удержать тему и попросить уточнение."""
    conv = "dialog-followup-compare-001"
    engine.chat("Расскажи о праве", conversation_id=conv)

    result = engine.chat("Сравни", conversation_id=conv)

    assert result["method"] == "dialog-context"
    text = result["response"].lower()
    assert "прав" in text
    assert "с чем" in text or "уточн" in text


def test_followup_compare_with_target_produces_comparison(engine):
    """«Сравни с ...» должно строить сравнение на базе текущей темы."""
    conv = "dialog-followup-compare-target-001"
    engine.chat("Расскажи о праве", conversation_id=conv)

    result = engine.chat("Сравни с моралью", conversation_id=conv)

    assert result["method"] == "dialog-context"
    text = result["response"].lower()
    assert "прав" in text
    assert "морал" in text


def test_followup_compare_uses_reference_fallback_for_second_topic(engine, monkeypatch: pytest.MonkeyPatch):
    """Если по второй теме нет локальной формулы, сравнение должно добирать краткую reference-справку."""
    def fake_fetch_reference_answer(query: str, *, timeout: float = 5.0) -> str | None:
        if "морал" in query.lower():
            return "Мораль — система внутренних нравственных норм, оценок и ориентиров поведения."
        return None

    monkeypatch.setattr("backend.service.ai_engine.fetch_reference_answer", fake_fetch_reference_answer)

    conv = "dialog-followup-compare-ref-001"
    engine.chat("Расскажи о праве", conversation_id=conv)
    result = engine.chat("Сравни с моралью", conversation_id=conv)

    assert result["method"] == "dialog-context"
    text = result["response"].lower()
    assert "прав" in text
    assert "морал" in text
    assert "не хватает чистого локального материала" not in text


def test_followup_chain_keeps_topic_for_three_steps(engine):
    """Цепочка из нескольких follow-up не должна терять исходную тему."""
    conv = "dialog-followup-chain-001"
    engine.chat("Объясни архитектуру Kolibri простым языком", conversation_id=conv)
    second = engine.chat("А проще", conversation_id=conv)
    third = engine.chat("По пунктам", conversation_id=conv)
    fourth = engine.chat("А если проще ещё", conversation_id=conv)

    for result in (second, third, fourth):
        assert result["method"] == "dialog-context"
        text = result["response"].lower()
        assert "архитект" in text or "kolibri" in text


def test_followup_confirm_mode_stays_on_same_topic(engine):
    """«Это точно?» должно подтверждать текущую тему, а не открывать новый retrieval."""
    conv = "dialog-followup-confirm-001"
    engine.chat("Что ты знаешь о медицине?", conversation_id=conv)

    result = engine.chat("Это точно?", conversation_id=conv)

    assert result["method"] == "dialog-context"
    text = result["response"].lower()
    assert "медиц" in text
    assert "основной смысл" in text or "по текущей теме" in text


def test_followup_chain_keeps_topic_for_five_steps(engine):
    """Пять follow-up подряд не должны терять исходную тему."""
    conv = "dialog-followup-chain-005"
    engine.chat("Расскажи о праве", conversation_id=conv)
    chain = [
        engine.chat("А проще", conversation_id=conv),
        engine.chat("По пунктам", conversation_id=conv),
        engine.chat("А пример?", conversation_id=conv),
        engine.chat("Это точно?", conversation_id=conv),
        engine.chat("Что ещё?", conversation_id=conv),
    ]

    for result in chain:
        assert result["method"] == "dialog-context"
        text = result["response"].lower()
        assert "прав" in text


def test_followup_more_after_example_prefers_new_aspect(engine, monkeypatch: pytest.MonkeyPatch):
    """После примера «Что ещё?» должно искать следующий аспект темы, а не ещё один пример."""
    conv = "dialog-followup-more-after-example-001"
    engine.chat("Расскажи о праве", conversation_id=conv)

    monkeypatch.setattr(engine, "_enable_c_inference", True)
    monkeypatch.setattr(
        engine,
        "_is_valid_c_formula_answer",
        lambda query, payload: bool(payload and payload.get("response")),
    )

    class DummyCInference:
        available = True

        @staticmethod
        def query(text: str, strategy: str = "formula") -> dict[str, object]:
            normalized = text.lower()
            if normalized == "пример из право":
                return {"response": "Пример из права: договор определяет обязанности сторон."}
            if normalized == "роль право":
                return {"response": "Право играет роль механизма согласования правил, ответственности и защиты интересов."}
            if normalized == "почему важно право":
                return {"response": "Право важно для защиты законных интересов и предсказуемости общественных отношений."}
            return {"response": ""}

    monkeypatch.setattr(engine, "c_inference", DummyCInference())
    monkeypatch.setattr(
        engine,
        "_get_recent_assistant_answer",
        lambda context_window, current_query=None: "Например: договор определяет обязанности сторон.",
    )
    monkeypatch.setattr(engine, "_topic_summary_for_followup", lambda topic: None)

    result = engine.chat("Что ещё?", conversation_id=conv)

    assert result["method"] == "dialog-context"
    text = result["response"].lower()
    assert "роль" in text or "защит" in text or "ответствен" in text
    assert "договор определяет обязанности сторон" not in text


def test_followup_weather_uses_previous_user_query_for_lookup(engine, monkeypatch: pytest.MonkeyPatch):
    """Follow-up к внешней теме должен разворачиваться в предыдущий вопрос пользователя."""
    conv = "weather-followup-anchor-001"
    client = engine._sanitize_client_id(f"conv:{conv}")
    scoped = engine._scoped_conversation_id(conv, client)
    conversation = engine.get_or_create_conversation(scoped, client_id=client)
    context_window = engine._get_or_create_context_window(conversation.id)

    conversation.add("user", "Какая погода в Лениногорске?")
    context_window.add_message("user", "Какая погода в Лениногорске?")
    conversation.add("assistant", "Погода в Лениногорске: ясно, +12 °C.")
    context_window.add_message("assistant", "Погода в Лениногорске: ясно, +12 °C.")

    captured: dict[str, str] = {}

    def fake_fetch_weather_answer(location_hint: str, *, timeout: float = 5.5, language: str = "ru") -> str | None:
        captured["location_hint"] = location_hint
        return "Сейчас в Лениногорск, Татарстан, Россия ясно: +12 °C, ощущается как +12 °C, ветер 2 км/ч."

    monkeypatch.setattr("backend.service.ai_engine.fetch_weather_answer", fake_fetch_weather_answer)

    result = engine.chat("А подробнее", conversation_id=conv)

    assert "лениногорск" in captured.get("location_hint", "").lower()
    assert result["method"] == "web-augment-weather"
    assert "лениногорск" in result["response"].lower()


def test_weather_followup_location_fragment_builds_new_weather_query(engine, monkeypatch: pytest.MonkeyPatch):
    """Короткий follow-up вроде «а в лениногорске?» должен разворачиваться в новый погодный вопрос."""
    calls: list[str] = []

    def fake_fetch_weather_answer(location_hint: str, *, timeout: float = 5.5, language: str = "ru") -> str | None:
        calls.append(location_hint)
        if "альметьев" in location_hint.lower():
            return "Сейчас в Альметьевск, Татарстан, Россия ясно: +2 °C."
        if "лениногор" in location_hint.lower():
            return "Сейчас в Лениногорск, Татарстан, Россия ясно: +1 °C."
        return None

    monkeypatch.setattr("backend.service.ai_engine.fetch_weather_answer", fake_fetch_weather_answer)

    conv = "weather-location-followup-001"
    first = engine.chat("Какая погода в Альметьевске?", conversation_id=conv)
    second = engine.chat("А в Лениногорске?", conversation_id=conv)

    assert first["method"] == "web-augment-weather"
    assert second["method"] == "web-augment-weather"
    assert "лениногорск" in second["response"].lower()
    assert any("лениногор" in item.lower() for item in calls)


def test_weather_location_hint_normalizes_common_city_case(engine):
    """Локация в форме «в Лениногорске» должна нормализоваться до пригодного weather-hint."""
    hint = engine._build_weather_location_hint("в Лениногорске какая погода?")
    assert "лениногор" in hint.lower()


def test_weather_is_not_handled_as_early_special_command(engine):
    """Погода больше не должна жить в ранней special-command ветке, а идти через canonical runtime."""
    result = engine._handle_special_commands(
        "Какая погода в Лениногорске?",
        "какая погода в лениногорске?",
        context_window=None,
    )
    assert result is None


def test_weather_queries_prefer_realtime_lookup(engine, monkeypatch: pytest.MonkeyPatch):
    """Погодные вопросы должны сначала идти в realtime weather lookup."""
    calls: list[str] = []

    def fake_fetch_weather_answer(location_hint: str, *, timeout: float = 5.5, language: str = "ru") -> str | None:
        calls.append(location_hint)
        return (
            "Сейчас в Лениногорск, Татарстан, Россия снег: -6.7 °C, "
            "ощущается как -11.5 °C, ветер 11.6 км/ч. "
            "Сегодня прогноз: снег, от -12.0 до -5.0 °C. Вероятность осадков до 20%."
        )

    monkeypatch.setattr("backend.service.ai_engine.fetch_weather_answer", fake_fetch_weather_answer)

    conv = "weather-realtime-001"
    first = engine.chat("Какая погода в Лениногорске?", conversation_id=conv)
    second = engine.chat("А подробнее", conversation_id=conv)

    assert first["method"] == "web-augment-weather"
    assert second["method"] == "web-augment-weather"
    assert any("лениногорск" in item.lower() for item in calls)


def test_weather_queries_return_honest_unavailable_when_realtime_sources_fail(engine, monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setattr("backend.service.ai_engine.external_network_available", lambda: True)
    monkeypatch.setattr("backend.service.ai_engine.fetch_weather_answer", lambda *a, **k: None)
    monkeypatch.setattr(engine, "_try_web_augment_answer", lambda *a, **k: None)

    result = engine.chat("Какая погода в Сургуте?", conversation_id="weather-unavailable-001")

    assert result["method"] == "weather-unavailable"
    assert "сургут" in result["response"].lower()
    assert "не буду подменять ответ локальной заглушкой" in result["response"].lower()


def test_weather_queries_fail_fast_when_external_network_is_down(engine, monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setattr("backend.service.ai_engine.external_network_available", lambda: False)

    result = engine.chat("Какая погода в Сургуте?", conversation_id="weather-network-down-001")

    assert result["method"] == "weather-unavailable"
    assert "сургут" in result["response"].lower()


def test_news_queries_prefer_realtime_digest(engine, monkeypatch: pytest.MonkeyPatch):
    """Новостной запрос должен возвращать realtime digest, а не локальный мусорный сниппет."""
    def fake_fetch_news_digest(query: str, *, timeout: float = 5.0, max_items: int = 3) -> str | None:
        return (
            f"Вот что нашёл по теме «{query}»: "
            "1. Первая новость — краткое описание. "
            "2. Вторая новость — ещё одно описание."
        )

    monkeypatch.setattr("backend.service.ai_engine.fetch_news_digest", fake_fetch_news_digest)

    result = engine.chat("какие новости в мире", conversation_id="news-realtime-001")

    assert result["method"] == "web-news"
    assert "Первая новость" in result["response"]


def test_time_queries_prefer_realtime_lookup(engine, monkeypatch: pytest.MonkeyPatch):
    """Запросы времени должны идти через realtime time lookup."""
    def fake_fetch_time_answer(query: str, *, timeout: float = 4.5, language: str = "ru") -> str | None:
        return "Сейчас в Париж, Иль-де-Франс, Франция 2026-03-06 22:15 (Europe/Paris)."

    monkeypatch.setattr("backend.service.ai_engine.fetch_time_answer", fake_fetch_time_answer)

    result = engine.chat("Сколько времени в Париже?", conversation_id="time-realtime-001")

    assert result["method"] == "web-time"
    assert "Europe/Paris" in result["response"]


def test_currency_queries_prefer_realtime_lookup(engine, monkeypatch: pytest.MonkeyPatch):
    """Запросы курса валют должны идти через realtime exchange lookup."""
    def fake_fetch_exchange_rate_answer(query: str, *, timeout: float = 5.0) -> str | None:
        return "По курсу на Fri, 06 Mar 2026 00:00:01 +0000, 1 USD ≈ 91.25 RUB. 100 USD ≈ 9125 RUB."

    monkeypatch.setattr("backend.service.ai_engine.fetch_exchange_rate_answer", fake_fetch_exchange_rate_answer)

    result = engine.chat("100 долларов в рублях", conversation_id="rate-realtime-001")

    assert result["method"] == "web-rate"
    assert "USD" in result["response"]
    assert "RUB" in result["response"]


def test_reference_queries_prefer_realtime_lookup(engine, monkeypatch: pytest.MonkeyPatch):
    """Базовые справочные вопросы должны идти через realtime reference lookup."""
    def fake_fetch_reference_answer(query: str, *, timeout: float = 6.0) -> str | None:
        return "Париж — столица и крупнейший город Франции."

    monkeypatch.setattr("backend.service.ai_engine.fetch_reference_answer", fake_fetch_reference_answer)

    result = engine.chat("какая столица Франции", conversation_id="reference-realtime-001")

    assert result["method"] == "web-reference"
    assert "Париж" in result["response"]


def test_build_contextual_query_includes_recent_thread_facts(engine):
    """Referential follow-up должен опираться не только на один anchor, но и на недавние факты треда."""
    from backend.service.context_window import ContextWindow

    cw = ContextWindow()
    cw.add_message("user", "В проекте используется порт 8001 для API.")
    cw.add_message("assistant", "Принял. Зафиксировал в контексте: В проекте используется порт 8001 для API.")
    cw.add_message("user", "Nginx проксирует запросы на backend.")
    cw.add_message("assistant", "По контексту текущего диалога: Nginx проксирует запросы на backend.")
    cw.add_message("user", "Redis сейчас отключён.")

    query = engine._build_contextual_query("А как это связано?", cw)

    assert query is not None
    assert "контекст треда" in query.lower()
    assert "порт 8001" in query.lower()
    assert "nginx" in query.lower() or "redis" in query.lower()


def test_cleanup_response_text_preserves_decimal_numbers(engine):
    """Очистка ответа не должна ломать десятичные числа пробелом после точки."""
    cleaned = engine._cleanup_response_text("Температура -6.9 °C.Сейчас снег.")
    assert "-6.9 °C" in cleaned
    assert "°C. Сейчас" in cleaned


def test_cleanup_response_text_preserves_domains(engine):
    """Очистка ответа не должна рвать домены вида ria.ru пробелом после точки."""
    cleaned = engine._cleanup_response_text("Источник ria.ru и bbc.com.А теперь подробнее.")
    assert "ria.ru" in cleaned
    assert "bbc.com" in cleaned
    assert ". А теперь" in cleaned


def test_document_learning_and_retell(engine):
    """Колибри должен запоминать длинный текст и пересказывать его своими словами."""
    conv = "doc-memory-test-001"
    chunk = (
        "Сказка Колобок: колобок ушел от деда и бабки, встретил зайца, волка, "
        "медведя, а потом лису. "
    )
    long_story = chunk * 20

    learned = engine.chat(f"Научи: {long_story}", conversation_id=conv)
    assert learned["method"] == "train-command"
    assert "Материал принят" in learned["response"] or "Обучение выполнено" in learned["response"]

    docs = engine.chat("Какие тексты ты знаешь?", conversation_id=conv)
    assert docs["method"] == "document-list"
    assert "Колобок" in docs["response"] or "колобок" in docs["response"]

    retell = engine.chat("Перескажи колобок своими словами", conversation_id=conv)
    assert retell["method"] == "retell-memory"
    assert "Пересказ" in retell["response"]
    assert "колобок" in retell["response"].lower()


def test_auto_learning_ack_on_long_text(engine):
    """Длинный текст без вопроса должен идти в автообучение с явным подтверждением."""
    conv = "auto-learning-ack-test-001"
    long_text = (
        "История про теремок. В поле стоял теремок, и по очереди в него приходили звери. "
        "Сначала мышка, потом лягушка, затем зайчик, лиса и волк. "
        "Каждый просился жить вместе, и теремок становился домом для друзей. "
        "Потом пришел медведь, теремок не выдержал и развалился. "
        "Но звери не поссорились и построили новый большой терем. "
    ) * 5

    result = engine.chat(long_text, conversation_id=conv)
    assert result["method"] == "auto-learning-ack"
    assert "добавил в обучение" in result["response"]


def test_math_eval_with_number_words(engine):
    """Числа словами должны вычисляться так же, как цифрами."""
    conv = "math-words-test-001"
    result = engine.chat("сколько будет один плюс один умножить на 128 делить на 32", conversation_id=conv)
    assert result["method"] == "math-eval"
    assert "= **5**" in result["response"] or " = **5.0**" in result["response"]


def test_math_eval_with_power_words(engine):
    """Проверка степени в словесной форме."""
    conv = "math-words-test-002"
    result = engine.chat("два в степени десять", conversation_id=conv)
    assert result["method"] == "math-eval"
    assert "1024" in result["response"]


def test_math_eval_with_explanation_tail(engine):
    """Математика не должна ломаться от хвостов вроде «и почему?»."""
    conv = "math-words-test-003"
    result = engine.chat("Сколько будет 128 * 30 и почему?", conversation_id=conv)
    assert result["method"] == "math-eval"
    assert "3840" in result["response"]


def test_math_eval_with_parentheses_words(engine):
    """Словесные скобки должны поддерживаться в natural-language математике."""
    conv = "math-words-test-004"
    result = engine.chat(
        "Посчитай открыть скобку два плюс три закрыть скобку умножить на четыре",
        conversation_id=conv,
    )
    assert result["method"] == "math-eval"
    assert "20" in result["response"]


def test_logic_solver_syllogism(engine):
    """Логический решатель должен корректно выводить заключение из посылок."""
    conv = "logic-syllogism-test-001"
    result = engine.chat("Все люди смертны. Владислав — человек. Какой вывод?", conversation_id=conv)
    assert result["method"] == "logic-solver"
    text = result["response"].lower()
    assert "владислав" in text
    assert "смерт" in text


def test_quality_case_rejects_placeholder(engine):
    """Quality benchmark не должен засчитывать шаблон 'мало данных' как правильный ответ."""
    case = {"expect_any": [r"париж"]}
    ok, reason = engine._quality_case_passed(
        "По теме в моей локальной базе пока мало проверенных данных. Добавьте материал.",
        case,
    )
    assert not ok
    assert reason == "placeholder"


def test_quality_control_cases_include_curated_russian_domains(engine):
    """Week 2 plan: quality benchmark должен видеть доменные русские кейсы."""
    ids = {str(case.get("id", "")) for case in engine._quality_control_cases()}
    assert {
        "definition_medicine",
        "definition_geography",
        "definition_philosophy",
        "definition_biology",
        "definition_physics",
        "definition_astronomy",
        "definition_anatomy",
        "definition_therapy",
        "definition_chemistry",
        "definition_history",
        "definition_economics",
        "definition_law",
        "explain_physics",
        "tell_chemistry",
        "tell_detailed_law",
        "knowledge_astronomy",
        "study_history",
        "study_chemistry",
        "occupation_law",
        "structure_law",
        "importance_law",
        "importance_math",
        "importance_medicine",
        "importance_physics",
        "importance_chemistry",
    }.issubset(ids)


def test_definition_focus_terms_canonicalize_inflected_russian_forms(engine):
    assert "физика" in engine._definition_focus_terms("объясни физику простыми словами")
    assert "химия" in engine._definition_focus_terms("расскажи о химии")
    assert "право" in engine._definition_focus_terms("расскажи подробно о праве")
    assert "право" in engine._definition_focus_terms("расскажи о праве")
    assert "право" in engine._definition_focus_terms("что ты знаешь о праве")
    assert "астрономия" in engine._definition_focus_terms("что ты знаешь об астрономии")
    assert "право" in engine._definition_focus_terms("как устроено право")
    assert "право" in engine._definition_focus_terms("почему важно право")
    assert "математика" in engine._definition_focus_terms("почему важна математика")
    assert "медицина" in engine._definition_focus_terms("зачем нужна медицина")
    assert not engine._is_greeting_intent("расскажи о праве")
    assert not engine._is_greeting_intent("расскажи подробно о праве")
    assert not engine._is_greeting_intent("что ты знаешь о праве")
    assert not engine._is_greeting_intent("как устроено право")
    assert not engine._is_greeting_intent("зачем нужна медицина")


def test_quality_case_can_reject_topic_substitution_with_forbidden_tokens(engine):
    """Domain eval не должен принимать подмену темы вроде географии -> математика."""
    case = {
        "expect_any": [r"географ"],
        "expect_not": [r"математик"],
    }

    ok, reason = engine._quality_case_passed(
        "География изучает Землю и пространственные связи на поверхности планеты.",
        case,
    )
    assert ok
    assert reason == "matched_any"

    ok, reason = engine._quality_case_passed(
        "География и математика связаны, но это точная формальная наука.",
        case,
    )
    assert not ok
    assert reason == "contains_forbidden:математик"


def test_quality_metrics_summary_has_percentiles_and_gates(engine):
    """Сводка бенчмарка должна считать latency/confidence и quality gates."""
    details = [
        {
            "id": "a",
            "category": "facts",
            "passed": True,
            "reason": "matched_any",
            "method": "dialog-context",
            "weight": 1.0,
            "confidence": 0.86,
            "latency_ms": 300.0,
        },
        {
            "id": "b",
            "category": "facts",
            "passed": False,
            "reason": "missing_any",
            "method": "dynamic-fallback",
            "weight": 1.0,
            "confidence": 0.41,
            "latency_ms": 900.0,
        },
        {
            "id": "c",
            "category": "memory",
            "passed": False,
            "reason": "placeholder",
            "method": "dynamic-fallback",
            "weight": 1.0,
            "confidence": 0.22,
            "latency_ms": 1200.0,
        },
    ]

    metrics = engine._quality_metrics(details, score=0.66)
    assert metrics["latency_p50_ms"] >= 300.0
    assert metrics["latency_p95_ms"] >= metrics["latency_p50_ms"]
    assert metrics["placeholder_rate"] > 0.0
    assert metrics["hallucination_proxy_rate"] > 0.0
    assert isinstance(metrics["categories"], list) and metrics["categories"]
    assert isinstance(metrics["methods"], list) and metrics["methods"]
    assert isinstance(metrics["gates"], dict)
    assert "overall_pass" in metrics["gates"]


def test_quality_chat_timeout_helper(engine, monkeypatch: pytest.MonkeyPatch):
    """Benchmark helper должен прерывать слишком долгий кейс по таймауту."""
    def _slow_chat(*args, **kwargs):  # noqa: ANN002, ANN003
        time.sleep(0.3)
        return {"response": "ok", "method": "dynamic-fallback", "confidence": 0.4}

    monkeypatch.setattr(engine, "chat", _slow_chat)
    t0 = time.perf_counter()
    payload, err = engine._quality_chat_with_timeout(
        message="slow",
        conversation_id="bench-timeout-helper-001",
        client_id="bench-timeout-helper-001",
        response_profile="fast",
        time_budget_ms=500,
        timeout_sec=0.05,
    )
    elapsed = time.perf_counter() - t0
    assert payload is None
    assert err == "benchmark_case_timeout"
    assert elapsed < 0.2


def test_dynamic_fallback_can_skip_web_augment(engine, monkeypatch: pytest.MonkeyPatch):
    """При allow_web_augment=False динамический fallback не должен ходить в web-путь."""
    web_calls = {"count": 0}

    def _fake_web(*args, **kwargs):  # noqa: ANN002, ANN003
        web_calls["count"] += 1
        return "WEB ANSWER"

    monkeypatch.setattr(engine, "_try_web_augment_answer", _fake_web)
    monkeypatch.setattr(engine, "_try_web_news_digest", lambda *a, **k: "WEB NEWS")
    monkeypatch.setattr(engine, "_is_weather_query", lambda *a, **k: True)
    monkeypatch.setattr(engine, "_is_project_runtime_query", lambda *a, **k: False)
    monkeypatch.setattr(engine, "_extract_topic_focus", lambda *a, **k: "погода лениногорск")
    monkeypatch.setattr(engine, "_lm_trained", False)

    text, method = engine._build_dynamic_no_knowledge_response(
        message="Какая погода в Лениногорске?",
        retrieved_sentences=[],
        graph_answer="",
        context_window=None,
        deadline_ts=time.time() + 1.0,
        fast_mode=True,
        allow_web_augment=False,
    )

    assert method == "dynamic-fallback"
    assert "погода лениногорск" in (text or "").lower()
    assert web_calls["count"] == 0


def test_math_eval_skips_persist_for_ephemeral_client(engine, monkeypatch: pytest.MonkeyPatch):
    """Служебные benchmark/proof клиенты не должны триггерить persist в math-eval."""
    called = {"assoc": 0, "persist": 0}

    def _assoc(*args, **kwargs):  # noqa: ANN002, ANN003
        called["assoc"] += 1

    def _persist(*args, **kwargs):  # noqa: ANN002, ANN003
        called["persist"] += 1

    monkeypatch.setattr(engine.formula_pool, "add_association", _assoc)
    monkeypatch.setattr(engine, "_persist_state_throttled", _persist)

    token = engine._active_client_id_var.set("quality-bench:test-client")
    try:
        result = engine._try_math_eval("сколько будет 2+2")
    finally:
        engine._active_client_id_var.reset(token)

    assert isinstance(result, dict)
    assert result["method"] == "math-eval"
    assert called["assoc"] == 0
    assert called["persist"] == 0


def test_persona_changes_response_surface(engine):
    """persona из runtime-настроек должна менять подачу ответа."""
    result = engine.chat(
        "Что такое контекстное окно?",
        conversation_id="persona-style-001",
        persona="storyteller",
    )
    assert result["response"].startswith("Представлю это как короткий рассказ.")


def test_ephemeral_prefix_is_treated_as_non_persistent_client(engine):
    """Frontend memory-off должен маппиться на ephemeral-клиента."""
    assert engine._is_ephemeral_client("ephemeral:test-client:chat-1")


def test_uniqueness_proof_suite(engine):
    """Proof-suite должен подтверждать набор уникальных свойств движка."""
    report = engine.run_uniqueness_proof("pytest")
    assert isinstance(report, dict)
    assert int(report.get("total", 0)) >= 8
    assert float(report.get("score", 0.0)) >= 0.875
    assert isinstance(report.get("fingerprint", ""), str) and len(str(report.get("fingerprint", ""))) >= 12

    details = {str(item.get("id", "")): item for item in report.get("details", [])}
    for case_id in (
        "numeric_word_encoding_64digits",
        "formula_architecture",
        "chat_formula_payload",
        "self_check_present",
        "memory_isolation_cross_client",
        "scoped_conversation_ids",
    ):
        assert case_id in details
        assert bool(details[case_id].get("passed", False))


def test_association_relevance_filter(engine):
    """Шумные ассоциации без тематического совпадения должны отбрасываться."""
    assert engine._association_is_relevant(
        "Напиши пример цикла for на Python",
        "```python\\nfor i in range(5):\\n    print(i)\\n```",
    )
    assert not engine._association_is_relevant(
        "Столица Франции и столица Японии?",
        "Его третий сольный сингл Fiesta стал популярным в радиочартах.",
    )


def test_load_from_db_restores_edges() -> None:
    """При старте должны восстанавливаться и паттерны, и рёбра из SQLite."""
    from backend.service.ai_engine import KolibriAIEngine
    from backend.service.number_mind import KnowledgeGraph

    class _FakeDB:
        def is_enabled(self) -> bool:
            return True

        def load_patterns(self) -> list[dict]:
            return [
                {"hash": 11, "word": "alpha", "pattern": [1] * 64, "fitness": 0.2, "frequency": 2},
                {"hash": 22, "word": "beta", "pattern": [2] * 64, "fitness": 0.3, "frequency": 4},
            ]

        def load_edges(self) -> list[dict]:
            return [
                {"source_hash": 11, "target_hash": 22, "weight": 0.75, "cooccurrence": 9},
            ]

        def get_meta(self, key: str, default: str = "") -> str:
            if key == "documents_trained":
                return "7"
            return default

    engine = KolibriAIEngine.__new__(KolibriAIEngine)
    engine.graph = KnowledgeGraph()
    engine._db = _FakeDB()
    engine._load_from_db()

    assert len(engine.graph.patterns) == 2
    assert len(engine.graph.edges) == 1
    edge = next(iter(engine.graph.edges.values()))
    assert edge.weight == pytest.approx(0.75)
    assert edge.cooccurrence == 9
    assert engine.graph.documents_trained == 7


def test_russian_fallback_ignores_noisy_smoke_text() -> None:
    """RU fallback не должен склеивать служебный шум из smoke-проверок."""
    from backend.service.ai_engine import KolibriAIEngine

    engine = KolibriAIEngine.__new__(KolibriAIEngine)
    retrieved = [
        ("Проверка стрим-ответа точных хватает уверенного предложением источники уточните", 0.92),
        ("Интеграл простыми словами — это способ посчитать накопленный итог процесса.", 0.51),
        ("Интеграл помогает находить площадь под графиком и общий результат изменений.", 0.48),
    ]

    response = engine._build_russian_fallback(
        "Что такое интеграл простыми словами?",
        retrieved,
    )
    assert "Проверка стрим-ответа" not in response
    assert "Интеграл" in response


def test_normalize_qa_response_removes_question_only_noise() -> None:
    """Если retrieval вернул только 'Вопрос ... Связанные понятия', шум должен удаляться."""
    from backend.service.ai_engine import KolibriAIEngine

    engine = KolibriAIEngine.__new__(KolibriAIEngine)
    noisy = "Вопрос: Что такое интеграл простыми словами? Связанные понятия: какими, навещает."
    cleaned = engine._normalize_qa_response(noisy)
    assert "Вопрос:" not in cleaned
    assert "Связанные понятия" not in cleaned
    assert cleaned.strip() != noisy.strip()
