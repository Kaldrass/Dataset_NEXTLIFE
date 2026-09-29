"""Resolve a validated active release; explicit environment overrides take priority."""
import json
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def dataset_info(distorted):
    """Selection and stable identity travel with the active distortion folder."""
    path = distorted / "dataset_info.json"
    return json.loads(path.read_text(encoding="utf-8")) if path.is_file() else {}


def dataset_identity(distorted):
    info = dataset_info(distorted)
    if info.get("release"):
        return info["release"]
    return distorted.parent.relative_to(ROOT).as_posix() if distorted.parent.parent == ROOT / "Objects/Releases" else ""


def dataset_selection(distorted):
    ids = dataset_info(distorted).get("retained_object_ids")
    return set(ids) if ids is not None else None


def dataset_paths():
    pointer = ROOT / "ExperimentSecurity/dataset_release.json"
    active = json.loads(pointer.read_text(encoding="utf-8")) if pointer.exists() else {}
    originals = Path(os.environ.get("NEXTLIFE_ORIGINALS_DIR", ROOT / active.get("originals", "Objects/Originals"))).resolve()
    distorted = Path(os.environ.get("NEXTLIFE_DISTORTED_DIR", ROOT / active.get("distorted", "Objects/Distorted"))).resolve()
    originals.relative_to(ROOT)
    distorted.relative_to(ROOT)
    return originals, distorted
