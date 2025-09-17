
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/freetype")
add_subdirectory(freetype/freetype)

set(RMLUI_MATRIX_ROW_MAJOR OFF CACHE BOOL "Use row-major matrices. Column-major matrices are used by default." FORCE)

add_subdirectory( RmlUi )


find_package(Freetype REQUIRED)
if(WIN32 AND CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    message(STATUS "Copying freetype binaries ${FREETYPE_SHARED_LIBRARIES} to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
	add_custom_target(copy_freetype_binaries ALL
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FREETYPE_SHARED_LIBRARIES} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        COMMENT "Copying freetype binaries..."
        VERBATIM
    )
endif()

if(UNRAVEL_UNITY_BUILD)
    set_target_properties(spdlog PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_UNIQUE_ID "ANONYMOUS"
    )
endif()
