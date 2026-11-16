file(GLOB_RECURSE libsrc *.h *.cpp *.hpp)

# The sample projects the start page offers: reading samples.json, fetching the latest release's copy and the
# samples' pictures, the cache and the schedule. Linked by the editor and by the tests, never by the game.
set(target_name sample_catalog)

add_library(${target_name} STATIC ${libsrc})

target_include_directories(${target_name}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(${target_name} PUBLIC engine https_client)

set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)
