"""Shared paths and helpers for the Python tests (run via scripts/test.sh)."""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "hairclip"
WEB = ROOT / "web"
BUILD = ROOT / "build" / "host"

sys.path.insert(0, str(ROOT / "tools"))


def compile_cpp(output: Path, sources: list[Path]) -> Path:
    """Builds a host C++ tool for the tests."""
    output.parent.mkdir(parents=True, exist_ok=True)
    cxx = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
    if cxx is None:
        raise RuntimeError("no C++ compiler found")
    subprocess.run([cxx, "-std=c++17", "-O1", "-I", str(FIRMWARE), "-o", str(output), *map(str, sources)],
                   check=True)
    return output
