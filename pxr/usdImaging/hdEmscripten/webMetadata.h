// Typed authoring helpers shared by the generated USD bindings.
#ifndef PXR_HD_EMSCRIPTEN_WEB_METADATA_H
#define PXR_HD_EMSCRIPTEN_WEB_METADATA_H
#include "webValue.h"
#include "pxr/usd/sdf/schema.h"
#include "pxr/usd/sdf/types.h"
#include <cmath>
#include <limits>
#include <type_traits>

namespace pxr { namespace hdEmscriptenValues {
[[noreturn]] inline void _TypeError(std::string const& message) {
    emscripten::val::global("TypeError").new_(message).throw_();
    __builtin_unreachable();
}
inline bool _IsJsArray(emscripten::val const& value) {
    return emscripten::val::global("Array").call<bool>("isArray", value);
}
inline bool _Has(emscripten::val const& value, char const* key) {
    return emscripten::val::global("Object").call<bool>("hasOwn", value, std::string(key));
}
inline bool _IsPlainObject(emscripten::val const& value) {
    if (value.isNull() || value.typeOf().as<std::string>() != "object") return false;
    auto prototype = emscripten::val::global("Object").call<emscripten::val>("getPrototypeOf", value);
    return prototype.isNull() || prototype.strictlyEquals(emscripten::val::global("Object")["prototype"]);
}
template<class Integer> Integer _IntegerFromJs(emscripten::val const& value) {
    const auto kind = value.typeOf().as<std::string>();
    if (kind == "number") {
        double number = value.as<double>();
        if (!std::isfinite(number) || std::trunc(number) != number || std::abs(number) > 9007199254740991.0 ||
            static_cast<long double>(number) < std::numeric_limits<Integer>::lowest() ||
            static_cast<long double>(number) > std::numeric_limits<Integer>::max())
            _TypeError("USD integer is out of range; use an exact decimal string or bigint for 64-bit values");
        return static_cast<Integer>(number);
    }
    if constexpr (sizeof(Integer) == 8) {
        if (kind == "string" || kind == "bigint") {
            const auto text = kind == "string" ? value.as<std::string>() : value.call<std::string>("toString");
            const bool negative = !text.empty() && text[0] == '-';
            if (text.empty() || (negative && (!std::is_signed<Integer>::value || text.size() == 1)))
                _TypeError("Invalid USD integer");
            const uint64_t limit = negative ? uint64_t(std::numeric_limits<Integer>::max()) + 1 : uint64_t(std::numeric_limits<Integer>::max());
            uint64_t result = 0;
            for (size_t i = negative ? 1 : 0; i < text.size(); ++i) {
                if (text[i] < '0' || text[i] > '9' || result > (limit - unsigned(text[i] - '0')) / 10)
                    _TypeError("Invalid or out-of-range USD integer");
                result = result * 10 + unsigned(text[i] - '0');
            }
            if (negative && result == uint64_t(std::numeric_limits<Integer>::max()) + 1) return std::numeric_limits<Integer>::lowest();
            return negative ? -static_cast<Integer>(result) : static_cast<Integer>(result);
        }
    }
    _TypeError("USD integers require integer numbers");
}
template<class Item, class Convert> VtArray<Item> _ArrayFromJs(emscripten::val const& value, Convert convert) {
    const bool array = _IsJsArray(value) || emscripten::val::global("ArrayBuffer").call<bool>("isView", value);
    if (!array || value["length"].isUndefined()) _TypeError("USD array values require arrays or typed arrays");
    VtArray<Item> result(value["length"].as<unsigned>());
    for (unsigned i = 0; i < result.size(); ++i) result[i] = convert(value[i]);
    return result;
}

template<class Convert> VtValue _MetadataFromJs(VtValue const&, emscripten::val const&, Convert, unsigned = 0);
template<class Convert> VtDictionary _DictionaryFromJs(VtDictionary const& existing, emscripten::val const& value, Convert convert, unsigned depth) {
    if (depth > 64 || !_IsPlainObject(value)) _TypeError("USD dictionaries require plain objects with at most 64 levels");
    VtDictionary result;
    auto keys = emscripten::val::global("Object").call<emscripten::val>("keys", value);
    for (unsigned i = 0; i < keys["length"].as<unsigned>(); ++i) {
        auto key = keys[i].as<std::string>(); auto found = existing.find(key);
        result[key] = _MetadataFromJs(found == existing.end() ? VtValue() : found->second, value[key], convert, depth + 1);
    }
    return result;
}
template<class Convert> SdfReference _ReferenceFromJs(emscripten::val const& value, Convert convert, unsigned depth) {
    if (!_IsPlainObject(value)) _TypeError("USD references require assetPath and primPath objects");
    const auto asset = _Has(value, "assetPath") ? value["assetPath"].as<std::string>() : std::string();
    const SdfPath path(_Has(value, "primPath") ? value["primPath"].as<std::string>() : std::string());
    if (!path.IsEmpty() && (!path.IsAbsolutePath() || !path.IsPrimPath())) _TypeError("USD reference primPath must be an absolute prim path");
    double offset = 0, scale = 1;
    if (_Has(value, "layerOffset")) {
        auto item = value["layerOffset"];
        if (!_IsPlainObject(item)) _TypeError("USD layerOffset requires an object");
        if (_Has(item, "offset")) { if (item["offset"].typeOf().as<std::string>() != "number") _TypeError("USD layer offset requires a number"); offset = item["offset"].as<double>(); }
        if (_Has(item, "scale")) { if (item["scale"].typeOf().as<std::string>() != "number") _TypeError("USD layer scale requires a number"); scale = item["scale"].as<double>(); }
    }
    if (!std::isfinite(offset) || !std::isfinite(scale) || scale == 0) _TypeError("Invalid USD layer offset");
    auto data = _Has(value, "customData") ? _DictionaryFromJs(VtDictionary(), value["customData"], convert, depth + 1) : VtDictionary();
    return SdfReference(asset, path, SdfLayerOffset(offset, scale), data);
}
template<class Item, class Convert> Item _ListItemFromJs(emscripten::val const& value, Convert convert, unsigned depth) {
    if constexpr (std::is_same<Item, TfToken>::value) return TfToken(value.as<std::string>());
    else if constexpr (std::is_same<Item, std::string>::value) return value.as<std::string>();
    else if constexpr (std::is_same<Item, SdfPath>::value) {
        auto text = value.as<std::string>(); if (!SdfPath::IsValidPathString(text)) _TypeError("Invalid USD path"); return SdfPath(text);
    } else if constexpr (std::is_same<Item, SdfReference>::value) return _ReferenceFromJs(value, convert, depth);
    else if constexpr (std::is_same<Item, SdfPayload>::value) {
        auto ref = _ReferenceFromJs(value, convert, depth); return SdfPayload(ref.GetAssetPath(), ref.GetPrimPath(), ref.GetLayerOffset());
    } else return _IntegerFromJs<Item>(value);
}
template<class Item, class Convert> SdfListOp<Item> _ListOpFromJs(emscripten::val const& value, Convert convert, unsigned depth) {
    auto items = [&](emscripten::val const& input) {
        if (!_IsJsArray(input)) _TypeError("USD list operations require arrays");
        std::vector<Item> result; result.reserve(input["length"].as<unsigned>());
        for (unsigned i = 0; i < input["length"].as<unsigned>(); ++i) result.push_back(_ListItemFromJs<Item>(input[i], convert, depth));
        return result;
    };
    SdfListOp<Item> result;
    if (_IsJsArray(value)) { if (!result.SetExplicitItems(items(value))) _TypeError("Invalid USD explicit list"); return result; }
    if (!_IsPlainObject(value)) _TypeError("USD list operations require an array or an operation object");
    auto keys = emscripten::val::global("Object").call<emscripten::val>("keys", value);
    for (unsigned i = 0; i < keys["length"].as<unsigned>(); ++i) {
        auto key = keys[i].as<std::string>();
        if (key != "isExplicit" && key != "explicitItems" && key != "prependedItems" && key != "appendedItems" && key != "deletedItems" && key != "orderedItems" && key != "addedItems") _TypeError("Unknown USD list operation: " + key);
    }
    if (_Has(value, "isExplicit") && value["isExplicit"].typeOf().as<std::string>() != "boolean") _TypeError("isExplicit requires a boolean");
    const bool explicitMode = _Has(value, "isExplicit") ? value["isExplicit"].as<bool>() : _Has(value, "explicitItems");
    if (explicitMode) {
        if (_Has(value,"explicitItems") && !result.SetExplicitItems(items(value["explicitItems"]))) _TypeError("Invalid USD explicit list");
        if (!_Has(value,"explicitItems")) result.SetExplicitItems({});
    }
    for (auto key : {"prependedItems", "appendedItems", "deletedItems", "orderedItems", "addedItems"}) {
        if (!_Has(value, key)) continue;
        auto list = items(value[key]);
        if (explicitMode) { if (!list.empty()) _TypeError("Explicit USD lists cannot also contain edit operations"); continue; }
        bool valid = true;
        if (std::string(key) == "prependedItems") valid = result.SetPrependedItems(list);
        if (std::string(key) == "appendedItems") valid = result.SetAppendedItems(list);
        if (std::string(key) == "deletedItems") valid = result.SetDeletedItems(list);
        if (std::string(key) == "orderedItems") result.SetOrderedItems(list);
        if (std::string(key) == "addedItems") result.SetAddedItems(list);
        if (!valid) _TypeError("Invalid USD list operation");
    }
    if (!explicitMode && _Has(value,"explicitItems") && !items(value["explicitItems"]).empty()) _TypeError("Non-explicit USD list cannot contain explicitItems");
    return result;
}
template<class Convert> VtValue _MetadataFromJs(VtValue const& existing, emscripten::val const& value, Convert convert, unsigned depth) {
    if (depth > 64) _TypeError("USD dictionary is too deeply nested");
    if (existing.IsHolding<VtDictionary>()) return VtValue(_DictionaryFromJs(existing.UncheckedGet<VtDictionary>(), value, convert, depth));
#define HD_METADATA_LIST(T) if (existing.IsHolding<SdfListOp<T>>()) return VtValue(_ListOpFromJs<T>(value, convert, depth));
    HD_METADATA_LIST(TfToken) HD_METADATA_LIST(std::string) HD_METADATA_LIST(SdfPath)
    HD_METADATA_LIST(SdfReference) HD_METADATA_LIST(SdfPayload) HD_METADATA_LIST(int) HD_METADATA_LIST(unsigned int)
    HD_METADATA_LIST(int64_t) HD_METADATA_LIST(uint64_t)
#undef HD_METADATA_LIST
    if (!existing.IsEmpty()) {
        auto type = SdfSchema::GetInstance().FindType(existing);
        if (!type) _TypeError("Unsupported USD metadata type: " + existing.GetTypeName());
        return convert(type, value);
    }
    if (_IsPlainObject(value)) return VtValue(_DictionaryFromJs(VtDictionary(), value, convert, depth));
    const auto kind = value.typeOf().as<std::string>();
    if (kind == "boolean") return VtValue(value.as<bool>());
    if (kind == "string") return VtValue(value.as<std::string>());
    if (kind == "number") return VtValue(value.as<double>());
    if (_IsJsArray(value)) {
        const auto length = value["length"].as<unsigned>();
        // Without a declared type an empty array is ambiguous; preserve an
        // existing entry's type above, and require a typed entry for new ones.
        if (!length) _TypeError("New empty dictionary arrays need an existing declared USD type");
        const auto itemKind = value[0].typeOf().as<std::string>();
        for (unsigned i = 1; i < length; ++i) if (value[i].typeOf().as<std::string>() != itemKind) _TypeError("USD dictionary arrays must have a single element type");
        if (itemKind == "string") return VtValue(_ArrayFromJs<std::string>(value, [](emscripten::val const& item) { return item.as<std::string>(); }));
        if (itemKind == "boolean") return VtValue(_ArrayFromJs<bool>(value, [](emscripten::val const& item) { return item.as<bool>(); }));
        if (itemKind == "number") return VtValue(_ArrayFromJs<double>(value, [](emscripten::val const& item) { return item.as<double>(); }));
    }
    _TypeError("Cannot infer a USD dictionary value type");
}

template<class Item> emscripten::val _AuthoredListToJs(SdfListOp<Item> const& op) {
    auto result = emscripten::val::object(); result.set("isExplicit",op.IsExplicit());
    auto items = [](std::vector<Item> const& input) { auto array = emscripten::val::array(); unsigned i = 0; for (auto const& item : input) array.set(i++, _ValueToJsVal(item)); return array; };
    result.set("explicitItems",items(op.GetExplicitItems())); result.set("prependedItems",items(op.GetPrependedItems()));
    result.set("appendedItems",items(op.GetAppendedItems())); result.set("deletedItems",items(op.GetDeletedItems()));
    result.set("orderedItems",items(op.GetOrderedItems())); result.set("addedItems",items(op.GetAddedItems())); return result;
}
inline emscripten::val _AuthoredMetadataToJs(VtValue const& value) {
#define HD_AUTHORED_LIST(T) if(value.IsHolding<SdfListOp<T>>()) return _AuthoredListToJs(value.UncheckedGet<SdfListOp<T>>());
    HD_AUTHORED_LIST(TfToken) HD_AUTHORED_LIST(std::string) HD_AUTHORED_LIST(SdfPath)
    HD_AUTHORED_LIST(SdfReference) HD_AUTHORED_LIST(SdfPayload) HD_AUTHORED_LIST(int) HD_AUTHORED_LIST(unsigned int)
    HD_AUTHORED_LIST(int64_t) HD_AUTHORED_LIST(uint64_t)
#undef HD_AUTHORED_LIST
    return _VtValueToJsVal(value, true);
}
}}
#endif
