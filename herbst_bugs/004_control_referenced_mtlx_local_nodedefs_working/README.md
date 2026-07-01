# Referenced `.mtlx` With Local NodeDef Control

Suggested issue title:

None yet. This is a verified working control, not an OpenUSD issue repro.

## Summary

This folder tests the suspected failure mode where a USD scene references a material from a subfolder `.mtlx` file, and that `.mtlx` file contains a local custom nodedef implemented by a nodegraph.

The tested shape works:

- `materials/referenced_local_nodedef.mtlx` defines `ND_local_tint`, `NG_local_tint`, and a `LocalNodeMaterial`.
- `referenced_mtlx_local_nodedef_works.usda` references `@materials/referenced_local_nodedef.mtlx@</MaterialX/Materials/LocalNodeMaterial>`.
- `usdchecker` succeeds.
- `usdrecord --renderer GL` renders a blue material rather than a gray fallback.
- MaterialXView also renders the source `.mtlx` file.

## Repro Commands

```sh
OPENUSD=/Users/herbst/OpenUSD-26.05-native
MTLXVIEW=/Users/herbst/git/MaterialX/build/bin/MaterialXView

$MTLXVIEW --material materials/referenced_local_nodedef.mtlx \
  --screenWidth 512 --screenHeight 512 \
  --captureFilename referenced_local_nodedef.materialxview.png

$OPENUSD/bin/usdchecker referenced_mtlx_local_nodedef_works.usda
$OPENUSD/bin/usdrecord --renderer GL --imageWidth 256 \
  referenced_mtlx_local_nodedef_works.usda referenced_mtlx_local_nodedef_works.png
```

## Artifacts

- Referenced MaterialX material: `materials/referenced_local_nodedef.mtlx`
- USD scene referencing that material: `referenced_mtlx_local_nodedef_works.usda`
- MaterialXView screenshot: `referenced_local_nodedef.materialxview.png`
- OpenUSD render: `referenced_mtlx_local_nodedef_works.png`
- Validation log: `referenced_mtlx_local_nodedef_works.usdchecker.txt`

## Current Conclusion

This simple referenced-local-nodedef case does not reproduce a bug. If a real asset still fails, the next repro should isolate the delta against this control, for example nested local nodedefs, file includes, implementation-source links, namespaces, or a specific node category.
