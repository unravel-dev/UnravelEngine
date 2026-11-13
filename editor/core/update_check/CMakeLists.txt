file(GLOB_RECURSE libsrc *.h *.cpp *.hpp)

# The editor's update check: reading a release's latest.json, the HTTPS request, the schedule and what it keeps
# between runs. Linked by the editor and by the tests, never by the game.
set(target_name update_check)

if(WIN32)
    list(FILTER libsrc EXCLUDE REGEX "/https_get_posix\\.cpp$")
else()
    list(FILTER libsrc EXCLUDE REGEX "/https_get_windows\\.cpp$")
endif()

add_library(${target_name} STATIC ${libsrc})

target_include_directories(${target_name}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(${target_name} PUBLIC engine PRIVATE subprocess)
if(WIN32)
    target_link_libraries(${target_name} PRIVATE winhttp)
endif()
# The platform part of this build's package name (UNRAVEL_PACKAGE_PLATFORM in the top CMakeLists.txt).
target_compile_definitions(${target_name} PRIVATE UNRAVEL_PACKAGE_PLATFORM="${UNRAVEL_PACKAGE_PLATFORM}")

set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)
