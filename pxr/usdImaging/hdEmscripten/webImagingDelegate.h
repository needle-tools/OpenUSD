#ifndef PXR_USD_IMAGING_HD_EMSCRIPTEN_WEB_IMAGING_DELEGATE_H
#define PXR_USD_IMAGING_HD_EMSCRIPTEN_WEB_IMAGING_DELEGATE_H

#include "pxr/usdImaging/usdImaging/delegate.h"
#include "pxr/usdImaging/usdImaging/materialParamUtils.h"
#include "pxr/usd/usd/notice.h"
#include "pxr/usd/usd/colorSpaceAPI.h"
#include "pxr/usd/usdShade/shader.h"
#include "pxr/usd/usdGeom/nurbsCurves.h"
#include "pxr/usd/usdGeom/nurbsPatch.h"
#include "pxr/imaging/hd/material.h"
#include "pxr/imaging/hd/changeTracker.h"
#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/tokens.h"
#include "webMdlMaterial.h"
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <set>

PXR_NAMESPACE_OPEN_SCOPE

// USD-specific recovery belongs before Hydra's legacy scene-index bridge.
// Renderers receive an ordinary material node or a constant primvar, including
// when Sync is called through HdSceneIndexAdapterSceneDelegate.
class WebImagingDelegate final : public UsdImagingDelegate {
public:
    WebImagingDelegate(HdRenderIndex* index, SdfPath const& id,
                       UsdStageRefPtr const& stage)
        : UsdImagingDelegate(index, id), _stage(stage) {
        _notice = TfNotice::Register(TfCreateWeakPtr(this),
            &WebImagingDelegate::_OnObjectsChanged, stage);
    }
    ~WebImagingDelegate() override { TfNotice::Revoke(_notice); }

    void SetTime(UsdTimeCode time) {
        if (time != GetTime()) _timeDirty.store(true);
        UsdImagingDelegate::SetTime(time);
    }

    void ApplyPendingUpdates() {
        UsdImagingDelegate::ApplyPendingUpdates();
        const bool timeChanged = _timeDirty.exchange(false);
        const bool materialsChanged = _materialsDirty.exchange(false);
        std::lock_guard<std::mutex> lock(_mutex);
        auto& tracker = GetRenderIndex().GetChangeTracker();
        for (auto const& entry : _materials) {
            if ((materialsChanged || (timeChanged && entry.second)) &&
                GetRenderIndex().GetSprim(HdPrimTypeTokens->material, entry.first))
                tracker.MarkSprimDirty(entry.first, HdMaterial::DirtyResource);
        }
        for (auto const& entry : _nurbs) {
            bool edited = false;
            for (auto const& path : _changedPaths) {
                if (path.HasPrefix(entry.second.path) ||
                    entry.second.path.HasPrefix(path)) { edited = true; break; }
            }
            if ((edited || (timeChanged && entry.second.varying)) &&
                GetRenderIndex().HasRprim(entry.first)) {
                tracker.MarkRprimDirty(entry.first,
                    HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyTopology);
            }
        }
        _changedPaths.clear();
    }

    VtValue GetMaterialResource(SdfPath const& id) override {
        VtValue resource = _GetMaterialResource(id);
        if (resource.IsHolding<HdMaterialNetworkMap>()) {
            HdMaterialNetworkMap network = resource.UncheckedRemove<HdMaterialNetworkMap>();
            _AddInheritedColorSpaces(&network);
            return VtValue(network);
        }
        return resource;
    }

    VtValue _GetMaterialResource(SdfPath const& id) {
        const SdfPath path = ConvertIndexPathToCachePath(id);
        const UsdShadeMaterial material(_stage->GetPrimAtPath(path));
        if (!material) return UsdImagingDelegate::GetMaterialResource(id);
        auto contexts = GetRenderIndex().GetRenderDelegate()->GetMaterialRenderContexts();
        const auto selected = material.ComputeSurfaceSource(contexts);
        const auto preview = HdEmscriptenGetPreferredPreviewSurface(material, contexts);
        const auto mdl = HdEmscriptenGetMdlMaterial(_stage, path, GetTime(), contexts);
        if (!mdl.empty()) {
            _TrackMaterial(id, selected);
            HdMaterialNetworkMap network;
            HdMaterialNode node;
            node.path = selected.GetPath();
            node.identifier = TfToken("needle:MdlSurface");
            node.parameters[TfToken("document")] = VtValue(mdl);
            network.map[HdMaterialTerminalTokens->surface].nodes.push_back(node);
            network.terminals.push_back(node.path);
            network.config = HdEmscriptenGetMaterialConfig(material);
            return VtValue(network);
        }
        if (preview && selected && preview.GetPath() != selected.GetPath()) {
            _TrackMaterial(id, preview);
            contexts.erase(std::remove(contexts.begin(), contexts.end(),
                TfToken("mdl")), contexts.end());
            ArResolverContextBinder binder(_stage->GetPathResolverContext());
            HdMaterialNetworkMap network;
            const auto sourceTypes = GetRenderIndex().GetRenderDelegate()->GetShaderSourceTypes();
            UsdImagingBuildHdMaterialNetworkFromTerminal(preview.GetPrim(),
                HdMaterialTerminalTokens->surface, sourceTypes, contexts,
                &network, GetTime());
            if (const auto displacement = material.ComputeDisplacementSource(contexts))
                UsdImagingBuildHdMaterialNetworkFromTerminal(displacement.GetPrim(),
                    HdMaterialTerminalTokens->displacement, sourceTypes, contexts,
                    &network, GetTime());
            network.config = HdEmscriptenGetMaterialConfig(material);
            return VtValue(network);
        }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _materials.erase(id);
        }
        return UsdImagingDelegate::GetMaterialResource(id);
    }

    VtValue Get(SdfPath const& id, TfToken const& key) override {
        if (key == TfToken("needle:geometry")) return _Geometry(id);
        return UsdImagingDelegate::Get(id, key);
    }
    VtValue GetIndexedPrimvar(SdfPath const& id, TfToken const& key,
                             VtIntArray* indices) override {
        if (key == TfToken("needle:geometry")) {
            indices->clear();
            return _Geometry(id);
        }
        return UsdImagingDelegate::GetIndexedPrimvar(id, key, indices);
    }
    HdPrimvarDescriptorVector GetPrimvarDescriptors(SdfPath const& id,
                                                   HdInterpolation interpolation) override {
        auto result = UsdImagingDelegate::GetPrimvarDescriptors(id, interpolation);
        if (interpolation == HdInterpolationConstant && !_Geometry(id).IsEmpty())
            result.emplace_back(TfToken("needle:geometry"), interpolation, TfToken());
        return result;
    }

private:
    // UsdImagingDelegate's material networks carry a parameter's color space only when
    // the attribute itself has colorSpace metadata (usdImaging/materialParamUtils.cpp).
    // The scene-index path, which Storm uses, also resolves UsdColorSpaceAPI on the
    // enclosing prims. Add those inherited color spaces as 'colorSpace:<input>' so
    // HdMtlx and the JS material bridge see the same color spaces as Storm. Only
    // explicitly applied color spaces are added: without one, a texture keeps the
    // UsdUVTexture "auto" rule instead of the fallback lin_rec709_scene.
    void _AddInheritedColorSpaces(HdMaterialNetworkMap* network) const {
        static const TfToken colorSpacePrefix("colorSpace:");
        for (auto& entry : network->map) {
            for (HdMaterialNode& node : entry.second.nodes) {
                const UsdShadeShader shader(_stage->GetPrimAtPath(node.path));
                if (!shader) continue;
                std::vector<std::pair<TfToken, VtValue>> added;
                for (auto const& parameter : node.parameters) {
                    const std::string& name = parameter.first.GetString();
                    if (name.find(':') != std::string::npos) continue;
                    const TfToken key(colorSpacePrefix.GetString() + name);
                    if (node.parameters.count(key)) continue;
                    const UsdShadeInput input = shader.GetInput(parameter.first);
                    if (!input) continue;
                    const UsdAttribute attr = input.GetAttr();
                    if (attr.HasColorSpace()) continue;
                    if (attr.GetRoleName() != SdfValueRoleNames->Color &&
                        attr.GetTypeName() != SdfValueTypeNames->Asset) continue;
                    const TfToken colorSpace = _ApplyingColorSpace(attr.GetPrim());
                    if (!colorSpace.IsEmpty()) added.emplace_back(key, VtValue(colorSpace));
                }
                for (auto const& value : added) node.parameters[value.first] = value.second;
            }
        }
    }

    static TfToken _ApplyingColorSpace(UsdPrim prim) {
        for (; prim; prim = prim.GetParent()) {
            if (!prim.HasAPI<UsdColorSpaceAPI>()) continue;
            TfToken name;
            if (UsdColorSpaceAPI(prim).GetColorSpaceNameAttr().Get(&name) && !name.IsEmpty())
                return name;
        }
        return TfToken();
    }

    struct NurbsSource { SdfPath path; bool varying; };
    UsdStageRefPtr _stage;
    TfNotice::Key _notice;
    std::mutex _mutex;
    std::map<SdfPath, bool> _materials;
    std::map<SdfPath, NurbsSource> _nurbs;
    std::set<SdfPath> _changedPaths;
    std::atomic<bool> _materialsDirty{false}, _timeDirty{false};

    void _TrackMaterial(SdfPath const& id, UsdShadeShader const& shader) {
        const bool varying = shader && UsdImagingIsHdMaterialNetworkTimeVarying(shader.GetPrim());
        std::lock_guard<std::mutex> lock(_mutex);
        _materials[id] = varying;
    }
    void _OnObjectsChanged(UsdNotice::ObjectsChanged const& notice,
                           UsdStageWeakPtr const&) {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto const& path : notice.GetResyncedPaths()) {
            _changedPaths.insert(path.GetPrimPath());
            _materialsDirty.store(true);
        }
        for (auto const& path : notice.GetChangedInfoOnlyPaths()) {
            _changedPaths.insert(path.GetPrimPath());
            const auto name = path.GetName();
            if (name.compare(0, 7, "inputs:") == 0 ||
                name.compare(0, 8, "outputs:") == 0 ||
                name.compare(0, 5, "info:") == 0) _materialsDirty.store(true);
        }
    }
    VtValue _Geometry(SdfPath const& id) {
        SdfPath path = GetScenePrimPath(id, 0);
        UsdPrim prim = _stage->GetPrimAtPath(path);
        if (!prim) prim = _stage->GetPrimAtPath(ConvertIndexPathToCachePath(id));
        const UsdGeomNurbsCurves curves(prim);
        const UsdGeomNurbsPatch patch(prim);
        if (!curves && !patch) return VtValue();
        VtDictionary data;
        bool varying = false;
        const auto read = [&](char const* name, UsdAttribute const& attribute) {
            VtValue value;
            if (attribute.Get(&value, GetTime())) data[name] = value;
            varying |= attribute.ValueMightBeTimeVarying();
        };
        if (curves) {
            data["type"] = VtValue(std::string("nurbs"));
            read("order", curves.GetOrderAttr());
            read("knots", curves.GetKnotsAttr());
            read("ranges", curves.GetRangesAttr());
            read("weights", curves.GetPointWeightsAttr());
        } else {
            data["type"] = VtValue(std::string("nurbsPatch"));
            read("uCount", patch.GetUVertexCountAttr());
            read("vCount", patch.GetVVertexCountAttr());
            read("uOrder", patch.GetUOrderAttr());
            read("vOrder", patch.GetVOrderAttr());
            read("uKnots", patch.GetUKnotsAttr());
            read("vKnots", patch.GetVKnotsAttr());
            read("uRange", patch.GetURangeAttr());
            read("vRange", patch.GetVRangeAttr());
            read("weights", patch.GetPointWeightsAttr());
            read("orientation", patch.GetOrientationAttr());
            read("doubleSided", patch.GetDoubleSidedAttr());
            read("trimCounts", patch.GetTrimCurveCountsAttr());
        }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _nurbs[id] = {prim.GetPath(), varying};
        }
        return VtValue(data);
    }
};

PXR_NAMESPACE_CLOSE_SCOPE
#endif
