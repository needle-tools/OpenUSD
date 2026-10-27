set(DIRS
    usdImaging
    usdExecImaging
    usdImagingGL
    usdProcImaging
    usdSkelImaging
    usdVolImaging
    usdIrImaging
    usdAppUtils
    usdviewq
    bin
    plugin
)

if (${PXR_BUILD_HD_EMSCRIPTEN})
    list(APPEND DIRS hdEmscripten)
endif()

foreach(d ${DIRS})
    add_subdirectory(${d})
endforeach()
