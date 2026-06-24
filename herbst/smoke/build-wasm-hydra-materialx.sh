#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

activate_emsdk

cmake --build "${OPENUSD_WASM_HYDRA_MTLX_BUILD_DIR}" \
  --target emHdBindings \
  --parallel "${JOBS:-8}"

mkdir -p \
  "${OPENUSD_WASM_HYDRA_MTLX_PREFIX}/bin" \
  "${OPENUSD_WASM_HYDRA_MTLX_PREFIX}/share/hdEmscripten"

cp "${OPENUSD_WASM_HYDRA_MTLX_BUILD_DIR}/pxr/usdImaging/hdEmscripten/emHdBindings.js" \
  "${OPENUSD_WASM_HYDRA_MTLX_PREFIX}/bin/"
cp "${OPENUSD_WASM_HYDRA_MTLX_BUILD_DIR}/pxr/usdImaging/hdEmscripten/emHdBindings.data" \
  "${OPENUSD_WASM_HYDRA_MTLX_PREFIX}/bin/"
cp "${OPENUSD_WASM_HYDRA_MTLX_BUILD_DIR}/pxr/usdImaging/hdEmscripten/emHdBindings.wasm" \
  "${OPENUSD_WASM_HYDRA_MTLX_PREFIX}/bin/"
cp "${OPENUSD_WASM_HYDRA_MTLX_BUILD_DIR}/pxr/usdImaging/hdEmscripten/generated/usd-core-bindings.d.ts" \
  "${OPENUSD_WASM_HYDRA_MTLX_PREFIX}/share/hdEmscripten/"

OPENUSD_WASM_HYDRA_PREFIX="${OPENUSD_WASM_HYDRA_MTLX_PREFIX}" \
  "${SCRIPT_DIR}/wasm-hydra-bindings-node.sh"

echo "Copied MaterialX-enabled Hydra wasm bundle to ${OPENUSD_WASM_HYDRA_MTLX_PREFIX}"
