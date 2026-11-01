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

`Attribute.Get(timeCode)` returns owned JavaScript values: scalars, strings and
nested numeric arrays for vectors, matrices, quaternions and their arrays.
Pass NaN for the default value. Unset values return undefined; GetValueString
remains available for display. Material transport reuses the same conversion.
Use `await Attribute.Get(timeCode)` when a sampled value may open a remote value clip.

Numeric two- and four-component vectors, integer vectors and their arrays can
be authored as component arrays. UV (`texCoord2f[]`, `texCoord2d[]`) and color4
roles preserve their USD type. Wrong dimensions, non-finite values, overflow and
fractional integer components throw TypeError. Bool arrays take booleans.

`await stage.OpenLayerStage(identifier)` opens a stage over an existing used or
local-stack layer, preserving its exact layer identity and resolver context. It
does not reopen an asset URL. Payloads start unloaded. Release the owned result
with `USD.ReleaseStage`; null means the layer is not part of the stage.

`Prim.ComputeVisibility(timeCode)` and `ComputePurpose()` return inherited USD
state. `MakeVisible(timeCode)` and `MakeInvisible(timeCode)` use
`UsdGeomImageable`, including ancestor and sibling visibility handling. Pass NaN
for default time. Both mutation methods return whether the requested computed
visibility was achieved.

`Stage.TraverseAllWithInstanceProxies(expandedInstances)` includes instance
children at their composed paths. Pass null for every proxy, or an array of
instance paths to expand only those branches. Instance proxies report
`Prim.IsInstanceProxy()` and remain read-only according to USD authoring rules.

`GetAllMetadataValues()` returns owned typed values on prims, attributes and
relationships. Composed list operations become arrays of applied items,
references/payloads retain asset and prim paths and layer offsets, and nested
dictionaries retain booleans/numbers/arrays. Integers outside JavaScript's safe
range are strings. `GetAllMetadata()` retains its legacy display strings.

`GetMetadata(key)` and `GetMetadataByDictKey(key, keyPath)` return typed composed
values. `SetMetadata` accepts dictionaries, explicit list arrays or list-operation
objects (`isExplicit`, `explicitItems`, `prependedItems`, `appendedItems`,
`deletedItems`, `orderedItems`, `addedItems`). Spec-stack entries expose
`metadataValues` with these operation buckets intact, alongside the legacy
`metadata` display strings. This preserves authored composition operations.

Dictionary updates preserve the types of existing entries. New scalar entries
infer bool/string/double, and homogeneous arrays infer bool[]/string[]/double[].
For an exact type, a new empty array or an exact 64-bit value, use
`SetMetadataByDictKey(key, keyPath, value, typeName)` with an Sdf type such as
`token[]` or `uint64`; an empty typeName preserves/infers the entry type.
`ClearMetadata` and `ClearMetadataByDictKey` remove authored opinions.

Attribute `Get(time)` / `Set(value, time)` and transform-op `GetValue(time)` /
`Set(value, time)` exchange owned JS values. Transform-op `Get(time)` remains a
legacy display-string API. Unsigned and 64-bit integers, half values, matrix2d,
matrix3d, timecode and asset arrays support typed reads and writes. 64-bit setters
accept bigint or exact decimal strings and reject unsafe JS integer numbers.

`GetRootLayer()` returns a reference-counted layer handle. Deleting its JS wrapper
releases only that reference; the owning stage and other handles remain valid.
A retained layer handle remains usable after the stage is released.

Instancing inspection exposes `Prim.IsInstanceable`, `GetPrototype`,
`GetPrimInPrototype`, `GetInstances` and `Stage.GetPrototypes`. Prototype prims
are generated by composition and are read-only. `Stage.TraversePrototypes`
accepts null for full traversal or an array of expanded prototype/instance paths
for lazy inspection, independently of the ordinary root tree.

`Stage.GetAnimationTimeRange()` returns a plain range or null. It scans active,
loaded, defined, concrete stage prims and each shared prototype once, including
value clips and splines. The result does not depend on hierarchy expansion.
