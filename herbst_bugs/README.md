# Herbst OpenUSD Bug Repros

Small repro packages for OpenUSD issues found while validating Needle Engine USDZ / MaterialX export.

Each subfolder is intended to be issue-ready:

- `*.usda` files are the minimal repro and control scenes.
- `*.png` files are renders produced with the local OpenUSD build.
- `*.usdchecker.txt` files show validation status.
- `*.generated.mtlx` files, where present, are emitted with `TF_DEBUG=HDMTLX_WRITE_DOCUMENT`.
- `*_materialx_equivalent.mtlx` and matching `*.png` files are standalone MaterialX-side controls rendered with `MaterialXView`.
- Each subfolder has its own `README.md` with a suggested issue title and body.

`004_control_referenced_mtlx_local_nodedefs_working` is intentionally a control, not a bug report. It checks the suspected case of a USD scene referencing a subfolder `.mtlx` material with a local nodedef and nodegraph implementation; this simple shape passes `usdchecker` and renders with Storm, so it is not currently counted as an OpenUSD issue.

`materialx_equivalent_screenshots.png` is a quick 2x2 overview of the MaterialXView/control screenshots. The per-case PNGs remain in their respective folders.

Commands assume:

```sh
OPENUSD=/Users/herbst/OpenUSD-26.05-native
MTLXVIEW=/Users/herbst/git/MaterialX/build/bin/MaterialXView
```
