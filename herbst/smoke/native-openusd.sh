#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

native_env
cd "${OPENUSD_ROOT}"

usdcat --help >/tmp/openusd-2605-usdcat-help.txt

"${PYTHON_BIN}" - <<'PY'
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

usdcat pxr/usdImaging/usdImagingGL/testenv/testUsdImagingGLMaterialXBasic/basicMxZup.usda \
  -o /tmp/openusd-2605-basicMxZup.usda

test -s /tmp/openusd-2605-basicMxZup.usda

usdrecord --renderer Embree --disableGpu --imageWidth 64 \
  /tmp/openusd-2605-embree-sphere.usda \
  /tmp/openusd-2605-embree-sphere.png
test -s /tmp/openusd-2605-embree-sphere.png

PRMAN_LOCATION="${PRMAN_LOCATION:-/Applications/Pixar/RenderManProServer-26.3}"
if [[ -x "${PRMAN_LOCATION}/bin/prman" && -f "${PRMAN_LOCATION}/include/prmanapi.h" ]]; then
  "${PYTHON_BIN}" - <<'PY'
from pxr import Plug

assert any(p.name == "hdPrmanLoader" for p in Plug.Registry().GetAllPlugins())
PY
elif [[ "${REQUIRE_PRMAN:-0}" == "1" ]]; then
  echo "RenderMan SDK is incomplete at ${PRMAN_LOCATION}; expected bin/prman and include/prmanapi.h" >&2
  exit 1
else
  echo "RenderMan SDK unavailable at ${PRMAN_LOCATION}; hdPrman was not built"
fi

echo "native-openusd smoke ok"
