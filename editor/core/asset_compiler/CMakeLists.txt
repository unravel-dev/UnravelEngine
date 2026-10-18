file(GLOB_RECURSE libsrc *.h *.cpp *.hpp)

# Turns asset sources into compiled assets: the compiler, the importers, and the tools and libraries they
# run (assimp, shaderc, texturec, the C# compiler). Linked by the editor and by the tests, never by the game.
set(target_name asset_compiler)

add_library(${target_name} STATIC ${libsrc})

target_include_directories(${target_name}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(${target_name}
    PUBLIC
        engine
    PRIVATE
        assimp
        subprocess
)

set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)
