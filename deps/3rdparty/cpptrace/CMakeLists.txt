
set(CMAKE_SKIP_INSTALL_RULES OFF)

# libdwarf is under the LGPL, so cpptrace names frames from the dynamic symbol table, or with dbghelp on Windows,
# MinGW included. File and line come afterwards, from the symbols a build keeps.
set(CPPTRACE_GET_SYMBOLS_WITH_LIBDWARF OFF CACHE BOOL "Read symbols with libdwarf." FORCE)
if(EMSCRIPTEN)
    # A browser offers neither a stack walk nor symbol tables for wasm frames; the crash handlers are not installed
    # there either.
    set(CPPTRACE_UNWIND_WITH_NOTHING ON CACHE BOOL "Walk no stacks." FORCE)
    set(CPPTRACE_GET_SYMBOLS_WITH_NOTHING ON CACHE BOOL "Read no symbols." FORCE)
    set(CPPTRACE_GET_SYMBOLS_WITH_LIBDL OFF CACHE BOOL "Read symbols with dladdr." FORCE)
elseif(WIN32)
    set(CPPTRACE_GET_SYMBOLS_WITH_DBGHELP ON CACHE BOOL "Read symbols with dbghelp." FORCE)
else()
    set(CPPTRACE_GET_SYMBOLS_WITH_LIBDL ON CACHE BOOL "Read symbols with dladdr." FORCE)
endif()

add_subdirectory(cpptrace)


if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cpptrace-lib PRIVATE -Wno-macro-redefined)
endif()