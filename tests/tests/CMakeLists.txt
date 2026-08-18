file(GLOB_RECURSE libsrc *.h *.cpp *.hpp *.c *.cc)

set(target_name tests)

add_executable(${target_name} ${libsrc})

target_include_directories(${target_name}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_dependencies(${target_name} engine)
target_link_libraries(${target_name} PUBLIC engine)

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
