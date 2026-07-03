from pathlib import Path
from typing import Dict, Optional

from ...common import RuntimeState
from .shared import run_blender_mesh_transform


GROUP_ID = "mesh_quantization_uv"


def apply_mesh_uv_quantization(obj_path: Path, profile: Dict, runtime: RuntimeState, workdir: Path) -> Optional[Dict[str, int]]:
    if not runtime.dry_run:
        run_blender_mesh_transform(
            input_obj=obj_path,
            output_obj=obj_path,
            runtime=runtime,
            qt_bits=int(profile["qt"]),
        )
    return None
