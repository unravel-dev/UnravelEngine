set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "Build bgfx examples." FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "Build bgfx tests." FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "Create installation target." FORCE)

# The browser gives the engine no threads, so there bgfx renders inside bgfx::frame.
if(EMSCRIPTEN)
    set(BGFX_CONFIG_MULTITHREADED OFF CACHE BOOL "Build bgfx with multithreaded configuration." FORCE)
else()
    set(BGFX_CONFIG_MULTITHREADED ON CACHE BOOL "Build bgfx with multithreaded configuration." FORCE)
endif()

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
# The Vulkan backend reuses a command list every BGFX_CONFIG_MAX_FRAME_LATENCY frames, and before it renders
# into a swap chain image again it waits on the fence of the command list that last rendered into it. With
# more images than frames in flight, that command list already belongs to the frame just submitted, so the
# CPU waits for the GPU every frame. The Mali driver on Android creates 4 images.
add_definitions(-DBGFX_CONFIG_MAX_FRAME_LATENCY=4)

# Android renders through Vulkan only. Defining one renderer leaves the others out of the build
# (src/config.h), the OpenGL ES backend and its EGL dependency included.
if(ANDROID)
    add_definitions(-DBGFX_CONFIG_RENDERER_VULKAN=1)
endif()

# The browser renders through WebGPU. bgfx's src/config.h leaves it off on Emscripten unless defined, and
# bgfx.cmake's option of the same name only links Dawn's emdawnwebgpu port.
if(EMSCRIPTEN)
    set(BGFX_CONFIG_RENDERER_WEBGPU ON CACHE BOOL "Enable the webgpu renderer" FORCE)
    add_definitions(-DBGFX_CONFIG_RENDERER_WEBGPU=1)
endif()

# Fixes to bgfx itself (patches/, applied in file name order by apply_patches.cmake). bgfx.cmake fetches bgfx
# with FetchContent, and the first FetchContent_Declare of a name wins: this one, with bgfx.cmake's pin and a
# patch step, replaces bgfx.cmake's own. A bgfx from BGFX_DIR or an in-tree checkout is built as it is.
include(FetchContent)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/bgfx/CMakeLists.txt bgfx_cmake_lists)
if(NOT bgfx_cmake_lists MATCHES "FetchContent_Declare\\([ \t\r\n]*bgfx[ \t\r\n]+[^)]*GIT_TAG[ \t\r\n]+([0-9a-fA-F]+)")
    message(FATAL_ERROR "Could not find the bgfx commit that bgfx/CMakeLists.txt pins.")
endif()
set(bgfx_pin "${CMAKE_MATCH_1}")
FetchContent_Declare(
    bgfx
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.git
    GIT_TAG ${bgfx_pin}
    PATCH_COMMAND "${CMAKE_COMMAND}" -DSOURCE_DIR=<SOURCE_DIR> -DPATCH_DIR=${CMAKE_CURRENT_SOURCE_DIR}/patches
                  -P ${CMAKE_CURRENT_SOURCE_DIR}/apply_patches.cmake
)

# A pin move updates the patched checkout, and FetchContent's update cannot carry the patches over: the checkout is
# reset first (reset_moved_checkout.cmake). Disconnected builds never update it, and a checkout of one's own
# (FETCHCONTENT_SOURCE_DIR_BGFX) is not touched.
if(NOT FETCHCONTENT_FULLY_DISCONNECTED AND NOT FETCHCONTENT_UPDATES_DISCONNECTED
   AND NOT FETCHCONTENT_UPDATES_DISCONNECTED_BGFX AND NOT FETCHCONTENT_SOURCE_DIR_BGFX)
    execute_process(COMMAND "${CMAKE_COMMAND}" -DSOURCE_DIR=${FETCHCONTENT_BASE_DIR}/bgfx-src -DPIN=${bgfx_pin}
                            -P ${CMAKE_CURRENT_SOURCE_DIR}/reset_moved_checkout.cmake
                    RESULT_VARIABLE bgfx_reset_result)
    if(NOT bgfx_reset_result EQUAL 0)
        message(FATAL_ERROR "Could not prepare the bgfx checkout for ${bgfx_pin}.")
    endif()
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


