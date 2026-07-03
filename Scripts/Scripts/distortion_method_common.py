import argparse
from typing import Optional

from generate_distorted_variants import run_generation


def run_group(group_id: str, config_path: Optional[str] = None) -> None:
    run_generation(
        raw_config_path=config_path,
        only_groups=[group_id],
    )


def main_for_group(group_id: str, description: str) -> None:
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument(
        "--config",
        help="Path to a generation config JSON file. Defaults to DistortionConfig/unified_generation_config.json",
    )
    args = parser.parse_args()
    run_group(group_id=group_id, config_path=args.config)
