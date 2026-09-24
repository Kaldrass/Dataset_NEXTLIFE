"""Read-only OBJ audit. Reports are hints for visual review, never exclusions."""
import argparse
import csv
import json
import hashlib
import math
import shlex
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

from build_recognition_experiment import ROOT, metadata_for, normalize_label


def audit_obj(obj: Path, max_faces: int) -> dict:
    issues, materials = [], []
    vertices = faces = invalid = 0
    max_index = 0
    low, high = [math.inf] * 3, [-math.inf] * 3
    try:
        with obj.open(encoding="utf-8-sig", errors="replace") as handle:
            for line in handle:
                parts = line.split("#", 1)[0].split()
                if not parts:
                    continue
                try:
                    if parts[0] == "v":
                        vertices += 1
                        xyz = [float(x) for x in parts[1:4]]
                        if len(xyz) != 3 or not all(math.isfinite(x) for x in xyz):
                            invalid += 1
                            continue
                        low = [min(a, b) for a, b in zip(low, xyz)]
                        high = [max(a, b) for a, b in zip(high, xyz)]
                    elif parts[0] == "f":
                        faces += 1
                        indices = [int(x.split("/")[0]) for x in parts[1:]]
                        if len(indices) < 3 or any(x == 0 or x < -vertices for x in indices):
                            invalid += 1
                        max_index = max([max_index] + indices)
                    elif parts[0] == "mtllib":
                        value = line.strip()[len("mtllib"):].strip().replace("\\", "/")
                        # Prefer a single filename, which can contain spaces.
                        materials.extend([value.strip('"')] if (obj.parent / value.strip('"')).is_file()
                                         else shlex.split(value))
                except ValueError:
                    invalid += 1
    except OSError as error:
        issues.append(f"Lecture impossible : {error}")
    if not vertices or not faces:
        issues.append("Géométrie vide ou sans faces")
    if invalid or max_index > vertices:
        issues.append("Géométrie invalide : coordonnées ou indices de faces")
    if vertices and low == high:
        issues.append("Étendue nulle")
    if max_faces and faces > max_faces:
        issues.append(f"À tester : {faces} faces dépassent le seuil {max_faces}")
    if not materials:
        issues.append("Aucun mtllib déclaré : vérifier les matériaux visuellement")
    for name in dict.fromkeys(materials):
        mtl = obj.parent / name
        if not mtl.is_file():
            issues.append(f"Matériau absent : {name}")
            continue
        try:
            for line in mtl.read_text(encoding="utf-8-sig", errors="replace").splitlines():
                parts = line.strip().split(maxsplit=1)
                if len(parts) != 2 or not (parts[0].lower().startswith("map_") or parts[0].lower() in {"bump", "disp", "decal", "norm"}):
                    continue
                value = parts[1].split("#", 1)[0].strip().replace("\\", "/")
                if value.startswith("-"):
                    issues.append(f"Texture avec options MTL à vérifier : {value}")
                elif not (mtl.parent / value.strip('"')).is_file():
                    issues.append(f"Texture absente : {value}")
        except OSError as error:
            issues.append(f"Matériau illisible : {error}")
    return {"vertices": vertices, "faces": faces, "issues": list(dict.fromkeys(issues))}


def build_audit(root: Path = ROOT, max_faces: int = 150000, hash_geometry: bool = False) -> dict:
    metadata = json.loads((root / "metadata.json").read_text(encoding="utf-8"))
    rows = []
    for folder in sorted((root / "Objects" / "Originals").iterdir()):
        if not folder.is_dir():
            continue
        meta = metadata_for(metadata, folder.name)
        candidates = sorted(folder.glob("*.obj"))
        obj = folder / f"{folder.name}.obj"
        obj = obj if obj.is_file() else next(iter(candidates), None)
        result = audit_obj(obj, max_faces) if obj else {"vertices": 0, "faces": 0, "issues": ["Modèle OBJ absent"]}
        if len(candidates) > 1:
            result["issues"].append("Plusieurs OBJ : vérifier le modèle choisi")
        label = normalize_label(str(meta.get("imagenet_class", "")))
        if not label:
            result["issues"].append("Classe absente")
        digest = ""
        if obj and hash_geometry:
            try:
                with obj.open("rb") as handle:
                    digest = hashlib.file_digest(handle, "sha256").hexdigest()
            except OSError as error:
                result["issues"].append(f"Empreinte impossible : {error}")
        rows.append({"object_id": folder.name, "imagenet_class": label, "geometry_sha256": digest,
                     "obj": obj.relative_to(root).as_posix() if obj else None, **result})
    hashes = {}
    for row in rows:
        if row["geometry_sha256"]:
            hashes.setdefault(row["geometry_sha256"], []).append(row["object_id"])
    for row in rows:
        duplicates = hashes.get(row["geometry_sha256"], [])
        if len(duplicates) > 1:
            row["issues"].append("OBJ identique octet par octet (matériaux à comparer) : " + ", ".join(oid for oid in duplicates if oid != row["object_id"]))
    return {"schema_version": 1, "generated_at": datetime.now(timezone.utc).isoformat(),
            "max_faces": max_faces, "source": "Objects/Originals (Old excluded)",
            "counts": {"objects": len(rows), "with_alerts": sum(bool(r["issues"]) for r in rows),
                       "classes": dict(Counter(r["imagenet_class"] for r in rows))}, "objects": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--max-faces", type=int, default=150000)
    parser.add_argument("--hash-geometry", action="store_true", help="Signaler les OBJ identiques par SHA-256, sans supprimer de fichiers.")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "ExperimentSecurity" / "results")
    args = parser.parse_args()
    report = build_audit(max_faces=args.max_faces, hash_geometry=args.hash_geometry)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    # Exclusive creation preserves any previous audit.
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    target = args.out_dir / f"object_audit_{stamp}.json"
    with target.open("x", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
    with target.with_suffix(".csv").open("x", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["object_id", "imagenet_class", "obj", "vertices", "faces", "geometry_sha256", "issues"])
        writer.writeheader()
        writer.writerows({**row, "issues": " | ".join(row["issues"])} for row in report["objects"])
    print(json.dumps(report["counts"], ensure_ascii=False))
    print(target)


if __name__ == "__main__":
    main()
