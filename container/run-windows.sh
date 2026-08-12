#!/usr/bin/env bash
# run-windows.sh - Cross-compile WoWee for Windows (x86_64) inside a Docker container.
#
# Usage (run from project root):
#   ./container/run-windows.sh [--rebuild-image]
#
# Environment variables:
#   WOWEE_FFX_SDK_REPO  - FidelityFX SDK git repo URL (passed through to container)
#   WOWEE_FFX_SDK_REF   - FidelityFX SDK git ref / tag      (passed through to container)
#   REBUILD_IMAGE       - Set to 1 to force a fresh docker build (same as --rebuild-image)
#
# Toolchain: LLVM-MinGW (Clang + LLD) targeting x86_64-w64-mingw32-ucrt
# vcpkg triplet: x64-mingw-static

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

IMAGE_NAME="wowee-builder-windows"
BUILD_OUTPUT="${PROJECT_ROOT}/build/windows"

# Parse arguments
REBUILD_IMAGE="${REBUILD_IMAGE:-0}"
for arg in "$@"; do
  case "$arg" in
    --rebuild-image) REBUILD_IMAGE=1 ;;
    *) echo "Unknown argument: $arg" >&2; exit 1 ;;
  esac
done

# Verify Docker is available
if ! command -v docker &>/dev/null; then
  echo "Error: docker is not installed or not in PATH." >&2
  exit 1
fi

# Build the image (skip if already present and --rebuild-image not given)
if [[ "$REBUILD_IMAGE" == "1" ]] || ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
  echo "==> Building Docker image: ${IMAGE_NAME}"
  docker build \
    -f "${SCRIPT_DIR}/builder-windows.Dockerfile" \
    -t "$IMAGE_NAME" \
    "${SCRIPT_DIR}"
else
  echo "==> Using existing Docker image: ${IMAGE_NAME}"
fi

# Create output directory on the host
mkdir -p "$BUILD_OUTPUT"

echo "==> Starting Windows cross-compile build (output: ${BUILD_OUTPUT})"

docker run --rm \
  --mount "type=bind,src=${PROJECT_ROOT},dst=/src,readonly" \
  --mount "type=bind,src=${BUILD_OUTPUT},dst=/out" \
  ${WOWEE_FFX_SDK_REPO:+--env "WOWEE_FFX_SDK_REPO=${WOWEE_FFX_SDK_REPO}"} \
  ${WOWEE_FFX_SDK_REF:+--env "WOWEE_FFX_SDK_REF=${WOWEE_FFX_SDK_REF}"} \
  "$IMAGE_NAME"

echo "==> Windows cross-compile build complete. Artifacts in: ${BUILD_OUTPUT}"
