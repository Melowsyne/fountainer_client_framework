# Toolchain and runtime image for the fountainer_client.
# Source code, certificates and build directory come in via volumes
# (see docker-compose.yml) — the image deliberately contains no code.
FROM debian:trixie-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ \
        cmake \
        make \
        libboost-dev \
        libboost-system-dev \
        libssl-dev \
        nlohmann-json3-dev \
        catch2 \
        libcatch2-dev \
        openssl \
        python3 \
        python3-websockets \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
