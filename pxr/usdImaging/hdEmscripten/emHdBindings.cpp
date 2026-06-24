#include "pxr/pxr.h"

#include "webSyncDriver.h"

#include "pxr/base/tf/stringUtils.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/relationship.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/tokens.h"

#include <emscripten/bind.h>
#include <string>
#include <vector>
using namespace emscripten;

EMSCRIPTEN_BINDINGS(test_usd_imaging_emscripten) {
  class_<pxr::SdfLayer>("Layer")
    .function("GetIdentifier", &pxr::SdfLayer::GetIdentifier)
    .function("GetDisplayName", &pxr::SdfLayer::GetDisplayName)
    .function("ExportToString", optional_override([](pxr::SdfLayer& layer) {
      std::string result;
      layer.ExportToString(&result);
      return result;
    }))
    .function("Save", optional_override([](pxr::SdfLayer& layer) {
      return layer.Save();
    }))
    ;

  class_<pxr::UsdAttribute>("Attribute")
    .function("IsValid", optional_override([](pxr::UsdAttribute const& attr) {
      return static_cast<bool>(attr);
    }))
    .function("GetName", optional_override([](pxr::UsdAttribute const& attr) {
      return attr.GetName().GetString();
    }))
    .function("GetPath", optional_override([](pxr::UsdAttribute const& attr) {
      return attr.GetPath().GetAsString();
    }))
    .function("GetTypeName", optional_override([](pxr::UsdAttribute const& attr) {
      return attr.GetTypeName().GetAsToken().GetString();
    }))
    .function("GetValueString", optional_override([](pxr::UsdAttribute const& attr) {
      pxr::VtValue value;
      if (!attr.Get(&value, pxr::UsdTimeCode::Default())) {
        return std::string();
      }
      return pxr::TfStringify(value);
    }))
    ;

  class_<pxr::UsdRelationship>("Relationship")
    .function("IsValid", optional_override([](pxr::UsdRelationship const& rel) {
      return static_cast<bool>(rel);
    }))
    .function("GetName", optional_override([](pxr::UsdRelationship const& rel) {
      return rel.GetName().GetString();
    }))
    .function("GetPath", optional_override([](pxr::UsdRelationship const& rel) {
      return rel.GetPath().GetAsString();
    }))
    .function("GetTargets", optional_override([](pxr::UsdRelationship const& rel) {
      pxr::SdfPathVector targets;
      std::vector<std::string> result;
      if (rel.GetTargets(&targets)) {
        result.reserve(targets.size());
        for (pxr::SdfPath const& target : targets) {
          result.push_back(target.GetAsString());
        }
      }
      return result;
    }))
    ;

  class_<pxr::UsdPrim>("Prim")
    .function("IsValid", optional_override([](pxr::UsdPrim const& prim) {
      return static_cast<bool>(prim);
    }))
    .function("GetName", optional_override([](pxr::UsdPrim const& prim) {
      return prim.GetName().GetString();
    }))
    .function("GetPath", optional_override([](pxr::UsdPrim const& prim) {
      return prim.GetPath().GetAsString();
    }))
    .function("GetTypeName", optional_override([](pxr::UsdPrim const& prim) {
      return prim.GetTypeName().GetString();
    }))
    .function("IsActive", &pxr::UsdPrim::IsActive)
    .function("SetActive", &pxr::UsdPrim::SetActive)
    .function("IsDefined", &pxr::UsdPrim::IsDefined)
    .function("IsLoaded", &pxr::UsdPrim::IsLoaded)
    .function("GetParent", &pxr::UsdPrim::GetParent)
    .function("GetChildren", optional_override([](pxr::UsdPrim const& prim) {
      std::vector<pxr::UsdPrim> result;
      for (pxr::UsdPrim const& child : prim.GetChildren()) {
        result.push_back(child);
      }
      return result;
    }))
    .function("GetPropertyNames", optional_override([](pxr::UsdPrim const& prim) {
      std::vector<std::string> result;
      for (pxr::TfToken const& name : prim.GetPropertyNames()) {
        result.push_back(name.GetString());
      }
      return result;
    }))
    .function("GetAttribute", optional_override([](pxr::UsdPrim const& prim, std::string const& name) {
      return prim.GetAttribute(pxr::TfToken(name));
    }))
    .function("GetRelationship", optional_override([](pxr::UsdPrim const& prim, std::string const& name) {
      return prim.GetRelationship(pxr::TfToken(name));
    }))
    ;

  class_<pxr::UsdStage>("Stage")
    .function("GetRootLayer", optional_override([](pxr::UsdStage& stage) {
      return get_pointer(stage.GetRootLayer());
    }), allow_raw_pointers())
    .function("GetPseudoRoot", &pxr::UsdStage::GetPseudoRoot)
    .function("GetPrimAtPath", optional_override([](pxr::UsdStage& stage, std::string const& path) {
      return stage.GetPrimAtPath(pxr::SdfPath(path));
    }))
    .function("Traverse", optional_override([](pxr::UsdStage& stage) {
      std::vector<pxr::UsdPrim> result;
      for (pxr::UsdPrim const& prim : stage.Traverse()) {
        result.push_back(prim);
      }
      return result;
    }))
    .function("GetStartTimeCode", &pxr::UsdStage::GetStartTimeCode)
    .function("GetEndTimeCode", &pxr::UsdStage::GetEndTimeCode)
    .function("GetTimeCodesPerSecond", &pxr::UsdStage::GetTimeCodesPerSecond)
    .function("GetUpAxis", optional_override([](pxr::UsdStage& stage) {
      pxr::TfToken upAxis;
      if (stage.HasAuthoredMetadata(pxr::UsdGeomTokens->upAxis)) {
        stage.GetMetadata(pxr::UsdGeomTokens->upAxis, &upAxis);
      } else {
        upAxis = pxr::UsdGeomGetFallbackUpAxis();
      }
      return upAxis == pxr::UsdGeomTokens->z ? 'z' : 'y';
    }))
    ;

  class_<pxr::HdWebSyncDriver>("HdWebSyncDriver")
    .constructor<emscripten::val, std::string>()
    .function("Draw", &pxr::HdWebSyncDriver::Draw)
    .function("getFile", &pxr::HdWebSyncDriver::getFile)
    .function("GetStage", optional_override([](pxr::HdWebSyncDriver& driver) {
      return get_pointer(driver.GetStage());
    }), allow_raw_pointers())
    .function("GetStageUpAxis", &pxr::HdWebSyncDriver::GetStageUpAxis)
    .function("GetStageStartTimeCode", &pxr::HdWebSyncDriver::GetStageStartTimeCode)
    .function("GetStageEndTimeCode", &pxr::HdWebSyncDriver::GetStageEndTimeCode)
    .function("GetStageTimeCodesPerSecond", &pxr::HdWebSyncDriver::GetStageTimeCodesPerSecond)
    .function("SetTime", &pxr::HdWebSyncDriver::SetTime)
    .function("GetTime", &pxr::HdWebSyncDriver::GetTime)
    .smart_ptr<std::shared_ptr<pxr::HdWebSyncDriver>>("std::shared_ptr<pxr::HdWebSyncDriver>")
    ;

  register_vector<int>("VectorInt");
  register_vector<double>("VectorDouble");
  register_vector<std::string>("VectorString");
  register_vector<pxr::UsdPrim>("VectorPrim");
}
