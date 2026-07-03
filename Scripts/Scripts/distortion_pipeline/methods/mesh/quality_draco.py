from pathlib import Path
from typing import Dict, Optional

from ...common import RuntimeState
from .shared import compute_target_faces, count_faces_from_obj, run_blender_mesh_transform, run_draco_encoder


GROUP_ID = "mesh_quality_simplification_draco"


def apply_mesh_quality_simplification_draco(
    obj_path: Path,
    profile: Dict,
    runtime: RuntimeState,
    workdir: Path,
) -> Optional[Dict[str, int]]:
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
            qp_bits=int(profile["qp"]),
            qt_bits=int(profile["qt"]),
        )

    return {"qp": int(profile["qp"]), "qt": int(profile["qt"])}

__all__ = [
    "apply_mesh_quality_simplification_draco",
    "run_draco_encoder",
]
