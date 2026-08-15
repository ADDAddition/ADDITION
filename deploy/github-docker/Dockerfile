FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Installation des dépendances système
RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    git \
    libssl-dev \
    python3 \
    python3-pip \
    ca-certificates \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

# Compilation de liboqs (nécessaire pour la crypto post-quantique)
WORKDIR /deps
RUN git clone --depth 1 --branch main https://github.com/open-quantum-safe/liboqs.git \
    && cmake -S liboqs -B liboqs/build -GNinja -DOQS_USE_OPENSSL=ON -DBUILD_SHARED_LIBS=ON \
    && cmake --build liboqs/build --target install

# Mise à jour du cache des bibliothèques pour Linux
RUN ldconfig

WORKDIR /app
COPY . /app

# Compilation de la blockchain ADDITION avec détection robuste de liboqs
RUN set -eux; \
        OQS_LIB=""; \
        if [ -f /usr/local/lib/liboqs.so ]; then OQS_LIB=/usr/local/lib/liboqs.so; fi; \
        if [ -z "$OQS_LIB" ] && [ -f /usr/local/lib/liboqs.a ]; then OQS_LIB=/usr/local/lib/liboqs.a; fi; \
        if [ -z "$OQS_LIB" ]; then echo "liboqs binary not found in /usr/local/lib"; exit 1; fi; \
        cmake -S . -B build -GNinja \
            -DCMAKE_BUILD_TYPE=Release \
            -DOQS_INCLUDE_DIR=/usr/local/include \
            -DOQS_LIBRARY="$OQS_LIB"; \
        cmake --build build --target additiond -v

EXPOSE 8545 18545 28545 8080

# Research testnet daemon. This is not a live mainnet.
CMD ["./build/additiond", "--network", "testnet", "--config", "config.toml"]
