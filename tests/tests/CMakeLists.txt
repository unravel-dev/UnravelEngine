file(GLOB_RECURSE libsrc *.h *.cpp *.hpp *.c *.cc)

set(target_name tests)

add_executable(${target_name} ${libsrc})

target_include_directories(${target_name}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_dependencies(${target_name} engine)
target_link_libraries(${target_name} PUBLIC engine)

# The GI constants parity test parses the shader mirror from the source tree, so the suite
# fails when gi_constants.h and gi_constants.sh drift (tasks/gi_rewrite_plan.md, R9), and the
# shader-compile test runs shaderc from the build's binary directory over every GI shader - a
# shader that fails to compile in the editor silently keeps its stale binary, which presents
# as wrong rendering rather than as an error.
target_compile_definitions(${target_name} PRIVATE
    GI_TESTS_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../engine_data/data/shaders"
    GI_TESTS_SHADERC="$<TARGET_FILE_DIR:${target_name}>/shaderc.exe"
)

set_target_properties(${target_name} PROPERTIES OUTPUT_NAME "unravel-tests")

set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

if (CMAKE_C_COMPILER_ID MATCHES GNU|Clang AND NOT WIN32)
  target_link_options(${target_name} PRIVATE -no-pie)
endif()
