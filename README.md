# NEXTLIFE — code et documentation

Le dépôt Git partage les viewers, scripts, configurations et métadonnées
(`metadata.json`). Les assets `Objects/`, `Scenes/`, `image/`, `images/` et
l'archive `Old/` restent locaux et sont exclus de Git, ainsi que les images,
objets 3D, catalogues, trials, exports de résultats et journaux générés.
Les assets doivent être transmis séparément et replacés à la racine du clone.
Pour des sorties personnalisées, utiliser `ExperimentSecurity/results/`,
`ExperimentDSIS/results/` ou `.tmp/` afin de conserver leur exclusion.

Deux expériences distinctes sont conservées :

- [ExperimentDSIS](ExperimentDSIS/README.md) : ancienne expérience DSIS.
- [ExperimentSecurity](ExperimentSecurity/README.md) : test participant de
  reconnaissance/sécurité visuelle, explorateur interne et viewer Three.js partagé.
  Les classes de chats sont regroupées dans `Cat` ; les générateurs parcourent
  `Objects/` et ne prennent pas l'archive `Old/` en compte.

Après avoir obtenu les assets, suivre les README des expériences pour régénérer
les catalogues/trials et lancer le serveur local.

**État des scripts de génération :** les fichiers sont actuellement dans
`Scripts/Scripts/`. La documentation historique ci-dessous suppose `Scripts/`.
Le calcul de racine détecte désormais le dépôt à partir de `metadata.json` et
`DistortionConfig/distortion_groups.json`, même avec cet emboîtement.
Le point d'entrée réel est `python Scripts/Scripts/generate_distorted_variants.py`.
La configuration par défaut reste une simulation limitée ; préparer une configuration
de régénération dédiée avant toute production.

## Description historique de l'espace de travail local

The full local NEXTLIFE dataset workspace (including assets supplied separately) contains:

* the reference objects
* the distorted object variants
* the scene assets kept with the dataset
* the minimal generation pipeline used to create or extend the distortions

## Overview

* Number of reference objects: `125`
* Scene set:
  * `art_gallery`
  * `exterior`
  * `exterior_city`
  * `room_white`
  * `sitting_room`
* Distortion organization:
  * mesh variants
  * texture variants
  * lightweight combined manifests

## Folder Structure

```
Dataset_share_copy/
  Objects/
    Originals/
      <object_id>/
        ...
    Distorted/
      MeshVariants/
        <object_id>/<mesh_variant_id>/
          model.obj
          model.mtl
          model.drc   # optional
          manifest.json
      TextureVariants/
        <object_id>/<texture_variant_id>/
          textures/...
          manifest.json
      CombinedVariants/
        <object_id>/<variant_id>/
          manifest.json

  Scenes/
    <scene_id>/
      ...

  DistortionConfig/
    distortion_groups.json
    unified_generation_config.json
    *_profiles.json

  Scripts/
    orchestrate_distortion_generation.py
    generate_<method>.py
    distortion_pipeline/
      ...

  ExternalDistortions/
    methods/
    operators/

  3D_encryption/
    methods/
    app / source files

  metadata.json
```

## Top-Level Files And Folders

* `Objects/` Main dataset content.
* `Scenes/` Scene assets associated with the dataset.
* `metadata.json` Canonical object metadata.
* `DistortionConfig/` Distortion group definitions, profiles, and active generation config.
* `Scripts/` Minimal distortion generation pipeline.
* `ExternalDistortions/` External texture operators and method definitions.
* `3D_encryption/` External mesh protection workspace used by mesh encryption and mesh data hiding.

## How Variants Are Stored

The distorted dataset is factorized into three branches:

* `Objects/Distorted/MeshVariants/` Stores the materialized mesh distortions for one object. A mesh variant folder contains the distorted geometry files (`model.obj`, `model.mtl`, optional `model.drc`) and a manifest describing the active mesh groups.
* `Objects/Distorted/TextureVariants/` Stores the materialized texture distortions for one object. A texture variant folder contains the distorted texture files under `textures/` and a manifest describing the active texture groups.
* `Objects/Distorted/CombinedVariants/` Stores lightweight manifests that describe how to combine one mesh asset and one texture asset into a final distorted object view.

In other words:

* mesh distortions are generated once in `MeshVariants`
* texture distortions are generated once in `TextureVariants`
* `CombinedVariants` records which mesh branch and which texture branch should be used together

### How Combined Variants Work

A combined variant manifest does not necessarily duplicate a full object folder with mesh and textures copied together. Instead, it stores:

* `active_groups` The distortion groups used by the recipe.
* `mesh_asset` Either a reference to a mesh variant, or a fallback to the original mesh.
* `texture_asset` Either a reference to a texture variant, or a fallback to the original textures.
* `materialization_strategy` The rule used to reconstruct the final object from those two sources.

The current strategy is:

* use the mesh from `mesh_asset` if a mesh variant exists, otherwise use the original mesh
* use the textures from `texture_asset` if a texture variant exists, otherwise use the original textures

With the current shared configuration, the active recipe plan is atomic (`atomic_single_operator`), so combined variants are usually one of the following:

* distorted mesh + original textures
* original mesh + distorted textures

If multi-distortion combinations are enabled later, the same combined-variant mechanism can reference both a mesh variant and a texture variant at the same time.

## Distortion Methods

The current distortion space is organized by operator group.

### Texture Methods

* `texture_resize`
  * profiles: `resize1`, `resize2`, `resize4`
  * parameter: `ts = 1, 2, 4`
  * interpretation:
    * `1` = original size
    * `2` = width/2, height/2
    * `4` = width/4, height/4
* `texture_jpeg_quality`
  * profiles: `jpeg_q100`, `jpeg_q75`, `jpeg_q50`
  * parameter: `tq = 100, 75, 50`
  * note: on non-JPEG textures, the original file format is preserved
* `texture_blur`
  * profiles:
    * `texture_blur_level1`
    * `texture_blur_level2`
    * `texture_blur_level3`
  * parameterization:
    * kernel derived from nearest odd `width/32`
    * kernel derived from nearest odd `width/64`
    * kernel derived from nearest odd `width/128`
* `texture_encryption`
  * profiles:
    * `texture_encryption_level1`
    * `texture_encryption_level2`
    * `texture_encryption_level3`
  * parameterization:
    * level 1: encrypt bit planes `0..6`
    * level 2: encrypt bit plane `7` only (`MSB`)
    * level 3: encrypt bit planes `0..7` (`FULL`)
* `texture_block_obscuration`
  * profiles:
    * `texture_block_obscuration_level1`
    * `texture_block_obscuration_level2`
    * `texture_block_obscuration_level3`
  * parameterization:
    * block size `width/32`
    * block size `width/64`
    * block size `width/128`

### Mesh Methods

* `mesh_simplification`
  * profiles: `simpL1`, `simpL5`, `simpL10`
  * parameters:
    * `simpL = 1` with target face ratio `0.9`
    * `simpL = 5` with target face ratio `0.5`
    * `simpL = 10` with target face ratio `0.1`
* `mesh_quantization_position`
  * profiles: `qp7`, `qp9`, `qp11`
  * parameter: Draco position quantization bits `7, 9, 11`
* `mesh_quantization_uv`
  * profiles: `qt6`, `qt8`, `qt10`
  * parameter: Draco UV quantization bits `6, 8, 10`
* `mesh_encryption`
  * profiles:
    * `mesh_encryption_bits17`
    * `mesh_encryption_bits21`
    * `mesh_encryption_bits23`
  * parameter: encrypted mantissa bits `17, 21, 23`
* `mesh_data_hiding`
  * profile:
    * `mesh_data_hiding_bits5`
  * parameter: proxy level `5`
  * note: this is currently a simulated proxy built on the same `3D_encryption` operator, not a dedicated data-hiding implementation

## Current Generation Model

The active config is defined in:

* `DistortionConfig/unified_generation_config.json`

The default recipe plan in the shared copy is:

* `atomic_single_operator`

That means:

* one distortion group is active at a time
* combinations are not generated by default

The total number of atomic profiles currently described is `28` per object:

* texture:
  * `3 + 3 + 3 + 3 + 3 = 15`
* mesh:
  * `3 + 3 + 3 + 3 + 1 = 13`

Some protection groups are intentionally treated as mutually exclusive:

* `texture_encryption` vs `texture_block_obscuration`
* `mesh_encryption` vs `mesh_data_hiding`

## How To Run The Generator

From `Dataset_share_copy/` or from the workspace root:

Run all current method families:

```
python Scripts\orchestrate_distortion_generation.py
```

Run only one family:

```
python Scripts\orchestrate_distortion_generation.py --only-group texture_resize
```

Run a single wrapper directly:

```
python Scripts\generate_texture_resize.py
python Scripts\generate_mesh_simplification.py
```

The main internal entrypoint is:

* `Scripts/generate_distorted_variants.py`

The implementation package is:

* `Scripts/distortion_pipeline/`

## How To Extend The Dataset

There are two common extension cases.

### 1. Add New Objects

1. Create a new folder under `Objects/Originals/<object_id>/`
2. Add the source mesh and material files
3. Add the referenced textures
4. Update `metadata.json`
5. Run the distortion generation scripts

Recommended contents for a reference object folder:

* `model.obj`
* `model.mtl`
* texture files referenced by the `.mtl`

### 2. Add A New Distortion Method

If it is a texture method:

1. Add a new module under `Scripts/distortion_pipeline/methods/texture/`
2. Implement the method body there
3. Register it in `Scripts/distortion_pipeline/methods/texture/__init__.py`
4. Add the group definition to `DistortionConfig/distortion_groups.json`
5. Add a profile source if the method needs discrete parameter levels
6. Optionally add a wrapper script `Scripts/generate_<method>.py`
7. Optionally add it to `Scripts/orchestrate_distortion_generation.py`

If it is a mesh method:

1. Add a new module under `Scripts/distortion_pipeline/methods/mesh/`
2. Implement the method body there
3. Register it in `Scripts/distortion_pipeline/methods/mesh/__init__.py`
4. Add the group definition to `DistortionConfig/distortion_groups.json`
5. Add the profile source if needed
6. Optionally add a wrapper script
7. Optionally add it to the orchestrator

### 3. Add A Combination Of Methods

To generate mixed distortions, add or modify a recipe plan in:

* `DistortionConfig/unified_generation_config.json`

For example, a combination plan can activate several groups at once instead of using the atomic plan.

## Implementation Notes

* Texture generation uses:
  * `Scripts/distortion_pipeline/methods/texture/`
* Mesh generation uses:
  * `Scripts/distortion_pipeline/methods/mesh/`
* Asset parsing and UV-mask generation live in:
  * `Scripts/distortion_pipeline/assets.py`
* Variant materialization and manifest writing live in:
  * `Scripts/distortion_pipeline/variants.py`

## Scope Of This Shared Copy

This shared copy is focused on:

* the dataset itself
* the associated scene assets
* the distortion-generation pipeline
