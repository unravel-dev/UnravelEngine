file(GLOB_RECURSE libsrc *.h *.cpp *.hpp *.c *.cc *.sc)

set(target_name graphics)

if(NOT MESHOPTIMIZER_LIBRARIES)
	file(
		GLOB #
		MESHOPTIMIZER_SOURCES #
		${BGFX_DIR}/3rdparty/meshoptimizer/src/*.cpp #
		${BGFX_DIR}/3rdparty/meshoptimizer/src/*.h #
	)
	set(MESHOPTIMIZER_INCLUDE_DIR ${BGFX_DIR}/3rdparty)
endif()


add_library (${target_name} ${libsrc} ${MESHOPTIMIZER_SOURCES})

target_include_directories(${target_name}
    PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/..
        ${BGFX_DIR}/3rdparty
        ${MESHOPTIMIZER_INCLUDE_DIR}
)

target_link_libraries(${target_name} PUBLIC bgfx bimg bimg_decode bimg_encode base hpp context ${MESHOPTIMIZER_LIBRARIES})

set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

if(UNRAVEL_UNITY_BUILD)
    set_target_properties(${target_name} PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_UNIQUE_ID "ANONYMOUS"
    )
endif()

#target_precompile_headers(${target_name}
#    PUBLIC
#    <vector>
#    <memory>
#    <cstdint>
#    <string>
#    <unordered_map>
#    <limits>
#)
