#!/usr/bin/env bash
set -euo pipefail

OPENUSD_PREFIX="${OPENUSD_PREFIX:-/Users/herbst/OpenUSD-26.05-native-x86_64}"
RMANTREE="${RMANTREE:-/Applications/Pixar/RenderManProServer-27.2}"
PYTHON_BIN="${PYTHON_BIN:-/usr/local/bin/python3}"

test -x "${RMANTREE}/bin/prman"
test -x "${OPENUSD_PREFIX}/bin/usdview"
file "${OPENUSD_PREFIX}/bin/usdcat" | grep -q 'x86_64'

export RMANTREE
export PATH="${RMANTREE}/bin:${OPENUSD_PREFIX}/bin:${PATH}"
export PYTHONPATH="${OPENUSD_PREFIX}/lib/python"
export DYLD_LIBRARY_PATH="${OPENUSD_PREFIX}/lib:${RMANTREE}/lib"
export RMAN_SHADERPATH="${RMANTREE}/lib/shaders:${OPENUSD_PREFIX}/plugin/usd/resources/shaders"
export RMAN_RIXPLUGINPATH="${RMANTREE}/lib/plugins"
export RMAN_TEXTUREPATH="${RMANTREE}/lib/textures:${RMANTREE}/lib/plugins:${OPENUSD_PREFIX}/plugin/usd"
export RMAN_DISPLAYPATH="${RMANTREE}/lib/plugins"
export RMAN_PROCEDURALPATH="${RMANTREE}/lib/plugins"

# The inherited Adobe plugin is arm64 and cannot be loaded by this process.
unset PXR_PLUGINPATH_NAME

exec arch -x86_64 "${PYTHON_BIN}" "${OPENUSD_PREFIX}/bin/usdview" "$@"
