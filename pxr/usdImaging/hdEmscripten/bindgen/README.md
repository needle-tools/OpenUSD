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
