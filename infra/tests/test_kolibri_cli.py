#!/usr/bin/env python3
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path


def run_cli(binary: Path, *args: str, stdin: str | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(binary), *args],
        input=stdin,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def assert_ok(result: subprocess.CompletedProcess[str]) -> None:
    if result.returncode != 0:
        raise AssertionError(
            f"command failed with {result.returncode}\nSTDOUT:\n{result.stdout}\nSTDERR:\n{result.stderr}"
        )


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_kolibri_cli.py <path-to-kolibri-cli>", file=sys.stderr)
        return 2

    binary = Path(sys.argv[1]).resolve()
    if not binary.exists():
        print(f"missing binary: {binary}", file=sys.stderr)
        return 2

    help_result = run_cli(binary, "--help")
    assert_ok(help_result)
    assert "Usage:" in help_result.stdout
    assert "--ask" in help_result.stdout

    text_result = run_cli(binary, "--no-color", "--ask", "README.md")
    assert_ok(text_result)
    assert "README.md" in text_result.stdout
    assert "\x1b[" not in text_result.stdout

    json_result = run_cli(binary, "--json", "--ask", "README.md")
    assert_ok(json_result)
    payload = json.loads(json_result.stdout)
    assert payload["query"] == "README.md"
    assert "README.md" in payload["response"]
    assert payload["digit_count"] > 0
    assert payload["knowledge_entries"] > 0
    assert "[KOLIBRI]" not in json_result.stdout

    stdin_result = run_cli(binary, "--json", stdin="README.md\n")
    assert_ok(stdin_result)
    stdin_payload = json.loads(stdin_result.stdout)
    assert stdin_payload["query"] == "README.md"

    digit_encode = run_cli(binary, "digit", "encode", "Kolibri")
    assert_ok(digit_encode)
    assert digit_encode.stdout.strip() == "075111108105098114105"

    digit_encode_json = run_cli(binary, "--json", "digit", "encode", "Kolibri")
    assert_ok(digit_encode_json)
    digit_payload = json.loads(digit_encode_json.stdout)
    assert digit_payload["command"] == "digit.encode"
    assert digit_payload["input_bytes"] == 7
    assert digit_payload["digit_count"] == 21
    assert digit_payload["canonical"] is True
    assert digit_payload["digits"] == "075111108105098114105"

    digit_decode_json = run_cli(binary, "digit", "decode", "--json", "075111108105098114105")
    assert_ok(digit_decode_json)
    decoded_payload = json.loads(digit_decode_json.stdout)
    assert decoded_payload["command"] == "digit.decode"
    assert decoded_payload["byte_count"] == 7
    assert decoded_payload["bytes_hex"] == "4b6f6c69627269"
    assert decoded_payload["text"] == "Kolibri"

    formula_repeat = run_cli(binary, "--json", "formula", "inspect", "repeat", "456", "4")
    assert_ok(formula_repeat)
    formula_payload = json.loads(formula_repeat.stdout)
    assert formula_payload["command"] == "formula.inspect"
    assert formula_payload["type"] == "repeat"
    assert formula_payload["digits"] == "456456456456"
    assert formula_payload["verified"] is True

    gene = "01345600000040000000000000000000"
    formula_meta = run_cli(binary, "--json", "formula", "inspect", "meta", gene)
    assert_ok(formula_meta)
    meta_payload = json.loads(formula_meta.stdout)
    assert meta_payload["command"] == "formula.inspect"
    assert meta_payload["type"] == "meta"
    assert meta_payload["digits"] == "456456456456"
    assert meta_payload["verified"] is True
    assert meta_payload["gene_length"] == 32

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
