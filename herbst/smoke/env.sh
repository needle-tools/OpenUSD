#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENUSD_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

PYTHON_BIN="${PYTHON_BIN:-/usr/local/bin/python3}"
CMAKE_BIN_DIR="${CMAKE_BIN_DIR:-/Applications/CMake.app/Contents/bin}"
EMSDK_DIR="${EMSDK_DIR:-${HOME}/git/emsdk}"

# WASM_RUNTIME selects the async runtime and C++ exception model of the whole build:
#   asyncify  ASYNCIFY, JavaScript-based exceptions (emHdBindings.js/.wasm)
#   jspi      JSPI, native WebAssembly exceptions (emHdBindings.jspi.js/.wasm)
# Every static library linked into one runtime must use that runtime's exception
# model, so each runtime has its own dependency, MaterialX and glTF plugin prefixes.
WASM_RUNTIME="${WASM_RUNTIME:-asyncify}"
case "${WASM_RUNTIME}" in
  asyncify)
    PXR_EMSCRIPTEN_RUNTIME=ASYNCIFY
    WASM_EXCEPTION_FLAGS=""
    WASM_RUNTIME_SUFFIX=""
    WASM_BINDINGS_NAME=emHdBindings
    ;;
  jspi)
    PXR_EMSCRIPTEN_RUNTIME=JSPI
    WASM_EXCEPTION_FLAGS="-fwasm-exceptions"
    WASM_RUNTIME_SUFFIX="-jspi"
    WASM_BINDINGS_NAME=emHdBindings.jspi
    ;;
  *)
    echo "WASM_RUNTIME must be asyncify or jspi, not '${WASM_RUNTIME}'" >&2
    exit 1
    ;;
esac
WASM_COMPILE_FLAGS="-pthread --use-port=zlib${WASM_EXCEPTION_FLAGS:+ ${WASM_EXCEPTION_FLAGS}}"

OPENUSD_NATIVE_PREFIX="${OPENUSD_NATIVE_PREFIX:-${HOME}/OpenUSD-26.11-native}"
OPENUSD_WASM_PREFIX="${OPENUSD_WASM_PREFIX:-${HOME}/OpenUSD-26.11-wasm}"
# Standalone TBB/OpenSubdiv dependencies may be reused across USD releases.
# Keep this separate from the install prefix for the new USD ABI.
OPENUSD_WASM_DEPENDENCIES_PREFIX="${OPENUSD_WASM_DEPENDENCIES_PREFIX:-${HOME}/OpenUSD-wasm-dependencies${WASM_RUNTIME_SUFFIX}}"
OPENUSD_WASM_HYDRA_BUILD_DIR="${OPENUSD_WASM_HYDRA_BUILD_DIR:-${HOME}/OpenUSD-26.11-wasm-hydra-exp-build}"
OPENUSD_WASM_HYDRA_PREFIX="${OPENUSD_WASM_HYDRA_PREFIX:-${HOME}/OpenUSD-26.11-wasm-hydra-exp}"
OPENUSD_WASM_HYDRA_MTLX_BUILD_DIR="${OPENUSD_WASM_HYDRA_MTLX_BUILD_DIR:-${HOME}/OpenUSD-26.11-wasm-hydra-mtlx${WASM_RUNTIME_SUFFIX}-build}"
OPENUSD_WASM_HYDRA_MTLX_PREFIX="${OPENUSD_WASM_HYDRA_MTLX_PREFIX:-${HOME}/OpenUSD-26.11-wasm-hydra-mtlx${WASM_RUNTIME_SUFFIX}}"
# Pinned dependency sources; override these locations for a different build layout.
WASM_SOURCES_DIR="${WASM_SOURCES_DIR:-${HOME}/OpenUSD-wasm-sources}"
ONETBB_ZIP="${ONETBB_ZIP:-${WASM_SOURCES_DIR}/oneTBB-v2021.12.0.zip}"
OPENSUBDIV_ZIP="${OPENSUBDIV_ZIP:-${WASM_SOURCES_DIR}/OpenSubdiv-v3_6_1.zip}"
DRACO_SOURCE_DIR="${DRACO_SOURCE_DIR:-${WASM_SOURCES_DIR}/draco-1.5.6}"
TINYGLTF_SOURCE_DIR="${TINYGLTF_SOURCE_DIR:-${WASM_SOURCES_DIR}/tinygltf-4bfc1fc}"
# MaterialX 1.39.5 at 290d74c745d0708ce221c2f819dc87ee26b3bb43 (a worktree of the MaterialX fork).
MATERIALX_REPO="${MATERIALX_REPO:-${WASM_SOURCES_DIR}/MaterialX-290d74c}"
MATERIALX_WASM_OPENUSD_BUILD_DIR="${MATERIALX_WASM_OPENUSD_BUILD_DIR:-${HOME}/MaterialX-1.39.5-wasm-openusd${WASM_RUNTIME_SUFFIX}-build}"
MATERIALX_WASM_OPENUSD_PREFIX="${MATERIALX_WASM_OPENUSD_PREFIX:-${HOME}/MaterialX-1.39.5-wasm-openusd${WASM_RUNTIME_SUFFIX}}"
ADOBE_PLUGIN_REPO="${ADOBE_PLUGIN_REPO:-${HOME}/git/USD-Fileformat-plugins}"
ADOBE_PLUGIN_PREFIX="${ADOBE_PLUGIN_PREFIX:-${HOME}/USD-Fileformat-plugins-2026.03-openusd-26.11}"
ADOBE_PLUGIN_WASM_PREFIX="${ADOBE_PLUGIN_WASM_PREFIX:-${HOME}/USD-Fileformat-plugins-2026.03-wasm-openusd-26.11${WASM_RUNTIME_SUFFIX}}"
DRACO_WASM_PREFIX="${DRACO_WASM_PREFIX:-${ADOBE_PLUGIN_WASM_PREFIX}}"

# Keep diagnostics useful without embedding the machine's checkout or install paths.
# Apply broad mappings first: Clang uses the last matching prefix.
wasm_map_build_path() {
  local source="$1" destination="$2" flag
  if [[ -d "${source}" ]]; then
    source="$(cd "${source}" && pwd -P)"
  fi
  printf -v flag '%q' "-ffile-prefix-map=${source}=${destination}"
  WASM_COMPILE_FLAGS+=" ${flag}"
}

wasm_map_build_path "${HOME}" /build
wasm_map_build_path "${EMSDK_DIR}" /opt/emsdk
wasm_map_build_path "${OPENUSD_WASM_DEPENDENCIES_PREFIX}" /opt/dependencies
wasm_map_build_path "${OPENUSD_WASM_HYDRA_MTLX_PREFIX}" /opt/OpenUSD
wasm_map_build_path "${OPENUSD_WASM_HYDRA_MTLX_BUILD_DIR}" /build/OpenUSD
wasm_map_build_path "${MATERIALX_WASM_OPENUSD_PREFIX}" /opt/MaterialX
wasm_map_build_path "${MATERIALX_WASM_OPENUSD_BUILD_DIR}" /build/MaterialX
wasm_map_build_path "${MATERIALX_REPO}" /src/MaterialX
wasm_map_build_path "${ADOBE_PLUGIN_WASM_PREFIX}" /opt/usdGltf
wasm_map_build_path "${ADOBE_PLUGIN_WASM_PREFIX}-build" /build/usdGltf
wasm_map_build_path "${ADOBE_PLUGIN_REPO}" /src/usdGltf
wasm_map_build_path "${DRACO_SOURCE_DIR}" /src/draco
wasm_map_build_path "${TINYGLTF_SOURCE_DIR}" /src/tinygltf
wasm_map_build_path "${ONETBB_SRC_PARENT:-${HOME}/oneTBB-2021.12.0-wasm${WASM_RUNTIME_SUFFIX}-src}/oneTBB-2021.12.0" /src/oneTBB
wasm_map_build_path "${ONETBB_BUILD_DIR:-${HOME}/oneTBB-2021.12.0-wasm${WASM_RUNTIME_SUFFIX}-build}" /build/oneTBB
wasm_map_build_path "${OPENSUBDIV_SRC_DIR:-${OPENSUBDIV_SRC_PARENT:-${HOME}/OpenSubdiv-3_6_1-wasm${WASM_RUNTIME_SUFFIX}-src}/OpenSubdiv-3_6_1}" /src/OpenSubdiv
wasm_map_build_path "${OPENSUBDIV_BUILD_DIR:-${HOME}/OpenSubdiv-3_6_1-wasm${WASM_RUNTIME_SUFFIX}-build}" /build/OpenSubdiv
wasm_map_build_path "${OPENUSD_ROOT}" /src/OpenUSD

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
