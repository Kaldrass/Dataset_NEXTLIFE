import argparse
from typing import Dict, Iterable, List, Tuple

from generate_mesh_data_hiding import GROUP_ID as MESH_DATA_HIDING_GROUP_ID
from generate_mesh_data_hiding import run as run_mesh_data_hiding
from generate_mesh_encryption import GROUP_ID as MESH_ENCRYPTION_GROUP_ID
from generate_mesh_encryption import run as run_mesh_encryption
from generate_mesh_quantization_position import GROUP_ID as MESH_QP_GROUP_ID
from generate_mesh_quantization_position import run as run_mesh_quantization_position
from generate_mesh_quantization_uv import GROUP_ID as MESH_QT_GROUP_ID
from generate_mesh_quantization_uv import run as run_mesh_quantization_uv
from generate_mesh_simplification import GROUP_ID as MESH_SIMPLIFICATION_GROUP_ID
from generate_mesh_simplification import run as run_mesh_simplification
from generate_texture_blur import GROUP_ID as TEXTURE_BLUR_GROUP_ID
from generate_texture_blur import run as run_texture_blur
from generate_texture_encryption import GROUP_ID as TEXTURE_ENCRYPTION_GROUP_ID
from generate_texture_encryption import run as run_texture_encryption
from generate_texture_jpeg_quality import GROUP_ID as TEXTURE_JPEG_GROUP_ID
from generate_texture_jpeg_quality import run as run_texture_jpeg_quality
from generate_texture_block_obscuration import GROUP_ID as TEXTURE_BLOCK_OBSCURATION_GROUP_ID
from generate_texture_block_obscuration import run as run_texture_block_obscuration
from generate_texture_resize import GROUP_ID as TEXTURE_RESIZE_GROUP_ID
from generate_texture_resize import run as run_texture_resize


METHOD_RUNNERS: List[Tuple[str, str, object]] = [
    (TEXTURE_RESIZE_GROUP_ID, "texture_resize", run_texture_resize),
    (TEXTURE_JPEG_GROUP_ID, "texture_jpeg_quality", run_texture_jpeg_quality),
    (TEXTURE_BLUR_GROUP_ID, "texture_blur", run_texture_blur),
    (TEXTURE_ENCRYPTION_GROUP_ID, "texture_encryption", run_texture_encryption),
    (TEXTURE_BLOCK_OBSCURATION_GROUP_ID, "texture_block_obscuration", run_texture_block_obscuration),
    (MESH_SIMPLIFICATION_GROUP_ID, "mesh_simplification", run_mesh_simplification),
    (MESH_QP_GROUP_ID, "mesh_quantization_position", run_mesh_quantization_position),
    (MESH_QT_GROUP_ID, "mesh_quantization_uv", run_mesh_quantization_uv),
    (MESH_ENCRYPTION_GROUP_ID, "mesh_encryption", run_mesh_encryption),
    (MESH_DATA_HIDING_GROUP_ID, "mesh_data_hiding", run_mesh_data_hiding),
]


def normalize_group_selection(raw_values: Iterable[str]) -> List[str]:
    ordered: List[str] = []
    seen = set()
    for raw_value in raw_values:
        for token in str(raw_value).split(","):
            group_id = token.strip()
            if not group_id or group_id in seen:
                continue
            ordered.append(group_id)
            seen.add(group_id)
    return ordered


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Orchestrate one-script-per-method distortion generation."
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
            "Restrict orchestration to one or more explicit groups. "
            "Can be repeated or passed as a comma-separated list."
        ),
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    selected_groups = normalize_group_selection(args.only_group)
    runner_lookup: Dict[str, Tuple[str, str, object]] = {
        group_id: item for group_id, *item in METHOD_RUNNERS
    }

    if selected_groups:
        missing = [group_id for group_id in selected_groups if group_id not in runner_lookup]
        if missing:
            raise KeyError(f"Unknown group ids requested through --only-group: {missing}")
        ordered_runners = [(group_id, *runner_lookup[group_id]) for group_id in selected_groups]
    else:
        ordered_runners = METHOD_RUNNERS

    for idx, (group_id, label, runner) in enumerate(ordered_runners, start=1):
        print(f"[{idx}/{len(ordered_runners)}] Running {label}")
        runner(config_path=args.config)
        print()


if __name__ == "__main__":
    main()
