from pathlib import Path
from typing import Dict, List

from PIL import Image

from ...common import RuntimeState


GROUP_ID = "texture_resize"


def resize_image(image_path: Path, ts: int) -> None:
    if ts < 1:
        raise ValueError(f"Invalid ts: {ts}")

    with Image.open(image_path) as img:
        if ts == 1:
            return

        w, h = img.size
        img = img.resize((max(1, w // ts), max(1, h // ts)), Image.Resampling.LANCZOS)
        suffix = image_path.suffix.lower()
        if suffix in {".jpg", ".jpeg"} and img.mode not in ("RGB", "L"):
            img = img.convert("RGB")
        img.save(image_path)


def apply_texture_resize(images: List[Path], original_folder: Path, profile: Dict, runtime: RuntimeState) -> None:
    for image_path in images:
        if not runtime.dry_run:
            resize_image(
                image_path=image_path,
                ts=int(profile["ts"]),
            )
