#!/usr/bin/env python3
"""Generate embedded Godot class documentation for the BluetoothGD extension."""

import sys
from pathlib import Path

GODOT_CPP_DIR = Path(__file__).resolve().parents[1] / "godot-cpp"
sys.path.insert(0, str(GODOT_CPP_DIR))

from doc_source_generator import generate_doc_source  # noqa: E402


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: generate_extension_docs.py <output.cpp> <doc.xml> [more.xml ...]", file=sys.stderr)
        return 1

    output_path = sys.argv[1]
    xml_sources = sys.argv[2:]
    generate_doc_source(output_path, xml_sources)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())