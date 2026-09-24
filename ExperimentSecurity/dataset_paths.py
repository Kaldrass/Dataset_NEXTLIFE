"""Resolve a validated active release; explicit environment overrides take priority."""
import json
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def dataset_paths():
    pointer = ROOT / "ExperimentSecurity/dataset_release.json"
    active = json.loads(pointer.read_text(encoding="utf-8")) if pointer.exists() else {}
    originals = Path(os.environ.get("NEXTLIFE_ORIGINALS_DIR", ROOT / active.get("originals", "Objects/Originals"))).resolve()
    distorted = Path(os.environ.get("NEXTLIFE_DISTORTED_DIR", ROOT / active.get("distorted", "Objects/Distorted"))).resolve()
    originals.relative_to(ROOT)
    distorted.relative_to(ROOT)
    return originals, distorted
