#!/usr/bin/env bash
# Build the standalone B2L XNU 8796 loader against a prepared Mu workspace.
# The firmware platform itself remains an external dependency.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
: "${WORKSPACE:?Set WORKSPACE to a prepared Mu-Silicium build tree}"
: "${CLANG_BIN:?Set CLANG_BIN to a directory with clang, llvm-lib, llvm-rc and lld-link}"
: "${PYTHON_COMMAND:=python3}"
: "${BUILD_JOBS:=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

export WORKSPACE
export PYTHON_COMMAND
export EDK_TOOLS_PATH="${EDK_TOOLS_PATH:-$WORKSPACE/Mu_Basecore/BaseTools}"
export EDK_TOOLS_BIN="${EDK_TOOLS_BIN:-$EDK_TOOLS_PATH/Source/C/bin}"
export CLANG_BIN="${CLANG_BIN%/}/"
export CONF_PATH="${CONF_PATH:-$WORKSPACE/Conf}"
export PACKAGES_PATH="$WORKSPACE/Mu_Basecore${PACKAGES_PATH:+:$PACKAGES_PATH}"
export PATH="$EDK_TOOLS_PATH/BinWrappers/PosixLike:$EDK_TOOLS_BIN:$CLANG_BIN:$PATH"

for tool in clang llvm-lib llvm-rc lld-link; do
  [[ -x "$CLANG_BIN$tool" ]] || { echo "Missing tool: $CLANG_BIN$tool" >&2; exit 1; }
done
[[ -x "$EDK_TOOLS_BIN/GenFw" ]] || { echo 'Build native BaseTools first.' >&2; exit 1; }

mkdir -p "$CONF_PATH"
for template in tools_def build_rule target; do
  [[ -f "$CONF_PATH/$template.txt" ]] ||
    cp "$EDK_TOOLS_PATH/Conf/$template.template" "$CONF_PATH/$template.txt"
done

# The loader core is shared with the host self-test; stage it beside the app.
rm -rf "$WORKSPACE/B2LXnuPkg"
mkdir -p "$WORKSPACE/B2LXnuPkg/loader"
cp -R "$repo_root/boot/uefi/." "$WORKSPACE/B2LXnuPkg/"
cp -R "$repo_root/boot/loader/." "$WORKSPACE/B2LXnuPkg/loader/"

cd "$WORKSPACE"
"$EDK_TOOLS_PATH/BinWrappers/PosixLike/build" \
  -a AARCH64 -t CLANGPDB -b RELEASE -n "$BUILD_JOBS" \
  -p B2LXnuPkg/B2LXnuPkg.dsc
