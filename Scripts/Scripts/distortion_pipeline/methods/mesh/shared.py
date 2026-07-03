import subprocess
import tempfile
from pathlib import Path
from typing import Dict, Optional

from ...common import DRACO_COMPRESSION_LEVEL, ROOT, RuntimeState, WORK_TMP_DIR, ensure_parent_dir


def count_faces_from_obj(obj_path: Path) -> int:
    count = 0
    with obj_path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if line.startswith("f "):
                count += 1
    return count


def compute_target_face_ratio(simpL: int) -> float:
    if not (1 <= simpL <= 10):
        raise ValueError(f"simpL must be in [1, 10], got {simpL}")

    anchors = {
        1: 0.9,
        5: 0.5,
        10: 0.1,
    }
    if simpL in anchors:
        return anchors[simpL]

    if simpL < 5:
        left_level, right_level = 1, 5
    else:
        left_level, right_level = 5, 10

    left_ratio = anchors[left_level]
    right_ratio = anchors[right_level]
    progress = (simpL - left_level) / float(right_level - left_level)
    return left_ratio + (right_ratio - left_ratio) * progress


def compute_target_faces(nb_faces_initial: int, simpL: int, target_face_ratio: Optional[float] = None) -> int:
    ratio = float(target_face_ratio) if target_face_ratio is not None else compute_target_face_ratio(simpL)
    ratio = max(0.0, min(1.0, ratio))
    target = round(nb_faces_initial * ratio)
    target = max(1, min(nb_faces_initial, target))
    return int(target)


BLENDER_WORKER_SCRIPT = r'''
import bpy
import sys


def parse_args():
    argv = sys.argv
    if "--" not in argv:
        raise RuntimeError("Missing '--' in Blender worker args")

    argv = argv[argv.index("--") + 1:]
    if len(argv) != 5:
        raise RuntimeError(f"Expected 5 args, got {len(argv)}: {argv}")

    return {
        "input_obj": argv[0],
        "output_obj": argv[1],
        "target_faces": int(argv[2]),
        "qp_bits": int(argv[3]),
        "qt_bits": int(argv[4]),
    }


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_obj(obj_path: str):
    bpy.ops.wm.obj_import(filepath=obj_path)
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if not meshes:
        raise RuntimeError(f"No mesh imported from {obj_path}")
    return meshes


def join_meshes(meshes):
    if len(meshes) == 1:
        return meshes[0]

    bpy.ops.object.select_all(action='DESELECT')
    for obj in meshes:
        obj.select_set(True)

    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    return bpy.context.view_layer.objects.active


def get_face_count(obj):
    return len(obj.data.polygons)


def apply_decimate_to_target_faces(obj, target_faces: int):
    if target_faces < 0:
        return

    current_faces = get_face_count(obj)
    if current_faces <= target_faces:
        return

    ratio = max(0.0, min(1.0, target_faces / current_faces))
    mod = obj.modifiers.new(name="DecimateForDataset", type='DECIMATE')
    mod.ratio = ratio
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=mod.name)


def quantize_vertices(obj, bits: int):
    if bits < 0:
        return

    scale = float(2 ** bits)
    mesh = obj.data

    for v in mesh.vertices:
        v.co.x = round(v.co.x * scale) / scale
        v.co.y = round(v.co.y * scale) / scale
        v.co.z = round(v.co.z * scale) / scale


def quantize_uvs(obj, bits: int):
    if bits < 0:
        return

    scale = float(2 ** bits)
    mesh = obj.data

    for uv_layer in mesh.uv_layers:
        for uv in uv_layer.data:
            uv.uv.x = round(uv.uv.x * scale) / scale
            uv.uv.y = round(uv.uv.y * scale) / scale


def export_obj(obj, output_obj: str):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj

    bpy.ops.wm.obj_export(
        filepath=output_obj,
        export_selected_objects=True,
        path_mode='COPY'
    )


def main():
    args = parse_args()

    reset_scene()
    meshes = import_obj(args["input_obj"])
    obj = join_meshes(meshes)

    apply_decimate_to_target_faces(obj, args["target_faces"])
    quantize_vertices(obj, args["qp_bits"])
    quantize_uvs(obj, args["qt_bits"])

    export_obj(obj, args["output_obj"])


if __name__ == "__main__":
    main()
'''


def run_blender_mesh_transform(
    input_obj: Path,
    output_obj: Path,
    runtime: RuntimeState,
    target_faces: Optional[int] = None,
    qp_bits: Optional[int] = None,
    qt_bits: Optional[int] = None,
) -> None:
    if runtime.blender_exe is None:
        raise FileNotFoundError("Blender executable has not been resolved")

    WORK_TMP_DIR.mkdir(parents=True, exist_ok=True)

    with tempfile.NamedTemporaryFile(
        "w",
        suffix="_blender_worker.py",
        delete=False,
        dir=WORK_TMP_DIR,
        encoding="utf-8",
    ) as tmp_script:
        tmp_script.write(BLENDER_WORKER_SCRIPT)
        tmp_script_path = Path(tmp_script.name)

    try:
        args = [
            str(runtime.blender_exe),
            "--background",
            "--python",
            str(tmp_script_path),
            "--",
            str(input_obj),
            str(output_obj),
            str(target_faces if target_faces is not None else -1),
            str(qp_bits if qp_bits is not None else -1),
            str(qt_bits if qt_bits is not None else -1),
        ]

        result = subprocess.run(
            args,
            check=False,
            capture_output=True,
            text=True,
            cwd=str(ROOT.resolve()),
        )

        if result.returncode != 0:
            raise RuntimeError(
                "Blender mesh transform failed.\n"
                f"Command: {' '.join(args)}\n"
                f"STDOUT:\n{result.stdout}\n"
                f"STDERR:\n{result.stderr}"
            )
    finally:
        tmp_script_path.unlink(missing_ok=True)


def run_draco_encoder(input_obj: Path, output_drc: Path, runtime: RuntimeState, qp: int, qt: int) -> None:
    if runtime.draco_encoder_exe is None:
        raise FileNotFoundError("Draco encoder executable has not been resolved")

    ensure_parent_dir(output_drc)
    args = [
        str(runtime.draco_encoder_exe),
        "-i",
        str(input_obj),
        "-o",
        str(output_drc),
        "-qp",
        str(qp),
        "-qt",
        str(qt),
        "-cl",
        str(DRACO_COMPRESSION_LEVEL),
    ]

    result = subprocess.run(
        args,
        check=False,
        capture_output=True,
        text=True,
        cwd=str(ROOT.resolve()),
    )

    if result.returncode != 0:
        raise RuntimeError(
            "Draco encoder failed.\n"
            f"Command: {' '.join(args)}\n"
            f"STDOUT:\n{result.stdout}\n"
            f"STDERR:\n{result.stderr}"
        )
