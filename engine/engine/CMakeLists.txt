file(GLOB_RECURSE libsrc *.h *.cpp *.hpp *.c *.cc)

# Validation harnesses live in tests/ subdirectories and are built as their own
# EXCLUDE_FROM_ALL executables, so they must not end up in the engine library (they define
# their own main). Same arrangement as engine/core/seq.
set(GI_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rendering/gi/tests")
file(GLOB_RECURSE gi_tests_src "${GI_TESTS_DIR}/*.cpp" "${GI_TESTS_DIR}/*.h")
if(gi_tests_src)
    list(REMOVE_ITEM libsrc ${gi_tests_src})
endif()

set(target_name engine)

add_library(${target_name} ${libsrc} ${shader_files})

include(GenerateExportHeader)
generate_export_header(${target_name})

target_include_directories(${target_name}
    PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_include_directories(${target_name}
    PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}/..
)

target_link_libraries (${target_name}
    PUBLIC
    EnTT::EnTT
    Bullet3::Bullet3
    poolSTL::poolSTL
    RmlUi::RmlUi
    service
    context
    logging
    cmd_line
    concurrency
    memory
    ospp
    audiopp
    graphics
    simulation
    serialization
    reflection
    filesystem
    math
    uuidpp
    threadpp
    assimp
    string_utils
    dotnetpp
    seq
    libunibreak
    engine_version
    crash
)

target_precompile_headers(${target_name}
    PUBLIC
    pch.h
)

set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    INTERPROCEDURAL_OPTIMIZATION FALSE
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(UNRAVEL_UNITY_BUILD)
        set_target_properties(${target_name} PROPERTIES
            UNITY_BUILD ON
            UNITY_BUILD_UNIQUE_ID "ANONYMOUS"
            )
    endif()
endif()




###############################################################################################
# GI bake validation harness. Not built by default; run it explicitly:
#   cmake --build <build-dir> --target gi_tests
#   <build-dir>/bin/gi_tests
if(gi_tests_src)
    set(target_name gi_tests)
    add_executable(${target_name} EXCLUDE_FROM_ALL ${gi_tests_src})

    set_target_properties(${target_name} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )

    target_link_libraries(${target_name} PRIVATE engine)
endif()
