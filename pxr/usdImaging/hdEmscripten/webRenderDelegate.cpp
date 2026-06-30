//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "webRenderDelegate.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/vt/typeHeaders.h"
#include "pxr/imaging/hd/bufferArray.h"
#include "pxr/imaging/hd/camera.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/imaging/hd/material.h"
#include "pxr/imaging/hd/mesh.h"
#include "pxr/imaging/hd/points.h"
#include "pxr/imaging/hd/bprim.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/repr.h"
#include "pxr/imaging/hd/resourceRegistry.h"
#include "pxr/imaging/hd/unitTestNullRenderPass.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/materialNetwork2Interface.h"
#include "pxr/imaging/hd/smoothNormals.h"
#include "pxr/imaging/hd/vtBufferSource.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/pxOsd/refinerFactory.h"
#include "pxr/imaging/pxOsd/tokens.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/sdf/assetPath.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/range1f.h"
#include "pxr/base/gf/quaternion.h"

#include <opensubdiv/far/primvarRefiner.h>
#include <opensubdiv/far/topologyLevel.h>

#if __has_include("pxr/imaging/hdMtlx/hdMtlx.h") && __has_include(<MaterialXFormat/XmlIo.h>)
#include "pxr/imaging/hdMtlx/hdMtlx.h"
#include <MaterialXFormat/XmlIo.h>
#define HD_EMSCRIPTEN_HAS_MATERIALX 1
#endif

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <vector>

using namespace emscripten;

PXR_NAMESPACE_OPEN_SCOPE

const std::map<HdInterpolation, std::string> InterpolationStrings = {
    {HdInterpolationConstant, "constant"},
    {HdInterpolationUniform, "uniform"},
    {HdInterpolationVarying, "varying"},
    {HdInterpolationVertex, "vertex"},
    {HdInterpolationFaceVarying, "facevarying"},
    {HdInterpolationInstance, "instance"}
};

void _runInMainThread(int funPointer) {
    std::function<void()>  *function = reinterpret_cast<std::function<void()>*>(funPointer);
    (*function)();
}

// Only the main thread can communicate with the JS interpreter (other threads run in web workers).
// All direct invocations of JS functions need to go through the main thread.
void runInMainThread(std::function<void()> fun) {
    emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_VI, _runInMainThread, (void *) &fun);
}

bool _HasJsMethod(emscripten::val const& object, char const* name) {
    if (object.isUndefined() || object.isNull()) {
        return false;
    }
    emscripten::val method = object[name];
    return !method.isUndefined() && method.typeOf().as<std::string>() == "function";
}

std::string _CullStyleToString(HdCullStyle cullStyle)
{
    switch (cullStyle) {
        case HdCullStyleDontCare:
            return "dontCare";
        case HdCullStyleNothing:
            return "nothing";
        case HdCullStyleBack:
            return "back";
        case HdCullStyleFront:
            return "front";
        case HdCullStyleBackUnlessDoubleSided:
            return "backUnlessDoubleSided";
        case HdCullStyleFrontUnlessDoubleSided:
            return "frontUnlessDoubleSided";
    }
    return "dontCare";
}

template <class Vec>
emscripten::val _GfVecToJsVal(Vec const &vec)
{
    emscripten::val jsArray = emscripten::val::array();
    for (size_t i = 0; i < Vec::dimension; ++i) {
        jsArray.set(i, vec[i]);
    }
    return jsArray;
}

template <class T>
emscripten::val _ValueToJsVal(T const &value)
{
    return emscripten::val(value);
}

emscripten::val _ValueToJsVal(TfToken const &value)
{
    return emscripten::val(value.GetString());
}

emscripten::val _ValueToJsVal(SdfAssetPath const &value)
{
    return emscripten::val(value.GetAssetPath());
}

emscripten::val _MatrixToJsVal(GfMatrix4f const& matrix)
{
    return emscripten::val(
        emscripten::typed_memory_view(16, reinterpret_cast<float const*>(matrix.data())));
}

template <class T>
emscripten::val _VtArrayToJsVal(VtArray<T> const &array)
{
    emscripten::val jsArray = emscripten::val::array();
    for (size_t i = 0; i < array.size(); ++i) {
        jsArray.set(i, _ValueToJsVal(array[i]));
    }
    return jsArray;
}

emscripten::val _VtValueToJsVal(VtValue const &value)
{
#define HD_EMSCRIPTEN_CONVERT_SCALAR(T) \
    if (value.IsHolding<T>()) { \
        return _ValueToJsVal(value.UncheckedGet<T>()); \
    }

#define HD_EMSCRIPTEN_CONVERT_VEC(T) \
    if (value.IsHolding<T>()) { \
        return _GfVecToJsVal(value.UncheckedGet<T>()); \
    }

#define HD_EMSCRIPTEN_CONVERT_ARRAY(T) \
    if (value.IsHolding<VtArray<T>>()) { \
        return _VtArrayToJsVal(value.UncheckedGet<VtArray<T>>()); \
    }

    HD_EMSCRIPTEN_CONVERT_SCALAR(bool)
    HD_EMSCRIPTEN_CONVERT_SCALAR(int)
    HD_EMSCRIPTEN_CONVERT_SCALAR(float)
    HD_EMSCRIPTEN_CONVERT_SCALAR(double)
    HD_EMSCRIPTEN_CONVERT_SCALAR(std::string)
    HD_EMSCRIPTEN_CONVERT_SCALAR(TfToken)
    HD_EMSCRIPTEN_CONVERT_SCALAR(SdfAssetPath)

    HD_EMSCRIPTEN_CONVERT_VEC(GfVec2f)
    HD_EMSCRIPTEN_CONVERT_VEC(GfVec3f)
    HD_EMSCRIPTEN_CONVERT_VEC(GfVec4f)
    HD_EMSCRIPTEN_CONVERT_VEC(GfVec2d)
    HD_EMSCRIPTEN_CONVERT_VEC(GfVec3d)
    HD_EMSCRIPTEN_CONVERT_VEC(GfVec4d)
    HD_EMSCRIPTEN_CONVERT_VEC(GfVec2i)
    HD_EMSCRIPTEN_CONVERT_VEC(GfVec3i)
    HD_EMSCRIPTEN_CONVERT_VEC(GfVec4i)

    HD_EMSCRIPTEN_CONVERT_ARRAY(bool)
    HD_EMSCRIPTEN_CONVERT_ARRAY(int)
    HD_EMSCRIPTEN_CONVERT_ARRAY(float)
    HD_EMSCRIPTEN_CONVERT_ARRAY(double)
    HD_EMSCRIPTEN_CONVERT_ARRAY(std::string)
    HD_EMSCRIPTEN_CONVERT_ARRAY(TfToken)
    HD_EMSCRIPTEN_CONVERT_ARRAY(SdfAssetPath)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfVec2f)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfVec3f)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfVec4f)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfVec2d)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfVec3d)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfVec4d)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfVec2i)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfVec3i)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfVec4i)

#undef HD_EMSCRIPTEN_CONVERT_ARRAY
#undef HD_EMSCRIPTEN_CONVERT_VEC
#undef HD_EMSCRIPTEN_CONVERT_SCALAR

    TF_WARN("Unsupported material parameter type '%s'",
        ArchGetDemangled(value.GetTypeid()).c_str());
    return emscripten::val::undefined();
}

class Emscripten_Instancer final : public HdInstancer {
public:
    Emscripten_Instancer(HdSceneDelegate* delegate, SdfPath const& id)
        : HdInstancer(delegate, id)
        , _visible(true)
    {
    }

    void Sync(HdSceneDelegate *delegate,
              HdRenderParam *renderParam,
              HdDirtyBits *dirtyBits) override
    {
        if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
            _visible = delegate->GetVisible(GetId());
        }

        _UpdateInstancer(delegate, dirtyBits);

        if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, GetId())) {
            _SyncPrimvars(delegate, *dirtyBits);
        }
    }

    VtMatrix4dArray ComputeInstanceTransforms(SdfPath const &prototypeId)
    {
        if (!_visible) {
            return {};
        }

        HdSceneDelegate *delegate = GetDelegate();
        VtIntArray instanceIndices =
            delegate->GetInstanceIndices(GetId(), prototypeId);
        GfMatrix4d instancerTransform =
            delegate->GetInstancerTransform(GetId());

        VtMatrix4dArray transforms(instanceIndices.size());
        for (size_t i = 0; i < instanceIndices.size(); ++i) {
            transforms[i] = instancerTransform;
        }

        _ApplyTranslations(instanceIndices, &transforms);
        _ApplyRotations(instanceIndices, &transforms);
        _ApplyScales(instanceIndices, &transforms);
        _ApplyTransforms(instanceIndices, &transforms);

        if (GetParentId().IsEmpty()) {
            return transforms;
        }

        HdInstancer *parentInstancer =
            delegate->GetRenderIndex().GetInstancer(GetParentId());
        Emscripten_Instancer *parent =
            dynamic_cast<Emscripten_Instancer*>(parentInstancer);
        if (!parent) {
            return transforms;
        }

        VtMatrix4dArray parentTransforms =
            parent->ComputeInstanceTransforms(GetId());
        VtMatrix4dArray final(parentTransforms.size() * transforms.size());
        for (size_t i = 0; i < parentTransforms.size(); ++i) {
            for (size_t j = 0; j < transforms.size(); ++j) {
                final[i * transforms.size() + j] =
                    transforms[j] * parentTransforms[i];
            }
        }
        return final;
    }

private:
    std::map<TfToken, VtValue> _primvarMap;
    bool _visible;

    void _SyncPrimvars(HdSceneDelegate *delegate, HdDirtyBits dirtyBits)
    {
        SdfPath const& id = GetId();
        HdPrimvarDescriptorVector primvars =
            delegate->GetPrimvarDescriptors(id, HdInterpolationInstance);

        for (HdPrimvarDescriptor const& primvar : primvars) {
            if (!HdChangeTracker::IsPrimvarDirty(dirtyBits, id, primvar.name)) {
                continue;
            }

            VtValue value = delegate->Get(id, primvar.name);
            if (value.IsEmpty()) {
                _primvarMap.erase(primvar.name);
            } else {
                _primvarMap[primvar.name] = value;
            }
        }
    }

    template <class ArrayT, class ValueT>
    static bool _SampleArray(
        VtValue const &value,
        int index,
        ValueT *outValue)
    {
        if (!value.IsHolding<ArrayT>() || index < 0) {
            return false;
        }

        ArrayT const &array = value.UncheckedGet<ArrayT>();
        if (static_cast<size_t>(index) >= array.size()) {
            return false;
        }

        *outValue = array[index];
        return true;
    }

    VtValue const *_GetPrimvar(TfToken const &name) const
    {
        auto const it = _primvarMap.find(name);
        return it == _primvarMap.end() ? nullptr : &it->second;
    }

    void _ApplyTranslations(
        VtIntArray const &indices,
        VtMatrix4dArray *transforms) const
    {
        VtValue const *value = _GetPrimvar(HdInstancerTokens->instanceTranslations);
        if (!value) {
            return;
        }

        for (size_t i = 0; i < indices.size(); ++i) {
            GfVec3f translate;
            if (_SampleArray<VtVec3fArray>(*value, indices[i], &translate)) {
                GfMatrix4d matrix(1);
                matrix.SetTranslate(GfVec3d(translate));
                (*transforms)[i] = matrix * (*transforms)[i];
            }
        }
    }

    void _ApplyRotations(
        VtIntArray const &indices,
        VtMatrix4dArray *transforms) const
    {
        VtValue const *value = _GetPrimvar(HdInstancerTokens->instanceRotations);
        if (!value) {
            return;
        }

        for (size_t i = 0; i < indices.size(); ++i) {
            GfVec4f quat;
            if (_SampleArray<VtVec4fArray>(*value, indices[i], &quat)) {
                GfMatrix4d matrix(1);
                matrix.SetRotate(GfQuatd(
                    quat[0], quat[1], quat[2], quat[3]));
                (*transforms)[i] = matrix * (*transforms)[i];
            }
        }
    }

    void _ApplyScales(
        VtIntArray const &indices,
        VtMatrix4dArray *transforms) const
    {
        VtValue const *value = _GetPrimvar(HdInstancerTokens->instanceScales);
        if (!value) {
            return;
        }

        for (size_t i = 0; i < indices.size(); ++i) {
            GfVec3f scale;
            if (_SampleArray<VtVec3fArray>(*value, indices[i], &scale)) {
                GfMatrix4d matrix(1);
                matrix.SetScale(GfVec3d(scale));
                (*transforms)[i] = matrix * (*transforms)[i];
            }
        }
    }

    void _ApplyTransforms(
        VtIntArray const &indices,
        VtMatrix4dArray *transforms) const
    {
        VtValue const *value = _GetPrimvar(HdInstancerTokens->instanceTransforms);
        if (!value) {
            return;
        }

        for (size_t i = 0; i < indices.size(); ++i) {
            GfMatrix4d matrix;
            if (_SampleArray<VtMatrix4dArray>(*value, indices[i], &matrix)) {
                (*transforms)[i] = matrix * (*transforms)[i];
            }
        }
    }
};

class Emscripten_Rprim final : public HdMesh {
public:
    Emscripten_Rprim(TfToken const& typeId,
                 SdfPath const& id,
                 emscripten::val renderDelegateInterface)
     : HdMesh(id)
     , _typeId(typeId)
     , _renderDelegateInterface(renderDelegateInterface)
     , _rPrim(val::undefined())
     , _meshUtil(NULL)
     , _adjacencyValid(false)
     , _normalsValid(false)
     , _reprFlatShadingEnabled(false)
     , _displayStyleFlatShadingEnabled(false)
    {
      _rPrim = _renderDelegateInterface.call<val>("createRPrim", std::string(typeId.GetText()), id.GetAsString());
    }

    virtual ~Emscripten_Rprim() {
      if (_meshUtil != NULL) {
        delete _meshUtil;
        _meshUtil = NULL;
      }
    }

    struct Section {
        int start;
        int length;
        std::string materialId;
    };

    emscripten::val sectionsToJSArray(const std::vector<Section>& sections) {
        emscripten::val jsArray = emscripten::val::array();
        for (const auto& section : sections) {
            emscripten::val jsObj = emscripten::val::object();
            jsObj.set("start", section.start);
            jsObj.set("length", section.length);
            jsObj.set("materialId", section.materialId);
            jsArray.call<void>("push", jsObj);
        }
        return jsArray;
    }

    void findContiguousSections(const VtArray<int>& faces, std::string& materialId, std::vector<Section>& sections, const VtArray<int>& faceVertexCounts) {
        if (faces.empty()) return; // Return early if the input vector is empty

        int currentStart = 0;
        for (size_t i = 0; i < faces[0]; ++i) {
            currentStart += (faceVertexCounts[i] - 2) * 3;
        }

        int currentLength = (faceVertexCounts[0] - 2) * 3;

        for (size_t i = 1; i < faces.size(); ++i) {
            if (faces[i] == faces[i - 1] + 1) {
                currentLength += (faceVertexCounts[i] - 2) * 3;
            } else {
                sections.push_back({currentStart, currentLength, materialId});
                currentStart = currentLength;
                currentLength = (faceVertexCounts[i] - 2) * 3;
            }
        }

        sections.push_back({currentStart, currentLength, materialId});

        return;
    }

    virtual void Sync(HdSceneDelegate *delegate,
                      HdRenderParam   *renderParam,
                      HdDirtyBits     *dirtyBits,
                      TfToken const   &reprToken) override
    {
        // Get the id of this mesh. This is used to get various resources associated with it.
        SdfPath const& id = GetId();

        _UpdateVisibility(delegate, dirtyBits);
        _UpdateInstancer(delegate, dirtyBits);
        TfToken const renderTag = GetRenderTag();
        const bool visible = IsVisible() && renderTag != HdRenderTagTokens->hidden;
        runInMainThread([&]() {
            _rPrim.call<void>("setVisibilityState",
                visible,
                renderTag.GetString());
        });

        if (HdChangeTracker::IsDoubleSidedDirty(*dirtyBits, id) ||
            HdChangeTracker::IsCullStyleDirty(*dirtyBits, id)) {
            const bool doubleSided = IsDoubleSided(delegate);
            const std::string cullStyle = _CullStyleToString(GetCullStyle(delegate));
            runInMainThread([&]() {
                _rPrim.call<void>("setCullStyle", doubleSided, cullStyle);
            });
        }

        // Materials need to be synced before primvars, to allow the JS side to apply primvar information like
        // displayColor if no other material is set.
        bool fetchedTopology = false;
        const bool pointsDirty =
            HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points);
        const bool topologyDirty = HdChangeTracker::IsTopologyDirty(*dirtyBits, id);
        const bool subdivTagsDirty =
            HdChangeTracker::IsSubdivTagsDirty(*dirtyBits, id);
        const bool displayStyleDirty =
            HdChangeTracker::IsDisplayStyleDirty(*dirtyBits, id);
        if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
            auto materialId = delegate->GetMaterialId(id);

            if (materialId.IsEmpty()){
                int refineLevel = _topology.GetRefineLevel();
                _topology = HdMeshTopology(delegate->GetMeshTopology(id), refineLevel);
                fetchedTopology = true;

                auto faceVertexCounts = _topology.GetFaceVertexCounts();
                auto geomSubsets = _topology.GetGeomSubsets();
                if (!geomSubsets.empty()){
                    std::vector<Section> sections;
                    for (const auto& geomSubset : geomSubsets) {
                        auto materialID = geomSubset.materialId.GetAsString();
                        findContiguousSections(geomSubset.indices, materialID, sections, faceVertexCounts);
                    }

                    if (sections.size() > 0 ) {
                        runInMainThread([&]() {
                            emscripten::val jsSections = sectionsToJSArray(sections);
                            _rPrim.call<void>("setGeomSubsetMaterial", jsSections);
                        });
                    }
                }
            }
            else {
                runInMainThread([&]() {
                    _rPrim.call<void>("setMaterial", materialId.GetAsString());
                });
            }
        }

        // Update points
        if (pointsDirty) {
            VtValue value = delegate->Get(id, HdTokens->points);
            _points = value.Get<VtVec3fArray>();
            _normalsValid = false;
        }

        if (topologyDirty) {
            // When pulling a new topology, we don't want to overwrite the
            // refine level or subdiv tags, which are provided separately by the
            // scene delegate, so we save and restore them.
            PxOsdSubdivTags subdivTags = _topology.GetSubdivTags();

            if (!fetchedTopology){
                int refineLevel = _topology.GetRefineLevel();
                _topology = HdMeshTopology(delegate->GetMeshTopology(id), refineLevel);
            }
            _topology.SetSubdivTags(subdivTags);
        }

        if (subdivTagsDirty && _topology.GetRefineLevel() > 0) {
            _topology.SetSubdivTags(delegate->GetSubdivTags(id));
        }

        if (displayStyleDirty) {
            HdDisplayStyle const displayStyle = delegate->GetDisplayStyle(id);
            _topology = HdMeshTopology(_topology, displayStyle.refineLevel);
            _displayStyleFlatShadingEnabled = displayStyle.flatShadingEnabled;
        }

        if (pointsDirty || topologyDirty || subdivTagsDirty || displayStyleDirty) {
            _UpdateDisplayGeometry();
            _normalsValid = false;
            _adjacencyValid = false;
        }

        // Sync primvars
        if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, id)) {
            _SyncPrimvars(delegate, *dirtyBits);
        }

        _smoothNormals = _UseSmoothNormals();

        // Update the smooth normals in steps:
        // 1. If the topology is dirty, update the adjacency table, a processed
        //    form of the topology that helps calculate smooth normals quickly.
        // 2. If the points are dirty, update the smooth normal buffer itself.
        if (_smoothNormals && !_adjacencyValid) {
            const HdMeshTopology &displayTopology = _GetDisplayTopology();
            _adjacency.BuildAdjacencyTable(&displayTopology);
            _adjacencyValid = true;
            // If we rebuilt the adjacency table, force a rebuild of normals.
            _normalsValid = false;
        }

        if (_smoothNormals && !_normalsValid) {
            const VtVec3fArray &displayPoints = _GetDisplayPoints();
            _computedNormals = Hd_SmoothNormals::ComputeSmoothNormals(
                &_adjacency, displayPoints.size(), displayPoints.cdata());
            _normalsValid = true;
            runInMainThread([&]() {
                _rPrim.call<void>("updateNormals", val(typed_memory_view(3 * _computedNormals.size(), reinterpret_cast<float*>(_computedNormals.data()))));
            });
        }
        else if (!_smoothNormals && !_normalsValid) {
            _UpdateFlatGeometricNormals();
            _normalsValid = true;
        }

        if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
            _transform = GfMatrix4f(delegate->GetTransform(id));
            runInMainThread([&]() {
                _rPrim.call<void>("setTransform", val(typed_memory_view(16, reinterpret_cast<float*>(_transform.data()))));
            });
        }

        if (HdChangeTracker::IsInstancerDirty(*dirtyBits, id) ||
            HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
            HdInstancer::_SyncInstancerAndParents(
                delegate->GetRenderIndex(), GetInstancerId());

            std::vector<GfMatrix4f> transforms;
            if (!GetInstancerId().IsEmpty()) {
                HdInstancer *instancer =
                    delegate->GetRenderIndex().GetInstancer(GetInstancerId());
                Emscripten_Instancer *emscriptenInstancer =
                    dynamic_cast<Emscripten_Instancer*>(instancer);
                if (emscriptenInstancer) {
                    VtMatrix4dArray instanceTransforms =
                        emscriptenInstancer->ComputeInstanceTransforms(id);
                    transforms.reserve(instanceTransforms.size());
                    for (GfMatrix4d const &instanceTransform : instanceTransforms) {
                        transforms.push_back(
                            _transform * GfMatrix4f(instanceTransform));
                    }
                }
            }

            runInMainThread([&]() {
                _rPrim.call<void>(
                    "setInstanceTransforms",
                    val(typed_memory_view(
                        16 * transforms.size(),
                        reinterpret_cast<float*>(transforms.data()))),
                    static_cast<int>(transforms.size()));
            });
        }

        *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
    }


    virtual HdDirtyBits GetInitialDirtyBitsMask() const override
    {
        // Set all bits except the varying flag
        return  (HdChangeTracker::AllSceneDirtyBits) &
                (~HdChangeTracker::Varying);
    }

    virtual HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override
    {
        return bits;
    }


protected:
    virtual void _InitRepr(TfToken const &reprToken,
                           HdDirtyBits *dirtyBits) override
    {
        _ReprVector::iterator it = std::find_if(_reprs.begin(), _reprs.end(),
                                                _ReprComparator(reprToken));
        if (it == _reprs.end()) {
            _reprs.emplace_back(reprToken, HdReprSharedPtr());
        }

        _reprFlatShadingEnabled = false;
        for (HdMeshReprDesc const &desc : _GetReprDesc(reprToken)) {
            _reprFlatShadingEnabled |= desc.flatShadingEnabled;
        }
    }

private:
    struct _OsdVertex {
        float position[3] = {0.0f, 0.0f, 0.0f};

        void Clear(void * = nullptr) {
            position[0] = position[1] = position[2] = 0.0f;
        }

        void AddWithWeight(_OsdVertex const &src, float weight) {
            position[0] += src.position[0] * weight;
            position[1] += src.position[1] * weight;
            position[2] += src.position[2] * weight;
        }
    };

    TfToken _typeId;
    emscripten::val _renderDelegateInterface;
    emscripten::val _rPrim;
    HdMeshUtil *_meshUtil;

    VtVec3iArray _triangulatedIndices;
    VtIntArray _trianglePrimitiveParams;
    VtVec3fArray _computedNormals;

    HdMeshTopology _topology;
    HdMeshTopology _displayTopology;
    GfMatrix4f _transform;
    VtVec3fArray _points;
    VtVec3fArray _displayPoints;
    Hd_VertexAdjacency _adjacency;

    bool _adjacencyValid;
    bool _normalsValid;
    bool _smoothNormals;
    bool _reprFlatShadingEnabled;
    bool _displayStyleFlatShadingEnabled;
    bool _usingRefinedTopology = false;

    HdMeshTopology const &_GetDisplayTopology() const {
        return _usingRefinedTopology ? _displayTopology : _topology;
    }

    VtVec3fArray const &_GetDisplayPoints() const {
        return _usingRefinedTopology ? _displayPoints : _points;
    }

    bool _UseSmoothNormals() const
    {
        // Mirrors Storm's HdStMesh::_UseSmoothNormals behavior for the
        // topology/display-style cases the web delegate materializes itself.
        if (_displayStyleFlatShadingEnabled ||
            _reprFlatShadingEnabled ||
            _topology.GetScheme() == PxOsdOpenSubdivTokens->none ||
            _topology.GetScheme() == PxOsdOpenSubdivTokens->bilinear) {
            return false;
        }
        return true;
    }

    void _UpdateFlatGeometricNormals()
    {
        const HdMeshTopology &displayTopology = _GetDisplayTopology();
        const VtVec3fArray &displayPoints = _GetDisplayPoints();
        VtVec3fArray orderedNormals(3 * _triangulatedIndices.size());
        const bool flip =
            displayTopology.GetOrientation() != HdTokens->rightHanded;

        for (size_t i = 0; i < _triangulatedIndices.size(); ++i) {
            GfVec3i const &triangle = _triangulatedIndices[i];
            if (triangle[0] < 0 || triangle[1] < 0 || triangle[2] < 0 ||
                static_cast<size_t>(triangle[0]) >= displayPoints.size() ||
                static_cast<size_t>(triangle[1]) >= displayPoints.size() ||
                static_cast<size_t>(triangle[2]) >= displayPoints.size()) {
                continue;
            }

            GfVec3f normal = GfCross(
                displayPoints[triangle[1]] - displayPoints[triangle[0]],
                displayPoints[triangle[2]] - displayPoints[triangle[0]]);
            if (flip) {
                normal *= -1.0f;
            }
            if (normal.Normalize() == 0.0f) {
                normal = GfVec3f(0.0f, 0.0f, 1.0f);
            }

            orderedNormals[3 * i + 0] = normal;
            orderedNormals[3 * i + 1] = normal;
            orderedNormals[3 * i + 2] = normal;
        }

        runInMainThread([&]() {
            _rPrim.call<void>(
                "updateOrderedNormals",
                val(typed_memory_view(
                    3 * orderedNormals.size(),
                    reinterpret_cast<float*>(orderedNormals.data()))));
        });
    }

    bool _BuildRefinedTopology()
    {
        _usingRefinedTopology = false;
        _displayPoints.clear();

        const int refineLevel = std::min(_topology.GetRefineLevel(), 3);
        if (_points.empty() ||
            refineLevel <= 0 ||
            _topology.GetScheme() == PxOsdOpenSubdivTokens->none) {
            return false;
        }

        PxOsdTopologyRefinerSharedPtr refiner =
            PxOsdRefinerFactory::Create(_topology.GetPxOsdMeshTopology(), TfToken());
        if (!refiner) {
            TF_WARN("Failed to create OpenSubdiv refiner for <%s>.",
                GetId().GetText());
            return false;
        }

        OpenSubdiv::Far::TopologyRefiner::UniformOptions options(refineLevel);
        options.fullTopologyInLastLevel = true;
        refiner->RefineUniform(options);

        const int coarseVertexCount = refiner->GetLevel(0).GetNumVertices();
        if (static_cast<size_t>(coarseVertexCount) > _points.size()) {
            TF_WARN("OpenSubdiv topology for <%s> references %d coarse points, "
                "but Hydra provided %zu points.",
                GetId().GetText(), coarseVertexCount, _points.size());
            return false;
        }

        std::vector<_OsdVertex> vertices(refiner->GetNumVerticesTotal());
        for (int i = 0; i < coarseVertexCount; ++i) {
            vertices[i].position[0] = _points[i][0];
            vertices[i].position[1] = _points[i][1];
            vertices[i].position[2] = _points[i][2];
        }

        OpenSubdiv::Far::PrimvarRefiner primvarRefiner(*refiner);
        _OsdVertex *src = vertices.data();
        for (int level = 1; level <= refineLevel; ++level) {
            _OsdVertex *dst =
                src + refiner->GetLevel(level - 1).GetNumVertices();
            primvarRefiner.Interpolate(level, src, dst);
            src = dst;
        }

        OpenSubdiv::Far::TopologyLevel const &lastLevel =
            refiner->GetLevel(refineLevel);
        const int refinedVertexCount = lastLevel.GetNumVertices();
        const int firstRefinedVertex =
            refiner->GetNumVerticesTotal() - refinedVertexCount;

        _displayPoints.resize(refinedVertexCount);
        for (int i = 0; i < refinedVertexCount; ++i) {
            _OsdVertex const &vertex = vertices[firstRefinedVertex + i];
            _displayPoints[i] = GfVec3f(
                vertex.position[0],
                vertex.position[1],
                vertex.position[2]);
        }

        VtIntArray faceVertexCounts;
        VtIntArray faceVertexIndices;
        const int faceCount = lastLevel.GetNumFaces();
        for (int face = 0; face < faceCount; ++face) {
            OpenSubdiv::Far::ConstIndexArray faceVertices =
                lastLevel.GetFaceVertices(face);
            if (faceVertices.size() < 3) {
                continue;
            }

            faceVertexCounts.push_back(static_cast<int>(faceVertices.size()));
            for (int vertex = 0; vertex < faceVertices.size(); ++vertex) {
                faceVertexIndices.push_back(faceVertices[vertex]);
            }
        }

        _displayTopology = HdMeshTopology(
            PxOsdOpenSubdivTokens->none,
            _topology.GetOrientation(),
            faceVertexCounts,
            faceVertexIndices,
            0);
        _usingRefinedTopology = true;
        return true;
    }

    void _UpdateDisplayGeometry()
    {
        _BuildRefinedTopology();
        const HdMeshTopology &displayTopology = _GetDisplayTopology();
        const VtVec3fArray &displayPoints = _GetDisplayPoints();

        if (_meshUtil != NULL) {
            delete _meshUtil;
        }
        _meshUtil = new HdMeshUtil(&displayTopology, GetId());
        _meshUtil->ComputeTriangleIndices(
            &_triangulatedIndices, &_trianglePrimitiveParams);

        runInMainThread([&]() {
            _rPrim.call<void>(
                "updatePoints",
                val(typed_memory_view(
                    3 * displayPoints.size(),
                    reinterpret_cast<float*>(
                        const_cast<GfVec3f*>(displayPoints.cdata())))));
            _rPrim.call<void>(
                "updateIndices",
                val(typed_memory_view(
                    3 * _triangulatedIndices.size(),
                    reinterpret_cast<int32_t*>(_triangulatedIndices.data()))));
        });
    }

    // Send primvar data to JS
    void _SendPrimvar(const VtValue &value, const std::string &name, const HdInterpolation &interpolation)
    {
        const std::string &ip = InterpolationStrings.at(interpolation);
        if (value.CanCast<VtVec2fArray>()) {
            VtVec2fArray primvarData = value.Get<VtVec2fArray>();
            _rPrim.call<void>("updatePrimvar", name, val(typed_memory_view(2 * primvarData.size(), reinterpret_cast<float*>(primvarData.data()))), 2, ip);
        }
        if (value.CanCast<VtVec3fArray>()) {
            VtVec3fArray primvarData = value.Get<VtVec3fArray>();
            _rPrim.call<void>("updatePrimvar", name, val(typed_memory_view(3 * primvarData.size(), reinterpret_cast<float*>(primvarData.data()))), 3, ip);
        }
        if (value.CanCast<VtVec4fArray>()) {
            VtVec4fArray primvarData = value.Get<VtVec4fArray>();
            _rPrim.call<void>("updatePrimvar", name, val(typed_memory_view(4 * primvarData.size(), reinterpret_cast<float*>(primvarData.data()))), 4, ip);
        }
    }

    void _SyncPrimvars(HdSceneDelegate *delegate,
                       HdDirtyBits      dirtyBits)
    {
        runInMainThread([&]() {
            SdfPath const &id = GetId();
            for (size_t interpolation = HdInterpolationConstant;
                        interpolation < HdInterpolationCount;
                        ++interpolation) {
                HdInterpolation ip = static_cast<HdInterpolation>(interpolation);
                HdPrimvarDescriptorVector primvars = GetPrimvarDescriptors(delegate, ip);

                size_t numPrimVars = primvars.size();
                for (size_t primVarNum = 0;
                            primVarNum < numPrimVars;
                        ++primVarNum) {
                    HdPrimvarDescriptor const &primvar = primvars[primVarNum];
                    if (HdChangeTracker::IsPrimvarDirty(dirtyBits,
                                                        id,
                                                        primvar.name)) {
                        VtValue value = GetPrimvar(delegate, primvar.name);

                        switch(ip) {
                            case HdInterpolationFaceVarying: {
                                HdVtBufferSource buffer(primvar.name, value);

                                VtValue triangulated;
                                HdMeshComputationResult result =
                                    _meshUtil->ComputeTriangulatedFaceVaryingPrimvar(
                                        buffer.GetData(),
                                        buffer.GetNumElements(),
                                        buffer.GetTupleType().type,
                                        &triangulated);
                                if (result == HdMeshComputationResult::Error) {
                                    TF_CODING_ERROR("[%s] Could not triangulate face-varying data.",
                                        primvar.name.GetText());
                                    continue;
                                }

                                _SendPrimvar(
                                    result == HdMeshComputationResult::Unchanged
                                        ? value
                                        : triangulated,
                                    primvar.name.GetString(),
                                    ip);
                                break;
                            }
                            case HdInterpolationConstant:
                            case HdInterpolationVertex: {
                                _SendPrimvar(value, primvar.name.GetString(), ip);
                                break;
                            }
                            default:
                                TF_WARN("Unsupported interpolation type '%s' for primvar %s",
                                    InterpolationStrings.at(ip).c_str(),
                                    primvar.name.GetText());
                        }
                    }
                }
            }
        });
    }

    Emscripten_Rprim()                                 = delete;
    Emscripten_Rprim(const Emscripten_Rprim &)             = delete;
    Emscripten_Rprim &operator =(const Emscripten_Rprim &) = delete;
};

class Emscripten_Material final : public HdMaterial {
public:
    Emscripten_Material(SdfPath const& id, emscripten::val renderDelegateInterface) :
      HdMaterial(id)
     , _renderDelegateInterface(renderDelegateInterface)
     , _sPrim(val::undefined())
    {
      _sPrim = _renderDelegateInterface.call<val>("createSPrim", std::string("material"), id.GetAsString());
    }

    virtual ~Emscripten_Material() = default;

    virtual void Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam   *renderParam,
                      HdDirtyBits     *dirtyBits) override
    {
      if (*dirtyBits == HdMaterial::Clean) {
        return;
      }
      runInMainThread([&]() {

        VtValue vtMat = sceneDelegate->GetMaterialResource(GetId());
        if (vtMat.IsHolding<HdMaterialNetworkMap>()) {
            HdMaterialNetworkMap const& hdNetworkMap =
                vtMat.UncheckedGet<HdMaterialNetworkMap>();

            _sPrim.call<val>("beginMaterialSync");

#if HD_EMSCRIPTEN_HAS_MATERIALX
            _SendMaterialXDocument(hdNetworkMap);
#endif

            for (auto& [networkId, network]: hdNetworkMap.map) {
                for (auto& node : network.nodes) {
                    val parameters = val::object();
                    parameters.set("identifier", node.identifier.GetString());
                    parameters.set("path", node.path.GetAsString());
                    for (auto &[parameterName, value] : node.parameters) {
                        parameters.set(parameterName.GetString(), _VtValueToJsVal(value));
                        if (value.IsHolding<SdfAssetPath>()) {
                            SdfAssetPath assetPath = value.Get<SdfAssetPath>();
                            const std::string parameter =
                                parameterName.GetString();
                            const std::string resolvedPath =
                                assetPath.GetResolvedPath();
                            parameters.set(parameter + ":resolvedPath",
                                resolvedPath);
                            if (parameterName == TfToken("file")) {
                                parameters.set("resolvedPath", resolvedPath);
                            }
                            if (!resolvedPath.empty()) {
                                ArGetResolver().OpenAsset(
                                    ArResolvedPath(resolvedPath));
                            }
                        }
                    }
                    _sPrim.call<val>("updateNode", networkId.GetString(), node.path.GetAsString(), parameters);
                }

                val relationships = val::array();
                int i = 0;
                for (auto &relationship : network.relationships) {
                    val relationshipObj = val::object();
                    relationshipObj.set("inputId", relationship.inputId.GetAsString());
                    relationshipObj.set("inputName", relationship.inputName.GetString());
                    relationshipObj.set("outputId", relationship.outputId.GetAsString());
                    relationshipObj.set("outputName", relationship.outputName.GetString());
                    relationships.set(i++, relationshipObj);
                }

                _sPrim.call<val>("updateFinished", networkId.GetString(), relationships);
            }
        }
        *dirtyBits = HdMaterial::Clean;
      });
    };

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override {
        return HdMaterial::AllDirty;
    }

private:
    emscripten::val _renderDelegateInterface;
    emscripten::val _sPrim;

#if HD_EMSCRIPTEN_HAS_MATERIALX
    void _SendMaterialXDocument(HdMaterialNetworkMap const& hdNetworkMap)
    {
        HdMaterialNetwork2 hdNetwork = HdConvertToHdMaterialNetwork2(hdNetworkMap);
        if (hdNetwork.terminals.empty() || hdNetwork.nodes.empty()) {
            return;
        }

        for (auto const& terminal : hdNetwork.terminals) {
            auto const nodeIt = hdNetwork.nodes.find(terminal.second.upstreamNode);
            if (nodeIt == hdNetwork.nodes.end()) {
                continue;
            }
            if (nodeIt->second.nodeTypeId == TfToken("UsdPreviewSurface")) {
                continue;
            }

            try {
                MaterialX::DocumentPtr mtlxDoc =
                    HdMtlxCreateMtlxDocumentFromHdNetwork(
                        hdNetwork,
                        nodeIt->second,
                        terminal.second.upstreamNode,
                        GetId(),
                        HdMtlxStdLibraries());

                if (!mtlxDoc) {
                    continue;
                }

                MaterialX::XmlWriteOptions writeOptions;
                writeOptions.elementPredicate =
                    [](MaterialX::ConstElementPtr elem) -> bool {
                        return !elem->hasSourceUri();
                    };

                std::string xml = MaterialX::writeToXmlString(mtlxDoc, &writeOptions);
                if (xml.empty()) {
                    continue;
                }

                val document = val::object();
                document.set("terminal", terminal.first.GetString());
                document.set("terminalNodePath", terminal.second.upstreamNode.GetAsString());
                document.set("terminalOutputName", terminal.second.upstreamOutputName.GetString());
                document.set("xml", xml);
                _sPrim.call<val>("updateMaterialXDocument", document);
            } catch (std::exception const& e) {
                TF_WARN("Failed to serialize MaterialX document for <%s>: %s",
                    GetId().GetText(), e.what());
            }
        }
    }
#endif


    Emscripten_Material()                                  = delete;
    Emscripten_Material(const Emscripten_Material &)             = delete;
    Emscripten_Material &operator =(const Emscripten_Material &) = delete;
};

class Emscripten_Camera final : public HdCamera {
public:
    Emscripten_Camera(SdfPath const& id,
                      emscripten::val renderDelegateInterface)
        : HdCamera(id)
        , _renderDelegateInterface(renderDelegateInterface)
        , _sPrim(val::undefined())
    {
        _sPrim = _renderDelegateInterface.call<val>(
            "createSPrim", std::string(HdPrimTypeTokens->camera.GetText()),
            id.GetAsString());
    }

    virtual ~Emscripten_Camera() = default;

    virtual void Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam   *renderParam,
                      HdDirtyBits     *dirtyBits) override
    {
        HdCamera::Sync(sceneDelegate, renderParam, dirtyBits);

        if (!_HasJsMethod(_sPrim, "updateCameraState")) {
            return;
        }

        GfMatrix4f transform(GetTransform());
        GfRange1f const& clippingRange = GetClippingRange();
        val state = val::object();
        state.set("typeId", HdPrimTypeTokens->camera.GetString());
        state.set("id", GetId().GetAsString());
        state.set("transform", _MatrixToJsVal(transform));
        state.set("projection",
            GetProjection() == HdCamera::Orthographic
                ? std::string("orthographic")
                : std::string("perspective"));
        state.set("horizontalAperture", GetHorizontalAperture());
        state.set("verticalAperture", GetVerticalAperture());
        state.set("horizontalApertureOffset", GetHorizontalApertureOffset());
        state.set("verticalApertureOffset", GetVerticalApertureOffset());
        state.set("focalLength", GetFocalLength());
        state.set("near", clippingRange.GetMin());
        state.set("far", clippingRange.GetMax());

        runInMainThread([&]() {
            _sPrim.call<void>("updateCameraState", state);
        });
    }

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override
    {
        return HdCamera::AllDirty;
    }

private:
    emscripten::val _renderDelegateInterface;
    emscripten::val _sPrim;

    Emscripten_Camera() = delete;
    Emscripten_Camera(const Emscripten_Camera &) = delete;
    Emscripten_Camera &operator=(const Emscripten_Camera &) = delete;
};

class Emscripten_Light final : public HdLight {
public:
    Emscripten_Light(TfToken const& typeId,
                     SdfPath const& id,
                     emscripten::val renderDelegateInterface)
        : HdLight(id)
        , _typeId(typeId)
        , _renderDelegateInterface(renderDelegateInterface)
        , _sPrim(val::undefined())
    {
        _sPrim = _renderDelegateInterface.call<val>(
            "createSPrim", std::string(typeId.GetText()), id.GetAsString());
    }

    virtual ~Emscripten_Light() = default;

    virtual void Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam   *renderParam,
                      HdDirtyBits     *dirtyBits) override
    {
        if (_HasJsMethod(_sPrim, "updateLightState")) {
            SdfPath const& id = GetId();
            GfMatrix4f transform(sceneDelegate->GetTransform(id));
            const float exposure = _GetFloatParam(
                sceneDelegate, id, HdLightTokens->exposure, 0.0f);
            val state = val::object();
            state.set("typeId", _typeId.GetString());
            state.set("id", id.GetAsString());
            state.set("visible", sceneDelegate->GetVisible(id));
            state.set("transform", _MatrixToJsVal(transform));
            state.set("color", _GfVecToJsVal(_GetVec3fParam(
                sceneDelegate, id, HdLightTokens->color, GfVec3f(1.0f))));
            state.set("intensity",
                _GetFloatParam(sceneDelegate, id, HdLightTokens->intensity, 1.0f)
                * std::pow(2.0f, exposure));
            state.set("exposure", exposure);
            state.set("radius",
                _GetFloatParam(sceneDelegate, id, HdLightTokens->radius, 0.25f));
            state.set("width",
                _GetFloatParam(sceneDelegate, id, HdLightTokens->width, 1.0f));
            state.set("height",
                _GetFloatParam(sceneDelegate, id, HdLightTokens->height, 1.0f));
            state.set("angle",
                _GetFloatParam(sceneDelegate, id, HdLightTokens->angle, 0.53f));

            runInMainThread([&]() {
                _sPrim.call<void>("updateLightState", state);
            });
        }

        *dirtyBits = HdLight::Clean;
    }

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override
    {
        return HdLight::AllDirty;
    }

private:
    static float _GetFloatParam(HdSceneDelegate *sceneDelegate,
                                SdfPath const& id,
                                TfToken const& name,
                                float fallback)
    {
        VtValue value = sceneDelegate->GetLightParamValue(id, name);
        if (value.IsHolding<float>()) {
            return value.UncheckedGet<float>();
        }
        if (value.IsHolding<double>()) {
            return static_cast<float>(value.UncheckedGet<double>());
        }
        if (value.IsHolding<int>()) {
            return static_cast<float>(value.UncheckedGet<int>());
        }
        return fallback;
    }

    static GfVec3f _GetVec3fParam(HdSceneDelegate *sceneDelegate,
                                  SdfPath const& id,
                                  TfToken const& name,
                                  GfVec3f const& fallback)
    {
        VtValue value = sceneDelegate->GetLightParamValue(id, name);
        if (value.IsHolding<GfVec3f>()) {
            return value.UncheckedGet<GfVec3f>();
        }
        if (value.IsHolding<GfVec3d>()) {
            GfVec3d const& vec = value.UncheckedGet<GfVec3d>();
            return GfVec3f(vec[0], vec[1], vec[2]);
        }
        return fallback;
    }

    TfToken _typeId;
    emscripten::val _renderDelegateInterface;
    emscripten::val _sPrim;

    Emscripten_Light() = delete;
    Emscripten_Light(const Emscripten_Light &) = delete;
    Emscripten_Light &operator=(const Emscripten_Light &) = delete;
};

const TfTokenVector WebRenderDelegate::SUPPORTED_RPRIM_TYPES =
{
    HdPrimTypeTokens->mesh,
    HdPrimTypeTokens->points
};

const TfTokenVector WebRenderDelegate::SUPPORTED_SPRIM_TYPES =
{
    HdPrimTypeTokens->camera,
    HdPrimTypeTokens->material,
    HdPrimTypeTokens->domeLight,
    HdPrimTypeTokens->cylinderLight,
    HdPrimTypeTokens->diskLight,
    HdPrimTypeTokens->distantLight,
    HdPrimTypeTokens->light,
    HdPrimTypeTokens->rectLight,
    HdPrimTypeTokens->simpleLight,
    HdPrimTypeTokens->sphereLight
};

const TfTokenVector WebRenderDelegate::SUPPORTED_BPRIM_TYPES =
{
};

const TfTokenVector &
WebRenderDelegate::GetSupportedRprimTypes() const
{
    return SUPPORTED_RPRIM_TYPES;
}

const TfTokenVector &
WebRenderDelegate::GetSupportedSprimTypes() const
{
    return SUPPORTED_SPRIM_TYPES;
}

const TfTokenVector &
WebRenderDelegate::GetSupportedBprimTypes() const
{
    return SUPPORTED_BPRIM_TYPES;
}

HdRenderParam *
WebRenderDelegate::GetRenderParam() const
{
    return nullptr;
}

HdResourceRegistrySharedPtr
WebRenderDelegate::GetResourceRegistry() const
{
    static HdResourceRegistrySharedPtr resourceRegistry(new HdResourceRegistry);
    return resourceRegistry;
}

TfTokenVector
WebRenderDelegate::GetMaterialRenderContexts() const
{
    static const TfTokenVector renderContexts = {
#if HD_EMSCRIPTEN_HAS_MATERIALX
        TfToken("mtlx"),
#endif
        TfToken()
    };
    return renderContexts;
}

TfTokenVector
WebRenderDelegate::GetShaderSourceTypes() const
{
    return GetMaterialRenderContexts();
}

TfTokenVector
WebRenderDelegate::GetShadingSystems() const
{
    return GetShaderSourceTypes();
}

HdRenderPassSharedPtr
WebRenderDelegate::CreateRenderPass(HdRenderIndex *index,
                                HdRprimCollection const& collection)
{
    return HdRenderPassSharedPtr(
        new Hd_UnitTestNullRenderPass(index, collection));
}

HdInstancer *
WebRenderDelegate::CreateInstancer(HdSceneDelegate *delegate,
                                               SdfPath const& id)
{
    return new Emscripten_Instancer(delegate, id);
}

void
WebRenderDelegate::DestroyInstancer(HdInstancer *instancer)
{
    delete instancer;
}


HdRprim *
WebRenderDelegate::CreateRprim(TfToken const& typeId,
                                    SdfPath const& rprimId)
{
    return new Emscripten_Rprim(typeId, rprimId, _renderDelegateInterface);
}

void
WebRenderDelegate::DestroyRprim(HdRprim *rPrim)
{
    if (_HasJsMethod(_renderDelegateInterface, "destroyRPrim")) {
        _renderDelegateInterface.call<void>(
            "destroyRPrim", rPrim->GetId().GetAsString());
    }
    delete rPrim;
}

HdSprim *
WebRenderDelegate::CreateSprim(TfToken const& typeId,
    SdfPath const& sprimId)
{
    if (typeId == HdPrimTypeTokens->camera) {
        return new Emscripten_Camera(sprimId, _renderDelegateInterface);
    } else if (typeId == HdPrimTypeTokens->material) {
        return new Emscripten_Material(sprimId, _renderDelegateInterface);
    } else if (typeId == HdPrimTypeTokens->domeLight ||
               typeId == HdPrimTypeTokens->cylinderLight ||
               typeId == HdPrimTypeTokens->diskLight ||
               typeId == HdPrimTypeTokens->distantLight ||
               typeId == HdPrimTypeTokens->light ||
               typeId == HdPrimTypeTokens->rectLight ||
               typeId == HdPrimTypeTokens->simpleLight ||
               typeId == HdPrimTypeTokens->sphereLight) {
        return new Emscripten_Light(typeId, sprimId, _renderDelegateInterface);
    } else {
        TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    }

    return nullptr;
}

HdSprim *
WebRenderDelegate::CreateFallbackSprim(TfToken const& typeId)
{
    if (typeId == HdPrimTypeTokens->camera) {
        return new Emscripten_Camera(SdfPath::EmptyPath(), _renderDelegateInterface);
    } else if (typeId == HdPrimTypeTokens->material) {
        return new Emscripten_Material(SdfPath::EmptyPath(), _renderDelegateInterface);
    } else if (typeId == HdPrimTypeTokens->domeLight ||
               typeId == HdPrimTypeTokens->cylinderLight ||
               typeId == HdPrimTypeTokens->diskLight ||
               typeId == HdPrimTypeTokens->distantLight ||
               typeId == HdPrimTypeTokens->light ||
               typeId == HdPrimTypeTokens->rectLight ||
               typeId == HdPrimTypeTokens->simpleLight ||
               typeId == HdPrimTypeTokens->sphereLight) {
        return new Emscripten_Light(typeId, SdfPath::EmptyPath(), _renderDelegateInterface);
    } else {
        TF_CODING_ERROR("Unknown Sprim Type %s", typeId.GetText());
    }

    return nullptr;
}


void
WebRenderDelegate::DestroySprim(HdSprim *sPrim)
{
    if (_HasJsMethod(_renderDelegateInterface, "destroySPrim")) {
        _renderDelegateInterface.call<void>(
            "destroySPrim", sPrim->GetId().GetAsString());
    }
    delete sPrim;
}

HdBprim *
WebRenderDelegate::CreateBprim(TfToken const& typeId,
                                    SdfPath const& bprimId)
{
    TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());

    return nullptr;
}

HdBprim *
WebRenderDelegate::CreateFallbackBprim(TfToken const& typeId)
{
    TF_CODING_ERROR("Unknown Bprim Type %s", typeId.GetText());

    return nullptr;
}

void
WebRenderDelegate::DestroyBprim(HdBprim *bPrim)
{
    delete bPrim;
}

void
WebRenderDelegate::CommitResources(HdChangeTracker *tracker)
{
    _renderDelegateInterface.call<void>("CommitResources");
}

PXR_NAMESPACE_CLOSE_SCOPE
