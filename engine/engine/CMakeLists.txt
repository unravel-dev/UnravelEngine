file(GLOB_RECURSE libsrc *.h *.cpp *.hpp *.c *.cc)

set(target_name engine)

add_library(${target_name} ${libsrc} ${shader_files})

# The third-party notices compile into the engine: every player carries them on every platform, packed or not.
set(notices_file ${CMAKE_CURRENT_SOURCE_DIR}/../../THIRD_PARTY_NOTICES.txt)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${notices_file})
file(READ ${notices_file} notices_hex HEX)
string(REGEX REPLACE "(................................)" "\\1\n" notices_lines "${notices_hex}")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," notices_bytes "${notices_lines}")
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/third_party_notices_text.inc CONTENT "${notices_bytes}")
# The notices name cpptrace only in builds that link it (the browser build does not), libdwarf, under the LGPL,
# only in builds whose cpptrace reads symbols with it, and what Emscripten links (Dawn, its runtime, musl) only in the
# browser build.
set(links_cpptrace false)
set(links_libdwarf false)
if(TARGET cpptrace-lib)
    set(links_cpptrace true)
    get_target_property(cpptrace_definitions cpptrace-lib COMPILE_DEFINITIONS)
    if("CPPTRACE_GET_SYMBOLS_WITH_LIBDWARF" IN_LIST cpptrace_definitions)
        set(links_libdwarf true)
    endif()
endif()
set(links_web_runtime false)
if(EMSCRIPTEN)
    set(links_web_runtime true)
endif()
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/third_party_notices_build.inc
     CONTENT "constexpr bool LINKS_LIBDWARF = ${links_libdwarf};\nconstexpr bool LINKS_CPPTRACE = ${links_cpptrace};\nconstexpr bool LINKS_WEB_RUNTIME = ${links_web_runtime};\n")

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
    box3d
    RmlUi::RmlUi
    service
    context
    logging
    cmd_line
    concurrency
    raytracing
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

if(UNRAVEL_UNITY_BUILD AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(${target_name} PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_UNIQUE_ID "ANONYMOUS"
        )
endif()