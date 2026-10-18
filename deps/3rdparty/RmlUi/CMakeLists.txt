
# RmlUi's find_package(Freetype) gets the library built from deps/3rdparty/freetype.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../freetype")

set(RMLUI_MATRIX_ROW_MAJOR OFF CACHE BOOL "Use row-major matrices. Column-major matrices are used by default." FORCE)

add_subdirectory( RmlUi )


if(UNRAVEL_UNITY_BUILD)
    set_target_properties(spdlog PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_UNIQUE_ID "ANONYMOUS"
    )
endif()
