#ifndef PXR_USD_IMAGING_HD_EMSCRIPTEN_WEB_MDL_MATERIAL_H
#define PXR_USD_IMAGING_HD_EMSCRIPTEN_WEB_MDL_MATERIAL_H

#include "pxr/pxr.h"
#include "pxr/base/vt/dictionary.h"
#include "pxr/base/vt/array.h"
#include "pxr/usd/ar/resolverContextBinder.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdShade/material.h"
#include "pxr/usd/usdShade/shader.h"
#include "pxr/usd/usdShade/input.h"
#include <algorithm>

PXR_NAMESPACE_OPEN_SCOPE

// Match UsdImagingMaterialAdapter: strip the config namespace, preserve value
// types and nested names, and read attributes at default time (not draw time).
inline VtDictionary
HdEmscriptenGetMaterialConfig(UsdShadeMaterial const &material)
{
    VtDictionary config;
    const TfToken prefix("config:");
    for (const auto &property : material.GetPrim().GetPropertiesInNamespace(prefix)) {
        const auto attribute = property.As<UsdAttribute>();
        if (!attribute) {
            continue;
        }
        const auto name = SdfPath::StripPrefixNamespace(
            attribute.GetName().GetString(), prefix);
        VtValue value;
        attribute.Get(&value);
        config.insert({name.first, value});
    }
    return config;
}

// A real authored PreviewSurface is preferable to an MDL approximation.
inline UsdShadeShader
HdEmscriptenGetPreferredPreviewSurface(
    UsdShadeMaterial const &material, TfTokenVector renderContexts)
{
    renderContexts.erase(std::remove(renderContexts.begin(), renderContexts.end(),
                                     TfToken("mdl")), renderContexts.end());
    const auto shader = material.ComputeSurfaceSource(renderContexts);
    TfToken identifier;
    if (shader && shader.GetShaderId(&identifier) &&
        identifier == TfToken("UsdPreviewSurface")) {
        return shader;
    }
    return UsdShadeShader();
}

// Read authored values without an MDL Sdr plugin. This is deliberately not an
// MDL evaluator. UsdShade resolves material/nodegraph interface chains and the
// layer-relative asset paths. Only one level of known glTF texture lookup is
// serialized; JavaScript validates its module and supported lookup parameters.
inline VtDictionary
HdEmscriptenReadMdlShader(UsdShadeShader const &shader, UsdTimeCode time,
                         bool allowTextureLookup = true)
{
    const TfToken mdl("mdl");
    SdfAssetPath sourceAsset;
    shader.GetSourceAsset(&sourceAsset, mdl);
    TfToken subIdentifier;
    shader.GetSourceAssetSubIdentifier(&subIdentifier, mdl);

    VtDictionary inputs, resolvedPaths, colorSpaces, textureInputs;
    VtStringArray unsupportedInputs;
    for (const UsdShadeInput &input : shader.GetInputs()) {
        const std::string name = input.GetBaseName().GetString();
        const auto sources = input.GetValueProducingAttributes();
        SdfPathVector connections;
        input.GetAttr().GetConnections(&connections);
        if (!connections.empty() && sources.size() == 1 &&
            sources.front() == input.GetAttr()) {
            unsupportedInputs.push_back(name);
            continue;
        }
        if (sources.empty()) {
            // A missing optional input uses the known family's default; an
            // unresolved authored connection must not silently do so.
            if (!connections.empty()) {
                unsupportedInputs.push_back(name);
            }
            continue;
        }
        if (sources.size() != 1 || !UsdShadeInput::IsInput(sources.front())) {
            if (allowTextureLookup && sources.size() == 1) {
                const UsdShadeShader lookup(sources.front().GetPrim());
                TfToken lookupId;
                if (lookup) {
                    lookup.GetSourceAssetSubIdentifier(&lookupId, mdl);
                }
                const std::string id = lookupId.GetString();
                const auto separator = id.rfind("::");
                const std::string nameOnly = separator == std::string::npos
                    ? id : id.substr(separator + 2);
                if (nameOnly == "gltf_texture_lookup" ||
                    nameOnly == "gltf_normal_texture_lookup") {
                    textureInputs[name] = VtValue(
                        HdEmscriptenReadMdlShader(lookup, time, false));
                    continue;
                }
            }
            unsupportedInputs.push_back(name);
            continue;
        }
        const UsdAttribute &source = sources.front();
        VtValue value;
        if (!source.Get(&value, time)) {
            continue;
        }
        inputs[name] = value;
        if (value.IsHolding<SdfAssetPath>()) {
            resolvedPaths[name] = VtValue(
                value.UncheckedGet<SdfAssetPath>().GetResolvedPath());
        }
        TfToken colorSpace = input.GetAttr().GetColorSpace();
        if (colorSpace.IsEmpty()) {
            colorSpace = source.GetColorSpace();
        }
        if (!colorSpace.IsEmpty()) {
            colorSpaces[name] = VtValue(colorSpace);
        }
    }
    return {
        {"sourceAsset", VtValue(sourceAsset.GetAssetPath())},
        {"subIdentifier", VtValue(subIdentifier.GetString())},
        {"inputs", VtValue(inputs)},
        {"resolvedPaths", VtValue(resolvedPaths)},
        {"colorSpaces", VtValue(colorSpaces)},
        {"unsupportedInputs", VtValue(unsupportedInputs)},
        {"textureInputs", VtValue(textureInputs)}
    };
}

inline VtDictionary
HdEmscriptenGetMdlMaterial(
    UsdStageRefPtr const &stage, SdfPath const &materialPath,
    UsdTimeCode time, TfTokenVector const &renderContexts)
{
    if (!stage || materialPath.IsEmpty()) {
        return {};
    }
    const UsdShadeMaterial material(stage->GetPrimAtPath(materialPath));
    if (!material || HdEmscriptenGetPreferredPreviewSurface(material, renderContexts)) {
        return {};
    }
    ArResolverContextBinder binder(stage->GetPathResolverContext());
    const UsdShadeShader shader = material.ComputeSurfaceSource(renderContexts);
    if (!shader) {
        return {};
    }
    const TfToken mdl("mdl");
    SdfAssetPath sourceAsset;
    const bool hasMdlAsset = shader.GetSourceAsset(&sourceAsset, mdl);
    const auto mdlOutput = material.GetSurfaceOutput(mdl);
    const auto sources = mdlOutput ? mdlOutput.GetValueProducingAttributes(true)
                                  : UsdShadeAttributeVector();
    const bool isMdlTerminal = !sources.empty() &&
        sources.front().GetPrim() == shader.GetPrim();
    if (!hasMdlAsset && !isMdlTerminal) {
        return {};
    }
    VtDictionary data = HdEmscriptenReadMdlShader(shader, time);
    data["renderContext"] = VtValue(std::string("mdl"));
    return data;
}

PXR_NAMESPACE_CLOSE_SCOPE

#endif
