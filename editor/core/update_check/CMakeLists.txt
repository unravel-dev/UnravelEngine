file(GLOB_RECURSE libsrc *.h *.cpp *.hpp)

# The editor's update check: reading a release's latest.json, the schedule and what it keeps between runs. The
# request itself is https_client's. Linked by the editor and by the tests, never by the game.
set(target_name update_check)

add_library(${target_name} STATIC ${libsrc})

target_include_directories(${target_name}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(${target_name} PUBLIC engine https_client)
# The platform part of this build's package name (UNRAVEL_PACKAGE_PLATFORM in the top CMakeLists.txt).
target_compile_definitions(${target_name} PRIVATE UNRAVEL_PACKAGE_PLATFORM="${UNRAVEL_PACKAGE_PLATFORM}")

set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)
