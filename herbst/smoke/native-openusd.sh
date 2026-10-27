#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

native_env
cd "${OPENUSD_ROOT}"
unset PXR_PLUGINPATH_NAME

native_arch=""
if [[ "$(uname -m)" == "arm64" ]] && \
   file "${OPENUSD_NATIVE_PREFIX}/bin/usdcat" | grep -q 'x86_64'; then
  native_arch="x86_64"
fi

run_native() {
  if [[ -n "${native_arch}" ]]; then
    arch -"${native_arch}" "$@"
  else
    "$@"
  fi
}

run_native "${OPENUSD_NATIVE_PREFIX}/bin/usdcat" --help \
  >/tmp/openusd-2605-usdcat-help.txt

run_native "${PYTHON_BIN}" - <<'PY'
from pxr import Plug, Sdf, Usd, UsdGeom

mtlx = Sdf.FileFormat.FindByExtension("mtlx")
plugins = Plug.Registry().GetAllPlugins()
print("mtlx", mtlx.formatId if mtlx else None)
print("plugin_count", len(plugins))
print("has_usdMtlx", any(p.name == "usdMtlx" for p in plugins))
print("has_hdEmbree", any(p.name == "hdEmbree" for p in plugins))
assert mtlx and mtlx.formatId == "mtlx"
assert any(p.name == "usdMtlx" for p in plugins)
assert any(p.name == "hdEmbree" for p in plugins)

stage = Usd.Stage.CreateNew("/tmp/openusd-2605-embree-sphere.usda")
UsdGeom.Sphere.Define(stage, "/Sphere")
stage.GetRootLayer().Save()
PY

run_native "${OPENUSD_NATIVE_PREFIX}/bin/usdcat" \
  pxr/usdImaging/usdImagingGL/testenv/testUsdImagingGLMaterialXBasic/basicMxZup.usda \
  -o /tmp/openusd-2605-basicMxZup.usda

test -s /tmp/openusd-2605-basicMxZup.usda

run_native "${PYTHON_BIN}" "${OPENUSD_NATIVE_PREFIX}/bin/usdrecord" \
  --renderer Embree --disableGpu --imageWidth 64 \
  /tmp/openusd-2605-embree-sphere.usda \
  /tmp/openusd-2605-embree-sphere.png
test -s /tmp/openusd-2605-embree-sphere.png

PRMAN_LOCATION="${PRMAN_LOCATION:-/Applications/Pixar/RenderManProServer-27.2}"
if run_native "${PYTHON_BIN}" - <<'PY'
from pxr import Plug

raise SystemExit(
    0 if any(p.name == "hdPrmanLoader" for p in Plug.Registry().GetAllPlugins())
    else 1)
PY
then
  test -x "${PRMAN_LOCATION}/bin/prman"
  test -f "${PRMAN_LOCATION}/include/prmanapi.h"
elif [[ "${REQUIRE_PRMAN:-0}" == "1" ]]; then
  echo "hdPrman was not found in ${OPENUSD_NATIVE_PREFIX}" >&2
  exit 1
else
  echo "hdPrman was not built in ${OPENUSD_NATIVE_PREFIX}"
fi

echo "native-openusd smoke ok"
