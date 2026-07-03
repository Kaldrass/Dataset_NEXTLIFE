import re
import shutil
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from PIL import Image, ImageDraw

from .common import (
    DIFFUSE_TEXTURE_KEYS,
    EXTERNAL_DISTORTIONS_DIR,
    GENERATED_MASK_OBJECTS,
    SUPPORTED_IMAGE_EXTS,
    TEXTURE_KEYS,
    ensure_parent_dir,
)


def find_single_obj_file(folder: Path) -> Path:
    obj_files = sorted(folder.glob("*.obj"))
    if not obj_files:
        raise FileNotFoundError(f"No OBJ file found in {folder}")
    if len(obj_files) == 1:
        return obj_files[0]
    candidate = folder / f"{folder.name}.obj"
    if candidate.exists():
        return candidate
    raise RuntimeError(f"Multiple OBJ files found in {folder}: {[p.name for p in obj_files]}")


def find_single_mtl_file(folder: Path) -> Optional[Path]:
    mtl_files = sorted(folder.glob("*.mtl"))
    if not mtl_files:
        return None
    if len(mtl_files) == 1:
        return mtl_files[0]
    candidate = folder / f"{folder.name}.mtl"
    if candidate.exists():
        return candidate
    return mtl_files[0]


def canonicalize_mesh_names(folder: Path) -> Tuple[Path, Optional[Path]]:
    obj_path = find_single_obj_file(folder)
    mtl_path = find_single_mtl_file(folder)

    target_obj = folder / "model.obj"
    target_mtl = folder / "model.mtl" if mtl_path else None

    if obj_path != target_obj:
        obj_path.rename(target_obj)
        obj_path = target_obj

    if mtl_path and target_mtl and mtl_path != target_mtl:
        mtl_path.rename(target_mtl)
        obj_text = obj_path.read_text(encoding="utf-8", errors="ignore")
        obj_text = re.sub(
            r"^\s*mtllib\s+.+$",
            f"mtllib {target_mtl.name}",
            obj_text,
            flags=re.MULTILINE,
        )
        if "mtllib" not in obj_text:
            obj_text = f"mtllib {target_mtl.name}\n{obj_text}"
        obj_path.write_text(obj_text, encoding="utf-8")
        mtl_path = target_mtl

    return obj_path, target_mtl


def parse_mtl_texture_paths(mtl_path: Path) -> List[Path]:
    return [entry["source_path"] for entry in parse_mtl_texture_entries(mtl_path)]


def parse_mtl_texture_entries(mtl_path: Path) -> List[Dict[str, Any]]:
    textures = []

    if not mtl_path or not mtl_path.exists():
        return textures

    with mtl_path.open("r", encoding="utf-8", errors="ignore") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue

            parts = line.split()
            if not parts:
                continue

            key = parts[0].lower()
            if key not in TEXTURE_KEYS:
                continue

            rel_path = Path(parts[-1].strip())
            textures.append(
                {
                    "map_key": key,
                    "relative_path": rel_path,
                    "source_path": (mtl_path.parent / rel_path),
                }
            )

    return textures


def parse_mtl_material_texture_map(mtl_path: Path) -> Dict[str, Dict[str, Path]]:
    materials: Dict[str, Dict[str, Path]] = {}
    current_material: Optional[str] = None

    if not mtl_path or not mtl_path.exists():
        return materials

    with mtl_path.open("r", encoding="utf-8", errors="ignore") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue

            parts = line.split()
            if not parts:
                continue

            key = parts[0].lower()
            if key == "newmtl" and len(parts) >= 2:
                current_material = " ".join(parts[1:])
                materials.setdefault(current_material, {})
                continue

            if current_material is None or key not in TEXTURE_KEYS:
                continue

            materials.setdefault(current_material, {})[key] = Path(parts[-1].strip())

    return materials


def parse_obj_uv_faces_by_material(obj_path: Path) -> Tuple[Dict[str, List[List[Tuple[float, float]]]], List[List[Tuple[float, float]]]]:
    vt_coords: List[Tuple[float, float]] = []
    faces_by_material: Dict[str, List[List[Tuple[float, float]]]] = {}
    all_faces: List[List[Tuple[float, float]]] = []
    current_material = "__default__"

    with obj_path.open("r", encoding="utf-8", errors="ignore") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue

            if line.startswith("vt "):
                parts = line.split()
                if len(parts) >= 3:
                    vt_coords.append((float(parts[1]), float(parts[2])))
                continue

            if line.startswith("usemtl "):
                current_material = line.split(maxsplit=1)[1].strip() or "__default__"
                continue

            if not line.startswith("f "):
                continue

            uv_face: List[Tuple[float, float]] = []
            for token in line.split()[1:]:
                fields = token.split("/")
                if len(fields) < 2 or not fields[1]:
                    continue

                vt_index = int(fields[1])
                if vt_index > 0:
                    resolved_index = vt_index - 1
                else:
                    resolved_index = len(vt_coords) + vt_index

                if 0 <= resolved_index < len(vt_coords):
                    uv_face.append(vt_coords[resolved_index])

            if len(uv_face) < 3:
                continue

            faces_by_material.setdefault(current_material, []).append(uv_face)
            all_faces.append(uv_face)

    return faces_by_material, all_faces


def normalize_mtl_texture_rel(path: Path) -> str:
    if path.is_absolute():
        return path.name.lower()

    clean_parts = [part for part in path.parts if part not in ("", ".")]
    if any(part == ".." for part in clean_parts):
        return path.name.lower()

    normalized = Path(*clean_parts) if clean_parts else Path(path.name)
    return str(normalized).replace("\\", "/").lower()


def wrap_uv(value: float) -> float:
    if 0.0 <= value <= 1.0:
        return value

    wrapped = value % 1.0
    if wrapped == 0.0 and value > 0.0:
        return 1.0
    return wrapped


def uv_to_image_xy(uv: Tuple[float, float], width: int, height: int) -> Tuple[float, float]:
    u, v = uv
    u = wrap_uv(u)
    v = wrap_uv(v)
    x = u * max(0, width - 1)
    y = (1.0 - v) * max(0, height - 1)
    return x, y


def rasterize_uv_faces_to_mask(image_size: Tuple[int, int], faces: List[List[Tuple[float, float]]]) -> Image.Image:
    width, height = image_size
    mask = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(mask)

    for face in faces:
        polygon = [uv_to_image_xy(uv, width, height) for uv in face]
        if len(polygon) >= 3:
            draw.polygon(polygon, fill=255)

    return mask


def ensure_generated_texture_masks(original_folder: Path) -> None:
    cache_key = str(original_folder.resolve())
    if cache_key in GENERATED_MASK_OBJECTS:
        return

    obj_path = find_single_obj_file(original_folder)
    mtl_path = find_single_mtl_file(original_folder)
    texture_entries = collect_texture_entries(original_folder, mtl_path)
    if not mtl_path or not texture_entries:
        GENERATED_MASK_OBJECTS.add(cache_key)
        return

    masks_dir = original_folder / "masks"
    masks_dir.mkdir(parents=True, exist_ok=True)

    material_texture_map = parse_mtl_material_texture_map(mtl_path)
    faces_by_material, all_faces = parse_obj_uv_faces_by_material(obj_path)
    faces_by_texture: Dict[Tuple[str, str], List[List[Tuple[float, float]]]] = {}
    faces_by_relpath: Dict[str, List[List[Tuple[float, float]]]] = {}

    for material_name, faces in faces_by_material.items():
        texture_map = material_texture_map.get(material_name, {})
        for map_key, rel_path in texture_map.items():
            normalized_rel = normalize_mtl_texture_rel(rel_path)
            faces_by_texture.setdefault((map_key.lower(), normalized_rel), []).extend(faces)
            faces_by_relpath.setdefault(normalized_rel, []).extend(faces)

    for entry in texture_entries:
        mask_path = masks_dir / f"{entry['source_path'].stem}.png"
        if mask_path.exists():
            continue

        normalized_rel = normalize_mtl_texture_rel(entry["relative_path"])
        candidate_faces = faces_by_texture.get((str(entry["map_key"]).lower(), normalized_rel), [])
        if not candidate_faces:
            candidate_faces = faces_by_relpath.get(normalized_rel, [])
        if not candidate_faces:
            candidate_faces = all_faces

        with Image.open(entry["source_path"]) as img:
            mask = rasterize_uv_faces_to_mask(img.size, candidate_faces)
            mask.save(mask_path)

    GENERATED_MASK_OBJECTS.add(cache_key)


def collect_texture_entries(folder: Path, mtl_path: Optional[Path]) -> List[Dict[str, Any]]:
    if mtl_path is None:
        return []

    entries = []
    seen = set()
    for entry in parse_mtl_texture_entries(mtl_path):
        source_path = entry["source_path"]
        relative_path = entry["relative_path"]
        key = (str(source_path), str(relative_path))
        if key in seen:
            continue
        seen.add(key)

        if source_path.exists() and source_path.suffix.lower() in SUPPORTED_IMAGE_EXTS:
            entries.append(entry)

    return entries


def normalize_variant_relative_path(path: Path) -> Path:
    if path.is_absolute():
        return Path(path.name)

    clean_parts = [part for part in path.parts if part not in ("", ".")]
    if any(part == ".." for part in clean_parts):
        return Path(path.name)

    return Path(*clean_parts) if clean_parts else Path(path.name)


def resolve_variant_texture_path(entry: Dict[str, Any], textures_root: Path) -> Path:
    rel_path = normalize_variant_relative_path(entry["relative_path"])
    return textures_root / rel_path


def select_texture_distortion_targets(texture_entries: List[Dict[str, Any]], textures_root: Path) -> List[Path]:
    targets = [
        resolve_variant_texture_path(entry, textures_root)
        for entry in texture_entries
        if str(entry.get("map_key", "")).lower() in DIFFUSE_TEXTURE_KEYS
    ]
    seen = set()
    unique_targets: List[Path] = []
    for path in targets:
        key = str(path)
        if key in seen:
            continue
        seen.add(key)
        unique_targets.append(path)
    return unique_targets


def pick_texture_mask(original_folder: Path, texture_path: Path) -> Path:
    object_masks_dir = original_folder / "masks"
    direct_candidate = object_masks_dir / f"{texture_path.stem}.png"
    if direct_candidate.exists():
        return direct_candidate

    ensure_generated_texture_masks(original_folder)
    if direct_candidate.exists():
        return direct_candidate

    fallback = EXTERNAL_DISTORTIONS_DIR / "masks" / "white.png"
    if fallback.exists():
        return fallback

    raise FileNotFoundError(f"No mask found for {texture_path} and no white fallback mask available")


def resolve_mask_for_current_texture(texture_path: Path, original_folder: Path) -> Tuple[Path, Optional[Path]]:
    base_mask_path = pick_texture_mask(original_folder, texture_path)

    with Image.open(texture_path) as texture_img, Image.open(base_mask_path) as mask_img:
        if mask_img.size == texture_img.size:
            return base_mask_path, None

        resized_mask_path = texture_path.with_name(
            f"{texture_path.stem}.__mask__{texture_img.size[0]}x{texture_img.size[1]}.png"
        )
        resized_mask = mask_img.convert("L").resize(texture_img.size, Image.Resampling.NEAREST)
        resized_mask.save(resized_mask_path)
        return resized_mask_path, resized_mask_path


def copy_file(src_path: Path, dst_path: Path) -> None:
    ensure_parent_dir(dst_path)
    shutil.copy2(src_path, dst_path)


def relative_path_str(path: Path, start: Path) -> str:
    return str(path.relative_to(start)).replace("\\", "/")
