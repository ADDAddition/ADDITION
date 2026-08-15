#!/usr/bin/env bash
# Idempotent repository bootstrap for the ADDITION (ADD) node.
# System dependencies (toolchain, OpenSSL, liboqs) come from the base image;
# this script only compiles the C++ sources into ./build/additiond.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

# Safety net: if this ever runs on a base without liboqs (e.g. the default
# image), build it from source so the CMake configure step can find it.
if ! ldconfig -p | grep -q 'liboqs'; then
    echo "liboqs not found; building it from source..."
    tmp_dir="$(mktemp -d)"
    git clone --depth 1 --branch 0.12.0 \
        https://github.com/open-quantum-safe/liboqs.git "${tmp_dir}/liboqs"
    cmake -GNinja -S "${tmp_dir}/liboqs" -B "${tmp_dir}/liboqs/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DOQS_BUILD_ONLY_LIB=ON \
        -DCMAKE_INSTALL_PREFIX=/usr/local
    cmake --build "${tmp_dir}/liboqs/build"
    sudo cmake --install "${tmp_dir}/liboqs/build"
    sudo ldconfig
    rm -rf "${tmp_dir}"
fi

# Configure and build the node. Reusing ./build keeps this incremental and
# safe to run repeatedly.
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=g++
cmake --build build -j"$(nproc)"

echo "Build complete: ${repo_root}/build/additiond"
