set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "Build bgfx examples." FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "Build bgfx tests." FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "Create installation target." FORCE)

set(BGFX_CONFIG_MULTITHREADED ON CACHE BOOL "Build bgfx with multithreaded configuration." FORCE)

# bx is built per source file: bx 2531549 declares IsDebuggerPresent() returning bool in
# debug.cpp, which conflicts with the <windows.h> declaration other sources pull into the
# single amalgamated translation unit ("conflicting types for 'IsDebuggerPresent'").
set(BX_AMALGAMATED OFF CACHE BOOL "Amalgamated bx build for faster compilation" FORCE)
set(BGFX_AMALGAMATED ON CACHE BOOL "Amalgamated bgfx build for faster compilation" FORCE)

if(BUILD_SHARED_LIBS)
    set(BGFX_LIBRARY_TYPE "SHARED" CACHE STRING "Linking type for library" FORCE)
else()
    set(BGFX_LIBRARY_TYPE "STATIC" CACHE STRING "Linking type for library" FORCE)
endif()

# shaderc, texturec and geometryc are run by the editor's asset compiler and by the tests; a
# player-only build leaves them out.
set(BGFX_BUILD_TOOLS ${UNRAVEL_BUILD_ASSET_TOOLS} CACHE BOOL "Build bgfx tools." FORCE)
set(BGFX_BUILD_TOOLS_BIN2C ON CACHE BOOL "Build bx binary to c converter." FORCE)
set(BGFX_BUILD_TOOLS_SHADER ON CACHE BOOL "Build bgfx shader tools." FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY ON CACHE BOOL "Build bgfx geometry tools." FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE ON CACHE BOOL "Build bgfx texture tools." FORCE)


add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
if(MSVC)
    add_compile_options(/wd4244 /wd4267 /wd4291)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wno-deprecated-builtins)
    add_compile_options(-Wno-deprecated-declarations)
    add_compile_options(-Wno-deprecated-this-capture)
    add_compile_options(-Wno-deprecated-enum-enum-conversion)
    add_compile_options(-Wno-bitfield-constant-conversion)
    add_compile_options(-Wno-invalid-noreturn)
    add_compile_options(-Wno-tautological-constant-compare)
    add_compile_options(-Wno-comment)
endif()

if(MINGW)
    add_compile_definitions(ASTCENC_SSE=0)

    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--export-all-symbols" )
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--export-all-symbols" )
endif()

add_definitions(-DBGFX_CONFIG_PROFILER=1)
add_definitions(-DBGFX_CONFIG_MAX_SHADERS=4096)
add_definitions(-DBGFX_CONFIG_MAX_UNIFORMS=4096)
add_definitions(-DBGFX_CONFIG_MAX_FRAME_BUFFERS=16384)
add_definitions(-DBGFX_CONFIG_MAX_TEXTURES=16384)
add_definitions(-DBGFX_CONFIG_MAX_VERTEX_LAYOUTS=4096)
add_definitions(-DBGFX_CONFIG_SORT_KEY_NUM_BITS_PROGRAM=10)
add_definitions(-DBGFX_CONFIG_MAX_VIEWS=4096)
add_definitions(-DBGFX_CONFIG_MAX_BONES=128)
add_definitions(-DBGFX_CONFIG_MAX_TRANSIENT_VERTEX_BUFFER_SIZE=6<<24)
add_definitions(-DBGFX_CONFIG_MAX_TRANSIENT_INDEX_BUFFER_SIZE=2<<24)
add_definitions(-DBGFX_CONFIG_DEBUG_UNIFORM=0)

# Android renders through Vulkan only. Defining one renderer leaves the others out of the build
# (src/config.h), the OpenGL ES backend and its EGL dependency included.
if(ANDROID)
    add_definitions(-DBGFX_CONFIG_RENDERER_VULKAN=1)
endif()

add_subdirectory(bgfx)

# bgfx.cmake builds bgfx from the folder its own BGFX_DIR names: the commit it pins, fetched into the build
# tree (FetchContent), unless BGFX_DIR is set to a checkout. That variable stays in bgfx.cmake's scope; the
# engine reads bgfx's 3rdparty headers (stb, meshoptimizer) and its shader headers from UNRAVEL_BGFX_DIR.
get_directory_property(unravel_bgfx_dir DIRECTORY bgfx DEFINITION BGFX_DIR)
set(UNRAVEL_BGFX_DIR "${unravel_bgfx_dir}" CACHE INTERNAL "The bgfx sources bgfx.cmake builds.")

set_target_properties(bgfx PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    #CXX_FLAGS "-Wl,--export-all-symbols"
)

set_target_properties(bx PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

set_target_properties(bimg PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)


