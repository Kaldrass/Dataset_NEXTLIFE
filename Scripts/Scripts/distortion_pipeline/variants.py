import hashlib
import shutil
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from .assets import (
    canonicalize_mesh_names,
    collect_texture_entries,
    copy_file,
    find_single_mtl_file,
    find_single_obj_file,
    normalize_variant_relative_path,
    relative_path_str,
    resolve_variant_texture_path,
    select_texture_distortion_targets,
)
from .common import (
    COMBINED_VARIANTS_DIR,
    MESH_VARIANTS_DIR,
    ORIGINALS_DIR,
    SUPPORTED_IMAGE_EXTS,
    TEXTURE_VARIANTS_DIR,
    WORK_TMP_DIR,
    RuntimeState,
    build_combined_variant_id,
    build_mesh_variant_id,
    build_texture_variant_id,
    load_json,
    make_path_for_report,
    save_json,
    sort_active_groups,
)
from .methods.mesh import MESH_METHOD_HANDLERS, run_draco_encoder
from .methods.texture import apply_texture_group


def split_recipe_profiles(recipe: Dict) -> Tuple[List[Dict], List[Dict]]:
    active_profiles = recipe["active_groups"]
    mesh_profiles = [p for p in active_profiles if p["modality"] == "mesh"]
    texture_profiles = [p for p in active_profiles if p["modality"] == "texture"]
    return mesh_profiles, texture_profiles


def ensure_fresh_output_folder(folder: Path, manifest_path: Path, runtime: RuntimeState) -> str:
    if folder.exists():
        if manifest_path.exists() and not runtime.overwrite_existing:
            return "reused"
        shutil.rmtree(folder)

    folder.mkdir(parents=True, exist_ok=True)
    return "created"


def read_existing_manifest(manifest_path: Path) -> Optional[Dict[str, Any]]:
    if not manifest_path.exists():
        return None
    return load_json(manifest_path)


def copy_mesh_inputs_to_workdir(original_folder: Path, workdir: Path) -> Tuple[Path, Optional[Path], List[Dict[str, Path]]]:
    workdir.mkdir(parents=True, exist_ok=True)

    obj_path = find_single_obj_file(original_folder)
    mtl_path = find_single_mtl_file(original_folder)
    texture_entries = collect_texture_entries(original_folder, mtl_path)

    copy_file(obj_path, workdir / obj_path.name)
    if mtl_path:
        copy_file(mtl_path, workdir / mtl_path.name)

    for entry in texture_entries:
        dst_path = workdir / normalize_variant_relative_path(entry["relative_path"])
        copy_file(entry["source_path"], dst_path)

    return canonicalize_mesh_names(workdir) + (texture_entries,)


def copy_texture_entries_to_variant(texture_entries: List[Dict[str, Any]], textures_root: Path) -> List[Path]:
    copied_images: List[Path] = []
    for entry in texture_entries:
        dst_path = resolve_variant_texture_path(entry, textures_root)
        copy_file(entry["source_path"], dst_path)
        copied_images.append(dst_path)
    return copied_images


def build_mesh_asset_reference(original_folder: Path, variant_folder: Optional[Path], variant_id: Optional[str], drc_enabled: bool) -> Dict:
    if variant_folder is None or variant_id is None:
        obj_path = find_single_obj_file(original_folder)
        mtl_path = find_single_mtl_file(original_folder)
        return {
            "source_kind": "original",
            "variant_id": None,
            "folder": make_path_for_report(original_folder),
            "manifest_path": None,
            "files": {
                "obj": obj_path.name,
                "mtl": mtl_path.name if mtl_path else None,
                "drc": None,
            },
        }

    return {
        "source_kind": "mesh_variant",
        "variant_id": variant_id,
        "folder": make_path_for_report(variant_folder),
        "manifest_path": make_path_for_report(variant_folder / "manifest.json"),
        "files": {
            "obj": "model.obj",
            "mtl": "model.mtl",
            "drc": "model.drc" if drc_enabled else None,
        },
    }


def build_texture_asset_reference(original_folder: Path, variant_folder: Optional[Path], variant_id: Optional[str], texture_entries: List[Path]) -> Dict:
    if variant_folder is None or variant_id is None:
        mtl_path = find_single_mtl_file(original_folder)
        original_entries = collect_texture_entries(original_folder, mtl_path)
        return {
            "source_kind": "original",
            "variant_id": None,
            "folder": make_path_for_report(original_folder),
            "manifest_path": None,
            "files": {
                "textures": [
                    str(normalize_variant_relative_path(entry["relative_path"])).replace("\\", "/")
                    for entry in original_entries
                ]
            },
        }

    return {
        "source_kind": "texture_variant",
        "variant_id": variant_id,
        "folder": make_path_for_report(variant_folder),
        "manifest_path": make_path_for_report(variant_folder / "manifest.json"),
        "files": {
            "textures": [
                relative_path_str(path, variant_folder)
                for path in texture_entries
            ]
        },
    }


def build_mesh_variant_manifest(
    original_folder: Path,
    variant_folder: Path,
    mesh_variant_id: str,
    mesh_profiles: List[Dict],
    drc_enabled: bool,
) -> Dict:
    return {
        "object_id": original_folder.name,
        "mesh_variant_id": mesh_variant_id,
        "parent_object_id": original_folder.name,
        "source_original_folder": make_path_for_report(original_folder),
        "active_groups": mesh_profiles,
        "files": {
            "obj": "model.obj",
            "mtl": "model.mtl",
            "drc": "model.drc" if drc_enabled else None,
        },
        "storage_role": "mesh_variant",
        "compression": {
            "draco_written": drc_enabled,
        },
    }


def build_texture_variant_manifest(
    original_folder: Path,
    variant_folder: Path,
    texture_variant_id: str,
    texture_profiles: List[Dict],
    texture_paths: List[Path],
) -> Dict:
    return {
        "object_id": original_folder.name,
        "texture_variant_id": texture_variant_id,
        "parent_object_id": original_folder.name,
        "source_original_folder": make_path_for_report(original_folder),
        "active_groups": texture_profiles,
        "files": {
            "textures": [relative_path_str(path, variant_folder) for path in texture_paths],
        },
        "storage_role": "texture_variant",
        "mask_strategy": "generated_uv_masks_then_external_white_fallback",
    }


def build_combined_variant_manifest(
    original_folder: Path,
    variant_folder: Path,
    recipe: Dict,
    mesh_asset: Dict,
    texture_asset: Dict,
) -> Dict:
    return {
        "object_id": original_folder.name,
        "variant_id": variant_folder.name,
        "recipe_plan_id": recipe["recipe_plan_id"],
        "recipe_id": recipe["recipe_id"],
        "source_original_folder": make_path_for_report(original_folder),
        "active_groups": recipe["active_groups"],
        "mesh_asset": mesh_asset,
        "texture_asset": texture_asset,
        "materialization_strategy": {
            "type": "overlay_texture_asset_on_mesh_asset",
            "mesh_source_order": ["mesh_variant", "original"],
            "texture_source_order": ["texture_variant", "original"],
        },
        "storage_role": "combined_variant_manifest",
    }


def get_mesh_draco_params(mesh_profiles: List[Dict]) -> Optional[Dict[str, int]]:
    qp = 11
    qt = 10
    has_mesh_quality = False

    for mesh_profile in sort_active_groups(mesh_profiles):
        params = mesh_profile["params"]
        group_id = mesh_profile["group_id"]

        if group_id == "mesh_quality_simplification_draco":
            has_mesh_quality = True
            qp = int(params["qp"])
            qt = int(params["qt"])
        elif group_id == "mesh_simplification":
            has_mesh_quality = True
        elif group_id == "mesh_quantization_position":
            has_mesh_quality = True
            qp = int(params["qp"])
        elif group_id == "mesh_quantization_uv":
            has_mesh_quality = True
            qt = int(params["qt"])

    if not has_mesh_quality:
        return None

    return {
        "qp": qp,
        "qt": qt,
    }


def ensure_mesh_variant(original_folder: Path, mesh_profiles: List[Dict], runtime: RuntimeState) -> Tuple[Dict, str]:
    if not mesh_profiles:
        return build_mesh_asset_reference(original_folder, None, None, drc_enabled=False), "original"

    mesh_variant_id = build_mesh_variant_id(mesh_profiles)
    if mesh_variant_id is None:
        raise RuntimeError("Failed to build mesh_variant_id")

    variant_folder = MESH_VARIANTS_DIR / original_folder.name / mesh_variant_id
    manifest_path = variant_folder / "manifest.json"
    draco_params = get_mesh_draco_params(mesh_profiles)
    expect_drc = runtime.write_draco_file and draco_params is not None

    if runtime.dry_run:
        return build_mesh_asset_reference(
            original_folder,
            variant_folder,
            mesh_variant_id,
            drc_enabled=expect_drc,
        ), "planned"

    existing_manifest = read_existing_manifest(manifest_path)
    can_reuse = (
        existing_manifest is not None
        and (
            not expect_drc
            or (variant_folder / "model.drc").exists()
        )
    )

    if can_reuse and not runtime.overwrite_existing:
        return build_mesh_asset_reference(
            original_folder,
            variant_folder,
            mesh_variant_id,
            drc_enabled=expect_drc and (variant_folder / "model.drc").exists(),
        ), "reused"

    state = ensure_fresh_output_folder(variant_folder, manifest_path, runtime)

    WORK_TMP_DIR.mkdir(parents=True, exist_ok=True)

    tmp_seed = hashlib.sha1(
        f"{original_folder.name}|{mesh_variant_id}".encode("utf-8")
    ).hexdigest()[:12]
    tmp_dir = WORK_TMP_DIR / f"mesh_work_{tmp_seed}"

    if tmp_dir.exists():
        shutil.rmtree(tmp_dir, ignore_errors=True)
    tmp_dir.mkdir(parents=True, exist_ok=True)

    try:
        workdir = tmp_dir
        obj_path, mtl_path, _ = copy_mesh_inputs_to_workdir(original_folder, workdir)

        drc_path = None
        for mesh_profile in sort_active_groups(mesh_profiles):
            handler = MESH_METHOD_HANDLERS.get(mesh_profile["group_id"])
            if handler is None:
                raise NotImplementedError(f"Unsupported mesh group: {mesh_profile['group_id']}")
            handler_result = handler(obj_path, mesh_profile["params"], runtime, workdir)
            if mesh_profile["group_id"] == "mesh_quality_simplification_draco":
                draco_params = handler_result

        if runtime.write_draco_file and draco_params is not None:
            drc_path = obj_path.with_suffix(".drc")
            run_draco_encoder(
                input_obj=obj_path,
                output_drc=drc_path,
                runtime=runtime,
                qp=int(draco_params["qp"]),
                qt=int(draco_params["qt"]),
            )

        copy_file(obj_path, variant_folder / "model.obj")
        if mtl_path and mtl_path.exists():
            copy_file(mtl_path, variant_folder / "model.mtl")
        if drc_path and drc_path.exists():
            copy_file(drc_path, variant_folder / "model.drc")
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)

    manifest = build_mesh_variant_manifest(
        original_folder=original_folder,
        variant_folder=variant_folder,
        mesh_variant_id=mesh_variant_id,
        mesh_profiles=mesh_profiles,
        drc_enabled=(variant_folder / "model.drc").exists(),
    )
    save_json(manifest_path, manifest)

    return build_mesh_asset_reference(
        original_folder,
        variant_folder,
        mesh_variant_id,
        drc_enabled=(variant_folder / "model.drc").exists(),
    ), state


def ensure_texture_variant(original_folder: Path, texture_profiles: List[Dict], runtime: RuntimeState) -> Tuple[Dict, str]:
    if not texture_profiles:
        return build_texture_asset_reference(original_folder, None, None, []), "original"

    texture_variant_id = build_texture_variant_id(texture_profiles)
    if texture_variant_id is None:
        raise RuntimeError("Failed to build texture_variant_id")

    variant_folder = TEXTURE_VARIANTS_DIR / original_folder.name / texture_variant_id
    manifest_path = variant_folder / "manifest.json"

    mtl_path = find_single_mtl_file(original_folder)
    texture_entries = collect_texture_entries(original_folder, mtl_path)
    if not texture_entries:
        raise RuntimeError(f"No textures found for texture variant generation in {original_folder}")

    if runtime.dry_run:
        planned_paths = [
            variant_folder / "textures" / normalize_variant_relative_path(entry["relative_path"])
            for entry in texture_entries
        ]
        return build_texture_asset_reference(
            original_folder,
            variant_folder,
            texture_variant_id,
            planned_paths,
        ), "planned"

    existing_manifest = read_existing_manifest(manifest_path)
    if existing_manifest is not None and not runtime.overwrite_existing:
        existing_textures = sorted([
            path
            for path in (variant_folder / "textures").rglob("*")
            if path.is_file() and path.suffix.lower() in SUPPORTED_IMAGE_EXTS
        ])
        return build_texture_asset_reference(
            original_folder,
            variant_folder,
            texture_variant_id,
            existing_textures,
        ), "reused"

    state = ensure_fresh_output_folder(variant_folder, manifest_path, runtime)

    textures_root = variant_folder / "textures"
    copied_images = copy_texture_entries_to_variant(texture_entries, textures_root)
    distortion_target_images = select_texture_distortion_targets(texture_entries, textures_root)
    if not distortion_target_images:
        print(
            f"[warn] No diffuse texture maps (map_Kd) found for {original_folder.name}; "
            "texture variant will keep non-diffuse maps unchanged."
        )

    for texture_profile in sort_active_groups(texture_profiles):
        texture_profile_data = dict(texture_profile["params"])
        texture_profile_data["group_id"] = texture_profile["group_id"]
        texture_profile_data["profile_id"] = texture_profile["profile_id"]
        apply_texture_group(
            images=distortion_target_images,
            original_folder=original_folder,
            profile=texture_profile_data,
            runtime=runtime,
        )

    manifest = build_texture_variant_manifest(
        original_folder=original_folder,
        variant_folder=variant_folder,
        texture_variant_id=texture_variant_id,
        texture_profiles=texture_profiles,
        texture_paths=copied_images,
    )
    save_json(manifest_path, manifest)

    return build_texture_asset_reference(
        original_folder,
        variant_folder,
        texture_variant_id,
        copied_images,
    ), state


def ensure_combined_variant_manifest(original_folder: Path, recipe: Dict, mesh_asset: Dict, texture_asset: Dict, runtime: RuntimeState) -> Tuple[Path, str]:
    variant_id = build_combined_variant_id(recipe)
    variant_folder = COMBINED_VARIANTS_DIR / original_folder.name / variant_id
    manifest_path = variant_folder / "manifest.json"

    if runtime.dry_run:
        return variant_folder, "planned"

    manifest = build_combined_variant_manifest(
        original_folder=original_folder,
        variant_folder=variant_folder,
        recipe=recipe,
        mesh_asset=mesh_asset,
        texture_asset=texture_asset,
    )

    state = ensure_fresh_output_folder(variant_folder, manifest_path, runtime)
    save_json(manifest_path, manifest)
    return variant_folder, state


def process_variant(original_folder: Path, recipe: Dict, runtime: RuntimeState) -> Dict[str, str]:
    variant_id = build_combined_variant_id(recipe)
    variant_folder = COMBINED_VARIANTS_DIR / original_folder.name / variant_id
    manifest_path = variant_folder / "manifest.json"

    row = {
        "object_id": original_folder.name,
        "recipe_plan_id": recipe["recipe_plan_id"],
        "recipe_id": recipe["recipe_id"],
        "variant_id": variant_id,
        "output_folder": str(variant_folder),
        "active_groups": " | ".join(
            f"{item['group_id']}:{item['profile_id']}" for item in recipe["active_groups"]
        ),
        "status": "",
        "message": "",
        "manifest_path": str(manifest_path),
    }

    mesh_profiles, texture_profiles = split_recipe_profiles(recipe)

    if runtime.dry_run:
        mesh_asset, mesh_state = ensure_mesh_variant(original_folder, mesh_profiles, runtime)
        texture_asset, texture_state = ensure_texture_variant(original_folder, texture_profiles, runtime)
        _, combined_state = ensure_combined_variant_manifest(original_folder, recipe, mesh_asset, texture_asset, runtime)
        row["status"] = "dry_run"
        row["message"] = f"mesh={mesh_state}; texture={texture_state}; combined={combined_state}"
        return row

    mesh_asset, mesh_state = ensure_mesh_variant(original_folder, mesh_profiles, runtime)
    texture_asset, texture_state = ensure_texture_variant(original_folder, texture_profiles, runtime)
    variant_folder, combined_state = ensure_combined_variant_manifest(original_folder, recipe, mesh_asset, texture_asset, runtime)

    row["output_folder"] = str(variant_folder)
    row["manifest_path"] = str(variant_folder / "manifest.json")
    row["status"] = "ok"
    row["message"] = f"mesh={mesh_state}; texture={texture_state}; combined={combined_state}"
    return row
