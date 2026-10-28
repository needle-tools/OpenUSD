// Point and curve resources use their own topology, not mesh triangulation.
// Included inside the hdEmscripten implementation namespace after JS helpers.
static val _StrokeValueToJs(VtValue const& value) {
    if (value.IsHolding<VtDictionary>()) {
        val result = val::object();
        for (auto const& entry : value.UncheckedGet<VtDictionary>()) result.set(entry.first, _StrokeValueToJs(entry.second));
        return result;
    }
#define STROKE_VECTOR_ARRAY(T) \
    if (value.IsHolding<T>()) return _GfVecToJsVal(value.UncheckedGet<T>()); \
    if (value.IsHolding<VtArray<T>>()) { \
        val result = val::array(); \
        auto const& values = value.UncheckedGet<VtArray<T>>(); \
        for (size_t i = 0; i < values.size(); ++i) result.set(i, _GfVecToJsVal(values[i])); \
        return result; \
    }
    STROKE_VECTOR_ARRAY(GfVec2f)
    STROKE_VECTOR_ARRAY(GfVec3f)
    STROKE_VECTOR_ARRAY(GfVec4f)
    STROKE_VECTOR_ARRAY(GfVec2d)
    STROKE_VECTOR_ARRAY(GfVec3d)
    STROKE_VECTOR_ARRAY(GfVec4d)
#undef STROKE_VECTOR_ARRAY
    return _VtValueToJsVal(value);
}

class Emscripten_Stroke final : public HdRprim {
public:
    Emscripten_Stroke(TfToken type, SdfPath const& id,
                     val* interface, WebRenderDelegate* owner)
        : HdRprim(id), _type(type), _interface(interface), _owner(owner) {
        const std::string path = id.GetString(), typeName = type.GetString();
        runInMainThread([this, path, typeName]() {
            _rPrim.AssignFromMainThread(_interface->call<val>("createRPrim", typeName, path));
        });
    }
    void Sync(HdSceneDelegate* delegate, HdRenderParam*, HdDirtyBits* bits,
              TfToken const&) override {
        const SdfPath& id = GetId();
        _UpdateVisibility(delegate, bits);
        _UpdateInstancer(delegate, bits);
        const TfToken tag = GetRenderTag();
        const bool visible = IsVisible() && tag != HdRenderTagTokens->hidden;
        _rPrim.With([&](val& prim) { prim.call<void>("setVisibilityState", visible, tag.GetString()); });
        if (*bits & HdChangeTracker::DirtyMaterialId) {
            const std::string material = delegate->GetMaterialId(id).GetString();
            _rPrim.With([&](val& prim) { prim.call<void>("setMaterial", material); });
        }
        if (HdChangeTracker::IsAnyPrimvarDirty(*bits, id) ||
            HdChangeTracker::IsTopologyDirty(*bits, id) || (*bits & (HdChangeTracker::DirtyDisplayStyle | HdChangeTracker::DirtyDoubleSided))) {
            VtDictionary data;
            data["points"] = delegate->Get(id, HdTokens->points);
            const int refine = std::max(0, std::min(3, delegate->GetDisplayStyle(id).refineLevel));
            data["subdivisions"] = VtValue(16 << refine);
            VtDictionary primvars;
            for (int i = HdInterpolationConstant; i < HdInterpolationCount; ++i) {
                const auto interpolation = static_cast<HdInterpolation>(i);
                for (auto const& desc : delegate->GetPrimvarDescriptors(id, interpolation)) {
                    if (desc.name == HdTokens->points) continue;
                    VtIntArray indices;
                    VtValue values = delegate->GetIndexedPrimvar(id, desc.name, &indices);
                    if (values.IsEmpty()) continue;
                    VtDictionary entry;
                    entry["values"] = values;
                    entry["indices"] = VtValue(indices);
                    entry["interpolation"] = VtValue(InterpolationStrings.at(interpolation));
                    primvars[desc.name.GetString()] = VtValue(entry);
                }
            }
            data["primvars"] = VtValue(primvars);
            if (_type == HdPrimTypeTokens->basisCurves) {
                const HdBasisCurvesTopology topology = delegate->GetBasisCurvesTopology(id);
                data["counts"] = VtValue(topology.GetCurveVertexCounts());
                data["indices"] = VtValue(topology.GetCurveIndices());
                data["type"] = VtValue(topology.GetCurveType());
                data["basis"] = VtValue(topology.GetCurveBasis());
                data["wrap"] = VtValue(topology.GetCurveWrap());
                data["invisibleCurves"] = VtValue(topology.GetInvisibleCurves());
                // Hydra's NURBS approximation retains only the control cage.
                // Preserve authored rational topology for the web tessellator.
                if (_owner->GetStage()) {
                    SdfPath sourcePath = delegate->GetScenePrimPath(id, 0);
                    UsdGeomNurbsCurves curves(_owner->GetStage()->GetPrimAtPath(sourcePath));
                    if (!curves) curves = UsdGeomNurbsCurves(_owner->GetStage()->GetPrimAtPath(id));
                    if (curves) {
                        data["type"] = VtValue(std::string("nurbs"));
                        const UsdTimeCode time = _owner->GetTime();
                        for (auto const& item : std::vector<std::pair<std::string, UsdAttribute>>{
                                {"order", curves.GetOrderAttr()}, {"knots", curves.GetKnotsAttr()},
                                {"ranges", curves.GetRangesAttr()}, {"weights", curves.GetPointWeightsAttr()}}) {
                            VtValue value;
                            if (item.second.Get(&value, time)) data[item.first] = value;
                        }
                    }
                }
            }
            if (_type == HdPrimTypeTokens->nurbsPatch && _owner->GetStage()) {
                SdfPath sourcePath = delegate->GetScenePrimPath(id, 0);
                UsdGeomNurbsPatch patch(_owner->GetStage()->GetPrimAtPath(sourcePath));
                if (!patch) patch = UsdGeomNurbsPatch(_owner->GetStage()->GetPrimAtPath(id));
                if (patch) {
                    const UsdTimeCode time = _owner->GetTime();
                    for (auto const& item : std::vector<std::pair<std::string, UsdAttribute>>{
                            {"uCount", patch.GetUVertexCountAttr()}, {"vCount", patch.GetVVertexCountAttr()},
                            {"uOrder", patch.GetUOrderAttr()}, {"vOrder", patch.GetVOrderAttr()},
                            {"uKnots", patch.GetUKnotsAttr()}, {"vKnots", patch.GetVKnotsAttr()},
                            {"uRange", patch.GetURangeAttr()}, {"vRange", patch.GetVRangeAttr()},
                            {"weights", patch.GetPointWeightsAttr()}, {"orientation", patch.GetOrientationAttr()},
                            {"doubleSided", patch.GetDoubleSidedAttr()}, {"trimCounts", patch.GetTrimCurveCountsAttr()}}) {
                        VtValue value;
                        if (item.second.Get(&value, time)) data[item.first] = value;
                    }
                }
            }
            _rPrim.With([&](val& prim) { prim.call<void>("setStrokeData", _StrokeValueToJs(VtValue(data))); });
        }
        if (HdChangeTracker::IsTransformDirty(*bits, id)) {
            _transform = GfMatrix4f(delegate->GetTransform(id));
            _rPrim.With([&](val& prim) { prim.call<void>("setTransform", val(typed_memory_view(16, _transform.data()))); });
        }
        if (HdChangeTracker::IsInstancerDirty(*bits, id) || HdChangeTracker::IsTransformDirty(*bits, id)) {
            HdInstancer::_SyncInstancerAndParents(delegate->GetRenderIndex(), GetInstancerId());
            std::vector<GfMatrix4f> transforms;
            if (!GetInstancerId().IsEmpty()) {
                auto* instancer = dynamic_cast<Emscripten_Instancer*>(delegate->GetRenderIndex().GetInstancer(GetInstancerId()));
                if (instancer) for (auto const& matrix : instancer->ComputeInstanceTransforms(id)) {
                    transforms.push_back(_transform * GfMatrix4f(matrix));
                }
            }
            _rPrim.With([&](val& prim) {
                prim.call<void>("setInstanceTransforms", transforms.empty() ? val::array() :
                    val(typed_memory_view(16 * transforms.size(), reinterpret_cast<float*>(transforms.data()))),
                    static_cast<int>(transforms.size()), !GetInstancerId().IsEmpty());
            });
        }
        _rPrim.With([&](val& prim) { prim.call<void>("commit"); });
        *bits &= ~HdChangeTracker::AllSceneDirtyBits;
    }
    HdDirtyBits GetInitialDirtyBitsMask() const override {
        return HdChangeTracker::AllSceneDirtyBits & ~HdChangeTracker::Varying;
    }
    TfTokenVector const& GetBuiltinPrimvarNames() const override {
        static const TfTokenVector names = {HdTokens->points, HdTokens->widths, HdTokens->normals};
        return names;
    }
protected:
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override { return bits; }
    void _InitRepr(TfToken const& token, HdDirtyBits*) override {
        if (std::find_if(_reprs.begin(), _reprs.end(), _ReprComparator(token)) == _reprs.end())
            _reprs.emplace_back(token, HdReprSharedPtr());
    }
private:
    TfToken _type;
    val* _interface;
    WebRenderDelegate* _owner;
    MainThreadJsVal _rPrim;
    GfMatrix4f _transform{1};
};
