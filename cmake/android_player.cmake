# The Android player template: what the editor's deploy builds Android packages from.
#
# An arm64 sub-build compiles the player (libmain.so) in the editor's configuration. The android_player
# target then assembles <bin>/players/android-arm64 beside the editor (android_player_template.cmake):
# the stripped library, the Gradle project, SDL's Java sources, the .NET runtime and a stamp. Editor
# packages carry it, so a downloaded editor deploys to Android without the NDK.
#
# UNRAVEL_ANDROID_PLAYER: AUTO builds the template when the tools are found and says why not
# otherwise, ON fails the configure when a tool is missing, OFF skips it.

set(UNRAVEL_ANDROID_PLAYER "AUTO" CACHE STRING
    "Build the Android player template beside the editor: AUTO (when the Android NDK is found), ON or OFF.")
set_property(CACHE UNRAVEL_ANDROID_PLAYER PROPERTY STRINGS AUTO ON OFF)
set(UNRAVEL_ANDROID_PLAYER_JOBS "3" CACHE STRING
    "Parallel jobs of the Android player sub-build (unity builds need about 3 GB per job).")

set(UNRAVEL_ANDROID_NDK_VERSION "30.0.16248370")
set(UNRAVEL_ANDROID_PLATFORM "android-29")
set(UNRAVEL_ANDROID_ABI "arm64-v8a")
set(UNRAVEL_ANDROID_DOTNET_RUNTIME_VERSION "10.0.13")

# The Android SDK: ANDROID_HOME, ANDROID_SDK_ROOT, then the folder Android Studio installs it to.
function(unravel_find_android_sdk out_var)
    set(candidates "$ENV{ANDROID_HOME}" "$ENV{ANDROID_SDK_ROOT}")
    if(WIN32)
        list(APPEND candidates "$ENV{LOCALAPPDATA}/Android/Sdk")
    elseif(APPLE)
        list(APPEND candidates "$ENV{HOME}/Library/Android/sdk")
    else()
        list(APPEND candidates "$ENV{HOME}/Android/Sdk")
    endif()
    foreach(candidate IN LISTS candidates)
        if(NOT candidate)
            continue()
        endif()
        file(TO_CMAKE_PATH "${candidate}" candidate)
        if(IS_DIRECTORY "${candidate}")
            set(${out_var} "${candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} "" PARENT_SCOPE)
endfunction()

# The NDK of UNRAVEL_ANDROID_NDK_VERSION: ANDROID_NDK_HOME or ANDROID_NDK_ROOT when they hold that
# version, else the SDK's ndk/<version> folder.
function(unravel_find_android_ndk sdk out_var)
    set(candidates "$ENV{ANDROID_NDK_HOME}" "$ENV{ANDROID_NDK_ROOT}")
    if(sdk)
        list(APPEND candidates "${sdk}/ndk/${UNRAVEL_ANDROID_NDK_VERSION}")
    endif()
    foreach(candidate IN LISTS candidates)
        if(NOT candidate)
            continue()
        endif()
        file(TO_CMAKE_PATH "${candidate}" candidate)
        if(NOT EXISTS "${candidate}/source.properties" OR NOT EXISTS "${candidate}/build/cmake/android.toolchain.cmake")
            continue()
        endif()
        file(STRINGS "${candidate}/source.properties" revision REGEX "^Pkg\\.Revision")
        string(FIND "${revision}" "${UNRAVEL_ANDROID_NDK_VERSION}" position)
        if(position GREATER -1)
            set(${out_var} "${candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} "" PARENT_SCOPE)
endfunction()

# Sets android_player_reason to why the template cannot be built here, or to "" when it can, and the
# tools it found: android_ndk, android_ninja, android_strip.
function(unravel_check_android_player_tools)
    set(android_player_reason "" PARENT_SCOPE)
    if(CMAKE_CONFIGURATION_TYPES)
        set(android_player_reason "multi-configuration generators are not supported, use Ninja" PARENT_SCOPE)
        return()
    endif()
    if(NOT DOTNET_EXECUTABLE)
        set(android_player_reason "the .NET SDK (dotnet) was not found" PARENT_SCOPE)
        return()
    endif()
    unravel_find_android_sdk(sdk)
    unravel_find_android_ndk("${sdk}" ndk)
    if(NOT ndk)
        set(android_player_reason
            "no NDK ${UNRAVEL_ANDROID_NDK_VERSION} under ANDROID_HOME or the default SDK folder (install it from Android Studio's SDK Manager)"
            PARENT_SCOPE)
        return()
    endif()
    file(GLOB strip_candidates
         "${ndk}/toolchains/llvm/prebuilt/*/bin/llvm-strip"
         "${ndk}/toolchains/llvm/prebuilt/*/bin/llvm-strip.exe")
    if(NOT strip_candidates)
        set(android_player_reason "the NDK at ${ndk} has no llvm-strip" PARENT_SCOPE)
        return()
    endif()
    list(GET strip_candidates 0 strip)
    # The sub-build uses Ninja: the host's when this tree is a Ninja tree, else one on the PATH or in
    # the SDK's CMake packages.
    if(CMAKE_GENERATOR STREQUAL "Ninja")
        set(ninja "${CMAKE_MAKE_PROGRAM}")
    else()
        file(GLOB sdk_cmake_bins "${sdk}/cmake/*/bin")
        find_program(UNRAVEL_ANDROID_NINJA ninja HINTS ${sdk_cmake_bins})
        set(ninja "${UNRAVEL_ANDROID_NINJA}")
    endif()
    if(NOT ninja)
        set(android_player_reason "Ninja was not found" PARENT_SCOPE)
        return()
    endif()
    set(android_ndk "${ndk}" PARENT_SCOPE)
    set(android_ninja "${ninja}" PARENT_SCOPE)
    set(android_strip "${strip}" PARENT_SCOPE)
endfunction()

if(NOT UNRAVEL_ANDROID_PLAYER STREQUAL "OFF")
    unravel_check_android_player_tools()
    if(android_player_reason)
        if(UNRAVEL_ANDROID_PLAYER STREQUAL "ON")
            message(FATAL_ERROR "Android player: ${android_player_reason}.")
        endif()
        message(STATUS "Android player: off, ${android_player_reason}.")
    else()
        message(STATUS "Android player: on (NDK ${UNRAVEL_ANDROID_NDK_VERSION} at ${android_ndk}, ${CMAKE_BUILD_TYPE}).")
        include(ExternalProject)
        set(android_build_dir "${PROJECT_BINARY_DIR}/android-arm64")
        ExternalProject_Add(android_player_build
            SOURCE_DIR "${PROJECT_SOURCE_DIR}"
            BINARY_DIR "${android_build_dir}"
            CMAKE_GENERATOR Ninja
            CMAKE_ARGS
                "-DCMAKE_TOOLCHAIN_FILE=${android_ndk}/build/cmake/android.toolchain.cmake"
                "-DCMAKE_MAKE_PROGRAM=${android_ninja}"
                "-DANDROID_ABI=${UNRAVEL_ANDROID_ABI}"
                "-DANDROID_PLATFORM=${UNRAVEL_ANDROID_PLATFORM}"
                "-DANDROID_STL=c++_static"
                "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
                "-DUNRAVEL_BUILD_EDITOR=OFF"
                "-DUNRAVEL_BUILD_TESTS=OFF"
                # Dependencies that FetchContent downloads are built per toolchain: the host tree's
                # copies are for the desktop.
                "-DFETCHCONTENT_BASE_DIR=${android_build_dir}/_deps"
            BUILD_COMMAND "${CMAKE_COMMAND}" --build "${android_build_dir}" --target game -j ${UNRAVEL_ANDROID_PLAYER_JOBS}
            INSTALL_COMMAND ""
            BUILD_ALWAYS TRUE
            USES_TERMINAL_CONFIGURE TRUE
            USES_TERMINAL_BUILD TRUE
        )
        # After the editor, so the two compiles do not compete for memory.
        add_dependencies(android_player_build editor)
        # The sub-build globs its sources when it configures, and an external project configures once:
        # it configures again after every configure of this tree, so the files added since reach it.
        set(android_configure_stamp "${PROJECT_BINARY_DIR}/android_player_host_configure.stamp")
        file(TOUCH "${android_configure_stamp}")
        ExternalProject_Add_StepDependencies(android_player_build configure "${android_configure_stamp}")

        add_custom_target(android_player ALL
            COMMAND "${CMAKE_COMMAND}"
                "-DLIBRARY=${android_build_dir}/bin/libmain.so"
                "-DSTRIP=${android_strip}"
                "-DGRADLE_PROJECT=${PROJECT_SOURCE_DIR}/game/android"
                "-DSDL_JAVA=${PROJECT_SOURCE_DIR}/deps/ospp/ospp/3rdparty/SDL/SDL/android-project/app/src/main/java"
                "-DDOTNET=${DOTNET_EXECUTABLE}"
                "-DDOTNET_RUNTIME_VERSION=${UNRAVEL_ANDROID_DOTNET_RUNTIME_VERSION}"
                "-DENGINE_VERSION=${GIT_VERSION}"
                "-DCONFIGURATION=${CMAKE_BUILD_TYPE}"
                "-DABI=${UNRAVEL_ANDROID_ABI}"
                "-DWORK_DIR=${android_build_dir}/template"
                "-DOUTPUT=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/players/android-arm64"
                -P "${CMAKE_CURRENT_LIST_DIR}/android_player_template.cmake"
            COMMENT "Assembling the Android Player Template..."
            VERBATIM
        )
        add_dependencies(android_player android_player_build)
    endif()
endif()
