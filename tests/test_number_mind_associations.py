from __future__ import annotations

from backend.service.number_mind import Formula


def test_formula_lookup_prefers_latest_association():
    f = Formula()
    q = "Как начать день продуктивно?"
    f.add_association(q, "Старый ответ")
    f.add_association(q, "Новый ответ")

    assert f.lookup(q) == "Новый ответ"
