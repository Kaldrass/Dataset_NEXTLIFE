"""Generate a separate dataset release from a final review, preserving old assets."""
import argparse
import concurrent.futures
import csv
import hashlib
import json
import os
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from build_recognition_experiment import ROOT
from object_review import retained_object_ids


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as handle:
        json.dump(value, handle, ensure_ascii=False, indent=2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("review", type=Path)
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--release", type=Path)
    parser.add_argument("--resume", action="store_true", help="Resume an existing release, preserving incomplete attempts")
    args = parser.parse_args()
    ids = sorted(retained_object_ids(args.review))
    if not ids:
        parser.error("Empty retained selection")
    release = (args.release or ROOT / "Objects/Releases" / datetime.now(timezone.utc).strftime("clean_%Y%m%dT%H%M%SZ")).resolve()
    release.relative_to(ROOT / "Objects/Releases")
    if args.resume:
        if not args.release:
            parser.error("--resume requires --release")
        if json.loads(args.review.read_text(encoding="utf-8-sig")) != json.loads((release / "review.json").read_text(encoding="utf-8-sig")):
            raise ValueError("Review differs from the frozen release")
        config = json.loads((release / "generation_config.json").read_text())
        source_hashes = json.loads((release / "source_hashes.json").read_text())
        for rel, expected in source_hashes.items():
            path = (release / rel).resolve()
            path.relative_to(release)
            with path.open("rb") as handle:
                if hashlib.file_digest(handle, "sha256").hexdigest() != expected:
                    raise ValueError(f"Frozen source changed: {rel}")
    else:
        release.mkdir(parents=True, exist_ok=False)
        (release / "review.json").write_bytes(args.review.read_bytes())
        config = json.loads((ROOT / "DistortionConfig/unified_generation_config.json").read_text())
        config["execution"].update(dry_run=False, overwrite_existing=False, max_objects=None, max_recipes=None, object_whitelist=ids)
        write_json(release / "generation_config.json", config)
        source_hashes = {}
        for oid in ids:
            source = ROOT / "Objects/Originals" / oid
            target = release / "Originals" / oid
            if not source.is_dir():
                raise FileNotFoundError(source)
            shutil.copytree(source, target, ignore=shutil.ignore_patterns("masks", "*.blend", "*.blend1", "*.bak"))
            for path in target.rglob("*"):
                if path.is_file():
                    with path.open("rb") as handle:
                        source_hashes[path.relative_to(release).as_posix()] = hashlib.file_digest(handle, "sha256").hexdigest()
        write_json(release / "source_hashes.json", source_hashes)
    print(f"RELEASE {release}", flush=True)
    results = []
    pending = []
    archive = release / ("previous_attempt_" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ"))
    for oid in ids:
        logs = list((release / "reports" / oid).glob("*.generation_log.csv"))
        rows = []
        if len(logs) == 1:
            with logs[0].open(encoding="utf-8-sig", newline="") as handle:
                rows = list(csv.DictReader(handle))
        if (len(rows) == 28 and len({r["variant_id"] for r in rows}) == 28
                and all(r["status"] == "ok" and Path(r["manifest_path"]).is_file() for r in rows)):
            results.append({"object_id": oid, "exit_code": 0, "rows": 28, "errors": []})
            continue
        pending.append(oid)
        # Preserve every partial output before retrying; never let the pipeline
        # remove an unfinished output directory through its normal rebuild path.
        for rel in [Path("reports") / oid, Path("work") / oid] + [
                Path("Distorted") / kind / oid for kind in ("MeshVariants", "TextureVariants", "CombinedVariants")]:
            source = (release / rel).resolve()
            source.relative_to(release)
            if source.exists():
                target = (archive / rel).resolve()
                target.relative_to(release)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.move(str(source), str(target))
    if (release / "generation_results.json").exists():
        archive.mkdir(parents=True, exist_ok=True)
        shutil.move(str(release / "generation_results.json"), str(archive / "generation_results.json"))
    print(f"COMPLETE {len(results)}; PENDING {len(pending)}", flush=True)
    def generate(oid):
        report_dir = release / "reports" / oid
        report_dir.mkdir(parents=True)
        object_config = json.loads(json.dumps(config))
        object_config["execution"]["object_whitelist"] = [oid]
        cfg = report_dir / "config.json"
        write_json(cfg, object_config)
        env = {**os.environ, "PYTHONUNBUFFERED": "1", "NEXTLIFE_ORIGINALS_DIR": str(release / "Originals"),
               "NEXTLIFE_DISTORTED_DIR": str(release / "Distorted"), "NEXTLIFE_WORK_DIR": str(release / "work" / oid),
               "NEXTLIFE_REPORTS_DIR": str(report_dir)}
        with (report_dir / "generation.log").open("x", encoding="utf-8") as output:
            result = subprocess.run([sys.executable, str(ROOT / "Scripts/Scripts/generate_distorted_variants.py"), "--config", str(cfg)],
                                    cwd=ROOT, env=env, stdout=output, stderr=subprocess.STDOUT)
        logs = list(report_dir.glob("*.generation_log.csv"))
        rows = []
        if logs:
            with logs[0].open(encoding="utf-8-sig", newline="") as handle:
                rows = list(csv.DictReader(handle))
        errors = [row for row in rows if row["status"] != "ok"]
        print(f"OBJECT {oid} OK={len(rows)-len(errors)} ERRORS={len(errors)} exit={result.returncode}", flush=True)
        return {"object_id": oid, "exit_code": result.returncode, "rows": len(rows), "errors": errors}
    # Validate one complete object before launching the remaining operators.
    if pending:
        results.append(generate(pending.pop(0)))
    if results[-1]["exit_code"] or results[-1]["errors"] or results[-1]["rows"] != 28:
        write_json(release / "generation_results.json", results)
        raise RuntimeError("Pilot failed; inspect release reports before continuing")
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,args.workers)) as pool:
        results.extend(pool.map(generate, pending))
    write_json(release / "generation_results.json", results)
    failures = [r for r in results if r["exit_code"] or r["errors"] or r["rows"] != 28]
    if failures:
        raise RuntimeError(f"{len(failures)} objects incomplete; release not activated")
    print("GENERATION COMPLETE: release ready for catalog validation", flush=True)


if __name__ == "__main__":
    main()
