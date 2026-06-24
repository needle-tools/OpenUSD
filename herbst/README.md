# Herbst OpenUSD 26.05 Smoke Tests

These scripts record the local modernization checks for OpenUSD 26.05 and the wasm Hydra bridge work.

Run them from anywhere; each script computes the OpenUSD source root relative to its own location.

## Layout

- `smoke/env.sh`: shared paths and environment helpers.
- `smoke/native-openusd.sh`: verifies the native OpenUSD 26.05 build, MaterialX plugin, and a MaterialX sample.
- `smoke/adobe-gltf-plugin.sh`: verifies Adobe's glTF plugin against the native OpenUSD build.
- `smoke/wasm-fetch-resolver-browser.js`: browser smoke test for upstream OpenUSD's installed wasm fetch resolver sample.
- `smoke/wasm-fetch-resolver.sh`: starts the upstream wasm sample server and runs the browser smoke test.
- `smoke/build-wasm-opensubdiv.sh`: builds OpenSubdiv 3.6.1 for wasm into the upstream wasm prefix.
- `smoke/configure-wasm-hydra-imaging.sh`: configures a no-GPU wasm build with `usdImaging` enabled.
- `smoke/build-wasm-hydra-imaging.sh`: builds the configured no-GPU wasm imaging tree.
- `smoke/build-wasm-materialx-openusd.sh`: builds a minimal pthread-compatible wasm MaterialX dependency for OpenUSD's `usdMtlx` and `hdMtlx`.
- `smoke/configure-wasm-hydra-materialx.sh`: configures the no-GPU wasm Hydra build with `PXR_ENABLE_MATERIALX_SUPPORT=ON`.
- `smoke/build-wasm-hydra-materialx.sh`: builds, installs, and smokes the MaterialX-enabled wasm Hydra bundle.
- `smoke/wasm-hydra-bindings-node.sh`: loads the installed `emHdBindings.js` bundle in Node and verifies the Hydra APIs usd-viewer needs.

## Current Known Prefixes

- Native OpenUSD: `/Users/herbst/OpenUSD-26.05-native`
- Upstream wasm OpenUSD: `/Users/herbst/OpenUSD-26.05-wasm`
- Adobe glTF plugin: `/Users/herbst/USD-Fileformat-plugins-2026.03-openusd-26.05`
- Wasm Hydra experiment build: `/Users/herbst/OpenUSD-26.05-wasm-hydra-exp-build`
- Wasm Hydra experiment install: `/Users/herbst/OpenUSD-26.05-wasm-hydra-exp`
- Wasm MaterialX dependency: `/Users/herbst/MaterialX-1.39.5-wasm-openusd`
- Wasm Hydra MaterialX probe build: `/Users/herbst/OpenUSD-26.05-wasm-hydra-mtlx-probe-build`
- Wasm Hydra MaterialX probe install: `/Users/herbst/OpenUSD-26.05-wasm-hydra-mtlx-probe`

## Notes

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
- `emHdBindings.data`
- `emHdBindings.wasm`

Modern Emscripten does not emit a separate `emHdBindings.worker.js` sidecar for this build. The generated bundle exports `globalThis["NEEDLE:USD:GET"]` and the filesystem helpers expected by `usd-viewer`.

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
- The MaterialX-enabled installed sidecars are approximately `190K` for `emHdBindings.js`, `2.2M` for `emHdBindings.data`, and `29M` for `emHdBindings.wasm`.

One source fix was required: `_install_resource_files` now preserves absolute source resource paths when producing Emscripten embed/preload arguments. Without that, absolute `MATERIALX_STDLIB_DIR` entries were incorrectly prefixed with `pxr/usd/usdMtlx/`.
