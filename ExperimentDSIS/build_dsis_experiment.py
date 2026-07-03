import argparse
import json
import random
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "ExperimentDSIS"

REFERENCE_PRIORITIES = [
    "ms_l1",
    "mqp_p11",
    "mqt_t10",
    "mqp_p9",
    "mqt_t8",
    "mdh_l5",
    "menc_l17",
]

TEXTURE_REFERENCE_PRIORITIES = [
    "tj_q100",
    "tr_r1",
    "blur_l1",
    "bloc_obsc_l1",
    "aes_l1",
]


def rel(path: Path) -> str:
    return path.resolve().relative_to(ROOT).as_posix()


def load_json(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def find_mesh_root() -> Path:
    base = ROOT / "Objects" / "Distorted" / "MeshVariants"
    nested = base / "MeshVariants"
    return nested if nested.exists() else base


def read_metadata() -> dict:
    path = ROOT / "metadata.json"
    return load_json(path) if path.exists() else {}


def object_key(object_id: str) -> str:
    return object_id.removesuffix("_1024")


def metadata_for(metadata: dict, object_id: str) -> dict:
    return metadata.get(object_key(object_id), {})


def group_label(active_groups: list[dict]) -> tuple[str, str, dict]:
    if not active_groups:
        return "unknown", "unknown", {}
    group = active_groups[0]
    return (
        str(group.get("group_id", "unknown")),
        str(group.get("profile_id", "unknown")),
        dict(group.get("params", {})),
    )


def load_variants(root: Path) -> dict[str, list[dict]]:
    by_object: dict[str, list[dict]] = {}
    if not root.exists():
        return by_object

    for manifest_path in root.glob("*/*/manifest.json"):
        try:
            manifest = load_json(manifest_path)
        except Exception:
            continue
        object_id = str(manifest.get("object_id") or manifest.get("parent_object_id") or manifest_path.parent.parent.name)
        by_object.setdefault(object_id, []).append(
            {
                "manifest": manifest,
                "manifest_path": manifest_path,
                "folder": manifest_path.parent,
                "variant_id": manifest_path.parent.name,
            }
        )
    return by_object


def choose_reference(mesh_variants: list[dict]) -> dict | None:
    if not mesh_variants:
        return None

    def rank(item: dict) -> tuple[int, str]:
        variant_id = item["variant_id"]
        for idx, prefix in enumerate(REFERENCE_PRIORITIES):
            if variant_id.startswith(prefix):
                return idx, variant_id
        return len(REFERENCE_PRIORITIES), variant_id

    return sorted(mesh_variants, key=rank)[0]


def choose_reference_texture(texture_variants: list[dict]) -> dict | None:
    if not texture_variants:
        return None

    def rank(item: dict) -> tuple[int, str]:
        variant_id = item["variant_id"]
        for idx, prefix in enumerate(TEXTURE_REFERENCE_PRIORITIES):
            if variant_id.startswith(prefix):
                return idx, variant_id
        return len(TEXTURE_REFERENCE_PRIORITIES), variant_id

    return sorted(texture_variants, key=rank)[0]


def texture_folder_from_variant(item: dict | None) -> Path | None:
    if not item:
        return None
    texture_files = item["manifest"].get("files", {}).get("textures", [])
    if texture_files:
        texture_folder = item["folder"] / Path(texture_files[0]).parent
        if texture_folder.exists():
            return texture_folder
    fallback = item["folder"] / "textures"
    return fallback if fallback.exists() else None


def model_spec_from_mesh_variant(item: dict, texture_folder: Path | None = None) -> dict | None:
    files = item["manifest"].get("files", {})
    obj_name = files.get("obj") or "model.obj"
    mtl_name = files.get("mtl") or "model.mtl"
    obj_path = item["folder"] / obj_name
    mtl_path = item["folder"] / mtl_name
    if not obj_path.exists():
        return None
    return {
        "kind": "mesh_variant",
        "folder": rel(item["folder"]),
        "obj": rel(obj_path),
        "mtl": rel(mtl_path) if mtl_path.exists() else None,
        "texture_folder": rel(texture_folder) if texture_folder else None,
        "variant_id": item["variant_id"],
        "manifest": rel(item["manifest_path"]),
    }


def model_spec_texture_on_reference(reference: dict, texture_item: dict) -> dict | None:
    ref_spec = model_spec_from_mesh_variant(reference)
    if not ref_spec:
        return None

    texture_folder = texture_folder_from_variant(texture_item)
    if not texture_folder or not texture_folder.exists():
        return None

    spec = dict(ref_spec)
    spec.update(
        {
            "kind": "texture_variant_on_reference_mesh",
            "texture_folder": rel(texture_folder),
            "variant_id": texture_item["variant_id"],
            "manifest": rel(texture_item["manifest_path"]),
        }
    )
    return spec


def make_trials(max_objects: int, max_trials: int, seed: int, max_faces: int) -> list[dict]:
    metadata = read_metadata()
    mesh_root = find_mesh_root()
    mesh_by_object = load_variants(mesh_root)
    texture_by_object = load_variants(ROOT / "Objects" / "Distorted" / "TextureVariants")

    object_ids = []
    for object_id in sorted(set(mesh_by_object) | set(texture_by_object)):
        meta = metadata_for(metadata, object_id)
        face_count = int(meta.get("faceCount") or 0)
        if max_faces and face_count and face_count > max_faces:
            continue
        object_ids.append(object_id)
    random.Random(seed).shuffle(object_ids)
    selected = object_ids[:max_objects] if max_objects else object_ids

    trials = []
    for object_id in selected:
        reference = choose_reference(mesh_by_object.get(object_id, []))
        if not reference:
            continue
        reference_texture_item = choose_reference_texture(texture_by_object.get(object_id, []))
        reference_texture = texture_folder_from_variant(reference_texture_item)
        reference_spec = model_spec_from_mesh_variant(reference, reference_texture)
        if not reference_spec:
            continue

        meta = metadata_for(metadata, object_id)
        candidates = []

        for item in mesh_by_object.get(object_id, []):
            if item["variant_id"] == reference["variant_id"]:
                continue
            distorted_spec = model_spec_from_mesh_variant(item, reference_texture)
            if not distorted_spec:
                continue
            family, profile, params = group_label(item["manifest"].get("active_groups", []))
            candidates.append(
                {
                    "distortion_modality": "mesh",
                    "distortion_family": family,
                    "distortion_profile": profile,
                    "distortion_params": params,
                    "distorted": distorted_spec,
                    "distortion_manifest": distorted_spec["manifest"],
                }
            )

        for item in texture_by_object.get(object_id, []):
            if reference_texture_item and item["variant_id"] == reference_texture_item["variant_id"]:
                continue
            distorted_spec = model_spec_texture_on_reference(reference, item)
            if not distorted_spec:
                continue
            family, profile, params = group_label(item["manifest"].get("active_groups", []))
            candidates.append(
                {
                    "distortion_modality": "texture",
                    "distortion_family": family,
                    "distortion_profile": profile,
                    "distortion_params": params,
                    "distorted": distorted_spec,
                    "distortion_manifest": distorted_spec["manifest"],
                }
            )

        random.Random(f"{seed}-{object_id}").shuffle(candidates)
        for idx, candidate in enumerate(candidates):
            trial_id = f"{object_id}__{idx + 1:04d}"
            trials.append(
                {
                    "trial_id": trial_id,
                    "object_id": object_id,
                    "object_name": meta.get("name", object_id),
                    "imagenet_class": meta.get("imagenet_class", ""),
                    "scene_id": meta.get("default_scene_id", ""),
                    "face_count": meta.get("faceCount", ""),
                    "reference_policy": "proxy_reference_mesh_variant",
                    "reference_note": "No original OBJ was found in Objects/Originals; a least-distorted mesh variant is used as the DSIS reference proxy.",
                    "reference": reference_spec,
                    **candidate,
                }
            )

    random.Random(seed).shuffle(trials)
    return trials[:max_trials] if max_trials else trials


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a solo DSIS manual experiment plan.")
    parser.add_argument("--max-objects", type=int, default=10)
    parser.add_argument("--max-trials", type=int, default=120)
    parser.add_argument("--max-faces", type=int, default=150000)
    parser.add_argument("--seed", type=int, default=20260615)
    parser.add_argument("--output", default=str(OUT_DIR / "dsis_trials.json"))
    args = parser.parse_args()

    trials = make_trials(args.max_objects, args.max_trials, args.seed, args.max_faces)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "experiment": "DSIS_distance_solo",
        "scale": {
            "0": "aucune distance: identique ou quasi identique a la reference",
            "1": "distance tres faible: differences a peine visibles",
            "2": "distance faible: differences visibles mais l'objet reste tres proche",
            "3": "distance moyenne: ressemblant, mais plusieurs changements nets",
            "4": "distance forte: meme categorie possible, objet tres modifie",
            "5": "distance maximale: plus de ressemblance exploitable",
        },
        "viewport": {"reference_px": 960, "distorted_px": 960},
        "filters": {"max_faces": args.max_faces},
        "trials": trials,
    }
    with output.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)

    print(f"Wrote {len(trials)} trials to {output}")


if __name__ == "__main__":
    main()
