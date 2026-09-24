import shlex
import os
import subprocess
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from PIL import Image

from ...assets import (
    resolve_mask_for_current_texture,
)
from ...common import EXTERNAL_DISTORTIONS_DIR, RuntimeState, is_elf_binary, is_windows_host, path_to_wsl


def resolve_external_operator(operator_rel: str) -> Path:
    if os.environ.get("NEXTLIFE_OPERATOR_BIN_DIR"):
        candidate = Path(os.environ["NEXTLIFE_OPERATOR_BIN_DIR"]) / Path(operator_rel).name
        if not candidate.is_file():
            raise FileNotFoundError(candidate)
        return candidate
    base = EXTERNAL_DISTORTIONS_DIR / operator_rel
    if base.exists():
        return base
    exe_variant = base.with_suffix(base.suffix + ".exe") if base.suffix else Path(str(base) + ".exe")
    if exe_variant.exists():
        return exe_variant
    raise FileNotFoundError(f"External operator not found: {base} or {exe_variant}")


def tokenize_template(template: str) -> List[str]:
    return shlex.split(template, posix=False)


def replace_placeholders(tokens: List[str], replacements: Dict[str, str]) -> List[str]:
    out = []
    for token in tokens:
        new_token = token
        for old, new in replacements.items():
            new_token = new_token.replace(old, new)
        out.append(new_token)
    return out


def build_placeholder_map(
    input_path: Path,
    output_path: Path,
    mask_path: Path,
    image_size: Tuple[int, int],
    use_wsl_paths: bool = False,
) -> Dict[str, str]:
    width, height = image_size
    w32 = max(1, width // 32)
    w64 = max(1, width // 64)
    w128 = max(1, width // 128)

    def nearest_odd(x: int) -> int:
        return x if x % 2 == 1 else x + 1

    if use_wsl_paths:
        input_value = path_to_wsl(input_path)
        output_value = path_to_wsl(output_path)
        mask_value = path_to_wsl(mask_path)
    else:
        input_value = str(input_path)
        output_value = str(output_path)
        mask_value = str(mask_path)

    return {
        "{input}": input_value,
        "{output}": output_value,
        "{mask}": mask_value,
        "{width}": str(width),
        "{height}": str(height),
        "{width/32}": str(w32),
        "{width/64}": str(w64),
        "{width/128}": str(w128),
        "{1width/32}": str(nearest_odd(w32)),
        "{1width/64}": str(nearest_odd(w64)),
        "{1width/128}": str(nearest_odd(w128)),
    }


def run_external_texture_method(texture_path: Path, original_folder: Path, profile: Dict, runtime: RuntimeState) -> None:
    operator_path = resolve_external_operator(profile["operator_rel"])
    tmp_output = texture_path.with_name(f"{texture_path.stem}.__tmp__{profile['profile_id']}{texture_path.suffix}")
    generated_mask_path: Optional[Path] = None

    try:
        mask_path, generated_mask_path = resolve_mask_for_current_texture(texture_path, original_folder)
        use_wsl_operator = is_windows_host() and is_elf_binary(operator_path)
        with Image.open(texture_path) as img:
            placeholder_map = build_placeholder_map(
                input_path=texture_path.resolve(),
                output_path=tmp_output.resolve(),
                mask_path=mask_path.resolve(),
                image_size=img.size,
                use_wsl_paths=use_wsl_operator,
            )

        args = tokenize_template(profile["args_template"])
        args = replace_placeholders(args, placeholder_map)
        # Encryption generates these files. Keep them alongside each output
        # instead of overwriting repository files shared by concurrent workers.
        if "--op" in args and args[args.index("--op") + 1] == "encrypt":
            for flag, suffix in (("--key", "key.bin"), ("--iv", "iv.bin")):
                if flag in args:
                    sidecar = texture_path.with_name(f"{texture_path.name}.{profile['profile_id']}.{suffix}").resolve()
                    args[args.index(flag) + 1] = path_to_wsl(sidecar) if use_wsl_operator else str(sidecar)

        if use_wsl_operator:
            wsl_cwd = path_to_wsl(EXTERNAL_DISTORTIONS_DIR)
            wsl_operator = path_to_wsl(operator_path)
            full_cmd = [wsl_operator] + args
            shell_cmd = "cd " + shlex.quote(wsl_cwd) + " && " + " ".join(
                shlex.quote(token) for token in full_cmd
            )
            cmd = ["wsl.exe", "bash", "-lc", shell_cmd]
            run_cwd = str(EXTERNAL_DISTORTIONS_DIR.parents[0].resolve())
        else:
            cmd = [str(operator_path.resolve())] + args
            run_cwd = str(EXTERNAL_DISTORTIONS_DIR.resolve())

        if runtime.dry_run:
            return

        result = subprocess.run(
            cmd,
            check=False,
            capture_output=True,
            text=True,
            cwd=run_cwd,
        )

        if result.returncode != 0:
            raise RuntimeError(
                "External texture method failed.\n"
                f"Command: {' '.join(cmd)}\n"
                f"STDOUT:\n{result.stdout}\n"
                f"STDERR:\n{result.stderr}"
            )

        if not tmp_output.exists():
            raise FileNotFoundError(f"Expected method output not found: {tmp_output}")

        tmp_output.replace(texture_path)
    finally:
        if tmp_output.exists():
            tmp_output.unlink(missing_ok=True)
        if generated_mask_path and generated_mask_path.exists():
            generated_mask_path.unlink(missing_ok=True)


def apply_external_texture_group(images, original_folder: Path, profile: Dict, runtime: RuntimeState) -> None:
    for image_path in images:
        run_external_texture_method(
            texture_path=image_path,
            original_folder=original_folder,
            profile=profile,
            runtime=runtime,
        )
