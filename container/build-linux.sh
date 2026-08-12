#!/bin/bash
# Linux amd64 build entrypoint - runs INSIDE the linux container.
# Bind-mounts:
#   /src  (ro) - project source
#   /out  (rw) - host ./build/linux

set -euo pipefail

SRC=/src
OUT=/out
NPROC=$(nproc)

echo "==> [linux] Copying source tree..."
mkdir -p /wowee-build-src
tar -C "${SRC}" \
    --exclude='./build' --exclude='./logs' --exclude='./cache' \
    --exclude='./container' --exclude='./.git' \
    --exclude='./Data/character' --exclude='./Data/creature' \
    --exclude='./Data/db' --exclude='./Data/environment' \
    --exclude='./Data/interface' --exclude='./Data/item' \
    --exclude='./Data/misc' --exclude='./Data/sound' \
    --exclude='./Data/spell' --exclude='./Data/terrain' \
    --exclude='./Data/world' \
    -cf - . | tar -C /wowee-build-src -xf -

cd /wowee-build-src

echo "==> [linux] Fetching external SDKs (if needed)..."
if [ ! -f extern/FidelityFX-FSR2/src/ffx-fsr2-api/ffx_fsr2.h ]; then
    git clone --depth 1 \
        https://github.com/GPUOpen-Effects/FidelityFX-FSR2.git \
        extern/FidelityFX-FSR2 || echo "Warning: FSR2 clone failed - continuing without FSR2"
fi

SDK_REPO="${WOWEE_FFX_SDK_REPO:-https://github.com/Kelsidavis/FidelityFX-SDK.git}"
SDK_REF="${WOWEE_FFX_SDK_REF:-main}"
if [ ! -f "extern/FidelityFX-SDK/sdk/include/FidelityFX/host/ffx_frameinterpolation.h" ]; then
    git clone --depth 1 --branch "${SDK_REF}" "${SDK_REPO}" extern/FidelityFX-SDK \
        || echo "Warning: FidelityFX-SDK clone failed - continuing without FSR3"
fi

echo "==> [linux] Configuring with CMake (Release, Ninja, amd64)..."
cmake -S . -B "${OUT}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON

echo "==> [linux] Building with ${NPROC} cores..."
cmake --build "${OUT}" --parallel "${NPROC}"

echo "==> [linux] Creating Data symlink..."
mkdir -p "${OUT}/bin"
if [ ! -e "${OUT}/bin/Data" ]; then
    # Relative symlink so it resolves correctly on the host:
    # build/linux/bin/Data -> ../../../Data (project root)
    ln -s ../../../Data "${OUT}/bin/Data"
fi

echo ""
echo "==> [linux] Build complete. Artifacts in: ./build/linux/"
echo "    Binary: ./build/linux/bin/wowee"
