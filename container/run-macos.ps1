# run-macos.ps1 - Cross-compile WoWee for macOS (arm64 or x86_64) inside a Docker container.
#
# Usage (run from project root):
#   .\container\run-macos.ps1 [-RebuildImage] [-Arch arm64|x86_64]
#
# The macOS SDK is fetched automatically inside the Docker build from Apple's
# public software update catalog.  No manual SDK download required.
#
# Environment variables:
#   WOWEE_FFX_SDK_REPO  - FidelityFX SDK git repo URL (passed through to container)
#   WOWEE_FFX_SDK_REF   - FidelityFX SDK git ref / tag      (passed through to container)

param(
    [switch]$RebuildImage,
    [ValidateSet("arm64", "x86_64")]
    [string]$Arch = "arm64"
)

$ErrorActionPreference = "Stop"

$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectRoot = (Resolve-Path "$ScriptDir\..").Path

$ImageName   = "wowee-builder-macos"
$BuildOutput = "$ProjectRoot\build\macos"

# Verify Docker is available
if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Write-Error "docker is not installed or not in PATH."
    exit 1
}

# Build the image (skip if already present and -RebuildImage not given)
$imageExists = docker image inspect $ImageName 2>$null
if ($RebuildImage -or -not $imageExists) {
    Write-Host "==> Building Docker image: $ImageName"
    Write-Host "    (SDK will be fetched automatically from Apple's catalog)"
    docker build `
        -f "$ScriptDir\builder-macos.Dockerfile" `
        -t $ImageName `
        "$ScriptDir"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    Write-Host "==> Using existing Docker image: $ImageName"
}

# Create output directory on the host
New-Item -ItemType Directory -Force -Path $BuildOutput | Out-Null

Write-Host "==> Starting macOS cross-compile build (arch=$Arch, output: $BuildOutput)"

$dockerArgs = @(
    "run", "--rm",
    "--mount", "type=bind,src=$ProjectRoot,dst=/src,readonly",
    "--mount", "type=bind,src=$BuildOutput,dst=/out",
    "--env", "MACOS_ARCH=$Arch"
)

if ($env:WOWEE_FFX_SDK_REPO) {
    $dockerArgs += @("--env", "WOWEE_FFX_SDK_REPO=$env:WOWEE_FFX_SDK_REPO")
}
if ($env:WOWEE_FFX_SDK_REF) {
    $dockerArgs += @("--env", "WOWEE_FFX_SDK_REF=$env:WOWEE_FFX_SDK_REF")
}

$dockerArgs += $ImageName

& docker @dockerArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "==> macOS cross-compile build complete. Artifacts in: $BuildOutput"
