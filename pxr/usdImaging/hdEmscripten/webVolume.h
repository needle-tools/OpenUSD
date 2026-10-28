// USD volume/field metadata bridge. Grid decoding remains an optional JS service.
class Emscripten_Volume final : public HdVolume {
public:
    Emscripten_Volume(SdfPath const& id, val* interface)
        : HdVolume(id), _interface(interface) {
        const std::string path = id.GetString();
        runInMainThread([this, path]() {
            _rPrim.AssignFromMainThread(_interface->call<val>("createRPrim", std::string("volume"), path));
        });
    }
    void Sync(HdSceneDelegate* delegate, HdRenderParam*, HdDirtyBits* bits, TfToken const&) override {
        _UpdateVisibility(delegate, bits);
        const TfToken tag = GetRenderTag();
        const bool visible = IsVisible() && tag != HdRenderTagTokens->hidden;
        const GfMatrix4f transform(delegate->GetTransform(GetId()));
        const GfRange3d extent = delegate->GetExtent(GetId());
        const auto descriptors = delegate->GetVolumeFieldDescriptors(GetId());
        struct Field { std::string name, fieldName, filePath, resolvedPath, resolvedUrl, fieldType; GfMatrix4f transform; };
        std::vector<Field> fields;
        for (auto const& descriptor : descriptors) {
            const VtValue file = delegate->Get(descriptor.fieldId, HdFieldTokens->filePath);
            const VtValue fieldName = delegate->Get(descriptor.fieldId, HdFieldTokens->fieldName);
            const SdfAssetPath asset = file.GetWithDefault<SdfAssetPath>();
            fields.push_back({ descriptor.fieldName.GetString(), fieldName.GetWithDefault<TfToken>().GetString(),
                asset.GetAssetPath(), asset.GetResolvedPath(), _GetHttpResolvedUrl(asset.GetResolvedPath()),
                descriptor.fieldPrimType.GetString(), GfMatrix4f(delegate->GetTransform(descriptor.fieldId)) });
        }
        const std::string materialId = delegate->GetMaterialId(GetId()).GetString();
        _rPrim.With([&](val& prim) {
            val state = val::object(), entries = val::array();
            auto matrixValue = [](GfMatrix4f const& matrix) {
                val result = val::array(); for (int i = 0; i < 16; ++i) result.set(i, matrix.data()[i]); return result;
            };
            for (auto const& field : fields) {
                val entry = val::object();
                entry.set("name", field.name); entry.set("fieldName", field.fieldName);
                entry.set("filePath", field.filePath); entry.set("resolvedPath", field.resolvedPath);
                entry.set("resolvedUrl", field.resolvedUrl); entry.set("fieldType", field.fieldType);
                entry.set("transform", matrixValue(field.transform)); entries.call<void>("push", entry);
            }
            state.set("fields", entries); state.set("transform", matrixValue(transform));
            state.set("visible", visible); state.set("renderTag", tag.GetString()); state.set("materialId", materialId);
            if (!extent.IsEmpty()) {
                val bounds = val::array(); bounds.set(0, _GfVecToJsVal(extent.GetMin())); bounds.set(1, _GfVecToJsVal(extent.GetMax()));
                state.set("extent", bounds);
            }
            prim.call<void>("updateVolumeState", state);
        });
        *bits &= ~HdChangeTracker::AllSceneDirtyBits;
    }
    HdDirtyBits GetInitialDirtyBitsMask() const override { return HdChangeTracker::AllSceneDirtyBits & ~HdChangeTracker::Varying; }
protected:
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override { return bits; }
    void _InitRepr(TfToken const& token, HdDirtyBits*) override {
        if (std::find_if(_reprs.begin(), _reprs.end(), _ReprComparator(token)) == _reprs.end())
            _reprs.emplace_back(token, HdReprSharedPtr());
    }
private:
    val* _interface;
    MainThreadJsVal _rPrim;
};

class Emscripten_Field final : public HdField {
public:
    explicit Emscripten_Field(SdfPath const& id) : HdField(id) {}
    HdDirtyBits GetInitialDirtyBitsMask() const override { return AllDirty; }
    void Sync(HdSceneDelegate*, HdRenderParam*, HdDirtyBits* bits) override {
        *bits = Clean;
    }
};
