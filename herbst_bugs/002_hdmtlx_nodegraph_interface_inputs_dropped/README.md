# NodeGraph Interface Input Connection Is Dropped In Generated MaterialX

Suggested issue title:

`HdMtlx: NodeGraph interface input connection is omitted from generated MaterialX document`

## Summary

`interface_input_distance_fails.usda` defines a MaterialX-style USDShade `NodeGraph` with an interface input:

```usda
float3 inputs:center = (0, 100, 0)
```

Inside the graph, a shader consumes it:

```usda
float3 inputs:in2.connect = </Root/Looks/DistanceMaterial/Graph.inputs:center>
```

Expected: the generated MaterialX document should preserve this as either an interface input or an equivalent value/constant feeding `distance.in2`.

Actual: the generated MaterialX document omits `distance.in2` entirely. The control scene, `constant_node_distance_control.usda`, uses an explicit `ND_constant_vector3` shader instead of the NodeGraph interface input; in that case the generated MaterialX keeps `distance.in2`.

## Repro Commands

```sh
OPENUSD=/Users/herbst/OpenUSD-26.05-native

TF_DEBUG=HDMTLX_WRITE_DOCUMENT \
  $OPENUSD/bin/usdrecord --renderer GL --imageWidth 256 \
  interface_input_distance_fails.usda interface_input_distance_fails.png

cp DistanceMaterial_Surface.mtlx interface_input_distance_fails.generated.mtlx

TF_DEBUG=HDMTLX_WRITE_DOCUMENT \
  $OPENUSD/bin/usdrecord --renderer GL --imageWidth 256 \
  constant_node_distance_control.usda constant_node_distance_control.png

cp DistanceMaterial_Surface.mtlx constant_node_distance_control.generated.mtlx
```

## Key Difference

Failing generated MTLX:

```xml
<distance name="..." type="float" nodedef="ND_distance_vector3">
  <input name="in1" type="vector3" nodename="..." />
</distance>
```

Control generated MTLX:

```xml
<distance name="..." type="float" nodedef="ND_distance_vector3">
  <input name="in1" type="vector3" nodename="..." />
  <input name="in2" type="vector3" nodename="..." />
</distance>
```

## Artifacts

- Failing scene: `interface_input_distance_fails.usda`
- Control scene: `constant_node_distance_control.usda`
- Generated failing MaterialX: `interface_input_distance_fails.generated.mtlx`
- Generated control MaterialX: `constant_node_distance_control.generated.mtlx`
- Renders: `interface_input_distance_fails.png`, `constant_node_distance_control.png`
- Standalone MaterialX graph that preserves the graph interface input with `interfacename`: `interface_input_distance_materialx_equivalent.mtlx`
- MaterialXView screenshot: `interface_input_distance_materialx_equivalent.png`

## Why This Should Work

USDShade `NodeGraph` inputs are part of the graph interface and connections to them should be preserved when translating the USD material network into MaterialX. Dropping the input changes graph semantics and causes downstream shaders to use nodedef defaults instead of authored values.

The standalone MaterialX file uses the equivalent compound-graph shape:

```xml
<input name="center" type="vector3" value="0,100,0" />
<input name="in2" type="vector3" interfacename="center" />
```

MaterialXView renders that graph successfully. The failure is the HdMtlx-generated document from the USDShade source graph, which omits the `distance.in2` input.
