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

#include <emscripten/bind.h>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>
using namespace emscripten;

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
      result.set("path", paths.front().GetString());
      auto instances = emscripten::val::array();
      if (!contexts.empty()) {
        for (const auto& entry : contexts.front()) {
          auto instance = emscripten::val::object();
          instance.set("path", entry.first.GetString());
          instance.set("index", entry.second);
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
      std::vector<std::pair<pxr::SdfPath, pxr::HdInstancerContext>> targets;
      for (unsigned i = 0; i < selections["length"].as<unsigned>(); ++i) {
        const auto value = selections[i];
        const pxr::SdfPath target(value["path"].as<std::string>());
        if (!target.IsAbsolutePath()) continue;
        pxr::HdInstancerContext context;
        const auto instances = value["instances"];
        if (!instances.isUndefined() && !instances.isNull()) {
          for (unsigned j = 0; j < instances["length"].as<unsigned>(); ++j) {
            context.emplace_back(pxr::SdfPath(instances[j]["path"].as<std::string>()),
                                 instances[j]["index"].as<int>());
          }
        }
        targets.emplace_back(target, std::move(context));
      }
      auto result = emscripten::val::array();
      if (targets.empty() || !count) return result;
      std::vector<int> indices(count);
      for (int i = 0; i < count; ++i) indices[i] = i;
      std::vector<pxr::HdInstancerContext> contexts;
      const auto paths = delegate.GetScenePrimPaths(rprimPath, indices, &contexts);
      for (size_t i = 0; i < paths.size(); ++i) {
        const pxr::HdInstancerContext empty;
        const auto& context = i < contexts.size() ? contexts[i] : empty;
        for (const auto& target : targets) {
          bool matches = paths[i].HasPrefix(target.first);
          for (const auto& instance : context) {
            if (instance.first.HasPrefix(target.first)) matches = true;
          }
          if (!matches) continue;
          for (const auto& required : target.second) {
            if (std::find(context.begin(), context.end(), required) == context.end()) {
              matches = false; break;
            }
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
