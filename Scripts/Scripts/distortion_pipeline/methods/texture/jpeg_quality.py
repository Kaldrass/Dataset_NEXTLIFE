from pathlib import Path
from typing import Dict, List

from PIL import Image

from ...common import RuntimeState


GROUP_ID = "texture_jpeg_quality"


def jpeg_reencode_image(image_path: Path, tq: int) -> None:
    with Image.open(image_path) as img:
        suffix = image_path.suffix.lower()
        if suffix in {".jpg", ".jpeg"}:
            if img.mode not in ("RGB", "L"):
                img = img.convert("RGB")
            img.save(image_path, quality=int(tq))
        else:
            img.save(image_path)


def apply_texture_jpeg_quality(images: List[Path], original_folder: Path, profile: Dict, runtime: RuntimeState) -> None:
    for image_path in images:
        if not runtime.dry_run:
            jpeg_reencode_image(
                image_path=image_path,
                tq=int(profile["tq"]),
            )
