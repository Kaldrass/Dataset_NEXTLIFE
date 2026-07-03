from pathlib import Path
from typing import Dict, List

from ...common import RuntimeState
from .shared import apply_external_texture_group


GROUP_ID = "texture_blur"


def apply_texture_blur(images: List[Path], original_folder: Path, profile: Dict, runtime: RuntimeState) -> None:
    apply_external_texture_group(images, original_folder, profile, runtime)
