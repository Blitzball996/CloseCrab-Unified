#!/usr/bin/env python3
"""CloseCrab <-> Strata bridge: detect hardware and list runnable models.

Called by StrataManager.cpp (src/strata/StrataManager.cpp) as a subprocess:
    python scripts/strata_probe.py [--strata-root PATH]

Prints a single JSON object to stdout and exits 0 on success, 1 on error.
stderr is free for diagnostic text; CloseCrab only reads stdout.

Output schema (schema_version: 1):
{
  "schema_version": 1,
  "ok": true,
  "hardware": {
    "gpu_name": "RTX 3060",
    "vram_gb": 12.0,
    "ram_gb": 32.0,
    "disk_free_gb": 480.0,
    "summary": "RTX 3060 12 GB · 32 GB RAM · 480 GB free"
  },
  "recommended": {
    "family": "qwen",
    "model": "IQ3_XXS",
    "id": "strata-qwen-iq3_xxs",
    "about": "Fast, fits 12 GB GPU",
    "download_gb": 47.0
  },
  "models": [
    {
      "family": "qwen",
      "family_title": "Qwen3.8-Flash-Next",
      "model": "IQ3_XXS",
      "id": "strata-qwen-iq3_xxs",
      "about": "...",
      "download_gb": 47.0,
      "need_vram_gb": 11.5,
      "need_ram_gb": 0.0,
      "spill_gb": 0.0,
      "verdict": "fits",      // "fits" | "tight" | "no"
      "words": "fits",
      "recommended": true,
      "installed": false,
      "experimental": false
    },
    ...
  ]
}
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

def _strata_root(override: str | None) -> Path:
    """Resolve the Strata source tree.
    1. --strata-root CLI arg
    2. STRATA_ROOT env var
    3. <this script's dir>/../external/strata
    """
    if override:
        return Path(override).resolve()
    env = os.environ.get("STRATA_ROOT")
    if env:
        return Path(env).resolve()
    here = Path(__file__).resolve().parent
    candidate = here.parent / "external" / "strata"
    return candidate


def _fail(msg: str, code: int = 1) -> None:
    print(json.dumps({"schema_version": 1, "ok": False, "error": msg}), flush=True)
    sys.exit(code)


def main() -> None:
    ap = argparse.ArgumentParser(description="CloseCrab Strata hardware probe")
    ap.add_argument("--strata-root", help="path to vendored Strata source tree")
    ap.add_argument("--json-indent", type=int, default=None,
                    help="indent for human-readable output (default: compact)")
    args = ap.parse_args()

    root = _strata_root(args.strata_root)
    if not (root / "setup.py").exists():
        _fail(f"Strata source not found at {root} — expected setup.py there. "
              f"Set --strata-root or STRATA_ROOT, or run cmake -DUSE_STRATA=ON first.")

    # Add Strata source tree to sys.path so its modules import cleanly.
    strata_str = str(root)
    if strata_str not in sys.path:
        sys.path.insert(0, strata_str)

    try:
        from strata_cli.launch.catalog import Catalog, FITS, TIGHT
    except ImportError as e:
        _fail(f"Could not import Strata catalog: {e}. "
              f"Is the source tree at {root} complete?")

    try:
        cat = Catalog(root)
    except Exception as e:
        _fail(f"Catalog init failed: {e}")

    # ---- hardware
    try:
        hw = cat.hardware()
    except Exception as e:
        _fail(f"Hardware detection failed: {e}")

    comp = cat.computer()
    gpu_list = [g for g in (hw.get("gpus") or []) if g.get("usable", True)]
    primary = gpu_list[0] if gpu_list else {}
    hardware_out = {
        "gpu_name":     primary.get("name", ""),
        "vram_gb":      float(primary.get("vram_gb") or 0),
        "gpu_count":    len(gpu_list),
        "ram_gb":       float(hw.get("ram_gb") or 0),
        "disk_free_gb": float(hw.get("disk_free_gb") or 0),
        "summary":      comp.summary if hasattr(comp, "summary") else "",
        "backend":      hw.get("backend") or "",
        "os":           str(hw.get("os") or ""),
    }

    # ---- model list
    try:
        rows = cat.rows(hw)
    except Exception as e:
        _fail(f"Model table failed: {e}")

    models_out = []
    for r in rows:
        models_out.append({
            "family":       r.family,
            "family_title": r.family_title,
            "model":        r.model,
            "id":           r.id,
            "about":        r.about,
            "download_gb":  r.download_gb,
            "need_vram_gb": r.need_vram_gb,
            "need_ram_gb":  r.need_ram_gb,
            "spill_gb":     r.spill_gb,
            "verdict":      r.verdict,      # "fits" | "tight" | "no"
            "words":        r.words,
            "recommended":  r.recommended,
            "installed":    r.installed,
            "experimental": r.experimental,
        })

    # ---- recommended
    rec_row = next((m for m in models_out if m["recommended"]), None)
    if not rec_row:
        # fallback: first model that fits
        rec_row = next((m for m in models_out if m["verdict"] == FITS), None)
    if not rec_row and models_out:
        rec_row = models_out[0]

    recommended_out = {k: rec_row[k] for k in
                       ("family", "model", "id", "about", "download_gb")} if rec_row else None

    result = {
        "schema_version": 1,
        "ok":             True,
        "hardware":       hardware_out,
        "recommended":    recommended_out,
        "models":         models_out,
    }
    print(json.dumps(result, ensure_ascii=False, indent=args.json_indent), flush=True)


if __name__ == "__main__":
    main()
