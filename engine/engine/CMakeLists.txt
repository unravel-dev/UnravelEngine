file(GLOB_RECURSE libsrc *.h *.cpp *.hpp *.c *.cc)

# Validation harnesses live in tests/ subdirectories and are built as their own
# EXCLUDE_FROM_ALL executables, so they must not end up in the engine library (they define
# their own main). Same arrangement as engine/core/seq.
set(GI_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rendering/gi/tests")
file(GLOB_RECURSE gi_tests_src "${GI_TESTS_DIR}/*.cpp" "${GI_TESTS_DIR}/*.h")
if(gi_tests_src)
    list(REMOVE_ITEM libsrc ${gi_tests_src})
endif()

set(ANIMATION_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/animation/tests")
file(GLOB_RECURSE animation_tests_src "${ANIMATION_TESTS_DIR}/*.cpp" "${ANIMATION_TESTS_DIR}/*.h")
if(animation_tests_src)
    list(REMOVE_ITEM libsrc ${animation_tests_src})
endif()

set(IK_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rendering/ecs/systems/tests")
file(GLOB_RECURSE ik_tests_src "${IK_TESTS_DIR}/*.cpp" "${IK_TESTS_DIR}/*.h")
if(ik_tests_src)
    list(REMOVE_ITEM libsrc ${ik_tests_src})
endif()

set(PHYSICS_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/physics/tests")
file(GLOB_RECURSE physics_tests_src "${PHYSICS_TESTS_DIR}/*.cpp" "${PHYSICS_TESTS_DIR}/*.h")
if(physics_tests_src)
    list(REMOVE_ITEM libsrc ${physics_tests_src})
endif()

set(ECS_SERIALIZATION_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ecs/tests")
file(GLOB_RECURSE ecs_serialization_tests_src "${ECS_SERIALIZATION_TESTS_DIR}/*.cpp" "${ECS_SERIALIZATION_TESTS_DIR}/*.h")
if(ecs_serialization_tests_src)
    list(REMOVE_ITEM libsrc ${ecs_serialization_tests_src})
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

    # The GI v2 constants parity test parses the shader mirror from the source tree, so the
    # suite fails when gi_constants.h and gi_constants.sh drift (tasks/gi_rewrite_plan.md, R9),
    # and the shader-compile test runs shaderc from the build's binary directory over every GI
    # shader - a shader that fails to compile in the editor silently keeps its stale binary,
    # which presents as wrong rendering rather than as an error.
    target_compile_definitions(${target_name} PRIVATE
        GI_TESTS_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../engine_data/data/shaders"
        GI_TESTS_SHADERC="$<TARGET_FILE_DIR:${target_name}>/shaderc.exe"
    )
endif()

###############################################################################################
# Animation validation harness. Not built by default; run it explicitly:
#   cmake --build <build-dir> --target animation_tests
#   <build-dir>/bin/animation_tests
if(animation_tests_src)
    set(target_name animation_tests)
    add_executable(${target_name} EXCLUDE_FROM_ALL ${animation_tests_src})

    set_target_properties(${target_name} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )

    target_link_libraries(${target_name} PRIVATE engine)
endif()

###############################################################################################
# IK solver validation harness. Not built by default; run it explicitly:
#   cmake --build <build-dir> --target ik_tests
#   <build-dir>/bin/ik_tests
if(ik_tests_src)
    set(target_name ik_tests)
    add_executable(${target_name} EXCLUDE_FROM_ALL ${ik_tests_src})

    set_target_properties(${target_name} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )

    target_link_libraries(${target_name} PRIVATE engine)
endif()

###############################################################################################
# Contact tracking / destroy funnel validation harness. Not built by default; run it explicitly:
#   cmake --build <build-dir> --target physics_tests
#   <build-dir>/bin/physics_tests
if(physics_tests_src)
    set(target_name physics_tests)
    add_executable(${target_name} EXCLUDE_FROM_ALL ${physics_tests_src})

    set_target_properties(${target_name} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )

    target_link_libraries(${target_name} PRIVATE engine)
endif()

###############################################################################################
# The entity serialization / prefab / clone suite moved to the `tests` executable, which boots
# the engine so asset handles resolve:
#   cmake --build <build-dir> --target tests
#   <build-dir>/bin/unravel-tests                        # every suite
#   <build-dir>/bin/unravel-tests --suite serialization  # one of them
#   <build-dir>/bin/unravel-tests --bench                # checks + timings
