import argparse
from typing import Any, Dict, Iterable, List, Optional

from .common import (
    COMBINED_VARIANTS_DIR,
    DISTORTED_DIR,
    MESH_VARIANTS_DIR,
    ORIGINALS_DIR,
    RUNTIME_REPORTS_DIR,
    TEXTURE_VARIANTS_DIR,
    WORK_TMP_DIR,
    RuntimeState,
    apply_group_override_to_recipe_plan,
    build_blender_candidates,
    build_draco_candidates,
    build_group_lookup,
    build_mesh_encryption_candidates,
    build_profile_catalog,
    build_recipes,
    build_runtime_report_paths,
    get_execution_settings,
    get_recipe_plan,
    list_original_folders,
    load_distortion_groups,
    load_json,
    make_path_for_report,
    normalize_requested_group_ids,
    resolve_generation_config_path,
    resolve_tool_path,
    save_csv_rows,
    save_json,
)
from .variants import process_variant


def parse_cli_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate distorted object variants from the shared dataset generator."
    )
    parser.add_argument(
        "--config",
        help="Path to a generation config JSON file. Defaults to DistortionConfig/unified_generation_config.json",
    )
    parser.add_argument(
        "--only-group",
        action="append",
        default=[],
        help=(
            "Restrict generation to one or more explicit distortion groups. "
            "Can be repeated or passed as a comma-separated list."
        ),
    )
    return parser.parse_args()


def build_runtime_state(config: Dict[str, Any], recipe_plan_id: str) -> RuntimeState:
    execution_settings = get_execution_settings(config)
    tool_paths = execution_settings.get("tool_paths", {})
    return RuntimeState(
        dry_run=bool(execution_settings["dry_run"]),
        overwrite_existing=bool(execution_settings["overwrite_existing"]),
        max_objects=execution_settings["max_objects"],
        max_recipes=execution_settings["max_recipes"],
        object_whitelist=list(execution_settings["object_whitelist"]),
        write_draco_file=bool(execution_settings["write_draco_file"]),
        active_recipe_plan_id=recipe_plan_id,
        blender_exe=resolve_tool_path(
            configured_path=tool_paths.get("blender_exe"),
            env_var="BLENDER_EXE",
            candidates=build_blender_candidates(),
        ),
        draco_encoder_exe=resolve_tool_path(
            configured_path=tool_paths.get("draco_encoder_exe"),
            env_var="DRACO_ENCODER_EXE",
            candidates=build_draco_candidates(),
        ),
        mesh_encryption_exe=resolve_tool_path(
            configured_path=tool_paths.get("mesh_encryption_exe"),
            env_var="MESH_ENCRYPTION_EXE",
            candidates=build_mesh_encryption_candidates(),
        ),
    )


def run_generation(
    raw_config_path: Optional[str] = None,
    only_groups: Optional[Iterable[str]] = None,
) -> None:
    requested_group_ids = normalize_requested_group_ids(only_groups)
    generation_config_path = resolve_generation_config_path(raw_config_path)

    if not ORIGINALS_DIR.exists():
        raise FileNotFoundError(f"Originals directory not found: {ORIGINALS_DIR}")
    if not generation_config_path.exists():
        raise FileNotFoundError(f"Generation config not found: {generation_config_path}")

    for folder in [
        DISTORTED_DIR,
        MESH_VARIANTS_DIR,
        TEXTURE_VARIANTS_DIR,
        COMBINED_VARIANTS_DIR,
        WORK_TMP_DIR,
        RUNTIME_REPORTS_DIR,
    ]:
        folder.mkdir(parents=True, exist_ok=True)

    generation_config = load_json(generation_config_path)
    base_recipe_plan = get_recipe_plan(generation_config)
    groups = load_distortion_groups()
    groups_by_id = build_group_lookup(groups)
    recipe_plan = apply_group_override_to_recipe_plan(
        recipe_plan=base_recipe_plan,
        groups_by_id=groups_by_id,
        requested_group_ids=requested_group_ids,
    )
    runtime = build_runtime_state(generation_config, recipe_plan["recipe_plan_id"])

    catalog = build_profile_catalog(groups)
    recipes = build_recipes(recipe_plan, groups_by_id, catalog, runtime.max_recipes)
    runtime_recipes_path, runtime_log_path = build_runtime_report_paths(runtime.active_recipe_plan_id)

    save_json(
        runtime_recipes_path,
        {
            "generation_config_path": make_path_for_report(generation_config_path),
            "active_recipe_plan_id": runtime.active_recipe_plan_id,
            "recipe_mode": recipe_plan["mode"],
            "recipe_plan": recipe_plan,
            "execution": get_execution_settings(generation_config),
            "recipe_count": len(recipes),
            "recipes": recipes,
        },
    )

    original_folders = list_original_folders(
        object_whitelist=runtime.object_whitelist,
        max_objects=runtime.max_objects,
    )
    rows: List[Dict[str, str]] = []

    print(f"Found {len(original_folders)} original object(s)")
    print(f"Active recipe plan: {runtime.active_recipe_plan_id}")
    print(f"Blender: {runtime.blender_exe or 'not found'}")
    print(f"Draco:   {runtime.draco_encoder_exe or 'not found'}")
    print(f"MeshEnc: {runtime.mesh_encryption_exe or 'not found'}")
    print(f"Built {len(recipes)} recipe(s)")
    print(f"Dry run: {runtime.dry_run}")
    print()

    for obj_idx, original_folder in enumerate(original_folders, start=1):
        print(f"[{obj_idx}/{len(original_folders)}] {original_folder.name}")

        for recipe_idx, recipe in enumerate(recipes, start=1):
            print(f"  [{recipe_idx}/{len(recipes)}] {recipe['recipe_id']}")
            try:
                row = process_variant(original_folder, recipe, runtime)
            except Exception as e:
                row = {
                    "object_id": original_folder.name,
                    "recipe_plan_id": recipe["recipe_plan_id"],
                    "recipe_id": recipe["recipe_id"],
                    "variant_id": recipe["recipe_id"],
                    "output_folder": str(COMBINED_VARIANTS_DIR / original_folder.name / recipe["recipe_id"]),
                    "active_groups": " | ".join(
                        f"{item['group_id']}:{item['profile_id']}" for item in recipe["active_groups"]
                    ),
                    "status": "error",
                    "message": str(e),
                    "manifest_path": str(
                        COMBINED_VARIANTS_DIR / original_folder.name / recipe["recipe_id"] / "manifest.json"
                    ),
                }
                print(f"    ERROR: {e}")

            rows.append(row)

    save_csv_rows(runtime_log_path, rows)

    ok_count = sum(1 for r in rows if r["status"] == "ok")
    skip_count = sum(1 for r in rows if r["status"] == "skipped")
    dry_count = sum(1 for r in rows if r["status"] == "dry_run")
    err_count = sum(1 for r in rows if r["status"] == "error")

    print()
    print("Done.")
    print(f"Recipes JSON written to: {runtime_recipes_path}")
    print(f"Generation log CSV written to: {runtime_log_path}")
    print(f"OK      : {ok_count}")
    print(f"Skipped : {skip_count}")
    print(f"Dry run : {dry_count}")
    print(f"Errors  : {err_count}")


def main() -> None:
    args = parse_cli_args()
    run_generation(
        raw_config_path=args.config,
        only_groups=args.only_group,
    )
