from distortion_method_common import main_for_group, run_group


GROUP_ID = "texture_resize"


def run(config_path=None) -> None:
    run_group(GROUP_ID, config_path=config_path)


if __name__ == "__main__":
    main_for_group(GROUP_ID, "Generate texture-resize distortions.")
