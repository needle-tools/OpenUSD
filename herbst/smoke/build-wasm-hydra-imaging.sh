#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

activate_emsdk
emmake cmake --build "${OPENUSD_WASM_HYDRA_BUILD_DIR}" --config Release --parallel "${JOBS:-8}"
emmake cmake --build "${OPENUSD_WASM_HYDRA_BUILD_DIR}" --config Release --target install --parallel "${JOBS:-8}"
