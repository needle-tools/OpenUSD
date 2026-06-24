#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

activate_emsdk

cmake --build "${OPENUSD_WASM_HYDRA_MTLX_BUILD_DIR}" \
  --target emHdBindings \
  --parallel "${JOBS:-8}"

cmake --build "${OPENUSD_WASM_HYDRA_MTLX_BUILD_DIR}" \
  --target install \
  --parallel "${JOBS:-8}"

OPENUSD_WASM_HYDRA_PREFIX="${OPENUSD_WASM_HYDRA_MTLX_PREFIX}" \
  "${SCRIPT_DIR}/wasm-hydra-bindings-node.sh"

mtlx_count="$(find "${OPENUSD_WASM_HYDRA_MTLX_PREFIX}/lib/usd/usdMtlx/resources/libraries" -name '*.mtlx' | wc -l | tr -d ' ')"
echo "Installed MaterialX-enabled Hydra wasm bundle at ${OPENUSD_WASM_HYDRA_MTLX_PREFIX}"
echo "Installed usdMtlx library documents: ${mtlx_count}"
