# Needle web and native Hydra

`needle/feature/hdEmscripten` is the maintained Needle OpenUSD branch for the
web imaging bridge and the imaging helpers used by the native Three.js Hydra
plugin. It is based on upstream OpenUSD 26.11.1. The resolved 26.11.1 port and
subsequent fork changes have linear history; upstream's own history is retained.

The source immediately after consolidation is identical to the previous
`d7aa0ef3bccda32648fd19873a21bffc7f60c6d8` tree. Original history is preserved at
`archive/hdEmscripten-before-26.11-linearization`. Existing wasm build metadata
continues to name the original commit that actually produced those binaries.
A history rewrite is not a reason to rebuild or relabel them.

The shared Three.js renderer, native `HdNeedleRendererPlugin`, host adapters,
runtime and verification scripts live in
[needle-tools/usd-viewer](https://github.com/needle-tools/usd-viewer), under
`native/`. See its [integration instructions](https://github.com/needle-tools/usd-viewer/blob/main/native/README.md)
and [distribution guide](https://github.com/needle-tools/usd-viewer/blob/main/native/DISTRIBUTION.md).
Keep the OpenUSD source revision paired with the viewer's recorded build
provenance when reproducing a build.

## Runtime and distribution

The host loads a native Hydra plugin compiled against its own USD ABI. That
plugin currently starts an Electron helper running the same Three.js
WebGPU/SSGI/TRAA code as the web viewer. GPU surfaces are shared with the native
plugin; hosts may request the standard Hydra CPU AOV interface. Electron remains
part of this implementation. Replacing it with a browserless JavaScript/WebGPU
runtime is deferred and is not necessary to build or test the current plugin.

A Hydra renderer is normally delivered as a host/platform-specific binary
package: dynamic library, `plugInfo.json`, resources, renderer runtime and host
registration. Shipping only this OpenUSD checkout or only a `.dylib` does not
deliver this renderer. The viewer distribution guide describes the current
source handoff, the binary package layout, host installation and the remaining
relocation/signing work before a standalone download is ready.

## Upstream fixes

`fix/animated-subdivision-invalidation` is a separate, narrowly scoped upstream
OpenUSD contribution based on upstream `dev`. It does not include the Needle
wasm/native renderer stack. Native hosts retain the documented subdivision
compatibility filter until their installed USD SDKs include the fix and pass its
regression without the filter. Upstream acceptance does not by itself update
Blender's, Houdini's or a standalone usdview installation's USD libraries.
