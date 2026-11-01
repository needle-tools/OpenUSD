#ifndef PXR_HD_EMSCRIPTEN_WEB_AUTHORING_H
#define PXR_HD_EMSCRIPTEN_WEB_AUTHORING_H
#include "webMetadata.h"

namespace pxr { namespace hdEmscriptenValues {
template<class Scalar> Scalar _NumberFromJs(emscripten::val const& value) {
    if (value.typeOf().as<std::string>() != "number") _TypeError("USD numeric values require numbers");
    return Scalar(value.as<double>());
}
template<class Matrix, unsigned N> Matrix _MatrixFromJs(emscripten::val const& value) {
    if (!_IsJsArray(value)) _TypeError("USD matrices require numeric arrays");
    const auto length = value["length"].as<unsigned>();
    const bool nested = length == N;
    if (!nested && length != N*N) _TypeError("USD matrix has the wrong dimensions");
    Matrix result(1);
    for (unsigned r=0;r<N;++r) {
        if (nested && (!_IsJsArray(value[r]) || value[r]["length"].as<unsigned>() != N)) _TypeError("USD matrix has the wrong dimensions");
        for (unsigned c=0;c<N;++c) result[r][c] = _NumberFromJs<double>(nested ? value[r][c] : value[r*N+c]);
    }
    return result;
}
template<class Vector> Vector _HalfVectorFromJs(emscripten::val const& value) {
    if (!_IsJsArray(value) || value["length"].as<unsigned>() != Vector::dimension) _TypeError("USD vector has the wrong dimensions");
    Vector result; for(unsigned i=0;i<Vector::dimension;++i) result[i] = _NumberFromJs<GfHalf>(value[i]); return result;
}
inline VtValue _AdditionalValueFromJs(SdfValueTypeName const& type, emscripten::val const& value) {
#define HD_EXTRA_VALUE(Name, Cpp, Fn) \
    if (type == SdfValueTypeNames->Name) return VtValue(Fn(value)); \
    if (type == SdfValueTypeNames->Name##Array) return VtValue(_ArrayFromJs<Cpp>(value, Fn));
    HD_EXTRA_VALUE(UChar, unsigned char, _IntegerFromJs<unsigned char>)
    HD_EXTRA_VALUE(UInt, unsigned int, _IntegerFromJs<unsigned int>)
    HD_EXTRA_VALUE(Int64, int64_t, _IntegerFromJs<int64_t>)
    HD_EXTRA_VALUE(UInt64, uint64_t, _IntegerFromJs<uint64_t>)
    HD_EXTRA_VALUE(Half, GfHalf, _NumberFromJs<GfHalf>)
    HD_EXTRA_VALUE(Half2, GfVec2h, _HalfVectorFromJs<GfVec2h>)
    HD_EXTRA_VALUE(Half4, GfVec4h, _HalfVectorFromJs<GfVec4h>)
    HD_EXTRA_VALUE(Matrix2d, GfMatrix2d, (_MatrixFromJs<GfMatrix2d,2>))
    HD_EXTRA_VALUE(Matrix3d, GfMatrix3d, (_MatrixFromJs<GfMatrix3d,3>))
    // Sdf has matrix4d; matrix4f can occur in Vt metadata and Hydra values but
    // is not a registered Sdf attribute type.
    if (type == SdfValueTypeNames->AssetArray) return VtValue(_ArrayFromJs<SdfAssetPath>(value, [](emscripten::val const& item) { return SdfAssetPath(item.as<std::string>()); }));
    if (type == SdfValueTypeNames->TimeCode) return VtValue(SdfTimeCode(_NumberFromJs<double>(value)));
    if (type == SdfValueTypeNames->TimeCodeArray) return VtValue(_ArrayFromJs<SdfTimeCode>(value, [](emscripten::val const& item) { return SdfTimeCode(_NumberFromJs<double>(item)); }));
#undef HD_EXTRA_VALUE
    return VtValue();
}
}}
#endif
