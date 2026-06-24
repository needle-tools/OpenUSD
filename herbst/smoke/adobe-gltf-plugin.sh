#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

adobe_plugin_env
cd "${ADOBE_PLUGIN_REPO}"

"${PYTHON_BIN}" - <<'PY'
from pxr import Plug, Sdf, Usd

for ext, expected in [("gltf", "gltf"), ("glb", "gltf"), ("mtlx", "mtlx")]:
    fmt = Sdf.FileFormat.FindByExtension(ext)
    print(ext, fmt.formatId if fmt else None)
    assert fmt and fmt.formatId == expected

plugins = Plug.Registry().GetAllPlugins()
gltf = [p for p in plugins if p.name == "usdGltf_plugin"]
assert gltf and gltf[0].isLoaded
print("plugin", gltf[0].name, gltf[0].path, gltf[0].isLoaded)

stage = Usd.Stage.Open("gltf/tests/SanityCube.gltf")
assert stage
print("defaultPrim", stage.GetDefaultPrim().GetPath())
print("meshes", [p.GetPath().pathString for p in stage.Traverse() if p.GetTypeName() == "Mesh"])

assert stage.Export("/tmp/usdff-sanity.glb")
roundtrip = Usd.Stage.Open("/tmp/usdff-sanity.glb")
assert roundtrip
print("roundtrip_default", roundtrip.GetDefaultPrim().GetPath())
PY

usdcat --loadOnly gltf/tests/SanityCube.gltf test/assets/gltf/cube-colors.glb >/tmp/usdff-loadonly.txt
usdcat /tmp/usdff-sanity.glb --loadOnly >/tmp/usdff-roundtrip-loadonly.txt
echo "adobe-gltf-plugin smoke ok"
