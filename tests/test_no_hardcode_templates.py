from pathlib import Path


def test_no_removed_hardcode_templates_in_ai_engine() -> None:
    engine = Path("backend/service/ai_engine.py").read_text(encoding="utf-8")
    banned_markers = [
        "_try_structured_local_answer",
        "_build_comparison_response",
        "_build_study_plan_response",
        'pair == {"http", "https"}',
        '"method": "structured-local"',
    ]
    for marker in banned_markers:
        assert marker not in engine, f"hardcoded marker must not be present: {marker}"
