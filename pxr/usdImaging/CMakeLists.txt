set(DIRS
    usdImaging
    usdImagingGL
    usdProcImaging
    usdSkelImaging
    usdVolImaging
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
