#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

MODULE_PATH="${OPENUSD_WASM_HYDRA_PREFIX}/bin/emHdBindings.js"

if [[ ! -f "${MODULE_PATH}" ]]; then
  echo "Missing Hydra binding module: ${MODULE_PATH}" >&2
  echo "Run configure-wasm-hydra-imaging.sh and build-wasm-hydra-imaging.sh first." >&2
  exit 1
fi

OPENUSD_WASM_HYDRA_PREFIX="${OPENUSD_WASM_HYDRA_PREFIX}" node - <<'NODE'
const path = `${process.env.OPENUSD_WASM_HYDRA_PREFIX}/bin/emHdBindings.js`;
const getUsdModule = require(path);

getUsdModule({
  locateFile(file) {
    return `${process.env.OPENUSD_WASM_HYDRA_PREFIX}/bin/${file}`;
  },
  print() {},
  printErr(message) {
    const text = String(message);
    if (!text.includes("warning:")) {
      console.error(text);
    }
  },
}).then((USD) => {
  const api = {
    HdWebSyncDriver: typeof USD.HdWebSyncDriver,
    FS_createDataFile: typeof USD.FS_createDataFile,
    FS_createPath: typeof USD.FS_createPath,
    FS_analyzePath: typeof USD.FS_analyzePath,
    FS_readdir: typeof USD.FS_readdir,
    FS_rmdir: typeof USD.FS_rmdir,
    FS_unlink: typeof USD.FS_unlink,
    readyThen: typeof USD.ready?.then,
  };

  for (const [name, type] of Object.entries(api)) {
    if (type !== "function") {
      throw new Error(`${name} expected function, got ${type}`);
    }
  }

  console.log(JSON.stringify(api, null, 2));
}).catch((error) => {
  console.error(error && error.stack || error);
  process.exit(1);
});
NODE
