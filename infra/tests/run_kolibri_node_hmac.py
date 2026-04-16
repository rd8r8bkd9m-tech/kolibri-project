#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


def run_with_mode(binary: str, mode: str) -> None:
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_path = Path(tmp_dir)
        genome_path = tmp_path / "genome.dat"

        if mode == "inline":
            key_argument = "custom-inline-secret"
        elif mode == "file":
            key_path = tmp_path / "hmac.key"
            key_path.write_text("file-secret-key\n", encoding="utf-8")
            key_argument = f"@{key_path}"
        else:
            raise SystemExit(f"неизвестный режим: {mode}")

        command = [
            binary,
            "--node-id",
            "9",
            "--genome",
            str(genome_path),
            "--verify-genome",
            "--hmac-key",
            key_argument,
        ]

        process = subprocess.run(
            command,
            input=":verify\n:quit\n",
            text=True,
            capture_output=True,
        )

        if process.returncode != 0:
            sys.stdout.write(process.stdout)
            sys.stderr.write(process.stderr)
            raise SystemExit(process.returncode or 1)

        stdout = process.stdout
        if mode == "inline":
            if "аргумент" not in stdout:
                raise SystemExit("ожидалось упоминание аргумента ключа")
        else:
            if "файл" not in stdout:
                raise SystemExit("ожидалось упоминание файла ключа")


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("использование: run_kolibri_node_hmac.py <binary> <mode>")
    run_with_mode(sys.argv[1], sys.argv[2])


if __name__ == "__main__":
    main()
