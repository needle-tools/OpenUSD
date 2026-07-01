# `ND_time_float` Does Not Advance With `usdrecord --frames`

Suggested issue title:

`HdMtlx/Storm: MaterialX ND_time_float appears constant across USD frame renders`

## Summary

This scene uses a MaterialX `ND_time_float` node to drive the material color. The stage has `startTimeCode = 0`, `endTimeCode = 600`, and `timeCodesPerSecond = 60`.

Expected: rendering frame `600` should evaluate the MaterialX time node at a later time than frame `0`, producing a brighter material.

Actual: `nd_time_float_fails.usda` renders frame `0` and frame `600` identically dark. The control file `time_sampled_constant_control.usda`, which replaces `ND_time_float` with a time-sampled `ND_constant_float`, renders frame `600` bright as expected.

## Repro Commands

```sh
OPENUSD=/Users/herbst/OpenUSD-26.05-native

$OPENUSD/bin/usdchecker nd_time_float_fails.usda
$OPENUSD/bin/usdrecord --renderer GL --imageWidth 256 --frames 0,600 \
  nd_time_float_fails.usda nd_time_float_fails.frame_###.png

$OPENUSD/bin/usdchecker time_sampled_constant_control.usda
$OPENUSD/bin/usdrecord --renderer GL --imageWidth 256 --frames 0,600 \
  time_sampled_constant_control.usda time_sampled_constant_control.frame_###.png
```

## Artifacts

- Failing scene: `nd_time_float_fails.usda`
- Control scene: `time_sampled_constant_control.usda`
- Failing renders: `nd_time_float_fails.frame_000.png`, `nd_time_float_fails.frame_600.png`
- Control renders: `time_sampled_constant_control.frame_000.png`, `time_sampled_constant_control.frame_600.png`
- Standalone MaterialX graph: `time_node_materialx_equivalent.mtlx`
- MaterialXView screenshot: `time_node_materialx_equivalent.png`

## Observed Render Stats

- Failing frame 0 mean RGB: approximately `[19.4, 19.4, 19.4]`
- Failing frame 600 mean RGB: approximately `[19.4, 19.4, 19.4]`
- Control frame 0 mean RGB: approximately `[19.4, 19.4, 19.4]`
- Control frame 600 mean RGB: approximately `[201.8, 201.8, 201.8]`

## Why This Should Work

MaterialX exposes `time` as a standard geometric/runtime node. A USD stage rendered at different time codes should provide a time value to the shader network, just as authored USD time samples do.

The matching standalone MaterialX graph can be opened with:

```sh
/Users/herbst/git/MaterialX/build/bin/MaterialXView \
  --material time_node_materialx_equivalent.mtlx
```

`time_node_materialx_equivalent.png` is the first captured viewer frame; the live viewer is the useful MaterialX-side check for time advancement.
