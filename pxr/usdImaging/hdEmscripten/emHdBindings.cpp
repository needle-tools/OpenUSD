#include "pxr/pxr.h"

#include "webSyncDriver.h"

#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/schema.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/editContext.h"
#include "pxr/usd/usd/payloads.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/relationship.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usd/variantSets.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/usd/usdUtils/usdzPackage.h"

#include <emscripten/bind.h>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace emscripten;

#include "generated/emHdCoreBindings.inc"

EMSCRIPTEN_BINDINGS(test_usd_imaging_emscripten) {
  pxr::hdEmscriptenGenerated::RegisterUsdCoreBindings();

  class_<pxr::HdWebSyncDriver>("HdWebSyncDriver")
    .constructor<emscripten::val, std::string>()
    .function("Draw", &pxr::HdWebSyncDriver::Draw)
    .function("getFile", &pxr::HdWebSyncDriver::getFile)
    .function("HasStage", &pxr::HdWebSyncDriver::HasStage)
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
}
