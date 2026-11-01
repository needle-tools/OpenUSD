#ifndef PXR_HD_EMSCRIPTEN_WEB_HIERARCHY_H
#define PXR_HD_EMSCRIPTEN_WEB_HIERARCHY_H
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/resolveInfo.h"
#include <emscripten/val.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <vector>

namespace pxr { namespace hdEmscriptenHierarchy {
// Prototypes are generated stage objects, separate from the authored root tree.
// Keep their contents lazy, including nested native instances.
inline std::vector<UsdPrim> TraversePrototypes(UsdStage const& stage, emscripten::val const& expanded) {
    const bool all = expanded.isNull() || expanded.isUndefined();
    std::set<std::string> paths;
    if (!all) for (unsigned i=0;i<expanded["length"].as<unsigned>();++i) paths.insert(expanded[i].as<std::string>());
    std::vector<UsdPrim> result;
    for (auto const& prototype : stage.GetPrototypes()) {
        auto range = UsdPrimRange(prototype, UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate));
        for (auto it=range.begin();it!=range.end();++it) {
            result.push_back(*it);
            if (!all && (it->IsInstance() || it->IsPrototype()) && !paths.count(it->GetPath().GetString())) it.PruneChildren();
        }
    }
    return result;
}

// Scan shared prototype data once. Tree expansion must not affect whether an
// animated stage has a time range, and repeated instances must not repeat work.
inline emscripten::val GetAnimationTimeRange(UsdStage const& stage) {
    double first = std::numeric_limits<double>::infinity();
    double last = -std::numeric_limits<double>::infinity();
    bool varying = false;
    auto scan = [&](UsdPrim const& prim) {
        if (!prim.IsActive() || !prim.IsLoaded() || prim.IsAbstract() || !prim.IsDefined()) return;
        for (auto const& attr : prim.GetAttributes()) {
            if (attr.GetNumTimeSamples() > 1) {
                varying = true;
                std::vector<double> times;
                if (attr.GetTimeSamples(&times) && !times.empty()) {
                    first = std::min(first,times.front()); last = std::max(last,times.back());
                }
            } else {
                auto info = attr.GetResolveInfo(UsdTimeCode(0));
                if (info.ValueSourceMightBeTimeVarying() &&
                    (info.GetSource() == UsdResolveInfoSourceValueClips || info.GetSource() == UsdResolveInfoSourceSpline)) varying = true;
            }
        }
    };
    for (auto const& prim : UsdPrimRange(stage.GetPseudoRoot(),UsdPrimAllPrimsPredicate)) scan(prim);
    for (auto const& prototype : stage.GetPrototypes()) for (auto const& prim : UsdPrimRange(prototype,UsdPrimAllPrimsPredicate)) scan(prim);
    if (!varying) return emscripten::val::null();
    if (stage.GetEndTimeCode() > stage.GetStartTimeCode()) {first=stage.GetStartTimeCode();last=stage.GetEndTimeCode();}
    if (!std::isfinite(first) || !std::isfinite(last)) return emscripten::val::null();
    auto result=emscripten::val::object();result.set("start",first);result.set("end",last);return result;
}
}}
#endif
