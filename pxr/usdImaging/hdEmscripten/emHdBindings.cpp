#include "pxr/pxr.h"

#include "webSyncDriver.h"
#include "generated/openusdBuildInfo.h"

#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/value.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/primSpec.h"
#include "pxr/usd/sdf/propertySpec.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/schema.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/pcp/errors.h"
#include "pxr/usd/pcp/layerStack.h"
#include "pxr/usd/pcp/node.h"
#include "pxr/usd/usd/notice.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/editContext.h"
#include "pxr/usd/usd/payloads.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primCompositionQuery.h"
#include "pxr/usd/usd/relationship.h"
#include "pxr/usd/usd/resolveInfo.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usd/variantSets.h"
#include "pxr/usd/usdGeom/camera.h"
#include "pxr/usd/usdGeom/cube.h"
#include "pxr/usd/usdGeom/imageable.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/scope.h"
#include "pxr/usd/usdGeom/sphere.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/xformable.h"
#include "pxr/usd/usdGeom/xformCommonAPI.h"
#include "pxr/usd/usdGeom/xformOp.h"
#include "pxr/usd/usdShade/input.h"
#include "pxr/usd/usdShade/material.h"
#include "pxr/usd/usdShade/materialBindingAPI.h"
#include "pxr/usd/usdShade/output.h"
#include "pxr/usd/usdShade/shader.h"
#include "pxr/usd/usdShade/tokens.h"
#include "pxr/usd/usdUtils/usdzPackage.h"
#include "pxr/usd/usdUtils/dependencies.h"

#include <emscripten/bind.h>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>
using namespace emscripten;

namespace {
// The legacy imaging adapter uses variant-selection syntax for some duplicated
// nested instancers. These cache addresses are not composed USD prim paths.
pxr::SdfPath AuthoredInstancePath(pxr::UsdStageRefPtr const& stage,
                                pxr::SdfPath const& path) {
  if (stage && stage->GetPrimAtPath(path)) return path;
  const auto composed = path.StripAllVariantSelections();
  return stage && stage->GetPrimAtPath(composed) ? composed : path;
}

using InstanceIdCache = std::map<pxr::SdfPath, pxr::VtInt64Array>;
std::string PointInstanceId(pxr::UsdStageRefPtr const& stage,
                            pxr::SdfPath const& path, int index,
                            pxr::UsdTimeCode time, InstanceIdCache& cache) {
  auto inserted = cache.emplace(path, pxr::VtInt64Array());
  if (inserted.second && stage) {
    const auto prim = stage->GetPrimAtPath(path);
    if (prim) prim.GetAttribute(pxr::TfToken("ids")).Get(&inserted.first->second, time);
  }
  const auto& ids = inserted.first->second;
  return index >= 0 && static_cast<size_t>(index) < ids.size()
      ? std::to_string(ids[index]) : std::string();
}
}

#include "generated/emHdCoreBindings.inc"

EMSCRIPTEN_BINDINGS(test_usd_imaging_emscripten) {
  pxr::hdEmscriptenGenerated::RegisterUsdCoreBindings();
  function("GetBuildInfoJson", optional_override([]() {
    return std::string(kHdEmscriptenOpenUsdBuildInfoJson);
  }));

  class_<pxr::HdWebSyncDriver>("HdWebSyncDriver")
    .constructor<emscripten::val, std::string>()
    .function("Draw", &pxr::HdWebSyncDriver::Draw, async())
    .function("DrawAsync", &pxr::HdWebSyncDriver::DrawAsync)
    .function("StartDraw", &pxr::HdWebSyncDriver::StartDraw)
    .function("IsDrawPending", &pxr::HdWebSyncDriver::IsDrawPending)
    .function("ConsumeDrawError", &pxr::HdWebSyncDriver::ConsumeDrawError)
    .function("Repopulate", &pxr::HdWebSyncDriver::Repopulate, async())
    .function("GetRefineLevelFallback", &pxr::HdWebSyncDriver::GetRefineLevelFallback)
    .function("SetRefineLevelFallback", &pxr::HdWebSyncDriver::SetRefineLevelFallback)
    .function("GetComplexity", &pxr::HdWebSyncDriver::GetComplexity)
    .function("SetComplexity", &pxr::HdWebSyncDriver::SetComplexity)
    .function("GetAdaptiveSubdivision", &pxr::HdWebSyncDriver::GetAdaptiveSubdivision)
    .function("SetAdaptiveSubdivision", &pxr::HdWebSyncDriver::SetAdaptiveSubdivision)
    .function("getFile", &pxr::HdWebSyncDriver::getFile)
    .function("resolveAssetUrl", &pxr::HdWebSyncDriver::resolveAssetUrl)
    .function("HasScopedHttpCache", &pxr::HdWebSyncDriver::HasScopedHttpCache)
    .function("HasStage", &pxr::HdWebSyncDriver::HasStage)
    // Resolve only the hit instance, not an O(instance count) JS mirror. Hydra's
    // scene delegate owns native/point/nested instancer index ordering.
    .function("ResolvePick", optional_override([](pxr::HdWebSyncDriver& driver,
                                                 const std::string& path,
                                                 int instanceIndex) {
      if (!driver.HasStage() || driver.IsDrawPending() || instanceIndex < 0) {
        return emscripten::val::null();
      }
      auto& delegate = driver.GetDelegate();
      const pxr::SdfPath rprimPath(path);
      if (!delegate.GetRenderIndex().GetRprim(rprimPath)) {
        return emscripten::val::null();
      }
      std::vector<pxr::HdInstancerContext> contexts;
      const auto paths = delegate.GetScenePrimPaths(
          rprimPath, std::vector<int>{instanceIndex}, &contexts);
      if (paths.empty() || paths.front().IsEmpty()) {
        return emscripten::val::null();
      }
      auto result = emscripten::val::object();
      result.set("path", AuthoredInstancePath(driver.GetStage(), paths.front()).GetString());
      auto instances = emscripten::val::array();
      InstanceIdCache idCache;
      if (!contexts.empty()) {
        for (const auto& entry : contexts.front()) {
          auto instance = emscripten::val::object();
          const auto authored = AuthoredInstancePath(driver.GetStage(), entry.first);
          instance.set("path", authored.GetString());
          instance.set("index", entry.second);
          const auto id = PointInstanceId(driver.GetStage(), authored, entry.second,
                                          delegate.GetTime(), idCache);
          if (!id.empty()) instance.set("id", id);
          instances.call<void>("push", instance);
        }
      }
      result.set("instances", instances);
      return result;
    }))
    .function("ResolveSelection", optional_override([](pxr::HdWebSyncDriver& driver,
                                                      const std::string& path,
                                                      int count,
                                                      emscripten::val selections) {
      if (!driver.HasStage() || driver.IsDrawPending() || count < 0) {
        return emscripten::val::null();
      }
      auto& delegate = driver.GetDelegate();
      const pxr::SdfPath rprimPath(path);
      if (!delegate.GetRenderIndex().GetRprim(rprimPath)) {
        return emscripten::val::null();
      }
      struct Context {pxr::SdfPath path; int index; std::string id;};
      struct Target {pxr::SdfPath path; std::vector<Context> context;};
      std::vector<Target> targets;
      for (unsigned i = 0; i < selections["length"].as<unsigned>(); ++i) {
        const auto value = selections[i];
        const pxr::SdfPath target(value["path"].as<std::string>());
        if (!target.IsAbsolutePath()) continue;
        std::vector<Context> context;
        const auto instances = value["instances"];
        if (!instances.isUndefined() && !instances.isNull()) {
          for (unsigned j = 0; j < instances["length"].as<unsigned>(); ++j) {
            const auto id = instances[j]["id"];
            context.push_back({pxr::SdfPath(instances[j]["path"].as<std::string>()),
                               instances[j]["index"].as<int>(),
                               id.isString() ? id.as<std::string>() : std::string()});
          }
        }
        targets.push_back({target, std::move(context)});
      }
      auto result = emscripten::val::array();
      if (targets.empty() || !count) return result;
      std::vector<int> indices(count);
      for (int i = 0; i < count; ++i) indices[i] = i;
      std::vector<pxr::HdInstancerContext> contexts;
      const auto paths = delegate.GetScenePrimPaths(rprimPath, indices, &contexts);
      InstanceIdCache idCache;
      for (size_t i = 0; i < paths.size(); ++i) {
        const pxr::HdInstancerContext empty;
        const auto& context = i < contexts.size() ? contexts[i] : empty;
        for (const auto& target : targets) {
          bool matches = AuthoredInstancePath(driver.GetStage(), paths[i]).HasPrefix(target.path);
          for (const auto& instance : context) {
            if (AuthoredInstancePath(driver.GetStage(), instance.first).HasPrefix(target.path)) matches = true;
          }
          if (!matches) continue;
          for (const auto& required : target.context) {
            bool found = false;
            for (const auto& actual : context) {
              const auto authored = AuthoredInstancePath(driver.GetStage(), actual.first);
              if (authored != required.path) continue;
              found = required.id.empty() ? actual.second == required.index
                  : PointInstanceId(driver.GetStage(), authored, actual.second,
                                    delegate.GetTime(), idCache) == required.id;
              if (found) break;
            }
            if (!found) {matches = false; break;}
          }
          if (matches) {result.call<void>("push", static_cast<int>(i)); break;}
        }
      }
      return result;
    }))
    .function("GetStage", optional_override([](pxr::HdWebSyncDriver& driver) {
      return get_pointer(driver.GetStage());
    }), allow_raw_pointers())
    .function("GetPhysicsSceneJson", &pxr::HdWebSyncDriver::GetPhysicsSceneJson)
    .function("GetStageUpAxis", &pxr::HdWebSyncDriver::GetStageUpAxis)
    .function("GetStageStartTimeCode", &pxr::HdWebSyncDriver::GetStageStartTimeCode)
    .function("GetStageEndTimeCode", &pxr::HdWebSyncDriver::GetStageEndTimeCode)
    .function("GetStageTimeCodesPerSecond", &pxr::HdWebSyncDriver::GetStageTimeCodesPerSecond)
    .function("SetIncludedPurposes", &pxr::HdWebSyncDriver::SetIncludedPurposes)
    .function("SetTime", &pxr::HdWebSyncDriver::SetTime)
    .function("GetTime", &pxr::HdWebSyncDriver::GetTime)
    .smart_ptr<std::shared_ptr<pxr::HdWebSyncDriver>>("std::shared_ptr<pxr::HdWebSyncDriver>")
    ;
}
