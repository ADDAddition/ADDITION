#!/usr/bin/env bash
# Idempotent bootstrap for the ADDITION (ADD) post-quantum L1 node.
#
# Builds ./build/additiond. Safe to run repeatedly: system dependencies and
# liboqs are only installed when missing, and the CMake build is incremental.
#
# On the saved Cloud Agent base (snapshot) the toolchain, OpenSSL headers, and
# liboqs are already present, so this script just compiles. It also works from a
# bare Ubuntu image by installing everything it needs.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

LIBOQS_VERSION="0.12.0"

sudo_if_needed() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    else
        sudo "$@"
    fi
}

# 1) System toolchain + OpenSSL headers (only if something is missing).
if ! command -v cmake >/dev/null 2>&1 \
    || ! command -v g++ >/dev/null 2>&1 \
    || ! command -v ninja >/dev/null 2>&1 \
    || [ ! -f /usr/include/openssl/ssl.h ]; then
    echo "Installing build toolchain and OpenSSL headers..."
    export DEBIAN_FRONTEND=noninteractive
    sudo_if_needed apt-get update
    sudo_if_needed apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl ca-certificates \
        pkg-config libssl-dev python3
fi

# 2) liboqs (Open Quantum Safe) — provides ML-DSA-87 signatures. Build from
#    source only when it is not already installed.
if ! ldconfig -p | grep -q 'liboqs'; then
    echo "Building liboqs ${LIBOQS_VERSION} from source..."
    tmp_dir="$(mktemp -d)"
    git clone --depth 1 --branch "${LIBOQS_VERSION}" \
        https://github.com/open-quantum-safe/liboqs.git "${tmp_dir}/liboqs"
    cmake -GNinja -S "${tmp_dir}/liboqs" -B "${tmp_dir}/liboqs/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DOQS_BUILD_ONLY_LIB=ON \
        -DCMAKE_INSTALL_PREFIX=/usr/local
    cmake --build "${tmp_dir}/liboqs/build"
    sudo_if_needed cmake --install "${tmp_dir}/liboqs/build"
    sudo_if_needed ldconfig
    rm -rf "${tmp_dir}"
fi

# 3) Configure and build the node (incremental).
# The repository ships a prebuilt Windows ./build directory. A CMake cache from
# another machine/OS breaks configuration, so drop it if it does not belong to
# this checkout; a matching Linux cache is kept for fast incremental rebuilds.
if [ -f build/CMakeCache.txt ] \
    && ! grep -qx "CMAKE_HOME_DIRECTORY:INTERNAL=${repo_root}" build/CMakeCache.txt; then
    echo "Removing stale/foreign CMake cache in ./build..."
    rm -rf build
fi

cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=g++
cmake --build build -j"$(nproc)"

echo "Build complete: ${repo_root}/build/additiond"
