#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

native_env
cd "${OPENUSD_ROOT}"

usdcat --help >/tmp/openusd-2605-usdcat-help.txt

"${PYTHON_BIN}" - <<'PY'
from pxr import Plug, Sdf, Usd

mtlx = Sdf.FileFormat.FindByExtension("mtlx")
plugins = Plug.Registry().GetAllPlugins()
print("mtlx", mtlx.formatId if mtlx else None)
print("plugin_count", len(plugins))
print("has_usdMtlx", any(p.name == "usdMtlx" for p in plugins))
assert mtlx and mtlx.formatId == "mtlx"
assert any(p.name == "usdMtlx" for p in plugins)
PY

usdcat pxr/usdImaging/usdImagingGL/testenv/testUsdImagingGLMaterialXBasic/basicMxZup.usda \
  -o /tmp/openusd-2605-basicMxZup.usda

test -s /tmp/openusd-2605-basicMxZup.usda
echo "native-openusd smoke ok"
