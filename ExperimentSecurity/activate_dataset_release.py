"""Validate a generated release and activate catalogs with backups."""
import argparse
import csv
import hashlib
import json
import os
import shutil
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("release", type=Path)
    args = parser.parse_args()
    release = args.release.resolve()
    release.relative_to(ROOT / "Objects/Releases")
    results = json.loads((release / "generation_results.json").read_text())
    review = json.loads((release / "review.json").read_text(encoding="utf-8-sig"))
    retained = {r["object_id"] for r in review["objects"] if r["status"] == "retained"}
    assert {r["object_id"] for r in results} == retained
    assert all(r["exit_code"] == 0 and not r["errors"] and r["rows"] == 28 for r in results)
    hashes = json.loads((release / "source_hashes.json").read_text())
    for rel, expected in hashes.items():
        with (release / rel).open("rb") as handle:
            assert hashlib.file_digest(handle, "sha256").hexdigest() == expected, rel
    os.environ["NEXTLIFE_ORIGINALS_DIR"] = str(release / "Originals")
    os.environ["NEXTLIFE_DISTORTED_DIR"] = str(release / "Distorted")
    from build_dataset_catalog import build_catalog, active_profile_catalog
    from build_recognition_experiment import make_trials, SECURITY_LEVELS
    sys.path.insert(0, str(ROOT / "Scripts/Scripts"))
    from distortion_pipeline.assets import parse_mtl_texture_entries
    catalog = build_catalog()
    assert {o["object_id"] for o in catalog["objects"]} == retained
    per_object = Counter(v["object_id"] for v in catalog["variants"] if not v["is_original"])
    assert all(per_object[oid] == 28 for oid in retained), dict(per_object)
    expected_profiles = active_profile_catalog()[1]
    for oid in retained:
        actual_profiles = {(v["distortion_family"], v["distortion_profile"]) for v in catalog["variants"]
                           if v["object_id"] == oid and not v["is_original"]}
        assert actual_profiles == expected_profiles, (oid, actual_profiles ^ expected_profiles)
    # Current geometry counts replace pre-cleanup metadata only in generated outputs.
    faces = {}
    for variant in catalog["variants"]:
        model = variant["model"]
        for key in ("obj", "mtl"):
            if model.get(key):
                path = (ROOT / model[key]).resolve()
                path.relative_to(release)
                assert path.is_file(), path
        if model.get("mtl"):
            for entry in parse_mtl_texture_entries(ROOT / model["mtl"]):
                # The shared viewer resolves maps inside the selected texture asset.
                texture = ROOT / model["texture_folder"] / entry["source_path"].name
                assert texture.is_file(), (variant["trial_id"], str(texture))
        if variant["is_original"]:
            with (ROOT / model["obj"]).open(encoding="utf-8", errors="replace") as handle:
                faces[variant["object_id"]] = sum(line.startswith("f ") for line in handle)
    for item in catalog["objects"] + catalog["variants"]:
        item["face_count"] = faces[item["object_id"]]
    catalog["source"] = release.relative_to(ROOT).as_posix()
    trials = make_trials(0, 0, 20260622, 0, 6, retained)
    assert Counter(t["object_id"] for t in trials) == per_object
    for trial in trials:
        trial["face_count"] = faces[trial["object_id"]]
    payload = {"experiment": "NEXTLIFE_recognition_security", "release": catalog["source"],
               "protocol": {"task_1": "Choose the object label among forced choices.",
                            "task_2": "Choose one visual security level.", "security_levels": SECURITY_LEVELS,
                            "viewer": "single freely rotatable and zoomable distorted object"},
               "filters": {"max_objects": 0, "max_trials": 0, "max_faces": 0, "choices": 6, "seed": 20260622,
                           "object_review": str(release / "review.json"), "retained_object_ids": sorted(retained)}, "trials": trials}
    activation = release / ("activation_" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ"))
    activation.mkdir()
    pointer = {"originals": (release / "Originals").relative_to(ROOT).as_posix(),
               "distorted": (release / "Distorted").relative_to(ROOT).as_posix(),
               "review": (release / "review.json").relative_to(ROOT).as_posix()}
    for name, value in [("dataset_catalog.json", catalog), ("recognition_trials.json", payload), ("dataset_release.json", pointer)]:
        target = ROOT / "ExperimentSecurity" / name
        if target.exists():
            shutil.copy2(target, activation / ("previous_" + name))
        prepared = activation / name
        prepared.write_text(json.dumps(value, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
        shutil.copy2(prepared, target)
    print(json.dumps({"counts": catalog["counts"], "trials": len(trials), "backups": str(activation)}, indent=2))


if __name__ == "__main__":
    main()
