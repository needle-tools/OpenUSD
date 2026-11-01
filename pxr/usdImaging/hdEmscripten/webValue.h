// Shared owned JavaScript values for material transport and typed authoring reads.
#ifndef PXR_HD_EMSCRIPTEN_WEB_VALUE_H
#define PXR_HD_EMSCRIPTEN_WEB_VALUE_H
#include "pxr/pxr.h"
#include "pxr/base/vt/value.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/dictionary.h"
#include "pxr/base/arch/demangle.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/sdf/listOp.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/reference.h"
#include "pxr/usd/sdf/payload.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/gf/half.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/gf/vec2d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec4d.h"
#include "pxr/base/gf/vec2i.h"
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/gf/vec4i.h"
#include "pxr/base/gf/vec3h.h"
#include "pxr/base/gf/vec2h.h"
#include "pxr/base/gf/vec4h.h"
#include "pxr/base/gf/matrix2d.h"
#include "pxr/base/gf/matrix3d.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/quath.h"
#include "pxr/usd/sdf/timeCode.h"
#include <emscripten/val.h>
namespace pxr { namespace hdEmscriptenValues {
using namespace PXR_NS;
inline emscripten::val _VtValueToJsVal(VtValue const &value, bool stringifyUnsupported = false);
template <class Vec>
inline emscripten::val _GfVecToJsVal(Vec const &vec)
{
    emscripten::val jsArray = emscripten::val::array();
    for (size_t i = 0; i < Vec::dimension; ++i) {
        jsArray.set(i, static_cast<double>(vec[i]));
    }
    return jsArray;
}

template <class T>
inline emscripten::val _ValueToJsVal(T const &value)
{
    return emscripten::val(value);
}

inline emscripten::val _ValueToJsVal(TfToken const &value)
{
    return emscripten::val(value.GetString());
}

inline emscripten::val _ValueToJsVal(SdfAssetPath const &value)
{
    return emscripten::val(value.GetAssetPath());
}

inline emscripten::val _ValueToJsVal(int64_t value) {
    return value < -9007199254740991LL || value > 9007199254740991LL ? emscripten::val(std::to_string(value)) : emscripten::val(static_cast<double>(value));
}
inline emscripten::val _ValueToJsVal(uint64_t value) {
    return value > 9007199254740991ULL ? emscripten::val(std::to_string(value)) : emscripten::val(static_cast<double>(value));
}
inline emscripten::val _ValueToJsVal(SdfPath const &value) { return emscripten::val(value.GetString()); }
inline emscripten::val _ValueToJsVal(SdfPayload const &value) {
    auto result = emscripten::val::object();
    result.set("assetPath", value.GetAssetPath()); result.set("primPath", value.GetPrimPath().GetString());
    auto offset = emscripten::val::object(); offset.set("offset", value.GetLayerOffset().GetOffset()); offset.set("scale", value.GetLayerOffset().GetScale());
    result.set("layerOffset", offset); return result;
}
inline emscripten::val _ValueToJsVal(SdfReference const &value) {
    auto result = _ValueToJsVal(SdfPayload(value.GetAssetPath(), value.GetPrimPath(), value.GetLayerOffset()));
    result.set("customData", _VtValueToJsVal(VtValue(value.GetCustomData()), true)); return result;
}
template <class T>
inline emscripten::val _ValueToJsVal(SdfListOp<T> const &value) {
    auto result = emscripten::val::array(); unsigned index = 0;
    for (auto const &item : value.GetAppliedItems()) result.set(index++, _ValueToJsVal(item));
    return result;
}

inline emscripten::val _ValueToJsVal(GfHalf value) { return emscripten::val(static_cast<double>(value)); }
inline emscripten::val _ValueToJsVal(GfVec2f const& value) { return _GfVecToJsVal(value); }
inline emscripten::val _ValueToJsVal(GfVec3f const& value) { return _GfVecToJsVal(value); }
inline emscripten::val _ValueToJsVal(GfVec4f const& value) { return _GfVecToJsVal(value); }
inline emscripten::val _ValueToJsVal(GfVec2d const& value) { return _GfVecToJsVal(value); }
inline emscripten::val _ValueToJsVal(GfVec3d const& value) { return _GfVecToJsVal(value); }
inline emscripten::val _ValueToJsVal(GfVec4d const& value) { return _GfVecToJsVal(value); }
inline emscripten::val _ValueToJsVal(GfVec2i const& value) { return _GfVecToJsVal(value); }
inline emscripten::val _ValueToJsVal(GfVec3i const& value) { return _GfVecToJsVal(value); }
inline emscripten::val _ValueToJsVal(GfVec4i const& value) { return _GfVecToJsVal(value); }
inline emscripten::val _ValueToJsVal(GfVec3h const& value) { return _GfVecToJsVal(value); }
inline emscripten::val _ValueToJsVal(GfVec2h const& value) { return _GfVecToJsVal(value); }
inline emscripten::val _ValueToJsVal(GfVec4h const& value) { return _GfVecToJsVal(value); }
inline emscripten::val _ValueToJsVal(SdfTimeCode const& value) { return emscripten::val(value.GetValue()); }
inline emscripten::val _ValueToJsVal(GfMatrix2d const& value) {
    auto result = emscripten::val::array(); for (int r=0;r<2;++r) result.set(r,_GfVecToJsVal(value.GetRow(r))); return result;
}
inline emscripten::val _ValueToJsVal(GfMatrix3d const& value) {
    auto result = emscripten::val::array(); for (int r=0;r<3;++r) result.set(r,_GfVecToJsVal(value.GetRow(r))); return result;
}
inline emscripten::val _ValueToJsVal(GfMatrix4d const& value) {
    auto result = emscripten::val::array();
    for (int r = 0; r < 4; ++r) result.set(r, _GfVecToJsVal(value.GetRow(r)));
    return result;
}
inline emscripten::val _ValueToJsVal(GfMatrix4f const& value) {
    auto result = emscripten::val::array();
    for (int r = 0; r < 4; ++r) result.set(r, _GfVecToJsVal(value.GetRow(r)));
    return result;
}
inline emscripten::val _ValueToJsVal(GfQuatf const& value) {
    auto result = emscripten::val::array();
    result.set(0, static_cast<double>(value.GetReal()));
    for (int i = 0; i < 3; ++i) result.set(i + 1, static_cast<double>(value.GetImaginary()[i]));
    return result;
}
inline emscripten::val _ValueToJsVal(GfQuatd const& value) {
    auto result = emscripten::val::array();
    result.set(0, static_cast<double>(value.GetReal()));
    for (int i = 0; i < 3; ++i) result.set(i + 1, static_cast<double>(value.GetImaginary()[i]));
    return result;
}
inline emscripten::val _ValueToJsVal(GfQuath const& value) {
    auto result = emscripten::val::array();
    result.set(0, static_cast<double>(value.GetReal()));
    for (int i = 0; i < 3; ++i) result.set(i + 1, static_cast<double>(value.GetImaginary()[i]));
    return result;
}

template <class T>
inline emscripten::val _VtArrayToJsVal(VtArray<T> const &array)
{
    emscripten::val jsArray = emscripten::val::array();
    for (size_t i = 0; i < array.size(); ++i) {
        jsArray.set(i, _ValueToJsVal(array[i]));
    }
    return jsArray;
}

inline emscripten::val _VtValueToJsVal(VtValue const &value, bool stringifyUnsupported)
{
    if (value.IsEmpty()) return emscripten::val::undefined();
    if (value.IsHolding<VtDictionary>()) {
        emscripten::val object = emscripten::val::object();
        for (auto const &entry : value.UncheckedGet<VtDictionary>()) {
            object.set(entry.first, _VtValueToJsVal(entry.second, stringifyUnsupported));
        }
        return object;
    }
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

    HD_EMSCRIPTEN_CONVERT_SCALAR(int64_t)
    HD_EMSCRIPTEN_CONVERT_SCALAR(uint64_t)
    HD_EMSCRIPTEN_CONVERT_SCALAR(unsigned int)
    HD_EMSCRIPTEN_CONVERT_SCALAR(unsigned char)
    HD_EMSCRIPTEN_CONVERT_ARRAY(int64_t)
    HD_EMSCRIPTEN_CONVERT_ARRAY(uint64_t)
    HD_EMSCRIPTEN_CONVERT_ARRAY(unsigned int)
    HD_EMSCRIPTEN_CONVERT_ARRAY(unsigned char)
    HD_EMSCRIPTEN_CONVERT_SCALAR(SdfTokenListOp)
    HD_EMSCRIPTEN_CONVERT_SCALAR(SdfStringListOp)
    HD_EMSCRIPTEN_CONVERT_SCALAR(SdfPathListOp)
    HD_EMSCRIPTEN_CONVERT_SCALAR(SdfReferenceListOp)
    HD_EMSCRIPTEN_CONVERT_SCALAR(SdfPayloadListOp)
    HD_EMSCRIPTEN_CONVERT_SCALAR(SdfIntListOp)
    HD_EMSCRIPTEN_CONVERT_SCALAR(SdfUIntListOp)
    HD_EMSCRIPTEN_CONVERT_SCALAR(SdfInt64ListOp)
    HD_EMSCRIPTEN_CONVERT_SCALAR(SdfUInt64ListOp)
    HD_EMSCRIPTEN_CONVERT_SCALAR(SdfPath)
    HD_EMSCRIPTEN_CONVERT_SCALAR(SdfReference)
    HD_EMSCRIPTEN_CONVERT_SCALAR(SdfPayload)
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

    HD_EMSCRIPTEN_CONVERT_SCALAR(GfHalf)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfHalf)
    HD_EMSCRIPTEN_CONVERT_SCALAR(GfVec3h)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfVec3h)
    HD_EMSCRIPTEN_CONVERT_SCALAR(GfVec2h)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfVec2h)
    HD_EMSCRIPTEN_CONVERT_SCALAR(GfVec4h)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfVec4h)
    HD_EMSCRIPTEN_CONVERT_SCALAR(GfMatrix2d)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfMatrix2d)
    HD_EMSCRIPTEN_CONVERT_SCALAR(GfMatrix3d)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfMatrix3d)
    HD_EMSCRIPTEN_CONVERT_SCALAR(SdfTimeCode)
    HD_EMSCRIPTEN_CONVERT_ARRAY(SdfTimeCode)
    HD_EMSCRIPTEN_CONVERT_SCALAR(GfMatrix4f)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfMatrix4f)
    HD_EMSCRIPTEN_CONVERT_SCALAR(GfMatrix4d)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfMatrix4d)
    HD_EMSCRIPTEN_CONVERT_SCALAR(GfQuatf)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfQuatf)
    HD_EMSCRIPTEN_CONVERT_SCALAR(GfQuatd)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfQuatd)
    HD_EMSCRIPTEN_CONVERT_SCALAR(GfQuath)
    HD_EMSCRIPTEN_CONVERT_ARRAY(GfQuath)

#undef HD_EMSCRIPTEN_CONVERT_ARRAY
#undef HD_EMSCRIPTEN_CONVERT_VEC
#undef HD_EMSCRIPTEN_CONVERT_SCALAR

    if (stringifyUnsupported) return emscripten::val(TfStringify(value));
    TF_WARN("Unsupported USD value type '%s'",
        ArchGetDemangled(value.GetTypeid()).c_str());
    return emscripten::val::undefined();
}

}}
#endif
