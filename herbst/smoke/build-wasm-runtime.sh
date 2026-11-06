#!/usr/bin/env bash
# Builds one complete usd-viewer wasm runtime from source:
#   WASM_RUNTIME=asyncify ./herbst/smoke/build-wasm-runtime.sh   -> emHdBindings.js/.wasm
#   WASM_RUNTIME=jspi     ./herbst/smoke/build-wasm-runtime.sh   -> emHdBindings.jspi.js/.wasm
# Order: oneTBB, OpenSubdiv, MaterialX, OpenUSD (installed once without the glTF plugin
# so the plugin can build against it), Adobe usdGltf with Draco, then OpenUSD again
# with the plugin linked. Every library uses the runtime's exception model.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/env.sh"

if ! git -C "${OPENUSD_ROOT}" diff --quiet HEAD --; then
  echo "OpenUSD has uncommitted changes; commit them so the build info names the source." >&2
  exit 1
fi

echo "== ${WASM_RUNTIME}: oneTBB" && "${SCRIPT_DIR}/build-wasm-onetbb.sh"
echo "== ${WASM_RUNTIME}: OpenSubdiv" && "${SCRIPT_DIR}/build-wasm-opensubdiv.sh"
echo "== ${WASM_RUNTIME}: MaterialX" && "${SCRIPT_DIR}/build-wasm-materialx-openusd.sh"
echo "== ${WASM_RUNTIME}: OpenUSD without the glTF plugin"
rm -rf "${OPENUSD_WASM_HYDRA_MTLX_BUILD_DIR}" "${OPENUSD_WASM_HYDRA_MTLX_PREFIX}" "${ADOBE_PLUGIN_WASM_PREFIX}" "${ADOBE_PLUGIN_WASM_PREFIX}-build"
ENABLE_GLTF_PLUGIN=0 "${SCRIPT_DIR}/configure-wasm-hydra-materialx.sh"
"${SCRIPT_DIR}/build-wasm-hydra-materialx.sh"
echo "== ${WASM_RUNTIME}: Adobe usdGltf" && "${SCRIPT_DIR}/build-wasm-adobe-gltf-plugin.sh"
echo "== ${WASM_RUNTIME}: OpenUSD with the glTF plugin"
"${SCRIPT_DIR}/configure-wasm-hydra-materialx.sh"
"${SCRIPT_DIR}/build-wasm-hydra-materialx.sh"
echo "Built ${WASM_BINDINGS_NAME} into ${OPENUSD_WASM_HYDRA_MTLX_PREFIX}/bin"
