# MaterialX `position(space="world")` On Transformed Prim Appears Inconsistent

Suggested issue title:

`HdMtlx/Storm: ND_position_vector3 with space="world" gives unexpected result on transformed prim`

## Summary

This is a candidate repro for a coordinate-space issue observed while exporting a transformed GLB with a MaterialX graph that branches based on `position(space="world")`.

The failing scene binds a material to a sphere translated by `(0, 2, 0)`. The material evaluates `ND_position_vector3` with `inputs:space = "world"` and compares it to a world-space reference point `(0, 2, 0)`.

Expected: the world-space position should account for the prim transform. The shader should be able to reason about the translated position relative to `(0, 2, 0)`.

Actual: the render differs sharply from the same setup using `inputs:space = "model"`. In the larger Needle export this caused the MaterialX branch selector to land on the wrong gray branch until we changed the emitted position space.

## Repro Commands

```sh
OPENUSD=/Users/herbst/OpenUSD-26.05-native

$OPENUSD/bin/usdchecker world_position_selector_fails.usda
$OPENUSD/bin/usdrecord --renderer GL --imageWidth 256 \
  world_position_selector_fails.usda world_position_selector_fails.png

$OPENUSD/bin/usdchecker model_position_control.usda
$OPENUSD/bin/usdrecord --renderer GL --imageWidth 256 \
  model_position_control.usda model_position_control.png
```

## Artifacts

- Candidate failing scene: `world_position_selector_fails.usda`
- Comparison scene: `model_position_control.usda`
- Renders: `world_position_selector_fails.png`, `model_position_control.png`
- Generated MTLX captures: `world_position_selector_fails.generated.mtlx`, `model_position_control.generated.mtlx`
- Standalone MaterialX graph: `world_position_materialx_equivalent.mtlx`
- MaterialXView screenshot: `world_position_materialx_equivalent.png`

## Notes

This one may need maintainer guidance on the exact expected coordinate-space convention for MaterialX `position` in Hydra/Storm. It is included because it 100% reproduces the behavior that broke the exported animated material branch in the real USDZ validation case.

The standalone MaterialX file proves the graph itself is valid, but it cannot encode the same USD prim transform condition by itself. The suspected bug is specifically the interaction between `ND_position_vector3(space="world")`, Hydra/Storm, and a transformed USD prim.
