
set(CMAKE_SKIP_INSTALL_RULES OFF)

# libdwarf is under the LGPL, so cpptrace names frames from the dynamic symbol table, or with dbghelp on Windows,
# MinGW included. File and line come afterwards, from the symbols a build keeps. The browser build has no cpptrace.
set(CPPTRACE_GET_SYMBOLS_WITH_LIBDWARF OFF CACHE BOOL "Read symbols with libdwarf." FORCE)
if(WIN32)
    set(CPPTRACE_GET_SYMBOLS_WITH_DBGHELP ON CACHE BOOL "Read symbols with dbghelp." FORCE)
else()
    set(CPPTRACE_GET_SYMBOLS_WITH_LIBDL ON CACHE BOOL "Read symbols with dladdr." FORCE)
endif()

add_subdirectory(cpptrace)


if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cpptrace-lib PRIVATE -Wno-macro-redefined)
endif()