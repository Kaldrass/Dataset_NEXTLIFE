# External Distortions

This folder is now the cleaned external distortion workspace that we keep for dataset generation.

It intentionally contains only the three texture method families selected for NEXTLIFE:

- `AES`
- `Blurring`
- `Block_all_operation`

Everything here is organized as a clean integration layer for the main dataset pipeline.
The old legacy workspace content was removed from this copy.

## Structure

```text
ExternalDistortions/
  methods/
    methods_nextlife_clean.json
    texture_blur.json
    texture_encryption.json
    texture_block_obscuration.json
  operators/
    aes/
      aes_image_tool.cpp
      Makefile
      key.bin
      iv.bin
    blurring/
      blurring_mask.cpp
      Makefile
    block_all_operation/
      block_all_operation.cpp
      Makefile
  masks/
    white.png
  Makefile
```

## Source Of Truth

The parameter choices were taken from the old legacy `methods_NEXTLIFE.json` file before cleanup.
The clean JSON files in `methods/` are now the source of truth we should use from now on.

## Operators

## `texture_blur`

- level 1: suffix `32`
- level 2: suffix `64`
- level 3: suffix `128`
- executable: `operators/blurring/masked_blur`

## `texture_encryption`

- level 1: AES on bits `0-6`
- level 2: AES on bit `7`
- level 3: AES on bits `0-7`
- executable: `operators/aes/aes_image_tool`

## `texture_block_obscuration`

- level 1: block size `width/32`
- level 2: block size `width/64`
- level 3: block size `width/128`
- executable: `operators/block_all_operation/block_all_operation`

## Build

This curated subtree is source-only.
It does not keep legacy compiled binaries.

On a Linux or WSL environment with OpenCV and Crypto++ installed:

```bash
make
```

Or per operator:

```bash
make -C operators/aes
make -C operators/blurring
make -C operators/block_all_operation
```

## Integration Intent

The main dataset generator should treat these operators as adapters:

- input: one texture image plus an optional mask
- config: one method profile from `methods/`
- output: one transformed texture image

The 3D object pipeline should remain responsible for:

- locating textures from OBJ/MTL
- choosing masks
- copying object folders into `Objects/Distorted/`
- writing `manifest.json`

This separation is what will keep the full distortion pipeline manageable.
