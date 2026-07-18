#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later

"""Verify that private implementation names are absent from the shared ABI."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_symbols.py library", file=sys.stderr)
        return 2

    library = Path(sys.argv[1])
    nm = subprocess.run(
        ["nm", "-D", "--defined-only", str(library)],
        check=True,
        capture_output=True,
        text=True,
    )
    demangled = subprocess.run(
        ["c++filt"],
        input=nm.stdout,
        check=True,
        capture_output=True,
        text=True,
    ).stdout

    forbidden = (
        "pkgreconcile::detail::",
        "pkgreconcile::candidate::fingerprint",
        "pkgreconcile::candidate::candidate(std::filesystem",
    )
    failures = [name for name in forbidden if name in demangled]
    if failures:
        for name in failures:
            print(f"private ABI name exported: {name}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
