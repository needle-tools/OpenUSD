# Herbst OpenUSD 26.05 Smoke Tests

These scripts record the local modernization checks for OpenUSD 26.05 and the wasm Hydra bridge work.

Run them from anywhere; each script computes the OpenUSD source root relative to its own location.

## Layout

- `smoke/env.sh`: shared paths and environment helpers.
- `smoke/native-openusd.sh`: verifies the native OpenUSD 26.05 build, MaterialX, and an actual CPU render through hdEmbree. Set `REQUIRE_PRMAN=1` to require a complete RenderMan SDK and hdPrman plugin too.
- `smoke/adobe-gltf-plugin.sh`: verifies Adobe's glTF plugin against the native OpenUSD build.
- `smoke/wasm-fetch-resolver-browser.js`: browser smoke test for upstream OpenUSD's installed wasm fetch resolver sample.
- `smoke/wasm-fetch-resolver.sh`: starts the upstream wasm sample server and runs the browser smoke test.
- `smoke/build-wasm-opensubdiv.sh`: builds OpenSubdiv for wasm into the upstream wasm prefix. By default it uses the OpenUSD 26.05 native build's vendored OpenSubdiv 3.6.1 source zip; pass `OPENSUBDIV_SRC_DIR=/Users/herbst/git/OpenSubdiv` explicitly when testing the checked-out OpenSubdiv repo.
- `smoke/configure-wasm-hydra-imaging.sh`: configures a no-GPU wasm build with `usdImaging` enabled.
- `smoke/build-wasm-hydra-imaging.sh`: builds the configured no-GPU wasm imaging tree.
- `smoke/build-wasm-materialx-openusd.sh`: builds a minimal pthread-compatible wasm MaterialX dependency for OpenUSD's `usdMtlx` and `hdMtlx`.
- `smoke/build-wasm-adobe-gltf-plugin.sh`: builds Adobe's glTF plugin as wasm static archives against the current OpenUSD wasm prefix.
- `smoke/configure-wasm-hydra-materialx.sh`: configures the no-GPU wasm Hydra build with `PXR_ENABLE_MATERIALX_SUPPORT=ON`.
- `smoke/build-wasm-hydra-materialx.sh`: builds, installs, and smokes the MaterialX-enabled wasm Hydra bundle.
- `smoke/wasm-hydra-bindings-node.sh`: loads the installed `emHdBindings.js` bundle in Node and verifies the Hydra APIs usd-viewer needs.

## Current Known Prefixes

- Native OpenUSD: `/Users/herbst/OpenUSD-26.05-native`
- Native OpenUSD with RenderMan (x86_64): `/Users/herbst/OpenUSD-26.05-native-x86_64`
- Upstream wasm OpenUSD: `/Users/herbst/OpenUSD-26.05-wasm`
- Adobe glTF plugin: `/Users/herbst/USD-Fileformat-plugins-2026.03-openusd-26.05`
- Wasm Hydra experiment build: `/Users/herbst/OpenUSD-26.05-wasm-hydra-exp-build`
- Wasm Hydra experiment install: `/Users/herbst/OpenUSD-26.05-wasm-hydra-exp`
- Wasm MaterialX dependency: `/Users/herbst/MaterialX-1.39.5-wasm-openusd`
- Wasm Hydra MaterialX probe build: `/Users/herbst/OpenUSD-26.05-wasm-hydra-mtlx-probe-build`
- Wasm Hydra MaterialX probe install: `/Users/herbst/OpenUSD-26.05-wasm-hydra-mtlx-probe`

## Notes

The arm64 native prefix is configured with Embree 4.3.3 and
`PXR_BUILD_EMBREE_PLUGIN=ON`. Reproduce that build with
`build_usd.py --embree`.

Pixar's macOS RenderMan Pro Server 27.2 SDK is x86_64, so it cannot be loaded
into the arm64 native process. The separate x86_64 prefix includes usdview,
Draco, MaterialX, Embree, and hdPrman. Reproduce it under Rosetta with:

```sh
arch -x86_64 /usr/local/bin/python3 build_scripts/build_usd.py \
  /Users/herbst/OpenUSD-26.05-native-x86_64 \
  --build-target x86_64 \
  --prman \
  --prman-location /Applications/Pixar/RenderManProServer-27.2 \
  --embree \
  --draco \
  --usdview \
  --materialx \
  --python \
  --tools \
  --usdValidation \
  --no-tests \
  --no-examples \
  --no-tutorials \
  --no-docs \
  --no-python-docs
```

The upstream OpenUSD 26.05 wasm target builds and runs the `wasmFetchResolver` sample, but it does not produce the viewer's `emHdBindings.*` artifacts.

The current porting path keeps the three.js Hydra architecture by reintroducing a modernized wasm Hydra bridge on top of OpenUSD 26.05.

After the Hydra experiment install exists, run:

```sh
./herbst/smoke/wasm-hydra-bindings-node.sh
```

## Current Result

The wasm Hydra bridge now builds on OpenUSD 26.05 and installs the viewer bundle sidecars into:

```sh
/Users/herbst/OpenUSD-26.05-wasm-hydra-exp/bin
```

Installed files:

- `emHdBindings.js`
- `emHdBindings.wasm`

Modern Emscripten does not emit a separate `emHdBindings.worker.js` sidecar for this build. With Emscripten 4.0.23 this checkpoint embeds resources directly, so there is no `emHdBindings.data` sidecar either. The generated bundle exports `globalThis["NEEDLE:USD:GET"]` and the filesystem helpers expected by `usd-viewer`.

The Node smoke test verifies:

- `HdWebSyncDriver`
- `HdWebSyncDriver.GetStage()` and basic `Stage`/`Prim`/`Layer` APIs
- generated stage authoring APIs (`CreateStage`, `OpenStage`, `ReleaseStage`)
- generated prim, attribute, variant, and time-sample authoring APIs
- USDZ package creation and byte readback for browser download handoff
- `FS_createDataFile`
- `FS_createPath`
- `FS_analyzePath`
- `FS_readdir`
- `FS_rmdir`
- `FS_unlink`
- `ready.then`

The generated artifacts have been copied into `/Users/herbst/git/usd-viewer/usd-wasm/src/bindings` on the `modernize-openusd-26-05-wasm` viewer branch. Headed Chromium matrix validation passes for the supported `usd-viewer` cases.

## MaterialX Wasm Probe

MaterialX can be enabled in the OpenUSD 26.05 wasm Hydra build, provided the MaterialX static libraries are built with the same Emscripten pthread/shared-memory flags as OpenUSD.

Reproduce the probe with:

```sh
./herbst/smoke/build-wasm-materialx-openusd.sh
./herbst/smoke/build-wasm-adobe-gltf-plugin.sh
./herbst/smoke/configure-wasm-hydra-materialx.sh
./herbst/smoke/build-wasm-hydra-materialx.sh
```

Observed result:

- `usdMtlx` builds for wasm.
- `hdMtlx` builds for wasm.
- `emHdBindings` links and installs with `PXR_ENABLE_MATERIALX_SUPPORT=ON`.
- The build script uses `cmake --build ... --target install`, not raw `cmake --install` after a partial build, so install dependencies such as `usdShaders` are built before install rules run.
- The wasm render delegate advertises `mtlx` as a material render context and shader source type, so OpenUSD creates real Hydra material sprims for MaterialX-authored materials.
- `wasm-hydra-bindings-node.sh` passes against `/Users/herbst/OpenUSD-26.05-wasm-hydra-mtlx-probe`, including generated authoring, variants, animated attributes, USDA export, USDZ package creation, and binary readback.
- The installed `usdMtlx` resources contain 56 `.mtlx` library files, including `gltf_pbr.mtlx`, `open_pbr_surface.mtlx`, and `usd_preview_surface.mtlx`.
- The MaterialX/glTF-enabled Emscripten 4.0.23 sidecars are approximately `164K` for `emHdBindings.js` and `33M` for `emHdBindings.wasm`.

Current source fixes include embedded hdEmscripten/USD/MaterialX resources for Emscripten 4.0.23, async Embind policies for generated APIs that can cross browser fetches, explicit async `Draw()`/`Repopulate()` bindings, and a larger Asyncify stack. The old broad `ASYNCIFY_REMOVE` pruning was removed because USD/Sdf/Crate paths can fetch assets during composition.
