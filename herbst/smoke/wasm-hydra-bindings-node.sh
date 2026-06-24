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
    CreateStage: typeof USD.CreateStage,
    OpenStage: typeof USD.OpenStage,
    ReleaseStage: typeof USD.ReleaseStage,
    CreateUsdzPackage: typeof USD.CreateUsdzPackage,
    ReadFile: typeof USD.ReadFile,
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

  try {
    USD.FS_createPath("/", "tmp", true, true);
  } catch {
    // The path may already exist when this smoke is run repeatedly.
  }

  const usdPath = "/tmp/hd-emscripten-authoring-smoke.usda";
  const usdzPath = "/tmp/hd-emscripten-authoring-smoke.usdz";
  const stage = USD.CreateStage(usdPath);
  if (!stage || typeof stage.DefinePrim !== "function") {
    throw new Error("CreateStage did not return a usable Stage");
  }

  stage.SetUpAxis("Z");
  stage.SetStartTimeCode(1);
  stage.SetEndTimeCode(24);
  stage.SetTimeCodesPerSecond(24);

  const root = stage.DefinePrim("/World", "Xform");
  if (!root.IsValid()) {
    throw new Error("DefinePrim did not create /World");
  }

  const color = root.CreateAttribute("primvars:displayColor", "color3f", true);
  if (!color.SetColor3f(1, 0.25, 0.5, Number.NaN)) {
    throw new Error("SetColor3f failed");
  }

  const spin = root.CreateAttribute("userProperties:spin", "float", true);
  if (!spin.SetFloat(0, 1) || !spin.SetFloat(90, 24)) {
    throw new Error("SetFloat time samples failed");
  }

  if (!root.AddVariant("lod", "low") || !root.DefinePrimInVariant("lod", "high", "/World/HighGeom", "Scope").IsValid()) {
    throw new Error("Variant authoring failed");
  }
  if (!root.SetVariantSelection("lod", "high") || root.GetVariantSelection("lod") !== "high") {
    throw new Error("Variant selection failed");
  }

  if (!stage.Export(usdPath) || !USD.FS_analyzePath(usdPath).exists) {
    throw new Error("Stage export failed");
  }
  if (!USD.CreateUsdzPackage(usdPath, usdzPath)) {
    throw new Error("USDZ package creation failed");
  }

  const usdzBytes = USD.ReadFile(usdzPath);
  if (!(usdzBytes instanceof Uint8Array) || usdzBytes.length < 100 || usdzBytes[0] !== 0x50 || usdzBytes[1] !== 0x4b) {
    throw new Error("USDZ bytes are not a valid zip payload");
  }

  const reopened = USD.OpenStage(usdPath);
  const reopenedRoot = reopened.GetPrimAtPath("/World");
  if (!reopenedRoot.IsValid() || reopenedRoot.GetVariantSelection("lod") !== "high") {
    throw new Error("Reopened stage did not preserve variant selection");
  }
  if (!reopened.GetPrimAtPath("/World/HighGeom").IsValid()) {
    throw new Error("Reopened stage did not compose selected variant contents");
  }
  if (reopenedRoot.GetAttribute("userProperties:spin").GetValueStringAtTime(24) !== "90") {
    throw new Error("Reopened stage did not preserve animated sample");
  }

  USD.ReleaseStage(reopened);
  USD.ReleaseStage(stage);

  console.log(JSON.stringify(api, null, 2));
}).catch((error) => {
  console.error(error && error.stack || error);
  process.exit(1);
});
NODE
