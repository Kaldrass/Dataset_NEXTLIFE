from pathlib import Path
from typing import Dict, Optional

from ...common import RuntimeState
from .shared import compute_target_faces, count_faces_from_obj, run_blender_mesh_transform


GROUP_ID = "mesh_simplification"


def apply_mesh_simplification(obj_path: Path, profile: Dict, runtime: RuntimeState, workdir: Path) -> Optional[Dict[str, int]]:
    nb_faces_initial = count_faces_from_obj(obj_path)
    target_faces = compute_target_faces(
        nb_faces_initial=nb_faces_initial,
        simpL=int(profile["simpL"]),
        target_face_ratio=profile.get("target_face_ratio"),
    )

    if not runtime.dry_run:
        run_blender_mesh_transform(
            input_obj=obj_path,
            output_obj=obj_path,
            runtime=runtime,
            target_faces=target_faces,
        )
    return None
