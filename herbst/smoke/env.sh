#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENUSD_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

PYTHON_BIN="${PYTHON_BIN:-/usr/local/bin/python3}"
CMAKE_BIN_DIR="${CMAKE_BIN_DIR:-/Applications/CMake.app/Contents/bin}"
EMSDK_DIR="${EMSDK_DIR:-/Users/herbst/git/emsdk}"

OPENUSD_NATIVE_PREFIX="${OPENUSD_NATIVE_PREFIX:-/Users/herbst/OpenUSD-26.05-native}"
OPENUSD_WASM_PREFIX="${OPENUSD_WASM_PREFIX:-/Users/herbst/OpenUSD-26.05-wasm}"
OPENUSD_WASM_HYDRA_BUILD_DIR="${OPENUSD_WASM_HYDRA_BUILD_DIR:-/Users/herbst/OpenUSD-26.05-wasm-hydra-exp-build}"
OPENUSD_WASM_HYDRA_PREFIX="${OPENUSD_WASM_HYDRA_PREFIX:-/Users/herbst/OpenUSD-26.05-wasm-hydra-exp}"
OPENUSD_WASM_HYDRA_MTLX_BUILD_DIR="${OPENUSD_WASM_HYDRA_MTLX_BUILD_DIR:-/Users/herbst/OpenUSD-26.05-wasm-hydra-mtlx-probe-build}"
OPENUSD_WASM_HYDRA_MTLX_PREFIX="${OPENUSD_WASM_HYDRA_MTLX_PREFIX:-/Users/herbst/OpenUSD-26.05-wasm-hydra-mtlx-probe}"
MATERIALX_REPO="${MATERIALX_REPO:-/Users/herbst/git/MaterialX}"
MATERIALX_WASM_OPENUSD_BUILD_DIR="${MATERIALX_WASM_OPENUSD_BUILD_DIR:-/Users/herbst/MaterialX-1.39.5-wasm-openusd-build}"
MATERIALX_WASM_OPENUSD_PREFIX="${MATERIALX_WASM_OPENUSD_PREFIX:-/Users/herbst/MaterialX-1.39.5-wasm-openusd}"
ADOBE_PLUGIN_REPO="${ADOBE_PLUGIN_REPO:-/Users/herbst/git/USD-Fileformat-plugins}"
ADOBE_PLUGIN_PREFIX="${ADOBE_PLUGIN_PREFIX:-/Users/herbst/USD-Fileformat-plugins-2026.03-openusd-26.05}"
ADOBE_PLUGIN_WASM_PREFIX="${ADOBE_PLUGIN_WASM_PREFIX:-/Users/herbst/USD-Fileformat-plugins-2026.03-wasm-probe}"

export PATH="${CMAKE_BIN_DIR}:${PATH}"

activate_emsdk() {
  # shellcheck source=/dev/null
  source "${EMSDK_DIR}/emsdk_env.sh" >/tmp/emsdk-env.log
  export EMSCRIPTEN="${EMSCRIPTEN:-${EMSDK_DIR}/upstream/emscripten}"
  export PATH="${CMAKE_BIN_DIR}:${PATH}"
}

native_env() {
  export PYTHONPATH="${OPENUSD_NATIVE_PREFIX}/lib/python"
  export PATH="${OPENUSD_NATIVE_PREFIX}/bin:${PATH}"
  export DYLD_LIBRARY_PATH="${OPENUSD_NATIVE_PREFIX}/lib${DYLD_LIBRARY_PATH:+:${DYLD_LIBRARY_PATH}}"
}

adobe_plugin_env() {
  native_env
  export DYLD_LIBRARY_PATH="${ADOBE_PLUGIN_PREFIX}/plugin/usd:${ADOBE_PLUGIN_PREFIX}/lib:${OPENUSD_NATIVE_PREFIX}/lib:/opt/homebrew/lib${DYLD_LIBRARY_PATH:+:${DYLD_LIBRARY_PATH}}"
  export PXR_PLUGINPATH_NAME="${ADOBE_PLUGIN_PREFIX}/plugin/usd"
}
