file(GLOB_RECURSE libsrc *.h *.cpp *.hpp *.c *.cc)

# The browser build installs no handlers (crash_web.cpp) and has no cpptrace, which does not support Emscripten.
if(EMSCRIPTEN)
    list(FILTER libsrc EXCLUDE REGEX "/crash\\.cpp$")
else()
    list(FILTER libsrc EXCLUDE REGEX "/crash_web\\.cpp$")
endif()

set(target_name crash)

add_library(${target_name} ${libsrc})

target_include_directories(${target_name}
    PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(${target_name} PRIVATE base hpp)
if(NOT EMSCRIPTEN)
    target_link_libraries(${target_name} PRIVATE cpptrace::cpptrace)
endif()

if(WIN32)
    target_link_libraries(${target_name} PRIVATE dbghelp)
endif()


set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
