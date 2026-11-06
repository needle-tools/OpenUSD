#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

activate_emsdk

cmake --build "${OPENUSD_WASM_HYDRA_MTLX_BUILD_DIR}" \
  --target "${WASM_BINDINGS_NAME}" \
  --parallel "${JOBS:-2}"

cmake --build "${OPENUSD_WASM_HYDRA_MTLX_BUILD_DIR}" \
  --target install \
  --parallel "${JOBS:-2}"

# The intermediate pass without usdGltf skips the smoke test, which expects usdGltf.
if [[ "${SKIP_NODE_SMOKE:-0}" != 1 ]]; then
  OPENUSD_WASM_HYDRA_PREFIX="${OPENUSD_WASM_HYDRA_MTLX_PREFIX}" \
    "${SCRIPT_DIR}/wasm-hydra-bindings-node.sh"
fi

echo "Installed MaterialX-enabled Hydra wasm bundle at ${OPENUSD_WASM_HYDRA_MTLX_PREFIX}"
