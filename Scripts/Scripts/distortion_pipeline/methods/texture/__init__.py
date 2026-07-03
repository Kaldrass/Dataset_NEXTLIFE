from typing import Callable, Dict, List
from pathlib import Path

from ...common import RuntimeState
from .blur import apply_texture_blur
from .encryption import apply_texture_encryption
from .jpeg_quality import apply_texture_jpeg_quality
from .block_obscuration import apply_texture_block_obscuration
from .resize import apply_texture_resize
from .resize_jpeg import apply_texture_quality_resize_jpeg


TextureHandler = Callable[[List[Path], Path, Dict, RuntimeState], None]

TEXTURE_METHOD_HANDLERS: Dict[str, TextureHandler] = {
    "texture_resize": apply_texture_resize,
    "texture_jpeg_quality": apply_texture_jpeg_quality,
    "texture_quality_resize_jpeg": apply_texture_quality_resize_jpeg,
    "texture_blur": apply_texture_blur,
    "texture_encryption": apply_texture_encryption,
    "texture_block_obscuration": apply_texture_block_obscuration,
}


def apply_texture_group(images: List[Path], original_folder: Path, profile: Dict, runtime: RuntimeState) -> None:
    group_id = profile["group_id"]
    handler = TEXTURE_METHOD_HANDLERS.get(group_id)
    if handler is None:
        raise NotImplementedError(f"Unsupported texture group: {group_id}")
    handler(images, original_folder, profile, runtime)


__all__ = [
    "TEXTURE_METHOD_HANDLERS",
    "apply_texture_group",
]
