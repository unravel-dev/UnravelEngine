file(GLOB_RECURSE libsrc *.h *.cpp *.hpp *.c *.cc)

set(target_name game)

if(WIN32 AND NOT MINGW)
    list(APPEND libsrc "${CMAKE_CURRENT_SOURCE_DIR}/../../rc/UnravelEngine.rc")
endif()

if(ANDROID)
    # SDL's Java activity loads libmain.so and calls its SDL_main.
    add_library(${target_name} SHARED ${libsrc})
else()
    add_executable(${target_name} ${libsrc})
endif()

target_include_directories(${target_name}
    PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_dependencies(${target_name} engine)
target_link_libraries(${target_name} PUBLIC engine)

if(ANDROID)
    set_target_properties(${target_name} PROPERTIES OUTPUT_NAME "main")
else()
    set_target_properties(${target_name} PROPERTIES OUTPUT_NAME "unravel-player")
endif()
unravel_set_default_thread_stack(${target_name})
unravel_add_windows_manifest(${target_name})
add_definitions(-DPLAYER_NAME="unravel-player")

# The deploy refuses a player of another engine version than the editor's, which may not read the data the editor
# deploys. Written when the player links, so building the editor alone leaves the older version here.
if(NOT ANDROID)
    set(player_stamp ${CMAKE_CURRENT_BINARY_DIR}/player.stamp)
    file(GENERATE OUTPUT ${player_stamp} CONTENT "engine_version=${GIT_VERSION}\n")
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${player_stamp} $<TARGET_FILE:${target_name}>.stamp
        VERBATIM)
endif()


set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

if(UNRAVEL_UNITY_BUILD AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(${target_name} PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_UNIQUE_ID "ANONYMOUS"
        )
endif()

# The deploy ships the player of the editor's own configuration, so every configuration opens the game
# without a console window. The log and the crash log are files in the user's data folder.
if(WIN32)
    set_target_properties(${target_name} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

if (CMAKE_C_COMPILER_ID MATCHES GNU|Clang AND NOT WIN32 AND NOT ANDROID AND NOT EMSCRIPTEN)
  target_link_options(${target_name} PRIVATE -no-pie)
endif()

# The browser player ships linked into the .NET runtime's wasm module, whose link sets a 5 MB stack and lets the
# memory grow; the standalone link here matches it.
if(EMSCRIPTEN)
    target_link_options(${target_name} PRIVATE "-sSTACK_SIZE=5MB" "-sALLOW_MEMORY_GROWTH=1")
    # What that link takes of this build, once every target links what it links. A deferred call reads its
    # arguments when it runs, in another scope: EVAL fixes them now.
    include("${CMAKE_SOURCE_DIR}/cmake/web_link_inputs.cmake")
    cmake_language(EVAL CODE "
        cmake_language(DEFER DIRECTORY [[${CMAKE_SOURCE_DIR}]]
                       CALL unravel_write_link_inputs [[${target_name}]] [[${CMAKE_BINARY_DIR}/web_link_inputs.txt]])")
endif()
