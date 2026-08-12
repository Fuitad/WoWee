#!/usr/bin/env bash
# run-macos.sh - Cross-compile WoWee for macOS (arm64 or x86_64) inside a Docker container.
#
# Usage (run from project root):
#   ./container/run-macos.sh [--rebuild-image]
#
# The macOS SDK is fetched automatically inside the Docker build from Apple's
# public software update catalog.  No manual SDK download required.
#
# Environment variables:
#   MACOS_ARCH          - Target arch: arm64 (default) or x86_64
#   WOWEE_FFX_SDK_REPO  - FidelityFX SDK git repo URL (passed through to container)
#   WOWEE_FFX_SDK_REF   - FidelityFX SDK git ref / tag      (passed through to container)
#   REBUILD_IMAGE       - Set to 1 to force a fresh docker build (same as --rebuild-image)
#
# Toolchain: osxcross (Clang + Apple ld)
# vcpkg triplets: arm64-osx-cross (arm64) / x64-osx-cross (x86_64)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

IMAGE_NAME="wowee-builder-macos"
MACOS_ARCH="${MACOS_ARCH:-arm64}"
BUILD_OUTPUT="${PROJECT_ROOT}/build/macos"

# Parse arguments
REBUILD_IMAGE="${REBUILD_IMAGE:-0}"
for arg in "$@"; do
  case "$arg" in
    --rebuild-image) REBUILD_IMAGE=1 ;;
    *) echo "Unknown argument: $arg" >&2; exit 1 ;;
  esac
done

# Validate arch
if [[ "$MACOS_ARCH" != "arm64" && "$MACOS_ARCH" != "x86_64" ]]; then
  echo "Error: MACOS_ARCH must be 'arm64' or 'x86_64' (got: ${MACOS_ARCH})" >&2
  exit 1
fi

# Verify Docker is available
if ! command -v docker &>/dev/null; then
  echo "Error: docker is not installed or not in PATH." >&2
  exit 1
fi

# Build the image (skip if already present and --rebuild-image not given)
if [[ "$REBUILD_IMAGE" == "1" ]] || ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
  echo "==> Building Docker image: ${IMAGE_NAME}"
  echo "    (SDK will be fetched automatically from Apple's catalog)"
  docker build \
    -f "${SCRIPT_DIR}/builder-macos.Dockerfile" \
    -t "$IMAGE_NAME" \
    "${SCRIPT_DIR}"
else
  echo "==> Using existing Docker image: ${IMAGE_NAME}"
fi

# Create output directory on the host
mkdir -p "$BUILD_OUTPUT"

echo "==> Starting macOS cross-compile build (arch=${MACOS_ARCH}, output: ${BUILD_OUTPUT})"

docker run --rm \
  --mount "type=bind,src=${PROJECT_ROOT},dst=/src,readonly" \
  --mount "type=bind,src=${BUILD_OUTPUT},dst=/out" \
  --env "MACOS_ARCH=${MACOS_ARCH}" \
  ${WOWEE_FFX_SDK_REPO:+--env "WOWEE_FFX_SDK_REPO=${WOWEE_FFX_SDK_REPO}"} \
  ${WOWEE_FFX_SDK_REF:+--env "WOWEE_FFX_SDK_REF=${WOWEE_FFX_SDK_REF}"} \
  "$IMAGE_NAME"

echo "==> macOS cross-compile build complete. Artifacts in: ${BUILD_OUTPUT}"
