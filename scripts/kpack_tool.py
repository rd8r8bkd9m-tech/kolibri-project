#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from backend.service.kpack import export_kpack, import_kpack, inspect_kpack


def _print(data: dict) -> None:
    print(json.dumps(data, ensure_ascii=False, indent=2))


def main() -> int:
    parser = argparse.ArgumentParser(description="Kolibri .kpack tool")
    sub = parser.add_subparsers(dest="cmd", required=True)

    export_parser = sub.add_parser("export", help="Export knowledge as .kpack")
    export_parser.add_argument("--source-root", default="", help="Knowledge root directory")
    export_parser.add_argument("--output", default="", help="Output .kpack path")
    export_parser.add_argument("--id", required=True, help="Package id")
    export_parser.add_argument("--title", required=True, help="Package title")
    export_parser.add_argument("--language", default="ru")
    export_parser.add_argument("--domain", action="append", default=[], help="Restrict to domain")
    export_parser.add_argument("--description", default="")
    export_parser.add_argument("--default-query", default="")

    import_parser = sub.add_parser("import", help="Import .kpack into live memory")
    import_parser.add_argument("--pack", required=True, help="Path to .kpack")
    import_parser.add_argument("--target-root", default="", help="Target live memory root")

    inspect_parser = sub.add_parser("inspect", help="Inspect .kpack manifest")
    inspect_parser.add_argument("--pack", required=True, help="Path to .kpack")

    args = parser.parse_args()
    if args.cmd == "export":
        _print(
            export_kpack(
                source_root=args.source_root or None,
                output_path=args.output or None,
                package_id=args.id,
                title=args.title,
                language=args.language,
                domains=list(args.domain or []),
                description=args.description,
                default_query=args.default_query,
            )
        )
        return 0
    if args.cmd == "import":
        _print(import_kpack(pack_path=args.pack, target_root=args.target_root or None))
        return 0
    if args.cmd == "inspect":
        _print(inspect_kpack(args.pack))
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())

