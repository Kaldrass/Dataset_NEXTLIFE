import argparse
import json
import random
from pathlib import Path
from object_review import retained_object_ids
from dataset_paths import dataset_paths


ROOT = Path(__file__).resolve().parents[1]
ORIGINALS_DIR, DISTORTED_DIR = dataset_paths()
OUT_DIR = ROOT / "ExperimentSecurity"
SECURITY_LEVELS = ["Original", "Transparent", "Suffisant", "Confidentiel"]
CAT_LABEL = "Cat"
CAT_IMAGENET_CLASSES = {
    "tabby",
    "tiger cat",
    "persian cat",
    "siamese cat",
    "egyptian cat",
    "cat",
}


def load_json(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def rel(path: Path) -> str:
    return path.resolve().relative_to(ROOT).as_posix()


def object_key(object_id: str) -> str:
    return object_id.removesuffix("_1024")


def metadata_for(metadata: dict, object_id: str) -> dict:
    return metadata.get(object_key(object_id), {})


def resolve_dataset_path(path_text: str | None) -> Path | None:
    if not path_text:
        return None
    path = ROOT / Path(path_text.replace("\\", "/"))
    if path.exists():
        return path

    marker = "Objects/Distorted/MeshVariants/"
    normalized = path_text.replace("\\", "/")
    if normalized.startswith(marker) and not normalized.startswith(marker + "MeshVariants/"):
        nested = ROOT / Path((marker + "MeshVariants/" + normalized[len(marker) :]).replace("/", "\\"))
        if nested.exists():
            return nested
    return path


def find_original_model(object_id: str) -> dict | None:
    folder = ORIGINALS_DIR / object_id
    obj = folder / f"{object_id}.obj"
    if not obj.exists():
        candidates = sorted(folder.glob("*.obj"))
        obj = candidates[0] if candidates else obj
    if not obj.exists():
        return None
    mtl = folder / f"{object_id}.mtl"
    if not mtl.exists():
        mtls = sorted(folder.glob("*.mtl"))
        mtl = mtls[0] if mtls else mtl
    return {
        "kind": "original",
        "folder": rel(folder),
        "obj": rel(obj),
        "mtl": rel(mtl) if mtl.exists() else None,
        "texture_folder": rel(folder),
        "variant_id": "original",
        "manifest": "",
    }


def asset_folder(asset: dict) -> Path | None:
    return resolve_dataset_path(asset.get("folder"))


def combined_model_spec(manifest: dict, manifest_path: Path) -> dict | None:
    mesh_asset = manifest.get("mesh_asset", {})
    texture_asset = manifest.get("texture_asset", {})
    mesh_folder = asset_folder(mesh_asset)
    if not mesh_folder:
        return None

    mesh_files = mesh_asset.get("files", {})
    obj = mesh_folder / (mesh_files.get("obj") or "model.obj")
    mtl = mesh_folder / (mesh_files.get("mtl") or "model.mtl")
    if not obj.exists():
        return None

    texture_folder = asset_folder(texture_asset)
    texture_files = texture_asset.get("files", {}).get("textures", [])
    if texture_folder and texture_files:
        first_texture_parent = Path(texture_files[0]).parent
        if str(first_texture_parent) not in ("", "."):
            texture_folder = texture_folder / first_texture_parent

    return {
        "kind": "combined_variant",
        "folder": rel(mesh_folder),
        "obj": rel(obj),
        "mtl": rel(mtl) if mtl.exists() else None,
        "texture_folder": rel(texture_folder) if texture_folder and texture_folder.exists() else None,
        "variant_id": str(manifest.get("variant_id") or manifest_path.parent.name),
        "manifest": rel(manifest_path),
    }


def group_label(active_groups: list[dict]) -> tuple[str, str, str, dict]:
    if not active_groups:
        return "unknown", "unknown", "unknown", {}
    group = active_groups[0]
    return (
        str(group.get("modality", "unknown")),
        str(group.get("group_id", "unknown")),
        str(group.get("profile_id", "unknown")),
        dict(group.get("params", {})),
    )


def normalize_label(label: str) -> str:
    clean = str(label or "").strip()
    if clean.lower() in CAT_IMAGENET_CLASSES:
        return CAT_LABEL
    return clean


def label_pool(metadata: dict) -> list[str]:
    labels = sorted(
        {
            normalize_label(item.get("imagenet_class", ""))
            for item in metadata.values()
            if normalize_label(item.get("imagenet_class", ""))
        }
    )
    return labels


def choices_for(correct_label: str, labels: list[str], rng: random.Random, choice_count: int) -> list[str]:
    distractors = [label for label in labels if label != correct_label]
    rng.shuffle(distractors)
    choices = [correct_label] + distractors[: max(0, choice_count - 1)]
    rng.shuffle(choices)
    return choices


def make_trials(max_objects: int, max_trials: int, seed: int, max_faces: int, choices: int, retained_ids: set[str] | None = None) -> list[dict]:
    metadata = load_json(ROOT / "metadata.json")
    labels = label_pool(metadata)
    rng = random.Random(seed)
    manifests = sorted((DISTORTED_DIR / "CombinedVariants").glob("*/*/manifest.json"))
    rng.shuffle(manifests)

    selected_objects = set()
    trials = []
    for manifest_path in manifests:
        try:
            manifest = load_json(manifest_path)
        except Exception:
            continue

        object_id = str(manifest.get("object_id") or manifest_path.parent.parent.name)
        if retained_ids is not None and object_id not in retained_ids:
            continue
        meta = metadata_for(metadata, object_id)
        raw_imagenet_class = str(meta.get("imagenet_class", "")).strip()
        correct_label = normalize_label(raw_imagenet_class)
        if not correct_label:
            continue

        face_count = int(meta.get("faceCount") or 0)
        if max_faces and face_count and face_count > max_faces:
            continue
        if max_objects and object_id not in selected_objects and len(selected_objects) >= max_objects:
            continue

        original = find_original_model(object_id)
        distorted = combined_model_spec(manifest, manifest_path)
        if not original or not distorted:
            continue

        selected_objects.add(object_id)
        modality, family, profile, params = group_label(manifest.get("active_groups", []))
        trial_rng = random.Random(f"{seed}-{object_id}-{distorted['variant_id']}")
        trials.append(
            {
                "trial_id": f"{object_id}__{distorted['variant_id']}",
                "object_id": object_id,
                "object_name": meta.get("name", object_id),
                "imagenet_class": correct_label,
                "raw_imagenet_class": raw_imagenet_class,
                "label_choices": choices_for(correct_label, labels, trial_rng, choices),
                "security_levels": SECURITY_LEVELS,
                "scene_id": meta.get("default_scene_id", ""),
                "face_count": meta.get("faceCount", ""),
                "thumbnail_url": meta.get("thumbnail_url", ""),
                "distortion_modality": modality,
                "distortion_family": family,
                "distortion_profile": profile,
                "distortion_params": params,
                "distorted": distorted,
                "original": original,
                "distortion_manifest": rel(manifest_path),
            }
        )
        if max_trials and len(trials) >= max_trials:
            break

    rng.shuffle(trials)
    return trials


def main() -> None:
    parser = argparse.ArgumentParser(description="Build the NEXTLIFE recognition/security experiment.")
    parser.add_argument("--max-objects", type=int, default=25)
    parser.add_argument("--max-trials", type=int, default=200)
    parser.add_argument("--max-faces", type=int, default=150000)
    parser.add_argument("--choices", type=int, default=6)
    parser.add_argument("--seed", type=int, default=20260622)
    parser.add_argument("--output", default=str(OUT_DIR / "recognition_trials.json"))
    parser.add_argument("--object-review", type=Path, help="Export JSON du tri : seuls les objets retained sont inclus.")
    args = parser.parse_args()

    try:
        retained_ids = retained_object_ids(args.object_review) if args.object_review else None
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if retained_ids is not None and not retained_ids:
        parser.error("Aucun objet retenu dans le fichier de tri ; aucun trial écrit.")
    trials = make_trials(args.max_objects, args.max_trials, args.seed, args.max_faces, args.choices, retained_ids)
    if retained_ids is not None and not trials:
        parser.error("Aucun trial exploitable pour les objets retenus ; sortie existante préservée.")
    payload = {
        "experiment": "NEXTLIFE_recognition_security",
        "release": rel(DISTORTED_DIR.parent) if DISTORTED_DIR.parent.parent == ROOT / "Objects/Releases" else "",
        "protocol": {
            "task_1": "Choose the object label among forced choices.",
            "task_2": "Choose one visual security level.",
            "security_levels": SECURITY_LEVELS,
            "viewer": "single freely rotatable and zoomable distorted object",
        },
        "filters": {
            "object_review": str(args.object_review) if args.object_review else None,
            "retained_object_ids": sorted(retained_ids) if retained_ids is not None else None,
            "max_objects": args.max_objects,
            "max_trials": args.max_trials,
            "max_faces": args.max_faces,
            "choices": args.choices,
            "seed": args.seed,
        },
        "trials": trials,
    }
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)
    print(f"Wrote {len(trials)} trials to {output}")


if __name__ == "__main__":
    main()
