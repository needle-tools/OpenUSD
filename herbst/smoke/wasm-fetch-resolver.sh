#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

EXAMPLE_DIR="${OPENUSD_WASM_PREFIX}/share/usd/examples/bin/wasmFetchResolver"
cd "${EXAMPLE_DIR}"

npm install
npm install --save-dev playwright

npm run server -- --port 8095 &
SERVER_PID=$!
trap 'kill "${SERVER_PID}" >/dev/null 2>&1 || true' EXIT

for _ in {1..50}; do
  if curl -fsS http://localhost:8095/wasmFetchResolver.html >/dev/null; then
    break
  fi
  sleep 0.2
done

node "${SCRIPT_DIR}/wasm-fetch-resolver-browser.js" http://localhost:8095/wasmFetchResolver.html
echo "wasm-fetch-resolver smoke ok"
