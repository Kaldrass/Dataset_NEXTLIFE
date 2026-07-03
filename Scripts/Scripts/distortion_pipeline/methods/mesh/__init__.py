from typing import Callable, Dict, Optional
from pathlib import Path

from ...common import RuntimeState
from .data_hiding import apply_mesh_data_hiding
from .encryption import apply_mesh_encryption
from .quality_draco import apply_mesh_quality_simplification_draco, run_draco_encoder
from .quantization_position import apply_mesh_position_quantization
from .quantization_uv import apply_mesh_uv_quantization
from .simplification import apply_mesh_simplification


MeshHandler = Callable[[Path, Dict, RuntimeState, Path], Optional[Dict[str, int]]]

MESH_METHOD_HANDLERS: Dict[str, MeshHandler] = {
    "mesh_quality_simplification_draco": apply_mesh_quality_simplification_draco,
    "mesh_simplification": apply_mesh_simplification,
    "mesh_quantization_position": apply_mesh_position_quantization,
    "mesh_quantization_uv": apply_mesh_uv_quantization,
    "mesh_encryption": apply_mesh_encryption,
    "mesh_data_hiding": apply_mesh_data_hiding,
}

__all__ = [
    "MESH_METHOD_HANDLERS",
    "run_draco_encoder",
]
