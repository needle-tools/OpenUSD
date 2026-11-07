#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

MODULE_PATH="${OPENUSD_WASM_HYDRA_PREFIX}/bin/${WASM_BINDINGS_NAME}.js"

if [[ ! -f "${MODULE_PATH}" ]]; then
  echo "Missing Hydra binding module: ${MODULE_PATH}" >&2
  echo "Run configure-wasm-hydra-imaging.sh and build-wasm-hydra-imaging.sh first." >&2
  exit 1
fi

OPENUSD_WASM_HYDRA_PREFIX="${OPENUSD_WASM_HYDRA_PREFIX}" WASM_BINDINGS_NAME="${WASM_BINDINGS_NAME}" node - <<'NODE'
const path = `${process.env.OPENUSD_WASM_HYDRA_PREFIX}/bin/${process.env.WASM_BINDINGS_NAME}.js`;
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
}).then(async (USD) => {
  // Bindings marked tsMayAsync return promises in both runtimes.
  const api = {
    CreateHydraDriver: typeof USD.CreateHydraDriver,
    CreateStage: typeof USD.CreateStage,
    OpenStage: typeof USD.OpenStage,
    ReleaseStage: typeof USD.ReleaseStage,
    CreateUsdzPackage: typeof USD.CreateUsdzPackage,
    GetBuildInfoJson: typeof USD.GetBuildInfoJson,
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

  const buildInfo = JSON.parse(USD.GetBuildInfoJson());
  if (buildInfo.openusd?.version !== "0.26.11") {
    throw new Error(`Unexpected OpenUSD version in build info: ${buildInfo.openusd?.version}`);
  }
  if (buildInfo.modules?.hydraBridge !== true) {
    throw new Error("Build info does not report the Hydra bridge as enabled");
  }
  for (const moduleName of ["usdImaging", "materialX", "openSubdiv", "usdGltf"]) {
    if (buildInfo.modules?.[moduleName] !== true) {
      throw new Error(`Build info does not report ${moduleName} as enabled`);
    }
  }
  if (!String(buildInfo.toolchain?.emscripten ?? "").includes("Emscripten")) {
    throw new Error(`Build info has an unexpected Emscripten version: ${buildInfo.toolchain?.emscripten}`);
  }
  if (!buildInfo.dependencies?.openSubdiv?.version) {
    throw new Error("Build info does not report an OpenSubdiv version");
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

  // Rig authoring uses typed arrays and primvar metadata, without importing text.
  const matrices = root.CreateAttribute("bindTransforms", "matrix4d[]", true);
  if (!matrices.Set([USD.GfMatrix4d.Identity()], Number.NaN)) throw new Error("Matrix array authoring failed");
  for (const type of ["quatf[]", "quatd[]", "quath[]"]) {
    const rotation = root.CreateAttribute(type.slice(0,5), type, true);
    if (!rotation.Set([[1,0,0,0]], Number.NaN)) throw new Error(type + " authoring failed");
  }
  const weights = root.CreateAttribute("primvars:weights", "float[]", false);
  if (!(await weights.SetMetadata("interpolation", "vertex")) || !(await weights.SetMetadata("elementSize", 3)))
    throw new Error("Primvar metadata authoring failed");
  stage.DefinePrim("/Prototype", "Scope");
  stage.DefinePrim("/Prototype/Part", "Cube");
  const copy = stage.DefinePrim("/World/Instance", "Xform");
  if (!(await copy.AddReference("", "/Prototype")) || !(await copy.SetInstanceable(true)) || !copy.IsInstance())
    throw new Error("Reference/instance authoring failed");
  (await stage.GetRootLayer().SetSubLayerPaths([]));
  if (stage.GetRootLayer().GetSubLayerPaths().length) throw new Error("Sublayer edit failed");

  const color = root.CreateAttribute("primvars:displayColor", "color3f", true);
  if (!color.SetColor3f(1, 0.25, 0.5, Number.NaN)) {
    throw new Error("SetColor3f failed");
  }

  const spin = root.CreateAttribute("userProperties:spin", "float", true);
  if (!spin.SetFloat(0, 1) || !spin.SetFloat(90, 24)) {
    throw new Error("SetFloat time samples failed");
  }

  if (!root.AddVariant("lod", "low") || !(await root.DefinePrimInVariant("lod", "high", "/World/HighGeom", "Scope")).IsValid()) {
    throw new Error("Variant authoring failed");
  }
  if (!(await root.SetVariantSelection("lod", "high")) || root.GetVariantSelection("lod") !== "high") {
    throw new Error("Variant selection failed");
  }

  if (!(await stage.Export(usdPath)) || !USD.FS_analyzePath(usdPath).exists) {
    throw new Error("Stage export failed");
  }
  if (!(await USD.CreateUsdzPackage(usdPath, usdzPath))) {
    throw new Error("USDZ package creation failed");
  }

  const usdzBytes = USD.ReadFile(usdzPath);
  if (!(usdzBytes instanceof Uint8Array) || usdzBytes.length < 100 || usdzBytes[0] !== 0x50 || usdzBytes[1] !== 0x4b) {
    throw new Error("USDZ bytes are not a valid zip payload");
  }

  const reopened = (await USD.OpenStage(usdPath));
  const reopenedRoot = reopened.GetPrimAtPath("/World");
  if (!reopenedRoot.IsValid() || reopenedRoot.GetVariantSelection("lod") !== "high") {
    throw new Error("Reopened stage did not preserve variant selection");
  }
  if (!reopened.GetPrimAtPath("/World/HighGeom").IsValid()) {
    throw new Error("Reopened stage did not compose selected variant contents");
  }
  if ((await reopenedRoot.GetAttribute("userProperties:spin").GetValueStringAtTime(24)) !== "90") {
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
