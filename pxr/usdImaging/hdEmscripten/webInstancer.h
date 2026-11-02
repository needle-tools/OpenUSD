// Shared Hydra instancer evaluation. Included inside the PXR namespace.
class HdNeedleInstancer final : public HdInstancer {
public:
    HdNeedleInstancer(HdSceneDelegate* delegate, SdfPath const& id)
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
        HdNeedleInstancer *parent =
            dynamic_cast<HdNeedleInstancer*>(parentInstancer);
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

