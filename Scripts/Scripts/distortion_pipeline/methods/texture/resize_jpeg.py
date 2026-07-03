from pathlib import Path
from typing import Dict, List

from ...common import RuntimeState
from .jpeg_quality import jpeg_reencode_image
from .resize import resize_image


GROUP_ID = "texture_quality_resize_jpeg"


def resize_and_reencode_image(image_path: Path, ts: int, tq: int) -> None:
    resize_image(image_path=image_path, ts=ts)
    jpeg_reencode_image(image_path=image_path, tq=tq)


def apply_texture_quality_resize_jpeg(images: List[Path], original_folder: Path, profile: Dict, runtime: RuntimeState) -> None:
    for image_path in images:
        if not runtime.dry_run:
            resize_and_reencode_image(
                image_path=image_path,
                ts=int(profile["ts"]),
                tq=int(profile["tq"]),
            )
