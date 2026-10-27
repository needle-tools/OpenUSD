// Focused native test: compile against an installed OpenUSD; no wasm rebuild.
#include "../webMdlMaterial.h"
#include "pxr/usd/usdShade/nodeGraph.h"
#include "pxr/base/gf/vec3f.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

int main()
{
    namespace fs = std::filesystem;
    const auto directory = fs::temp_directory_path() / ("hd-web-mdl-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(directory);
    std::ofstream(directory / "texture.png") << "test asset";
    const auto stage = UsdStage::CreateNew((directory / "material.usda").string());
    const auto material = UsdShadeMaterial::Define(stage, SdfPath("/Material"));
    const auto graph = UsdShadeNodeGraph::Define(stage, SdfPath("/Material/Graph"));
    auto shader = UsdShadeShader::Define(stage, SdfPath("/Material/Graph/Shader"));
    shader.SetSourceAsset(SdfAssetPath("OmniPBR.mdl"), TfToken("mdl"));
    shader.SetSourceAssetSubIdentifier(TfToken("OmniPBR"), TfToken("mdl"));
    const auto output = shader.CreateOutput(TfToken("out"), SdfValueTypeNames->Token);
    material.CreateSurfaceOutput(TfToken("mdl")).ConnectToSource(output);
    const auto tint = material.CreateInput(TfToken("tint"), SdfValueTypeNames->Color3f);
    tint.Set(GfVec3f(.2f, .4f, .6f));
    const auto graphTint = graph.CreateInput(TfToken("tint"), SdfValueTypeNames->Color3f);
    graphTint.ConnectToSource(tint);
    shader.CreateInput(TfToken("diffuse_color_constant"), SdfValueTypeNames->Color3f)
        .ConnectToSource(graphTint);
    const auto texture = graph.CreateInput(TfToken("texture"), SdfValueTypeNames->Asset);
    texture.Set(SdfAssetPath("./texture.png"));
    texture.GetAttr().SetColorSpace(TfToken("sRGB"));
    shader.CreateInput(TfToken("diffuse_texture"), SdfValueTypeNames->Asset)
        .ConnectToSource(texture);
    const auto roughness = shader.CreateInput(TfToken("reflection_roughness_constant"), SdfValueTypeNames->Float);
    roughness.Set(.25f, UsdTimeCode(1));
    roughness.Set(.75f, UsdTimeCode(2));
    const TfTokenVector contexts{TfToken("mtlx"), TfToken("mdl"), TfToken()};
    auto read = [&](double time) {
        return HdEmscriptenGetMdlMaterial(stage, material.GetPath(), UsdTimeCode(time), contexts);
    };
    auto data = read(1);
    assert(data.find("sourceAsset")->second.Get<std::string>() == "OmniPBR.mdl");
    assert(data.find("subIdentifier")->second.Get<std::string>() == "OmniPBR");
    auto inputs = data.find("inputs")->second.Get<VtDictionary>();
    assert(inputs.find("diffuse_color_constant")->second.Get<GfVec3f>() == GfVec3f(.2f, .4f, .6f));
    assert(inputs.find("reflection_roughness_constant")->second.Get<float>() == .25f);
    assert(data.find("resolvedPaths")->second.Get<VtDictionary>().find("diffuse_texture")->second.Get<std::string>() == (directory / "texture.png").string());
    assert(data.find("colorSpaces")->second.Get<VtDictionary>().find("diffuse_texture")->second.Get<TfToken>() == TfToken("sRGB"));
    assert(data.find("unsupportedInputs")->second.Get<VtStringArray>().empty());
    assert(read(2).find("inputs")->second.Get<VtDictionary>().find("reflection_roughness_constant")->second.Get<float>() == .75f);
    tint.Set(GfVec3f(.8f, .7f, .6f));
    assert(read(1).find("inputs")->second.Get<VtDictionary>().find("diffuse_color_constant")->second.Get<GfVec3f>() == GfVec3f(.8f, .7f, .6f));

    // glTF lookup nodes retain authored options without evaluating MDL.
    auto lookup = UsdShadeShader::Define(stage, SdfPath("/Material/Lookup"));
    lookup.SetSourceAsset(SdfAssetPath("gltf/pbr.mdl"), TfToken("mdl"));
    lookup.SetSourceAssetSubIdentifier(TfToken("gltf_texture_lookup"), TfToken("mdl"));
    lookup.CreateInput(TfToken("texture"), SdfValueTypeNames->Asset).ConnectToSource(texture);
    lookup.CreateInput(TfToken("tex_coord_index"), SdfValueTypeNames->Int).Set(1);
    const auto lookupOutput = lookup.CreateOutput(TfToken("out"), SdfValueTypeNames->Token);
    shader.CreateInput(TfToken("base_color_texture"), SdfValueTypeNames->Token).ConnectToSource(lookupOutput);
    data = read(1);
    const auto lookups = data.find("textureInputs")->second.Get<VtDictionary>();
    const auto lookupData = lookups.find("base_color_texture")->second.Get<VtDictionary>();
    assert(lookupData.find("sourceAsset")->second.Get<std::string>() == "gltf/pbr.mdl");
    assert(lookupData.find("inputs")->second.Get<VtDictionary>().find("tex_coord_index")->second.Get<int>() == 1);
    assert(lookupData.find("resolvedPaths")->second.Get<VtDictionary>().find("texture")->second.Get<std::string>() == (directory / "texture.png").string());
    assert(data.find("unsupportedInputs")->second.Get<VtStringArray>().empty());

    // Procedural shader outputs are never mistaken for interface constants.
    auto procedural = UsdShadeShader::Define(stage, SdfPath("/Material/Noise"));
    roughness.ConnectToSource(procedural.CreateOutput(TfToken("result"), SdfValueTypeNames->Float));
    data = read(1);
    assert(data.find("inputs")->second.Get<VtDictionary>().count("reflection_roughness_constant") == 0);
    assert(data.find("unsupportedInputs")->second.Get<VtStringArray>() == VtStringArray{"reflection_roughness_constant"});
    roughness.GetAttr().SetConnections({SdfPath("/Missing.outputs:result")});
    assert(read(1).find("unsupportedInputs")->second.Get<VtStringArray>() == VtStringArray{"reflection_roughness_constant"});

    // Unknown modules retain identity so the consumer can diagnose/fallback.
    shader.SetSourceAsset(SdfAssetPath("Unknown.mdl"), TfToken("mdl"));
    assert(read(1).find("sourceAsset")->second.Get<std::string>() == "Unknown.mdl");
    // A valid universal PreviewSurface remains preferred to the MDL approximation.
    auto preview = UsdShadeShader::Define(stage, SdfPath("/Material/Preview"));
    preview.SetShaderId(TfToken("UsdPreviewSurface"));
    material.CreateSurfaceOutput().ConnectToSource(preview.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token));
    assert(read(1).empty());
    assert(HdEmscriptenGetPreferredPreviewSurface(material, contexts).GetPath() == preview.GetPath());
    // A selected MaterialX surface retains priority over both alternatives.
    auto mtlx = UsdShadeShader::Define(stage, SdfPath("/Material/MaterialX"));
    mtlx.SetShaderId(TfToken("ND_standard_surface_surfaceshader"));
    material.CreateSurfaceOutput(TfToken("mtlx")).ConnectToSource(mtlx.CreateOutput(TfToken("out"), SdfValueTypeNames->Token));
    assert(read(1).empty());
    assert(!HdEmscriptenGetPreferredPreviewSurface(material, contexts));
    fs::remove_all(directory);
    std::cout << "MDL authored extraction: interfaces, samples, assets, colorSpace, unsupported connections, and context preference passed\n";
}
