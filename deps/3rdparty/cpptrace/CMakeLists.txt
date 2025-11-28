
set(CMAKE_SKIP_INSTALL_RULES OFF)

add_subdirectory(cpptrace)


if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cpptrace-lib PRIVATE -Wno-macro-redefined)
endif()