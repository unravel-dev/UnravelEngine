file(GLOB_RECURSE libsrc *.h *.cpp *.hpp)

# Content packages: the .unravelpkg file, its manifest, export from a project folder and the import plan that
# installs, updates and removes one. Linked by the editor and by the tests, never by the game.
set(target_name packages)

add_library(${target_name} STATIC ${libsrc})

target_include_directories(${target_name}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(${target_name} PUBLIC engine deploy asset_compiler)

set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)
