from pathlib import Path
from typing import Dict, Optional

from ...common import RuntimeState
from .encryption import apply_mesh_encryption


GROUP_ID = "mesh_data_hiding"


def apply_mesh_data_hiding(obj_path: Path, profile: Dict, runtime: RuntimeState, workdir: Path) -> Optional[Dict[str, int]]:
    return apply_mesh_encryption(obj_path, profile, runtime, workdir)
