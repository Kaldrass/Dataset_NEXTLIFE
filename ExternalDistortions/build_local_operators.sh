#!/usr/bin/env bash
# Run inside WSL. Build separate binaries; preserve the repository executables.
set -eu
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
output_dir="${1:-$repo_root/.tmp/operators-$(date -u +%Y%m%dT%H%M%SZ)}"
if [ -e "$output_dir" ]; then
    echo "Output already exists: $output_dir" >&2
    exit 1
fi
mkdir -p "$output_dir"
cd "$repo_root"
g++ -std=c++17 -O2 ExternalDistortions/operators/aes/aes_image_tool.cpp -o "$output_dir/aes_image_tool" $(pkg-config --cflags --libs opencv4) -lcryptopp
g++ -std=c++17 -O2 ExternalDistortions/operators/blurring/blurring_mask.cpp -o "$output_dir/masked_blur" $(pkg-config --cflags --libs opencv4)
g++ -std=c++17 -O2 ExternalDistortions/operators/block_all_operation/block_all_operation.cpp -o "$output_dir/block_all_operation" $(pkg-config --cflags --libs opencv4)
g++ -std=gnu++17 -O2 -I 3D_encryption/include -I 3D_encryption/third_party 3D_encryption/main.cpp 3D_encryption/src/*.cpp 3D_encryption/third_party_src/*.cpp -o "$output_dir/mesh_encryption" -lcryptopp -lboost_iostreams
echo "Built operators in $output_dir"
