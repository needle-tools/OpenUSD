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
set(HD_EMSCRIPTEN_BUILD_INFO_JSON_FILE "${HD_EMSCRIPTEN_GENERATED_DIR}/openusd-build-info.json")
set(HD_EMSCRIPTEN_BUILD_INFO_HEADER "${HD_EMSCRIPTEN_GENERATED_DIR}/openusdBuildInfo.h")
file(MAKE_DIRECTORY "${HD_EMSCRIPTEN_GENERATED_DIR}")
if(PYTHON_EXECUTABLE)
    set(HD_EMSCRIPTEN_PYTHON_EXECUTABLE "${PYTHON_EXECUTABLE}")
else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(HD_EMSCRIPTEN_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
endif()

function(_hd_emscripten_json_bool out value)
    if(${value})
        set(${out} "true" PARENT_SCOPE)
    else()
        set(${out} "false" PARENT_SCOPE)
    endif()
endfunction()

function(_hd_emscripten_git_sha out repo)
    if(EXISTS "${repo}/.git")
        execute_process(
            COMMAND git -C "${repo}" rev-parse HEAD
            OUTPUT_VARIABLE _sha
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
    set(${out} "${_sha}" PARENT_SCOPE)
endfunction()

function(_hd_emscripten_git_dirty out repo)
    set(_dirty "false")
    if(EXISTS "${repo}/.git")
        execute_process(
            COMMAND git -C "${repo}" diff --quiet --ignore-submodules HEAD --
            RESULT_VARIABLE _diff_result
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(NOT _diff_result EQUAL 0)
            set(_dirty "true")
        endif()
    endif()
    set(${out} "${_dirty}" PARENT_SCOPE)
endfunction()

_hd_emscripten_git_sha(HD_EMSCRIPTEN_OPENUSD_GIT_SHA "${PROJECT_SOURCE_DIR}")
_hd_emscripten_git_dirty(HD_EMSCRIPTEN_OPENUSD_GIT_DIRTY "${PROJECT_SOURCE_DIR}")
_hd_emscripten_json_bool(HD_EMSCRIPTEN_BUILD_USD_IMAGING PXR_BUILD_USD_IMAGING)
_hd_emscripten_json_bool(HD_EMSCRIPTEN_BUILD_HD_EMSCRIPTEN PXR_BUILD_HD_EMSCRIPTEN)
_hd_emscripten_json_bool(HD_EMSCRIPTEN_ENABLE_MATERIALX PXR_ENABLE_MATERIALX_SUPPORT)
_hd_emscripten_json_bool(HD_EMSCRIPTEN_ENABLE_USD_DRACO PXR_BUILD_DRACO_PLUGIN)
if(OPENSUBDIV_FOUND OR OPENSUBDIV_OSDCPU_LIBRARY OR OpenSubdiv_DIR)
    set(HD_EMSCRIPTEN_ENABLE_OPENSUBDIV "true")
else()
    set(HD_EMSCRIPTEN_ENABLE_OPENSUBDIV "false")
endif()
if(PXR_HD_EMSCRIPTEN_GLTF_PLUGIN_PREFIX)
    set(HD_EMSCRIPTEN_ENABLE_GLTF "true")
else()
    set(HD_EMSCRIPTEN_ENABLE_GLTF "false")
endif()
if(NOT DEFINED PXR_HD_EMSCRIPTEN_GLTF_DRACO)
    set(PXR_HD_EMSCRIPTEN_GLTF_DRACO OFF)
endif()
if(PXR_HD_EMSCRIPTEN_GLTF_PLUGIN_PREFIX AND PXR_HD_EMSCRIPTEN_GLTF_DRACO)
    set(HD_EMSCRIPTEN_ENABLE_GLTF_DRACO "true")
else()
    set(HD_EMSCRIPTEN_ENABLE_GLTF_DRACO "false")
endif()
if(NOT DEFINED PXR_HD_EMSCRIPTEN_EMSCRIPTEN_VERSION)
    set(PXR_HD_EMSCRIPTEN_EMSCRIPTEN_VERSION "${CMAKE_CXX_COMPILER_VERSION}")
endif()
if(NOT OPENSUBDIV_VERSION AND OPENSUBDIV_INCLUDE_DIR
        AND EXISTS "${OPENSUBDIV_INCLUDE_DIR}/opensubdiv/version.h")
    file(STRINGS
        "${OPENSUBDIV_INCLUDE_DIR}/opensubdiv/version.h"
        _hd_emscripten_opensubdiv_version_line
        REGEX "^#define OPENSUBDIV_VERSION v[0-9_]+")
    if(_hd_emscripten_opensubdiv_version_line)
        string(REGEX REPLACE
            "^#define OPENSUBDIV_VERSION v([0-9]+)_([0-9]+)_([0-9]+).*"
            "\\1.\\2.\\3"
            OPENSUBDIV_VERSION
            "${_hd_emscripten_opensubdiv_version_line}")
    endif()
endif()
if(NOT DEFINED PXR_HD_EMSCRIPTEN_MATERIALX_REPO_SHA)
    set(PXR_HD_EMSCRIPTEN_MATERIALX_REPO_SHA "")
endif()
if(NOT DEFINED PXR_HD_EMSCRIPTEN_GLTF_PLUGIN_REPO_SHA)
    set(PXR_HD_EMSCRIPTEN_GLTF_PLUGIN_REPO_SHA "")
endif()
if(NOT DEFINED PXR_HD_EMSCRIPTEN_EMSDK_SHA)
    set(PXR_HD_EMSCRIPTEN_EMSDK_SHA "")
endif()
if(NOT DEFINED HD_EMSCRIPTEN_MATERIALX_REPO_DIRTY)
    set(HD_EMSCRIPTEN_MATERIALX_REPO_DIRTY "false")
endif()
if(NOT DEFINED HD_EMSCRIPTEN_GLTF_PLUGIN_REPO_DIRTY)
    set(HD_EMSCRIPTEN_GLTF_PLUGIN_REPO_DIRTY "false")
endif()
if(NOT DEFINED HD_EMSCRIPTEN_EMSDK_DIRTY)
    set(HD_EMSCRIPTEN_EMSDK_DIRTY "false")
endif()
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/openusd-build-info.json.in"
    "${HD_EMSCRIPTEN_BUILD_INFO_JSON_FILE}"
    @ONLY
)
file(READ "${HD_EMSCRIPTEN_BUILD_INFO_JSON_FILE}" HD_EMSCRIPTEN_BUILD_INFO_JSON)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/openusdBuildInfo.h.in"
    "${HD_EMSCRIPTEN_BUILD_INFO_HEADER}"
    @ONLY
)

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
add_resources(usdDraco)
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
    set(_gltfPluginDracoLib "${_gltfPluginPrefix}/lib/libdraco.a")

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
    if(PXR_HD_EMSCRIPTEN_GLTF_DRACO)
        if(NOT EXISTS "${_gltfPluginDracoLib}")
            message(FATAL_ERROR
                "PXR_HD_EMSCRIPTEN_GLTF_DRACO is ON, but ${_gltfPluginDracoLib} was not found.")
        endif()
        list(APPEND HD_EMSCRIPTEN_GLTF_PLUGIN_LIBS "${_gltfPluginDracoLib}")
    endif()
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

set(HD_EMSCRIPTEN_EMBED_RESOURCES TRUE)
if(HD_EMSCRIPTEN_EMBED_RESOURCES)
    list(TRANSFORM RESOURCE_PARAMETERS REPLACE "-preload" "-embed")
endif()

if(NOT PXR_WASM_NODE)
    list(APPEND BUILD_FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${BINDINGS_NAME}.wasm"
    )
    if(NOT HD_EMSCRIPTEN_EMBED_RESOURCES)
        list(APPEND BUILD_FILES
            "${CMAKE_CURRENT_BINARY_DIR}/${BINDINGS_NAME}.data"
        )
    endif()
endif()

set(HD_EMSCRIPTEN_MATERIALX_LIBS "")
if(PXR_ENABLE_MATERIALX_SUPPORT)
    list(APPEND HD_EMSCRIPTEN_MATERIALX_LIBS
        usdMtlx
        hdMtlx
    )
endif()

set(HD_EMSCRIPTEN_NATIVE_PLUGIN_LIBS "")
if(PXR_BUILD_DRACO_PLUGIN)
    list(APPEND HD_EMSCRIPTEN_NATIVE_PLUGIN_LIBS
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,$<TARGET_FILE:usdDraco>>"
    )
endif()

add_executable(${BINDINGS_NAME}
    emHdBindings.cpp
    "${HD_EMSCRIPTEN_CORE_BINDINGS_INC}"
    "${HD_EMSCRIPTEN_BUILD_INFO_HEADER}"
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
    pxOsd
    httpResolver
    ${HD_EMSCRIPTEN_MATERIALX_LIBS}
    ${HD_EMSCRIPTEN_NATIVE_PLUGIN_LIBS}
    ${HD_EMSCRIPTEN_GLTF_PLUGIN_LIBS}
    ${RESOURCE_PARAMETERS}
    ${PXR_MALLOC_LIBRARY}
)

if(EMSCRIPTEN)
    target_link_options(${BINDINGS_NAME}
        PRIVATE
            "SHELL:-Wl,--whole-archive $<TARGET_FILE:usdShaders> $<TARGET_FILE:sdrGlslfx> -Wl,--no-whole-archive"
    )
    add_dependencies(${BINDINGS_NAME} usdShaders sdrGlslfx)
    if(PXR_BUILD_DRACO_PLUGIN)
        add_dependencies(${BINDINGS_NAME} usdDraco)
    endif()
endif()

set_target_properties(${BINDINGS_NAME}
    PROPERTIES
        SUFFIX ".js"
        LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/js/MemoryConfiguration.js;${CMAKE_CURRENT_SOURCE_DIR}/js/ViewerRuntimeExports.js;${CMAKE_CURRENT_SOURCE_DIR}/js/ViewerExternRuntimeExports.js"
)
target_compile_options(${BINDINGS_NAME} PRIVATE "SHELL: -lembind -pthread")
target_link_options(${BINDINGS_NAME} PRIVATE "SHELL: -Oz -s EXPORT_NAME='getUsdModule' \
    -s MODULARIZE=1 -lembind -s PTHREAD_POOL_SIZE=10 -s ASYNCIFY \
    -s ASYNCIFY_STACK_SIZE=1048576 \
    -sIMPORTED_MEMORY=1 --pre-js ${CMAKE_CURRENT_SOURCE_DIR}/js/MemoryConfiguration.js  \
    --post-js ${CMAKE_CURRENT_SOURCE_DIR}/js/ViewerRuntimeExports.js \
    --extern-post-js ${CMAKE_CURRENT_SOURCE_DIR}/js/ViewerExternRuntimeExports.js")

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

install(
    FILES "${HD_EMSCRIPTEN_BUILD_INFO_JSON_FILE}"
    DESTINATION ${CMAKE_INSTALL_PREFIX}/share/hdEmscripten
)
