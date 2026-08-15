# Base image for the ADDITION (ADD) post-quantum Layer 1 blockchain.
# Provides the C++20 toolchain, OpenSSL, and a from-source build of liboqs
# (Open Quantum Safe), which supplies the ML-DSA-87 signatures the node uses.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Stable system toolchain and libraries. git and curl are required so Cursor can
# check out the repository and so this image can fetch liboqs sources.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        curl \
        ca-certificates \
        pkg-config \
        libssl-dev \
        python3 \
    && rm -rf /var/lib/apt/lists/*

# Build and install liboqs (pinned release) into /usr/local. The node links
# against liboqs for post-quantum ML-DSA-87 (Dilithium) signatures.
ARG LIBOQS_VERSION=0.12.0
RUN git clone --depth 1 --branch "${LIBOQS_VERSION}" \
        https://github.com/open-quantum-safe/liboqs.git /tmp/liboqs \
    && cmake -GNinja -S /tmp/liboqs -B /tmp/liboqs/build \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DOQS_BUILD_ONLY_LIB=ON \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build /tmp/liboqs/build \
    && cmake --install /tmp/liboqs/build \
    && ldconfig \
    && rm -rf /tmp/liboqs

# Run as the non-root user that ships with the Ubuntu base image.
USER ubuntu
