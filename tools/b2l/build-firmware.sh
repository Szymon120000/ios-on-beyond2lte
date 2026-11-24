#!/usr/bin/env bash
# Embed the B2L loader in the external beyond2lte firmware profile and build
# the 4 MiB FD.  The platform overlay and patch script stay external inputs.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

: "${WORKSPACE:?Set WORKSPACE to a prepared Mu-Silicium tree}"
: "${LOADER:?Set LOADER to the built XnuLoader.efi}"
: "${B2L_PREPARE:=$repo_root/tools/b2l/prepare-boot-firmware.py}"
: "${CLANG_BIN:?Set CLANG_BIN to a directory with clang and lld-link}"
: "${PYTHON_COMMAND:=python3}"
: "${BUILD_JOBS:=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
: "${B2L_LOADER_GUID:=92742D9E-1A4F-42F8-9DC9-53895297B2F4}"

export WORKSPACE
export PYTHON_COMMAND
export EDK_TOOLS_PATH="${EDK_TOOLS_PATH:-$WORKSPACE/Mu_Basecore/BaseTools}"
export EDK_TOOLS_BIN="${EDK_TOOLS_BIN:-$EDK_TOOLS_PATH/Source/C/bin}"
export CLANG_BIN="${CLANG_BIN%/}/"
export CONF_PATH="${CONF_PATH:-$WORKSPACE/Conf}"
export PACKAGES_PATH="$WORKSPACE/Platforms/Samsung:$WORKSPACE/Common/Mu:$WORKSPACE/Common/Mu_OEM_Sample:$WORKSPACE/Silicon/Samsung:$WORKSPACE/Silicon/Silicium:$WORKSPACE/Silicium-ACPI:$WORKSPACE/Silicium-ACPI/Platforms/Samsung:$WORKSPACE/Silicium-ACPI/SoCs/Samsung:$WORKSPACE/Mu_Basecore"
export PATH="$EDK_TOOLS_PATH/BinWrappers/PosixLike:$EDK_TOOLS_BIN:$CLANG_BIN:$PATH"

[[ -x "$EDK_TOOLS_BIN/GenFw" ]] || { echo 'Build native BaseTools first.' >&2; exit 1; }
[[ -f "$WORKSPACE/Platforms/Samsung/beyond2ltePkg/beyond2lte.dsc" ]] ||
  { echo 'beyond2lte platform overlay is missing' >&2; exit 1; }
[[ -f "$LOADER" ]] || { echo "Missing loader: $LOADER" >&2; exit 1; }

"$PYTHON_COMMAND" "$B2L_PREPARE" \
  --mu "$WORKSPACE" --loader "$LOADER" --loader-guid "$B2L_LOADER_GUID"

cd "$WORKSPACE"
"$EDK_TOOLS_PATH/BinWrappers/PosixLike/build" \
  -a AARCH64 -t CLANGPDB -b RELEASE -n "$BUILD_JOBS" \
  -p beyond2ltePkg/beyond2lte.dsc \
  -D FD_BASE=0xC9000000 -D FD_SIZE=0x400000 -D FD_BLOCKS=0x400 \
  -D ENABLE_SECUREBOOT=0 -D DEVICE_MODEL=0

FD="$WORKSPACE/Build/beyond2ltePkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd"
[[ -f "$FD" ]] || { echo "Firmware FD missing: $FD" >&2; exit 1; }
echo "B2L firmware FD: $FD"
