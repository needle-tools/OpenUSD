# hdEmscripten Binding Generation

`core-bindings.json` is the source of truth for the core USD API exposed by the
wasm Hydra bridge. `generate_bindings.py` emits both:

- `emHdCoreBindings.inc`, included by `emHdBindings.cpp`
- `usd-core-bindings.d.ts`, installed to `share/hdEmscripten`

The generator deliberately covers the core object model used by `usd-viewer`
today: `SdfLayer`, `UsdStage`, `UsdPrim`, `UsdAttribute`, `UsdRelationship`, and
their vector helpers. It also exposes the first authoring/package checkpoint:
stage creation/opening/release, prim definition, typed attribute setters with
time samples, variant add/list/select helpers, selected-variant prim authoring,
USDA export, USDZ package creation, and binary readback for browser download
handoff. The bridge-specific `HdWebSyncDriver` binding remains in
`emHdBindings.cpp` because it is the render transport API, not a USD API.

Run it manually from the OpenUSD repository with:

```sh
python3 pxr/usdImaging/hdEmscripten/bindgen/generate_bindings.py \
  --manifest pxr/usdImaging/hdEmscripten/bindgen/core-bindings.json \
  --cpp-out /tmp/emHdCoreBindings.inc \
  --dts-out /tmp/usd-core-bindings.d.ts
```

CMake runs the same command automatically before compiling `emHdBindings`.

Array setters accept JavaScript arrays of `GfMatrix4d` objects, flat 16-number
row-major matrices or four rows of four numbers, and quaternion arrays in
`[real, x, y, z]` order (`quatf`, `quatd`, `quath`). Half-precision scales use
`half3[]`. `SetMetadata(key, value)` converts using the Sdf field definition, so
primvar interpolation remains a token and elementSize remains an integer.
Unsupported value types and malformed matrices/quaternions throw TypeError.

`Prim.AddReference(assetPath, primPath)` and `SetInstanceable(bool)` author
composition through USD. `Layer.GetSubLayerPaths/SetSubLayerPaths` inspect and
replace the ordered sublayer list. Stage edit targets and a prim's selected
variant edit target are exposed so callers can scope both definitions and
attribute values to a variant. Restore the previous edit target in a finally
block before yielding to unrelated authoring work.
