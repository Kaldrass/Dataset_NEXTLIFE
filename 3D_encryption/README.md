# 3D PLY-OBJ basics SelectiveEncryption & Secret Geometric Deformations per Axis (C++)

## Overview

This project implements basics for import/export PLY & OBJ 3D objects with selective encryption-decryption based on **XOR** and **AES** algorithms with secret geometric deformations with scaling factors using interpolation functions per axis.

## Dependencies

- **Nanoflann Library** (Static)
- **gnuplot-iostream Library** (Static)
- **Nlohmann/json Library** (Static)
- **Happly Library** (Static)
- **Tiny Obj Loader** (Static)
- **Crypto++ Library**: Install via:
```bash
  sudo apt-get install libcrypto++-dev
```

## Compilation

Based on a MakeFile : 
```bash
  make clean && make -j && ./app params.json 
```

## JSON parameters 
```json
{
    "outputFolder": "data_results/",
    "meshPath": "/mnt/c/Users/adubar/Documents/Dataset/Models/Unzip/OBJ/Banana_obj/scene.obj",
    "nbBytesKey": 32,
    "nbBytesIV": 32,
    "nbAttempts": 1,
    "save": {"orig": false, "enc": false, "dec": false},
    "plot": {"orig": false, "enc": false, "dec": false},
    "ranges": {"minRange": 0.01, "maxRange": 0.15, "idBand": 0.1},
    "oscillatory": {"f0Min": 2.0, "f0Max": 10.0, "f1Cycles": 2.0, "dMax": 1.6},
    "monotonic": {"aMin": 0.5, "aMax": 2.0, "bMin": 0.5, "bMax": 2.0}
}
```

## Warnings

**Byte Order**:
  Selective encryption assume Little Endian byte order (from LSB to MSB), and PLY & OBJ 3D object are loaded in **Float** format (32-bit) : S | E(08) | M(23) (Sign | Exponent | Mantissa).