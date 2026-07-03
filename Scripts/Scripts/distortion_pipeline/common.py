import csv
import hashlib
import json
import os
import re
import shlex
from dataclasses import dataclass, field
from itertools import product
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Set, Tuple


ROOT = Path(__file__).resolve().parents[2]
ORIGINALS_DIR = ROOT / "Objects" / "Originals"
DISTORTED_DIR = ROOT / "Objects" / "Distorted"
MESH_VARIANTS_DIR = DISTORTED_DIR / "MeshVariants"
TEXTURE_VARIANTS_DIR = DISTORTED_DIR / "TextureVariants"
COMBINED_VARIANTS_DIR = DISTORTED_DIR / "CombinedVariants"
WORK_TMP_DIR = ROOT / ".tmp" / "distortion_work"
EXTERNAL_DISTORTIONS_DIR = ROOT / "ExternalDistortions"
MESH_ENCRYPTION_DIR = ROOT / "3D_encryption"

DISTORTION_GROUPS_JSON = ROOT / "DistortionConfig" / "distortion_groups.json"
DEFAULT_GENERATION_CONFIG_JSON = ROOT / "DistortionConfig" / "unified_generation_config.json"
RUNTIME_REPORTS_DIR = ROOT / ".tmp" / "distortion_runtime"
DRACO_COMPRESSION_LEVEL = 7

SUPPORTED_IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp"}
TEXTURE_KEYS = {
    "map_kd",
    "map_ka",
    "map_ks",
    "map_ke",
    "map_bump",
    "bump",
    "map_d",
    "disp",
    "decal",
    "norm",
    "map_pr",
    "map_pm",
    "map_ps",
}
DIFFUSE_TEXTURE_KEYS = {
    "map_kd",
}
GENERATED_MASK_OBJECTS: Set[str] = set()

DEFAULT_3D_ENCRYPTION_FUNC_DEF_AXIS = {
    "ranges": {
        "minRange": 0.15,
        "maxRange": 0.75,
        "idBand": 0.1,
    },
    "oscillatory": {
        "f0Min": 2.0,
        "f0Max": 10.0,
        "f1Cycles": 2.0,
        "dMax": 1.6,
    },
    "monotonic": {
        "aMin": 0.5,
        "aMax": 2.0,
        "bMin": 0.5,
        "bMax": 2.0,
    },
}


@dataclass
class RuntimeState:
    dry_run: bool = True
    overwrite_existing: bool = False
    max_objects: Optional[int] = 1
    max_recipes: Optional[int] = 10
    object_whitelist: List[str] = field(default_factory=list)
    write_draco_file: bool = True
    active_recipe_plan_id: str = "atomic_single_operator"
    blender_exe: Optional[Path] = None
    draco_encoder_exe: Optional[Path] = None
    mesh_encryption_exe: Optional[Path] = None


def ensure_parent_dir(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def is_windows_host() -> bool:
    return os.name == "nt"


def is_elf_binary(path: Path) -> bool:
    try:
        with path.open("rb") as f:
            return f.read(4) == b"\x7fELF"
    except OSError:
        return False


def path_to_wsl(path: Path) -> str:
    raw = str(path.resolve())
    drive, tail = os.path.splitdrive(raw)
    if not drive:
        return raw.replace("\\", "/")
    drive_letter = drive.rstrip(":").lower()
    return f"/mnt/{drive_letter}{tail.replace(chr(92), '/')}"


def load_json(path: Path):
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def save_json(path: Path, payload) -> None:
    ensure_parent_dir(path)
    with path.open("w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=2)


def save_csv_rows(path: Path, rows: List[Dict[str, str]]) -> None:
    ensure_parent_dir(path)
    fieldnames = [
        "object_id",
        "recipe_plan_id",
        "recipe_id",
        "variant_id",
        "output_folder",
        "active_groups",
        "status",
        "message",
        "manifest_path",
    ]

    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def sanitize_token(value: str) -> str:
    value = re.sub(r"[^\w.-]+", "_", value.strip())
    value = re.sub(r"_+", "_", value).strip("_")
    return value or "unnamed"


def load_distortion_groups() -> List[Dict]:
    data = load_json(DISTORTION_GROUPS_JSON)
    return data["groups"]


def resolve_generation_config_path(raw_path: Optional[str]) -> Path:
    if raw_path:
        path = Path(raw_path)
        if not path.is_absolute():
            cwd_candidate = path.resolve()
            if cwd_candidate.exists():
                return cwd_candidate
            path = ROOT / path
        return path.resolve()

    env_path = os.environ.get("UNIFIED_GENERATION_CONFIG")
    if env_path:
        return resolve_generation_config_path(env_path)

    return DEFAULT_GENERATION_CONFIG_JSON


def make_path_for_report(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def parse_optional_path(raw_path: Optional[str]) -> Optional[Path]:
    if not raw_path:
        return None
    return Path(raw_path).expanduser()


def build_blender_candidates() -> List[Path]:
    candidates: List[Path] = []
    base_dirs = [
        Path(r"C:\Program Files\Blender Foundation"),
        Path(r"C:\Program Files"),
        Path.home() / "AppData" / "Local" / "Programs",
    ]

    for base_dir in base_dirs:
        if not base_dir.exists():
            continue

        candidates.extend(sorted(base_dir.glob("Blender*\\blender.exe"), reverse=True))
        candidates.extend(sorted(base_dir.glob("Blender Foundation\\Blender*\\blender.exe"), reverse=True))

    return list(dict.fromkeys(candidate.resolve() for candidate in candidates if candidate.exists()))


def build_draco_candidates() -> List[Path]:
    candidates = [
        ROOT / ".tools" / "draco-build" / "Release" / "draco_encoder.exe",
        Path(r"C:\Program Files (x86)\draco\build\Release\draco_encoder.exe"),
        Path(r"C:\Program Files\draco\build\Release\draco_encoder.exe"),
        Path(r"C:\draco\build\Release\draco_encoder.exe"),
    ]
    return [candidate.resolve() for candidate in candidates if candidate.exists()]


def build_mesh_encryption_candidates() -> List[Path]:
    candidates = [
        MESH_ENCRYPTION_DIR / "app.exe",
        MESH_ENCRYPTION_DIR / "app",
    ]
    return [candidate.resolve() for candidate in candidates if candidate.exists()]


def resolve_tool_path(
    configured_path: Optional[str],
    env_var: str,
    candidates: List[Path],
) -> Optional[Path]:
    explicit_candidates: List[Path] = []

    configured = parse_optional_path(configured_path)
    if configured:
        explicit_candidates.append(configured)

    env_candidate = parse_optional_path(os.environ.get(env_var))
    if env_candidate:
        explicit_candidates.append(env_candidate)

    for candidate in explicit_candidates + candidates:
        if candidate.exists():
            return candidate.resolve()

    return None


def get_execution_settings(config: Dict[str, Any]) -> Dict[str, Any]:
    execution = dict(config.get("execution", {}))
    execution.setdefault("dry_run", True)
    execution.setdefault("overwrite_existing", False)
    execution.setdefault("max_objects", 1)
    execution.setdefault("max_recipes", 10)
    execution.setdefault("object_whitelist", [])
    execution.setdefault("write_draco_file", True)
    execution.setdefault("tool_paths", {})
    return execution


def get_recipe_plan(config: Dict[str, Any]) -> Dict[str, Any]:
    active_recipe_plan_id = config.get("active_recipe_plan_id")
    recipe_plans = {
        plan["recipe_plan_id"]: plan
        for plan in config.get("recipe_plans", [])
    }

    if not recipe_plans:
        raise ValueError("No recipe plans defined in unified_generation_config.json")

    if not active_recipe_plan_id:
        raise KeyError("Missing active_recipe_plan_id in unified_generation_config.json")

    if active_recipe_plan_id not in recipe_plans:
        raise KeyError(
            f"Active recipe plan '{active_recipe_plan_id}' was not found in unified_generation_config.json"
        )

    return recipe_plans[active_recipe_plan_id]


def normalize_requested_group_ids(raw_values: Optional[Iterable[str]]) -> List[str]:
    if not raw_values:
        return []

    ordered: List[str] = []
    seen: Set[str] = set()
    for raw_value in raw_values:
        for token in str(raw_value).split(","):
            group_id = token.strip()
            if not group_id or group_id in seen:
                continue
            ordered.append(group_id)
            seen.add(group_id)
    return ordered


def apply_group_override_to_recipe_plan(
    recipe_plan: Dict[str, Any],
    groups_by_id: Dict[str, Dict],
    requested_group_ids: List[str],
) -> Dict[str, Any]:
    if not requested_group_ids:
        return dict(recipe_plan)

    missing = [group_id for group_id in requested_group_ids if group_id not in groups_by_id]
    if missing:
        raise KeyError(f"Unknown group ids requested through --only-group: {missing}")

    overridden_plan = dict(recipe_plan)
    overridden_plan["enabled_groups"] = list(requested_group_ids)
    overridden_plan["notes"] = (
        f"{recipe_plan.get('notes', '').strip()} "
        f"[group override: {', '.join(requested_group_ids)}]"
    ).strip()
    overridden_plan["recipe_plan_id"] = (
        f"{recipe_plan['recipe_plan_id']}__{sanitize_token('__'.join(requested_group_ids))}"
    )
    return overridden_plan


def build_runtime_report_paths(run_label: str) -> Tuple[Path, Path]:
    recipes_path = RUNTIME_REPORTS_DIR / f"{run_label}.distortion_recipes.json"
    log_path = RUNTIME_REPORTS_DIR / f"{run_label}.generation_log.csv"
    return recipes_path, log_path


def make_geometry_profile_id(profile: Dict) -> str:
    return f"simpL{profile['simpL']}_qp{profile['qp']}_qt{profile['qt']}"


def normalize_profile(group: Dict, raw_profile: Dict) -> Dict:
    group_id = group["group_id"]
    normalized = dict(raw_profile)

    if "profile_id" not in normalized:
        if "method_id" in normalized:
            normalized["profile_id"] = normalized["method_id"]
        elif group_id == "texture_resize":
            normalized["profile_id"] = f"resize{normalized['ts']}"
        elif group_id == "texture_jpeg_quality":
            normalized["profile_id"] = f"jpeg_q{normalized['tq']}"
        elif group_id == "texture_quality_resize_jpeg":
            normalized["profile_id"] = normalized["folder_name"]
        elif group_id == "mesh_simplification":
            normalized["profile_id"] = f"simpL{normalized['simpL']}"
        elif group_id == "mesh_quantization_position":
            normalized["profile_id"] = f"qp{normalized['qp']}"
        elif group_id == "mesh_quantization_uv":
            normalized["profile_id"] = f"qt{normalized['qt']}"
        elif group_id == "mesh_quality_simplification_draco":
            normalized["profile_id"] = make_geometry_profile_id(normalized)
        else:
            raise KeyError(f"Cannot infer profile_id for group {group_id}: {raw_profile}")

    normalized["group_id"] = group_id
    normalized["execution_stage"] = int(group.get("execution_stage", 999))
    normalized["modality"] = group["modality"]
    normalized["category"] = group["category"]
    normalized["status"] = group["status"]
    if group.get("exclusive_group"):
        normalized["exclusive_group"] = group["exclusive_group"]
    return normalized


def load_group_profiles(group: Dict) -> List[Dict]:
    if "profile_source" in group:
        source = group["profile_source"]
        if source["type"] != "json_file":
            raise ValueError(f"Unsupported profile source type: {source['type']}")
        raw_profiles = load_json(ROOT / source["path"])
    else:
        raw_profiles = group["profiles"]

    return [normalize_profile(group, profile) for profile in raw_profiles]


def build_profile_catalog(groups: List[Dict]) -> Dict[str, List[Dict]]:
    catalog = {}
    for group in groups:
        catalog[group["group_id"]] = load_group_profiles(group)
    return catalog


def build_group_lookup(groups: List[Dict]) -> Dict[str, Dict]:
    return {group["group_id"]: group for group in groups}


def list_original_folders(object_whitelist: Iterable[str], max_objects: Optional[int]) -> List[Path]:
    folders = sorted([p for p in ORIGINALS_DIR.iterdir() if p.is_dir()])

    allowed = set(object_whitelist)
    if allowed:
        folders = [p for p in folders if p.name in allowed]

    if max_objects is not None:
        folders = folders[:max_objects]

    return folders


def extract_profile_params(profile: Dict) -> Dict:
    params = dict(profile.get("params", {}))
    for key, value in profile.items():
        if key in {
            "group_id",
            "profile_id",
            "method_id",
            "modality",
            "execution_stage",
            "category",
            "status",
            "exclusive_group",
            "params",
        }:
            continue
        params[key] = value
    return params


def build_recipe_id(active_profiles: List[Dict]) -> str:
    parts = []
    for profile in active_profiles:
        parts.append(
            sanitize_token(f"{profile['group_id']}-{profile['profile_id']}")
        )
    return "__".join(parts)


def build_recipe_record(active_profiles: List[Dict], recipe_plan_id: str) -> Dict:
    ordered = sorted(
        active_profiles,
        key=lambda p: (0 if p["modality"] == "mesh" else 1, p["execution_stage"], p["group_id"], p["profile_id"]),
    )

    return {
        "recipe_plan_id": recipe_plan_id,
        "recipe_id": build_recipe_id(ordered),
        "active_groups": [
            {
                "group_id": p["group_id"],
                "profile_id": p["profile_id"],
                "modality": p["modality"],
                "execution_stage": p["execution_stage"],
                "params": extract_profile_params(p),
            }
            for p in ordered
        ],
    }


def sort_active_groups(active_groups: List[Dict]) -> List[Dict]:
    return sorted(
        active_groups,
        key=lambda p: (0 if p["modality"] == "mesh" else 1, p["execution_stage"], p["group_id"], p["profile_id"]),
    )


def build_profile_token(item: Dict) -> str:
    group_id = item["group_id"]
    profile_id = item["profile_id"]
    params = item["params"]

    if group_id == "mesh_simplification":
        return f"ms_s{params['simpL']}"
    if group_id == "mesh_quantization_position":
        return f"mqp_p{params['qp']}"
    if group_id == "mesh_quantization_uv":
        return f"mqt_t{params['qt']}"
    if group_id == "mesh_quality_simplification_draco":
        return f"mq_s{params['simpL']}_p{params['qp']}_t{params['qt']}"
    if group_id == "texture_resize":
        return f"tr_r{params['ts']}"
    if group_id == "texture_jpeg_quality":
        return f"tj_q{params['tq']}"
    if group_id == "texture_quality_resize_jpeg":
        return f"tq_r{params['ts']}_q{params['tq']}"
    if group_id == "texture_blur":
        return f"blur_l{params['level']}"
    if group_id == "texture_encryption":
        return f"aes_l{params['level']}"
    if group_id == "texture_block_obscuration":
        return f"bloc_obsc_l{params['level']}"
    if group_id == "mesh_encryption":
        return f"menc_l{params['level']}"
    if group_id == "mesh_data_hiding":
        return f"mdh_l{params['level']}"
    return sanitize_token(profile_id)[:16]


def build_compact_token(active_groups: List[Dict], seed: str, default_prefix: str) -> str:
    parts = []

    for item in sort_active_groups(active_groups):
        parts.append(build_profile_token(item))

    base = "__".join(parts) if parts else default_prefix
    digest = hashlib.sha1(seed.encode("utf-8")).hexdigest()[:10]
    return f"{base}__v{digest}"


def build_mesh_variant_id(mesh_groups: List[Dict]) -> Optional[str]:
    if not mesh_groups:
        return None
    signature = "|".join(
        f"{item['group_id']}:{item['profile_id']}"
        for item in sort_active_groups(mesh_groups)
    )
    return build_compact_token(mesh_groups, seed=f"mesh|{signature}", default_prefix="mesh")


def build_texture_variant_id(texture_groups: List[Dict]) -> Optional[str]:
    if not texture_groups:
        return None
    signature = "|".join(
        f"{item['group_id']}:{item['profile_id']}"
        for item in sort_active_groups(texture_groups)
    )
    return build_compact_token(texture_groups, seed=f"texture|{signature}", default_prefix="texture")


def build_combined_variant_id(recipe: Dict) -> str:
    return build_compact_token(
        recipe["active_groups"],
        seed=f"{recipe['recipe_plan_id']}|{recipe['recipe_id']}",
        default_prefix="combined",
    )


def resolve_group_ids(plan: Dict[str, Any], groups_by_id: Dict[str, Dict], key: str) -> List[str]:
    group_ids = list(plan.get(key, []))
    missing = [group_id for group_id in group_ids if group_id not in groups_by_id]
    if missing:
        raise KeyError(f"Unknown group ids in recipe plan '{plan['recipe_plan_id']}' for {key}: {missing}")
    return group_ids


def build_atomic_recipes(plan: Dict[str, Any], groups_by_id: Dict[str, Dict], catalog: Dict[str, List[Dict]]) -> List[Dict]:
    recipes = []
    enabled_group_ids = resolve_group_ids(plan, groups_by_id, "enabled_groups")

    for group_id in enabled_group_ids:
        for profile in catalog[group_id]:
            recipes.append(build_recipe_record([profile], recipe_plan_id=plan["recipe_plan_id"]))

    return recipes


def build_cartesian_enabled_recipes(plan: Dict[str, Any], groups_by_id: Dict[str, Dict], catalog: Dict[str, List[Dict]]) -> List[Dict]:
    enabled_group_ids = resolve_group_ids(plan, groups_by_id, "enabled_groups")
    enabled_groups = [groups_by_id[group_id] for group_id in enabled_group_ids]

    normal_groups = [g for g in enabled_groups if "exclusive_group" not in g]
    exclusive_buckets: Dict[str, List[Dict]] = {}
    for group in enabled_groups:
        bucket_name = group.get("exclusive_group")
        if not bucket_name:
            continue
        exclusive_buckets.setdefault(bucket_name, []).append(group)

    choice_lists: List[List[Optional[Dict]]] = []

    for group in normal_groups:
        choices = list(catalog[group["group_id"]])
        if not choices:
            raise ValueError(f"No profiles found for enabled group {group['group_id']}")
        choice_lists.append(choices)

    for bucket_name in sorted(exclusive_buckets):
        bucket_choices: List[Optional[Dict]] = []
        for group in sorted(exclusive_buckets[bucket_name], key=lambda g: g["group_id"]):
            bucket_choices.extend(catalog[group["group_id"]])
        bucket_mode = plan.get("exclusive_group_modes", {}).get(bucket_name, "exactly_one")
        if bucket_mode == "zero_or_one":
            bucket_choices.append(None)
        elif bucket_mode != "exactly_one":
            raise ValueError(
                f"Unsupported exclusive group mode '{bucket_mode}' for bucket '{bucket_name}'"
            )
        if not bucket_choices:
            raise ValueError(f"No profiles found for exclusive bucket {bucket_name}")
        choice_lists.append(bucket_choices)

    if not choice_lists:
        return []

    recipes = []
    for combo in product(*choice_lists):
        active_profiles = [profile for profile in combo if profile is not None]
        recipes.append(build_recipe_record(active_profiles, recipe_plan_id=plan["recipe_plan_id"]))

    return recipes


def build_required_optional_cartesian_recipes(
    plan: Dict[str, Any],
    groups_by_id: Dict[str, Dict],
    catalog: Dict[str, List[Dict]],
) -> List[Dict]:
    required_group_ids = resolve_group_ids(plan, groups_by_id, "required_groups")
    optional_group_ids = resolve_group_ids(plan, groups_by_id, "optional_groups")
    exclusive_group_modes = dict(plan.get("exclusive_group_modes", {}))

    overlap = sorted(set(required_group_ids) & set(optional_group_ids))
    if overlap:
        raise ValueError(
            f"Recipe plan '{plan['recipe_plan_id']}' has groups that are both required and optional: {overlap}"
        )

    ambiguous_groups = []
    for group_id in required_group_ids + optional_group_ids:
        group = groups_by_id[group_id]
        bucket_name = group.get("exclusive_group")
        if bucket_name and bucket_name in exclusive_group_modes:
            ambiguous_groups.append(group_id)
    if ambiguous_groups:
        raise ValueError(
            f"Recipe plan '{plan['recipe_plan_id']}' mixes explicit group selection with exclusive buckets: {ambiguous_groups}"
        )

    choice_lists: List[List[Optional[Dict]]] = []

    for group_id in required_group_ids:
        choices = list(catalog[group_id])
        if not choices:
            raise ValueError(f"No profiles found for required group {group_id}")
        choice_lists.append(choices)

    for group_id in optional_group_ids:
        choices = [None] + list(catalog[group_id])
        if len(choices) == 1:
            raise ValueError(f"No profiles found for optional group {group_id}")
        choice_lists.append(choices)

    for bucket_name in sorted(exclusive_group_modes):
        bucket_choices: List[Optional[Dict]] = []
        for group in sorted(
            [g for g in groups_by_id.values() if g.get("exclusive_group") == bucket_name],
            key=lambda g: g["group_id"],
        ):
            bucket_choices.extend(catalog[group["group_id"]])

        bucket_mode = exclusive_group_modes[bucket_name]
        if bucket_mode == "zero_or_one":
            bucket_choices = [None] + bucket_choices
        elif bucket_mode != "exactly_one":
            raise ValueError(
                f"Unsupported exclusive group mode '{bucket_mode}' for bucket '{bucket_name}'"
            )

        if not bucket_choices:
            raise ValueError(f"No profiles found for exclusive bucket {bucket_name}")

        choice_lists.append(bucket_choices)

    if not choice_lists:
        return []

    recipes = []
    for combo in product(*choice_lists):
        active_profiles = [profile for profile in combo if profile is not None]
        recipes.append(build_recipe_record(active_profiles, recipe_plan_id=plan["recipe_plan_id"]))

    return recipes


def build_recipes(
    recipe_plan: Dict[str, Any],
    groups_by_id: Dict[str, Dict],
    catalog: Dict[str, List[Dict]],
    max_recipes: Optional[int],
) -> List[Dict]:
    recipe_mode = recipe_plan["mode"]

    if recipe_mode == "atomic_single_operator":
        recipes = build_atomic_recipes(recipe_plan, groups_by_id, catalog)
    elif recipe_mode == "cartesian_enabled_groups":
        recipes = build_cartesian_enabled_recipes(recipe_plan, groups_by_id, catalog)
    elif recipe_mode == "required_optional_cartesian":
        recipes = build_required_optional_cartesian_recipes(recipe_plan, groups_by_id, catalog)
    else:
        raise ValueError(f"Unsupported recipe mode: {recipe_mode}")

    recipes = list({recipe["recipe_id"]: recipe for recipe in recipes}.values())

    if max_recipes is not None:
        recipes = recipes[:max_recipes]

    return recipes
