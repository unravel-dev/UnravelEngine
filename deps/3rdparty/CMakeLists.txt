# Hide all warnings for 3rdparty code
if (MSVC)
    add_compile_options(/W0)
else()
    add_compile_options(-w)
endif()

add_subdirectory(bgfx)
add_subdirectory(spdlog)
if(UNRAVEL_BUILD_ASSET_TOOLS) # the editor's UI only
    add_subdirectory(imgui)
endif()
add_subdirectory(glm)
if(UNRAVEL_BUILD_ASSET_TOOLS) # the asset compiler's mesh importer only
    add_subdirectory(assimp)
endif()
add_subdirectory(entt)
add_subdirectory(yaml-cpp)
add_subdirectory(bullet3)
add_subdirectory(box3d)
add_subdirectory(ser20)
add_subdirectory(poolSTL)
# On Emscripten poolSTL asks everything that links it to compile and link with -pthread. The browser player has
# no threads (the engine's single-threaded mode), and one -pthread object makes the whole module shared-memory.
if(EMSCRIPTEN)
    set_property(TARGET poolSTL PROPERTY INTERFACE_COMPILE_OPTIONS "")
    set_property(TARGET poolSTL PROPERTY INTERFACE_LINK_OPTIONS "")
endif()
add_subdirectory(embree)
add_subdirectory(libunibreak)
# cpptrace does not support Emscripten; the browser build installs no crash handlers.
if(NOT EMSCRIPTEN)
    add_subdirectory(cpptrace)
endif()
add_subdirectory(freetype)
add_subdirectory(RmlUi)
add_subdirectory(watcher)
# add_subdirectory(openssl)
if(UNRAVEL_BUILD_ASSET_TOOLS) # the editor's MCP server only
    add_subdirectory(cpp-httplib)
endif()
if(UNRAVEL_BUILD_ASSET_TOOLS) # the asset compiler's audio import only
    add_subdirectory(vorbis)
endif()
add_subdirectory(zstd)