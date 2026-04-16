"""Тесты для цифрового генома KolibriScript (.ksd)."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from core.kolibri_script.genome import (  # noqa: E402
    KsdBlock,
    KsdValidationError,
    KolibriGenomeLedger,
    deserialize_ksd,
    load_secrets_config,
    serialize_ksd,
)
from core.kolibri_sim import KolibriSim  # noqa: E402


def _write_secrets(tmp_path: Path) -> Path:
    config = {"kolibri": {"script": {"hmac_key": "kolibri-test-key"}}}
    secrets_path = tmp_path / "secrets.json"
    secrets_path.write_text(json.dumps(config, ensure_ascii=False), encoding="utf-8")
    return secrets_path


def _sample_records() -> list[dict[str, object]]:
    return [
        {
            "tip": "TEACH",
            "soobshenie": "a->b",
            "metka": 123.456,
            "block": {
                "nomer": 1,
                "pred_hash": "111",
                "payload": "001002",
                "hmac_summa": "333",
                "itogovy_hash": "444",
            },
        }
    ]


def test_ksd_round_trip(tmp_path: Path) -> None:
    secrets = load_secrets_config(_write_secrets(tmp_path))
    records = _sample_records()
    payload = serialize_ksd(records, secrets)
    document = deserialize_ksd(payload, secrets)
    assert document.records == records
    assert "TEACH" in document.tokens
    assert "a->b" in document.tokens
    assert len(document.blocks) == 1
    assert isinstance(document.blocks[0], KsdBlock)


def test_ksd_serialization_is_deterministic(tmp_path: Path) -> None:
    secrets = load_secrets_config(_write_secrets(tmp_path))
    records = _sample_records()
    first = serialize_ksd(records, secrets)
    second = serialize_ksd(records, secrets)
    assert first == second


def test_ksd_detects_tampering(tmp_path: Path) -> None:
    secrets = load_secrets_config(_write_secrets(tmp_path))
    payload = serialize_ksd(_sample_records(), secrets)
    flipped_digit = "0" if payload[-1] != "0" else "1"
    tampered = payload[:-1] + flipped_digit
    with pytest.raises(KsdValidationError):
        deserialize_ksd(tampered, secrets)


def test_genome_writer_integration(tmp_path: Path) -> None:
    secrets = load_secrets_config(_write_secrets(tmp_path))
    genome_path = tmp_path / "genome.dat"
    sim = KolibriSim(zerno=7, genome_path=genome_path, secrets_config=secrets)
    sim.obuchit_svjaz("x", "y")
    sim.sprosit("x")
    assert genome_path.exists()
    data = genome_path.read_text(encoding="utf-8")
    document = deserialize_ksd(data, secrets)
    tips = [record["tip"] for record in document.records]
    assert "GENESIS" in tips
    assert "TEACH" in tips
    assert "ASK" in tips
    assert not (tmp_path / ".genome.dat.tmp").exists()


def test_genome_ledger_streaming_journal(tmp_path: Path) -> None:
    secrets = load_secrets_config(_write_secrets(tmp_path))
    ledger_path = tmp_path / "genome.dat"
    ledger = KolibriGenomeLedger(ledger_path, secrets)
    try:
        assert not ledger.has_records()
        ledger.append({"nomer": 1}, {"tip": "ONE", "metka": 1.0})
        ledger.append({"nomer": 2}, {"tip": "TWO", "metka": 2.0})
        assert ledger.has_records()
        streamed = list(ledger.records())
        assert [record["tip"] for record in streamed] == ["ONE", "TWO"]
        data = ledger_path.read_text(encoding="utf-8")
        document = deserialize_ksd(data, secrets)
        assert document.blocks[0].offset == 0
        assert document.blocks[1].offset > document.blocks[0].offset
        assert [record["tip"] for record in document.records] == ["ONE", "TWO"]
        assert len(document.blocks) == 2
    finally:
        ledger.close()
