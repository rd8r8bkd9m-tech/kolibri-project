#!/usr/bin/env python3
"""Validate that CTest does not reference missing build artifacts.

This turns the Phase 0 "honest test inventory" rule into an executable check:

- every test command that points into the build directory must exist;
- every build-local executable used as the test command must be executable;
- every build-local path passed as a test argument must exist.

The script reads the generated ``CTestTestfile.cmake`` after CMake configure/build.
It is intended to run after ``cmake --build`` and before ``ctest``.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


ADD_TEST_RE = re.compile(r'^add_test\((?P<name>\S+)\s+(?P<body>.*)\)$')
QUOTED_RE = re.compile(r'"([^"]*)"')


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        default="build",
        help="Path to the CMake build directory containing CTestTestfile.cmake",
    )
    return parser.parse_args()


def build_local_paths(tokens: list[str], build_dir: Path) -> list[Path]:
    paths: list[Path] = []
    for token in tokens:
        candidates = [token]
        if token.startswith("-D") and "=" in token:
            _, rhs = token.split("=", 1)
            candidates.append(rhs)
        for candidate in candidates:
            try:
                path = Path(candidate)
            except OSError:
                continue
            if not path.is_absolute():
                continue
            try:
                path.relative_to(build_dir)
            except ValueError:
                continue
            paths.append(path)
    return paths


def main() -> int:
    args = parse_args()
    build_dir = Path(args.build_dir).resolve()
    ctest_file = build_dir / "CTestTestfile.cmake"
    if not ctest_file.exists():
        print(f"[ctest-inventory] missing {ctest_file}", file=sys.stderr)
        return 2

    missing: list[str] = []
    nonexec: list[str] = []
    total_tests = 0

    for raw_line in ctest_file.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        match = ADD_TEST_RE.match(line)
        if not match:
            continue

        total_tests += 1
        test_name = match.group("name")
        tokens = QUOTED_RE.findall(match.group("body"))
        if not tokens:
            missing.append(f"{test_name}: unable to parse add_test command")
            continue

        command = Path(tokens[0])
        for path in build_local_paths(tokens, build_dir):
            if not path.exists():
                missing.append(f"{test_name}: missing build artifact {path}")
                continue
            if path == command and path.is_file() and not path.stat().st_mode & 0o111:
                nonexec.append(f"{test_name}: command is not executable {path}")

    if missing or nonexec:
        print(
            f"[ctest-inventory] failed: {len(missing)} missing artifacts, "
            f"{len(nonexec)} non-executable commands across {total_tests} tests",
            file=sys.stderr,
        )
        for line in missing:
            print(f"  - {line}", file=sys.stderr)
        for line in nonexec:
            print(f"  - {line}", file=sys.stderr)
        return 1

    print(f"[ctest-inventory] ok: {total_tests} tests reference existing build artifacts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
