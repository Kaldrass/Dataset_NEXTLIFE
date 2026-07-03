import shutil
import subprocess
from pathlib import Path
from typing import Dict, Optional

from ...assets import copy_file
from ...common import (
    DEFAULT_3D_ENCRYPTION_FUNC_DEF_AXIS,
    MESH_ENCRYPTION_DIR,
    ROOT,
    RuntimeState,
    is_elf_binary,
    is_windows_host,
    path_to_wsl,
    save_json,
)


GROUP_ID = "mesh_encryption"


def build_mesh_encryption_output_name(input_obj: Path, profile: Dict) -> str:
    return (
        f"{input_obj.stem}_Attempt0_enc_"
        f"{int(profile['nbBitsEnc'])}_{int(profile.get('nbClearLSB', 0))}.obj"
    )


def build_mesh_encryption_params(
    input_obj: Path,
    output_dir: Path,
    profile: Dict,
    use_wsl_paths: bool = False,
) -> Dict:
    if use_wsl_paths:
        output_folder_value = path_to_wsl(output_dir)
        mesh_path_value = path_to_wsl(input_obj)
    else:
        output_folder_value = str(output_dir.resolve())
        mesh_path_value = str(input_obj.resolve())

    return {
        "outputFolder": output_folder_value,
        "meshPath": mesh_path_value,
        "nbBytesKey": int(profile.get("nbBytesKey", 16)),
        "nbBytesIV": int(profile.get("nbBytesIV", 16)),
        "nbAttempts": 1,
        "save": {
            "orig": False,
            "qp": False,
            "enc": True,
            "dec": False,
        },
        "plot": {
            "orig": False,
            "qp": False,
            "enc": False,
            "dec": False,
        },
        "qp": int(profile.get("qp", 0)),
        "funcEncMantissa": {
            "nbBitsEnc": int(profile["nbBitsEnc"]),
            "nbClearLSB": int(profile.get("nbClearLSB", 0)),
        },
        "funcDefAxis": DEFAULT_3D_ENCRYPTION_FUNC_DEF_AXIS,
    }


def apply_mesh_encryption(obj_path: Path, profile: Dict, runtime: RuntimeState, workdir: Path) -> Optional[Dict[str, int]]:
    if runtime.mesh_encryption_exe is None:
        raise FileNotFoundError("Mesh encryption executable has not been resolved")

    profile_tag = profile.get("profile_id", f"bits{int(profile['nbBitsEnc'])}")
    output_dir = workdir / f"mesh_encryption_{profile_tag}"
    if output_dir.exists():
        shutil.rmtree(output_dir, ignore_errors=True)
    output_dir.mkdir(parents=True, exist_ok=True)

    if is_windows_host() and is_elf_binary(runtime.mesh_encryption_exe):
        wsl_cwd = path_to_wsl(MESH_ENCRYPTION_DIR)
        wsl_exe = path_to_wsl(runtime.mesh_encryption_exe)
        params = build_mesh_encryption_params(
            input_obj=obj_path,
            output_dir=output_dir,
            profile=profile,
            use_wsl_paths=True,
        )
        params_path = output_dir / "params.generated.json"
        save_json(params_path, params)
        wsl_params = path_to_wsl(params_path)
        cmd = [
            "wsl.exe",
            "bash",
            "-lc",
            f"cd {wsl_cwd} && {wsl_exe} {wsl_params}",
        ]
        run_cwd = str(ROOT.resolve())
    else:
        params = build_mesh_encryption_params(
            input_obj=obj_path,
            output_dir=output_dir,
            profile=profile,
            use_wsl_paths=False,
        )
        params_path = output_dir / "params.generated.json"
        save_json(params_path, params)
        cmd = [str(runtime.mesh_encryption_exe.resolve()), str(params_path.resolve())]
        run_cwd = str(MESH_ENCRYPTION_DIR.resolve())

    result = subprocess.run(
        cmd,
        check=False,
        capture_output=True,
        text=True,
        cwd=run_cwd,
    )

    if result.returncode != 0:
        raise RuntimeError(
            "Mesh encryption method failed.\n"
            f"Command: {' '.join(cmd)}\n"
            f"STDOUT:\n{result.stdout}\n"
            f"STDERR:\n{result.stderr}"
        )

    encrypted_obj = output_dir / build_mesh_encryption_output_name(obj_path, profile)
    if not encrypted_obj.exists():
        raise FileNotFoundError(f"Expected encrypted OBJ not found: {encrypted_obj}")

    copy_file(encrypted_obj, obj_path)
    return None
