#!/usr/bin/env python3
"""Rebrand external/moe (ex-strata) C++ sources for CloseCrab integration.

Replacements (case-sensitive, in order):
  namespace strata     -> namespace closecrab::moe
  strata::             -> closecrab::moe::
  STRATA_              -> CC_MOE_
  STRATA_VERSION       -> CC_MOE_VERSION
  "strata "            -> "moe " (in string literals for --version)
  strata_artifact      -> moe_engine
  strata_warnings      -> moe_warnings
  strata_kernels       -> moe_kernels
  #include "strata/    -> #include "moe/

File renames under include/ and src/:
  include/strata/ -> include/moe/
  any *strata*.hpp / *strata*.cpp -> s/strata/moe/

Only touches include/ and src/ — bench/, apps/, docs/, tools/ left alone.
"""
from __future__ import annotations
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "external" / "moe"

CONTENT_REPLACEMENTS = [
    # namespace declarations  (order matters: most specific first)
    (r'\bnamespace strata\b',         'namespace closecrab_moe'),
    # using/qualified names
    (r'\bstrata::',                   'closecrab_moe::'),
    # include paths  (before macro replacements so "strata/" isn't eaten by STRATA_)
    (r'#include\s+"strata/',          '#include "moe/'),
    # macros
    (r'\bSTRATA_VERSION\b',           'CC_MOE_VERSION'),
    (r'\bSTRATA_',                    'CC_MOE_'),
    # CMake target names in CMakeLists (handled in content pass too)
    (r'\bstrata_artifact\b',          'moe_engine'),
    (r'\bstrata_warnings\b',          'moe_warnings'),
    (r'\bstrata_kernels_cpu\b',       'moe_kernels_cpu'),
    (r'\bstrata_kernels\b',           'moe_kernels'),
    # version print string
    (r'"strata %s\\n"',               '"closecrab-moe %s\\n"'),
    (r'"strata\\n"',                  '"closecrab-moe\\n"'),
]

COMPILED = [(re.compile(p), r) for p, r in CONTENT_REPLACEMENTS]

DIRS = [ROOT / "include", ROOT / "src"]
EXTS = {".hpp", ".cpp", ".h", ".c", ".cuh", ".cu"}

def rebrand_content(text: str) -> str:
    for pat, repl in COMPILED:
        text = pat.sub(repl, text)
    return text

def process_file(path: Path) -> bool:
    try:
        original = path.read_text(encoding="utf-8", errors="replace")
    except Exception as e:
        print(f"  SKIP (read error): {path}: {e}")
        return False
    rebranded = rebrand_content(original)
    if rebranded == original:
        return False
    path.write_text(rebranded, encoding="utf-8")
    return True

def rename_files(base: Path) -> int:
    """Rename files/dirs that have 'strata' in their name under base."""
    count = 0
    # Process deepest first so parent dirs don't get renamed before children
    targets = sorted(
        [p for p in base.rglob("*") if "strata" in p.name],
        key=lambda p: -len(p.parts)
    )
    for p in targets:
        new_name = p.name.replace("strata", "moe")
        new_path = p.parent / new_name
        try:
            p.rename(new_path)
            count += 1
        except Exception as e:
            print(f"  RENAME FAIL: {p} -> {new_path}: {e}")
    return count

def main():
    # 1. Rename include/strata/ directory to include/moe/
    old_inc = ROOT / "include" / "strata"
    new_inc = ROOT / "include" / "moe"
    if old_inc.exists() and not new_inc.exists():
        old_inc.rename(new_inc)
        print(f"Renamed include/strata -> include/moe")

    # 2. Content rebrand all .hpp/.cpp/.h/.cu under include/ and src/
    changed = 0
    total = 0
    for d in DIRS:
        if not d.exists():
            continue
        for path in d.rglob("*"):
            if path.suffix in EXTS and path.is_file():
                total += 1
                if process_file(path):
                    changed += 1
    print(f"Content rebrand: {changed}/{total} files modified")

    # 3. Rename files with 'strata' in name under src/
    renamed = rename_files(ROOT / "src")
    print(f"File renames under src/: {renamed}")
    renamed2 = rename_files(ROOT / "include")
    print(f"File renames under include/moe/: {renamed2}")

    # 4. Also rebrand CMakeLists.txt (target names)
    cmake = ROOT / "CMakeLists.txt"
    if cmake.exists():
        orig = cmake.read_text(encoding="utf-8")
        new  = rebrand_content(orig)
        if new != orig:
            cmake.write_text(new, encoding="utf-8")
            print("Rebranded CMakeLists.txt")

    print("Done.")

if __name__ == "__main__":
    main()
