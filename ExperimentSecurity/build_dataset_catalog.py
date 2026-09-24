import argparse
import json
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

from build_recognition_experiment import (
    ROOT,
    ORIGINALS_DIR,
    DISTORTED_DIR,
    combined_model_spec,
    find_original_model,
    load_json,
    metadata_for,
    normalize_label,
    rel,
)


OUT_DIR = ROOT / "ExperimentSecurity"
CORE_SECURITY_FAMILIES = {
    "texture_encryption",
    "texture_block_obscuration",
    "mesh_encryption",
}
FAMILY_LABELS = {
    "texture_resize": "Redimensionnement texture",
    "texture_jpeg_quality": "Compression JPEG",
    "texture_blur": "Flou texture",
    "texture_encryption": "Chiffrement texture AES",
    "texture_block_obscuration": "Obscurcissement par blocs",
    "mesh_simplification": "Simplification maillage",
    "mesh_quantization_position": "Quantification positions",
    "mesh_quantization_uv": "Quantification UV",
    "mesh_encryption": "Chiffrement maillage",
    "mesh_data_hiding": "Data hiding maillage (proxy)",
}


def readable_profile(profile: str) -> str:
    replacements = {
        "texture_encryption_level": "niveau ",
        "texture_block_obscuration_level": "niveau ",
        "texture_blur_level": "niveau ",
        "mesh_encryption_bits": "",
        "mesh_data_hiding_bits": "",
        "jpeg_q": "qualite ",
        "resize": "facteur ",
        "simpL": "niveau ",
        "qp": "QP ",
        "qt": "QT ",
    }
    for prefix, label in replacements.items():
        if profile.startswith(prefix):
            suffix = profile.removeprefix(prefix)
            if prefix in {"mesh_encryption_bits", "mesh_data_hiding_bits"}:
                return f"{suffix} bits"
            return f"{label}{suffix}"
    return profile.replace("_", " ")


def active_profile_catalog() -> tuple[dict[str, dict], set[tuple[str, str]]]:
    config = load_json(ROOT / "DistortionConfig" / "distortion_groups.json")
    groups = {}
    active_pairs = set()
    for group in config.get("groups", []):
        group_id = str(group.get("group_id", ""))
        groups[group_id] = group
        source = group.get("profile_source", {}).get("path")
        if not source:
            continue
        for profile in load_json(ROOT / source):
            active_pairs.add((group_id, str(profile.get("profile_id", ""))))
    return groups, active_pairs


def distortion_identity(active_groups: list[dict], recipe_id: str) -> tuple[str, str, str, str]:
    if not active_groups:
        return "unknown", "unknown", "unknown", "Distorsion inconnue"
    if len(active_groups) == 1:
        group = active_groups[0]
        family = str(group.get("group_id", "unknown"))
        profile = str(group.get("profile_id", "unknown"))
        modality = str(group.get("modality", "unknown"))
        family_label = FAMILY_LABELS.get(family, family.replace("_", " "))
        return f"{family}::{profile}", family, profile, f"{family_label} - {readable_profile(profile)}"

    parts = [f"{item.get('group_id', 'unknown')}:{item.get('profile_id', 'unknown')}" for item in active_groups]
    modalities = sorted({str(item.get("modality", "unknown")) for item in active_groups})
    return f"recipe::{recipe_id}", "combined", recipe_id, " + ".join(parts), "+".join(modalities)


def build_catalog() -> dict:
    metadata = load_json(ROOT / "metadata.json")
    group_config, active_pairs = active_profile_catalog()
    objects = []
    variants = []
    object_counts = Counter()
    object_security_counts = Counter()

    for original_folder in sorted(ORIGINALS_DIR.iterdir()):
        if not original_folder.is_dir():
            continue
        object_id = original_folder.name
        meta = metadata_for(metadata, object_id)
        original = find_original_model(object_id)
        if not original:
            continue
        raw_label = str(meta.get("imagenet_class", "")).strip()
        normalized_label = normalize_label(raw_label)
        variants.append(
            {
                "trial_id": f"{object_id}__original",
                "object_id": object_id,
                "object_name": meta.get("name", object_id),
                "imagenet_class": normalized_label,
                "raw_imagenet_class": raw_label,
                "scene_id": meta.get("default_scene_id", ""),
                "face_count": meta.get("faceCount", ""),
                "variant_id": "original",
                "distortion_key": "original",
                "distortion_label": "Original",
                "distortion_family": "original",
                "distortion_profile": "original",
                "distortion_modality": "reference",
                "distortion_category": "reference",
                "recipe_plan_id": "reference",
                "recipe_id": "original",
                "active_groups": [],
                "is_original": True,
                "is_canonical": True,
                "is_core_security": False,
                "model": original,
                "manifest": "",
            }
        )

    manifests = sorted((DISTORTED_DIR / "CombinedVariants").glob("*/*/manifest.json"))
    for manifest_path in manifests:
        try:
            manifest = load_json(manifest_path)
        except (OSError, json.JSONDecodeError):
            continue
        object_id = str(manifest.get("object_id") or manifest_path.parent.parent.name)
        meta = metadata_for(metadata, object_id)
        model = combined_model_spec(manifest, manifest_path)
        if not model:
            continue

        active_groups = list(manifest.get("active_groups", []))
        recipe_id = str(manifest.get("recipe_id", manifest_path.parent.name))
        identity = distortion_identity(active_groups, recipe_id)
        if len(identity) == 4:
            distortion_key, family, profile, distortion_label = identity
            modality = str(active_groups[0].get("modality", "unknown")) if active_groups else "unknown"
        else:
            distortion_key, family, profile, distortion_label, modality = identity
        plan_id = str(manifest.get("recipe_plan_id", ""))
        group_pairs = {
            (str(group.get("group_id", "")), str(group.get("profile_id", "")))
            for group in active_groups
        }
        is_canonical = (
            len(group_pairs) == 1
            and group_pairs.issubset(active_pairs)
            and plan_id == "atomic_single_operator"
        )
        is_core_security = is_canonical and family in CORE_SECURITY_FAMILIES
        category = str(group_config.get(family, {}).get("category", "combined"))
        raw_label = str(meta.get("imagenet_class", "")).strip()

        variants.append(
            {
                "trial_id": f"{object_id}__{model['variant_id']}",
                "object_id": object_id,
                "object_name": meta.get("name", object_id),
                "imagenet_class": normalize_label(raw_label),
                "raw_imagenet_class": raw_label,
                "scene_id": meta.get("default_scene_id", ""),
                "face_count": meta.get("faceCount", ""),
                "variant_id": model["variant_id"],
                "distortion_key": distortion_key,
                "distortion_label": distortion_label,
                "distortion_family": family,
                "distortion_profile": profile,
                "distortion_modality": modality,
                "distortion_category": category,
                "recipe_plan_id": plan_id,
                "recipe_id": recipe_id,
                "active_groups": [
                    {
                        "group_id": group.get("group_id", ""),
                        "profile_id": group.get("profile_id", ""),
                        "modality": group.get("modality", ""),
                    }
                    for group in active_groups
                ],
                "is_original": False,
                "is_canonical": is_canonical,
                "is_core_security": is_core_security,
                "model": model,
                "manifest": rel(manifest_path),
            }
        )
        object_counts[object_id] += 1
        if is_core_security:
            object_security_counts[object_id] += 1

    variants.sort(key=lambda item: (item["object_name"].lower(), item["distortion_label"], item["variant_id"]))
    for original_folder in sorted(ORIGINALS_DIR.iterdir()):
        if not original_folder.is_dir():
            continue
        object_id = original_folder.name
        meta = metadata_for(metadata, object_id)
        raw_label = str(meta.get("imagenet_class", "")).strip()
        objects.append(
            {
                "object_id": object_id,
                "object_name": meta.get("name", object_id),
                "imagenet_class": normalize_label(raw_label),
                "raw_imagenet_class": raw_label,
                "scene_id": meta.get("default_scene_id", ""),
                "face_count": meta.get("faceCount", ""),
                "thumbnail_url": meta.get("thumbnail_url", ""),
                "variant_count": object_counts[object_id],
                "core_security_variant_count": object_security_counts[object_id],
            }
        )
    objects.sort(key=lambda item: (item["imagenet_class"].lower(), item["object_name"].lower()))

    distortion_map = {}
    for variant in variants:
        key = variant["distortion_key"]
        entry = distortion_map.setdefault(
            key,
            {
                "distortion_key": key,
                "distortion_label": variant["distortion_label"],
                "distortion_family": variant["distortion_family"],
                "distortion_profile": variant["distortion_profile"],
                "distortion_modality": variant["distortion_modality"],
                "distortion_category": variant["distortion_category"],
                "is_original": variant["is_original"],
                "is_canonical": variant["is_canonical"],
                "is_core_security": variant["is_core_security"],
                "variant_count": 0,
                "object_ids": set(),
            },
        )
        entry["variant_count"] += 1
        entry["object_ids"].add(variant["object_id"])
        entry["is_canonical"] = entry["is_canonical"] or variant["is_canonical"]
        entry["is_core_security"] = entry["is_core_security"] or variant["is_core_security"]

    distortions = []
    for entry in distortion_map.values():
        entry["object_count"] = len(entry.pop("object_ids"))
        distortions.append(entry)
    distortions.sort(key=lambda item: (not item["is_original"], item["distortion_family"], item["distortion_profile"]))

    return {
        "schema_version": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "source": f"metadata.json + {rel(DISTORTED_DIR / 'CombinedVariants')} (Old excluded)",
        "release": rel(DISTORTED_DIR.parent) if DISTORTED_DIR.parent.parent == ROOT / "Objects/Releases" else "",
        "security_levels": ["Original", "Transparent", "Suffisant", "Confidentiel"],
        "core_security_families": sorted(CORE_SECURITY_FAMILIES),
        "counts": {
            "objects": len(objects),
            "variants_including_originals": len(variants),
            "distortions": len(distortions),
            "canonical_distorted_variants": sum(not item["is_original"] and item["is_canonical"] for item in variants),
            "core_security_variants": sum(item["is_core_security"] for item in variants),
        },
        "objects": objects,
        "distortions": distortions,
        "variants": variants,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Build the NEXTLIFE visual-security dataset catalog.")
    parser.add_argument("--output", default=str(OUT_DIR / "dataset_catalog.json"))
    args = parser.parse_args()
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    catalog = build_catalog()
    with output.open("w", encoding="utf-8") as handle:
        json.dump(catalog, handle, ensure_ascii=False, separators=(",", ":"))
    print(json.dumps(catalog["counts"], indent=2))
    print(f"Wrote catalog to {output}")


if __name__ == "__main__":
    main()
