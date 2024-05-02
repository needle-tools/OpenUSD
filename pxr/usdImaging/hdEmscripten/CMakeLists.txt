set(PXR_PREFIX pxr/hdEmscripten)
set(PXR_PACKAGE hdEmscripten)
add_subdirectory(httpResolver)

if (NOT PXR_ENABLE_JS_SUPPORT)
    return()
endif()

set(BINDINGS_NAME "emHdBindings")

pxr_library(hdEmscripten
    LIBRARIES
      hd
      usd
      usdUtils
      sdf
      -Wl,--whole-archive usdShaders -Wl,--no-whole-archive
        -Wl,--whole-archive usdSkel -Wl,--no-whole-archive
      httpResolver

    PUBLIC_CLASSES
        webRenderDelegate

    PUBLIC_HEADERS
        webRenderDelegate.h
)


target_link_options(hdEmscripten_internal PRIVATE "SHELL: -Oz")
target_compile_options(hdEmscripten_internal PRIVATE "SHELL: -lembind -pthread")

set(RESOURCE_PARAMETERS "")
function(add_resources target)
    # This local var is needed since list append cannot update the PARENT_SCOPE
    set(LOCAL_VAR ${RESOURCE_PARAMETERS})
    get_property(RESSOURCES TARGET ${target} PROPERTY EMSCRIPTEN_RESOURCES)
    list(APPEND LOCAL_VAR "${RESSOURCES}")
    set(RESOURCE_PARAMETERS "${LOCAL_VAR}" PARENT_SCOPE)
endfunction()

add_resources(usdShade)
add_resources(sdf)
add_resources(hd)
add_resources(usdHydra)
add_resources(usdShaders)
add_resources(usdRender)
add_resources(usdVol)
add_resources(usdImaging)
add_resources(usd)
add_resources(usdLux)
add_resources(ar)
add_resources(usdGeom)
add_resources(usdSkel)
add_resources(ndr)
add_resources(httpResolver)

list(APPEND RESOURCE_PARAMETERS "--preload-file ${PROJECT_BINARY_DIR}/plugins_plugInfo.json@/usd/plugInfo.json")

set(BUILD_FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${BINDINGS_NAME}.js
    ${CMAKE_CURRENT_BINARY_DIR}/${BINDINGS_NAME}.wasm
    ${CMAKE_CURRENT_BINARY_DIR}/${BINDINGS_NAME}.worker.js
    ${CMAKE_CURRENT_SOURCE_DIR}/static/hdEmscripten.html
    ${CMAKE_CURRENT_SOURCE_DIR}/static/simpleShading.usda
    ${CMAKE_CURRENT_SOURCE_DIR}/js/ThreeJsRenderDelegate.js
    ${CMAKE_CURRENT_SOURCE_DIR}/js/OrbitControls.js
    ${CMAKE_CURRENT_SOURCE_DIR}/js/coi-serviceworker.min.js
    ${CMAKE_CURRENT_SOURCE_DIR}/environments/helicopter-landing-pad-vis-4K.hdr
)

if(PXR_WASM_NODE)
    list(TRANSFORM RESOURCE_PARAMETERS REPLACE "-preload" "-embed")
else()
    list(APPEND BUILD_FILES "${CMAKE_CURRENT_BINARY_DIR}/${BINDINGS_NAME}.data")
endif()

pxr_cpp_bin(${BINDINGS_NAME}
  LIBRARIES
      hdEmscripten
      usdImaging
      httpResolver
      ${RESOURCE_PARAMETERS}
)

set_target_properties(${BINDINGS_NAME}
    PROPERTIES
        SUFFIX ".js"
)
target_link_options(${BINDINGS_NAME} PRIVATE "SHELL: -Oz -s EXPORT_NAME='getUsdModule' \
    -s MODULARIZE=1 -lembind -s PTHREAD_POOL_SIZE=10 -s ASYNCIFY -s ASYNCIFY_ADVISE \
    -sIMPORTED_MEMORY=1 --pre-js ${CMAKE_CURRENT_SOURCE_DIR}/js/MemoryConfiguration.js  \
    -s ASYNCIFY_REMOVE=['__vfprintf_internal','__fseeko_unlocked','*::itanium_demangle::*','*VtArray*','*_Clear*','*dlfree*','*TfType*','*Tf_Registry*','*TfRefPtr*','*::__shared_weak_count::*','*Sdf_VariableExpressionImpl::*','*Tf_Post*','*tao::PXR_INTERNAL_NS_pegtl*','*UsdObject*','*TfSmallVector*','*TfMallocTag*','*__cxa_guard_acquire*,'*UsdSchema*','*Usd_CrateFile*','*TfEnum*','*TfErrorMark*','*VtStreamOutArray*','*SdfSpec*','*UsdObject*','*Tf_PostNullSmartPtrDereferenceFatalError*','*std::logic_error*','*Tf_PostWarningHelper*','*SdfReference*','*TfStaticData*','*std::__2::enable_if*','*Usd_PrimDataHandle*','*TfMallocTag*','*Tf_AxiomHelper*','*Sdf_FileIOUtility*','*pxr_tsl*','*std::declval*','*std::__2::__hash*','*std::__2::__split*','*VtDictionary*','*byn$mgfn-shared*','*Tf_AxiomHelper*','*__variant_detail*','*ets_base*','*Usd_ClipSet*','*TfDebug*','*concurrent_vector_base_v3*','*HdTypedSampledDataSource*','*UsdGeomBBoxCache*','*TfDelegatedCountPtr*','*Tf_DiagnosticHelper*','*UsdAttributeQuery*','*HdMergingSceneIndex*','*Usd_CrateDataImpl*','*ostreambuf_iterator*','*PlugPlugin*','*PcpCulledDependency*','*Usd_SortAutoAppliedAPISchemas*','*GeomUtilMeshGeneratorBase*','*Arch_MeasureExecutionTime*','*PcpMapExpression*','*SdfAllowed*','*SdfPredicateProgram*','*pxr_tsl::*','*SdfGet*','*GfVec*','*GfMatrix*','*tbb::interface5::internal::concurrent_unordered_base*','*TfDenseHashMap*','*PcpSite*','*Sdf_LsdMapEditor*','*SdfPayload*','*UsdImagingIndexProxy*','*std::__2::locale::*','*UsdAttributeQuery*','*Usd_Clip*']")
install(
    FILES
    ${BUILD_FILES}
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
)