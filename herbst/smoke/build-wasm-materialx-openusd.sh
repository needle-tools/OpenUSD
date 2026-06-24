#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

activate_emsdk

if [[ ! -d "${MATERIALX_REPO}" ]]; then
  echo "MaterialX repo not found: ${MATERIALX_REPO}" >&2
  exit 1
fi

rm -rf "${MATERIALX_WASM_OPENUSD_BUILD_DIR}" "${MATERIALX_WASM_OPENUSD_PREFIX}"

emcmake cmake \
  -S "${MATERIALX_REPO}" \
  -B "${MATERIALX_WASM_OPENUSD_BUILD_DIR}" \
  -DCMAKE_INSTALL_PREFIX="${MATERIALX_WASM_OPENUSD_PREFIX}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DMATERIALX_BUILD_SHARED_LIBS=OFF \
  -DMATERIALX_BUILD_TESTS=OFF \
  -DMATERIALX_BUILD_PYTHON=OFF \
  -DMATERIALX_BUILD_JS=OFF \
  -DMATERIALX_BUILD_VIEWER=OFF \
  -DMATERIALX_BUILD_GRAPH_EDITOR=OFF \
  -DMATERIALX_BUILD_GEN_GLSL=OFF \
  -DMATERIALX_BUILD_GEN_OSL=OFF \
  -DMATERIALX_BUILD_GEN_MDL=OFF \
  -DMATERIALX_BUILD_GEN_MSL=OFF \
  -DMATERIALX_BUILD_GEN_SLANG=OFF \
  -DMATERIALX_BUILD_RENDER=OFF \
  -DMATERIALX_BUILD_RENDER_PLATFORMS=OFF \
  -DMATERIALX_BUILD_DATA_LIBRARY=OFF \
  -DMATERIALX_INSTALL_RESOURCES=OFF \
  -DCMAKE_CXX_FLAGS="-pthread" \
  -DCMAKE_C_FLAGS="-pthread" \
  -DCMAKE_EXE_LINKER_FLAGS="-pthread"

cmake --build "${MATERIALX_WASM_OPENUSD_BUILD_DIR}" --parallel "${JOBS:-8}"
cmake --install "${MATERIALX_WASM_OPENUSD_BUILD_DIR}"

materialx_config="${MATERIALX_WASM_OPENUSD_PREFIX}/lib/cmake/MaterialX/MaterialXConfig.cmake"
if [[ -f "${materialx_config}" ]]; then
  perl -0pi -e 's/if\(UNIX AND NOT APPLE\)/if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)/' "${materialx_config}"
fi

cmake -E copy_directory \
  "${MATERIALX_REPO}/libraries" \
  "${MATERIALX_WASM_OPENUSD_PREFIX}/libraries"

echo "Installed wasm MaterialX dependency for OpenUSD at ${MATERIALX_WASM_OPENUSD_PREFIX}"
