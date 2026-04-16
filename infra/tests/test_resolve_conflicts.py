import logging
import sys
from pathlib import Path
from typing import List, Optional

import pytest

ROOT = Path(__file__).resolve().parents[1]
if ROOT.name == "tests":
    ROOT = ROOT.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from scripts.resolve_conflicts import (  # noqa: E402
    KONFLIKT_DELIM,
    KONFLIKT_END,
    KONFLIKT_START,
    postroit_otchet,
)


@pytest.fixture
def agents_content() -> str:
    return (
        """```kolibri-policy
build: ours
code: ours
docs: ours

files:
  prefer_ours:
    - scripts/**
  prefer_theirs:
    - docs/**

budgets:
  wasm_max_kb: 1
  step_latency_ms: 1
  coverage_min_lines: 1
  coverage_min_branches: 1
```"""
    )


def zapisat_conflict(
    path: Path,
    ours: List[str],
    theirs: List[str],
    *,
    ours_final_newline: bool = True,
    theirs_final_newline: bool = True,
    tail: Optional[str] = "epilogue\n",
) -> None:
    lines = ["prelude\n", f"{KONFLIKT_START} ours\n"]
    for index, stroka in enumerate(ours):
        konec = "\n" if index < len(ours) - 1 or ours_final_newline else ""
        lines.append(f"{stroka}{konec}")
    lines.append(f"{KONFLIKT_DELIM}\n")
    for index, stroka in enumerate(theirs):
        konec = "\n" if index < len(theirs) - 1 or theirs_final_newline else ""
        lines.append(f"{stroka}{konec}")
    konec_marker = "\n" if theirs_final_newline else ""
    lines.append(f"{KONFLIKT_END} theirs{konec_marker}")
    if tail is not None:
        lines.append(tail)
    path.write_text("".join(lines), encoding="utf-8")


def test_prefers_ours_strategy(
    tmp_path: Path, caplog: pytest.LogCaptureFixture, agents_content: str
) -> None:
    (tmp_path / "AGENTS.md").write_text(agents_content, encoding="utf-8")
    conflict = tmp_path / "scripts" / "example.txt"
    conflict.parent.mkdir(parents=True)
    zapisat_conflict(conflict, ["ours-line"], ["theirs-line"])

    caplog.set_level(logging.INFO, logger="resolve_conflicts")
    report = postroit_otchet(tmp_path)

    assert conflict.read_text(encoding="utf-8") == "prelude\nours-line\nepilogue\n"
    entry = next(item for item in report["files"] if item["file"] == str(conflict))
    assert entry["status"] == "resolved"
    assert entry["strategy"] == "ours"
    assert any("selected ours" in record.getMessage() for record in caplog.records)


def test_fallback_to_both(tmp_path: Path, agents_content: str) -> None:
    (tmp_path / "AGENTS.md").write_text(agents_content, encoding="utf-8")
    conflict = tmp_path / "notes.txt"
    zapisat_conflict(conflict, ["ours"], ["theirs"], theirs_final_newline=False, tail=None)

    report = postroit_otchet(tmp_path)

    result = conflict.read_text(encoding="utf-8")
    assert result == "prelude\nours\ntheirs"
    entry = next(item for item in report["files"] if item["file"] == str(conflict))
    assert entry["strategy"] == "both"


def test_prefers_theirs_multiple_conflicts(tmp_path: Path, agents_content: str) -> None:
    (tmp_path / "AGENTS.md").write_text(agents_content, encoding="utf-8")
    target = tmp_path / "docs" / "guide.md"
    target.parent.mkdir(parents=True)
    text = [
        "intro\n",
        f"{KONFLIKT_START} block1\n",
        "ours-one\n",
        f"{KONFLIKT_DELIM}\n",
        "theirs-one\n",
        f"{KONFLIKT_END} block1\n",
        "middle\n",
        f"{KONFLIKT_START} block2\n",
        "ours-two\n",
        f"{KONFLIKT_DELIM}\n",
        "theirs-two",
        f"{KONFLIKT_END} block2",
    ]
    target.write_text("".join(text), encoding="utf-8")

    report = postroit_otchet(tmp_path)

    expected = "intro\ntheirs-one\nmiddle\ntheirs-two"
    assert target.read_text(encoding="utf-8") == expected
    entry = next(item for item in report["files"] if item["file"] == str(target))
    assert entry["strategy"] == "theirs"
