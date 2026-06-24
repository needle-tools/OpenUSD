set(PXR_PREFIX pxr/hdEmscripten)
set(PXR_PACKAGE hdEmscripten)
add_subdirectory(httpResolver)

if (NOT PXR_BUILD_HD_EMSCRIPTEN)
    return()
endif()

set(BINDINGS_NAME "emHdBindings")
set(HD_EMSCRIPTEN_BINDGEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bindgen")
set(HD_EMSCRIPTEN_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(HD_EMSCRIPTEN_CORE_BINDINGS_INC "${HD_EMSCRIPTEN_GENERATED_DIR}/emHdCoreBindings.inc")
set(HD_EMSCRIPTEN_CORE_BINDINGS_DTS "${HD_EMSCRIPTEN_GENERATED_DIR}/usd-core-bindings.d.ts")
if(PYTHON_EXECUTABLE)
    set(HD_EMSCRIPTEN_PYTHON_EXECUTABLE "${PYTHON_EXECUTABLE}")
else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(HD_EMSCRIPTEN_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
endif()

add_custom_command(
    OUTPUT
        "${HD_EMSCRIPTEN_CORE_BINDINGS_INC}"
        "${HD_EMSCRIPTEN_CORE_BINDINGS_DTS}"
    COMMAND
        "${HD_EMSCRIPTEN_PYTHON_EXECUTABLE}"
        "${HD_EMSCRIPTEN_BINDGEN_DIR}/generate_bindings.py"
        --manifest "${HD_EMSCRIPTEN_BINDGEN_DIR}/core-bindings.json"
        --cpp-out "${HD_EMSCRIPTEN_CORE_BINDINGS_INC}"
        --dts-out "${HD_EMSCRIPTEN_CORE_BINDINGS_DTS}"
    DEPENDS
        "${HD_EMSCRIPTEN_BINDGEN_DIR}/generate_bindings.py"
        "${HD_EMSCRIPTEN_BINDGEN_DIR}/core-bindings.json"
    COMMENT "Generating hdEmscripten core USD bindings"
    VERBATIM
)

set(RESOURCE_PARAMETERS "")
function(add_resources target)
    if (NOT TARGET ${target})
        return()
    endif()
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
add_resources(sdr)
add_resources(httpResolver)
if(PXR_ENABLE_MATERIALX_SUPPORT)
    add_resources(usdMtlx)
    add_resources(hdMtlx)
endif()

set(HD_EMSCRIPTEN_GLTF_PLUGIN_LIBS "")
if(PXR_HD_EMSCRIPTEN_GLTF_PLUGIN_PREFIX)
    set(_gltfPluginPrefix "${PXR_HD_EMSCRIPTEN_GLTF_PLUGIN_PREFIX}")
    set(_gltfPluginResources "${_gltfPluginPrefix}/plugin/usd/usdGltf/resources/plugInfo.json")
    set(_gltfPluginLib "${_gltfPluginPrefix}/lib/libusdGltf.a")
    set(_gltfPluginUtilsLib "${_gltfPluginPrefix}/lib/libfileformatUtils.a")
    set(_gltfPluginTinyGltfLib "${_gltfPluginPrefix}/lib/libtinygltf.a")

    foreach(_gltfRequiredFile
            "${_gltfPluginResources}"
            "${_gltfPluginLib}"
            "${_gltfPluginUtilsLib}"
            "${_gltfPluginTinyGltfLib}")
        if(NOT EXISTS "${_gltfRequiredFile}")
            message(FATAL_ERROR
                "PXR_HD_EMSCRIPTEN_GLTF_PLUGIN_PREFIX is set, but ${_gltfRequiredFile} was not found.")
        endif()
    endforeach()

    list(APPEND RESOURCE_PARAMETERS
        "--preload-file ${_gltfPluginResources}@/usd/usdGltf/resources/plugInfo.json")
    list(APPEND HD_EMSCRIPTEN_GLTF_PLUGIN_LIBS
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,${_gltfPluginLib}>"
        "${_gltfPluginUtilsLib}"
        "${_gltfPluginTinyGltfLib}"
    )
endif()

list(APPEND RESOURCE_PARAMETERS "--preload-file ${PROJECT_BINARY_DIR}/plugins_plugInfo.json@/usd/plugInfo.json")

set(BUILD_FILES
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
list(APPEND BUILD_FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${BINDINGS_NAME}.data"
    "${CMAKE_CURRENT_BINARY_DIR}/${BINDINGS_NAME}.wasm"
)
endif()

set(HD_EMSCRIPTEN_MATERIALX_LIBS "")
if(PXR_ENABLE_MATERIALX_SUPPORT)
    list(APPEND HD_EMSCRIPTEN_MATERIALX_LIBS
        usdMtlx
        hdMtlx
    )
endif()

add_executable(${BINDINGS_NAME}
    emHdBindings.cpp
    "${HD_EMSCRIPTEN_CORE_BINDINGS_INC}"
    webRenderDelegate.cpp
)

target_include_directories(${BINDINGS_NAME}
    PRIVATE
        "${PROJECT_BINARY_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}"
)

_pxr_target_link_libraries(${BINDINGS_NAME}
    usdImaging
    usdUtils
    usdSkel
    usdShaders
    httpResolver
    ${HD_EMSCRIPTEN_MATERIALX_LIBS}
    ${HD_EMSCRIPTEN_GLTF_PLUGIN_LIBS}
    ${RESOURCE_PARAMETERS}
    ${PXR_MALLOC_LIBRARY}
)

set_target_properties(${BINDINGS_NAME}
    PROPERTIES
        SUFFIX ".js"
        LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/js/MemoryConfiguration.js;${CMAKE_CURRENT_SOURCE_DIR}/js/ViewerRuntimeExports.js;${CMAKE_CURRENT_SOURCE_DIR}/js/ViewerExternRuntimeExports.js"
)
target_compile_options(${BINDINGS_NAME} PRIVATE "SHELL: -lembind -pthread")
target_link_options(${BINDINGS_NAME} PRIVATE "SHELL: -Oz -s EXPORT_NAME='getUsdModule' \
    -s MODULARIZE=1 -lembind -s PTHREAD_POOL_SIZE=10 -s ASYNCIFY \
    -sIMPORTED_MEMORY=1 --pre-js ${CMAKE_CURRENT_SOURCE_DIR}/js/MemoryConfiguration.js  \
    --post-js ${CMAKE_CURRENT_SOURCE_DIR}/js/ViewerRuntimeExports.js \
    --extern-post-js ${CMAKE_CURRENT_SOURCE_DIR}/js/ViewerExternRuntimeExports.js \
    -s ASYNCIFY_REMOVE=['__vfprintf_internal','__fseeko_unlocked','*::itanium_demangle::*','*VtArray*','*_Clear*','*dlfree*','*TfType*','*Tf_Registry*','*Tf_Post*','*tao::PXR_INTERNAL_NS_pegtl*','*UsdObject*','*TfSmallVector*','*TfMallocTag*','*__cxa_guard_acquire*,'*UsdSchema*','*Usd_CrateFile*','*TfEnum*','*TfErrorMark*','*VtStreamOutArray*','*SdfSpec*']")

install(
    TARGETS ${BINDINGS_NAME}
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
)

install(
    FILES
    ${BUILD_FILES}
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/${BINDINGS_NAME}.worker.js
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
    OPTIONAL
)

install(
    FILES "${HD_EMSCRIPTEN_CORE_BINDINGS_DTS}"
    DESTINATION ${CMAKE_INSTALL_PREFIX}/share/hdEmscripten
)
