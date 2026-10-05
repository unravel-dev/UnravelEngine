file(GLOB_RECURSE libsrc *.h *.cpp *.hpp *.c *.cc)

set(target_name raytracing)

add_library(${target_name} ${libsrc})

target_include_directories(${target_name}
    PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(${target_name} PUBLIC math hpp PRIVATE logging)

# Embree is optional: when deps/3rdparty/embree provides it, its backend (detail/embree_scene.cpp, the only file that
# includes Embree's headers) is compiled in; otherwise the native backend is the only one.
if(TARGET embree)
    target_link_libraries(${target_name} PRIVATE embree)
    # Embree's headers include a configuration header its own configure step writes.
    add_dependencies(${target_name} embree_external)
    target_compile_definitions(${target_name} PRIVATE RAYTRACING_WITH_EMBREE)
endif()

# The native backend's watertight triangle test needs a * b - c * d to round alike for both triangles of an edge; a
# fused multiply-add rounds the two differently.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set_source_files_properties(detail/native_scene.cpp PROPERTIES COMPILE_OPTIONS "/clang:-ffp-contract=off")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(detail/native_scene.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
