# Checks that an editor package holds what the editor and its deploys need, with the licence and the notices, and
# none of what a build leaves beside the editor. The release workflow runs it on every package it makes
# (.github/workflows/release.yml): a package that fails here cannot deploy, so it is not published.
#
#   cmake -DPACKAGE_ARCHIVE=<package>.zip -P cmake/check_package.cmake
#   cmake -DPACKAGE_DIR=<unpacked package> -P cmake/check_package.cmake
#   cmake -DRUNTIME_DIR=build/<configuration>/bin -P cmake/check_package.cmake
#
# PACKAGE_ARCHIVE  a package; a pattern that one file matches names it too. It is unpacked into WORK_DIR.
# PACKAGE_DIR      an unpacked package: the folder that holds the editor.
# RUNTIME_DIR      a build's runtime folder instead of a package. What only packaging adds or leaves out is not
#                  checked there: the licence, the notices, the test runner and the files of dependencies.
# WORK_DIR         a folder of the check's own for the unpacked archive, removed before and after
#                  (default: package_check beside the archive).
# PLATFORM         windows or linux, the system the package runs on (default: this one).
# ANDROID_PLAYER   OFF for a build without the Android player template (default: ON).
# WEB_PLAYER       OFF for a build without the browser player template (default: ON).
# ENGINE_VERSION   the version every stamp must name, as git describe prints it (v1.2.1-153-g1a2b3c4d);
#                  empty: the player templates must name the version of the desktop player's stamp.
#
# Every failed check is listed before the script fails.
cmake_minimum_required(VERSION 3.16)

set(ANDROID_TEMPLATE "players/android-arm64")
set(WEB_TEMPLATE "players/web")
# The VC++ runtime libraries every binary of the package loads; a deployed game carries the copies beside the player.
set(VC_RUNTIME_LIBRARIES msvcp140.dll vcruntime140.dll vcruntime140_1.dll)
# What dependencies build beside the editor, which it never runs: yaml-cpp's tools and bgfx's viewers.
set(DEPENDENCY_TOOLS parse read sandbox geometryv texturev)
# Where the install rules of dependencies put their libraries, headers and documentation.
set(DEPENDENCY_INSTALL_FOLDERS lib include share)
# Static libraries named in a failed check before "and N more".
set(LISTED_LIBRARIES_MAX 5)

# Records a failed check. The script fails at its end, with all of them.
function(add_problem text)
    set_property(GLOBAL APPEND PROPERTY package_problems "${text}")
endfunction()

# Fails the check when the package lacks the file `file`, which is `what`.
function(require_file file what)
    if(NOT EXISTS "${root}/${file}" OR IS_DIRECTORY "${root}/${file}")
        add_problem("${what} is missing: ${file}")
    endif()
endfunction()

# Fails the check when the package lacks the folder `folder`, which is `what`, or it holds nothing.
function(require_folder folder what)
    file(GLOB entries "${root}/${folder}/*")
    if(NOT entries)
        add_problem("${what} is missing or empty: ${folder}/")
    endif()
endfunction()

# Fails the check when the package holds the file or folder `entry`, which is `what`.
function(refuse_entry entry what)
    if(EXISTS "${root}/${entry}")
        add_problem("${what} is in the package: ${entry}")
    endif()
endfunction()

# The value of `key` in the key=value lines of the stamp `file`; "" when the stamp or the key is missing.
function(read_stamp_value out_var file key)
    set(${out_var} "" PARENT_SCOPE)
    if(NOT EXISTS "${root}/${file}")
        return()
    endif()
    file(STRINGS "${root}/${file}" lines REGEX "^${key}=")
    if(NOT lines)
        return()
    endif()
    list(GET lines 0 line)
    string(REGEX REPLACE "^${key}=" "" value "${line}")
    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

# Fails the check when the stamp `file`, which is `what`, is missing or names another engine version than
# ENGINE_VERSION: the editor refuses a player whose stamp names another version than its own.
function(require_stamp file what)
    read_stamp_value(version "${file}" engine_version)
    if(NOT version)
        add_problem("${what} is missing or names no engine version: ${file}")
    elseif(ENGINE_VERSION AND NOT version STREQUAL ENGINE_VERSION)
        add_problem("${what} names engine version ${version} instead of ${ENGINE_VERSION}: ${file}")
    endif()
endfunction()

# Unpacks the one archive PACKAGE_ARCHIVE names into WORK_DIR and sets `root` to the folder it unpacks into.
function(unpack_package_archive)
    get_filename_component(pattern "${PACKAGE_ARCHIVE}" ABSOLUTE)
    file(GLOB archives "${pattern}")
    list(LENGTH archives archive_count)
    if(NOT archive_count EQUAL 1)
        message(FATAL_ERROR "Package check: ${PACKAGE_ARCHIVE} names ${archive_count} files, and one package is checked at a time.")
    endif()
    file(REMOVE_RECURSE "${WORK_DIR}")
    file(MAKE_DIRECTORY "${WORK_DIR}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${archives}"
                    WORKING_DIRECTORY "${WORK_DIR}"
                    RESULT_VARIABLE unpack_result)
    if(NOT unpack_result EQUAL 0)
        message(FATAL_ERROR "Package check: ${archives} does not unpack (${unpack_result}).")
    endif()
    file(GLOB unpacked "${WORK_DIR}/*")
    list(LENGTH unpacked unpacked_count)
    if(NOT unpacked_count EQUAL 1 OR NOT IS_DIRECTORY "${unpacked}")
        message(FATAL_ERROR "Package check: ${archives} does not unpack into one folder.")
    endif()
    set(root "${unpacked}" PARENT_SCOPE)
    set(checked_name "${archives}" PARENT_SCOPE)
endfunction()

# What the editor needs to start, to compile assets and scripts, and to deploy to the desktop.
function(check_editor)
    require_file("unravel-editor${executable_suffix}" "The editor")
    require_file("unravel-player${executable_suffix}" "The desktop player")
    require_stamp("unravel-player${executable_suffix}.stamp" "The desktop player's stamp")
    require_file("clrpp/Clrpp.Managed.dll" "The script bridge")
    require_file("clrpp/Clrpp.Managed.runtimeconfig.json" "The script bridge's runtime configuration")
    require_file("shaderc${executable_suffix}" "The shader compiler the editor runs")
    require_file("texturec${executable_suffix}" "The texture compiler the editor runs")
    require_folder("data/engine/data" "The engine's data")
    require_folder("data/editor/data" "The editor's data")
    if(NOT PLATFORM STREQUAL "windows")
        return()
    endif()
    require_file("dxcompiler.dll" "The DirectX shader compiler library the shader compiler loads")
    require_file("data/editor/tools/dependencies/Dependencies.exe" "The dependency scanner of desktop deploys")
    foreach(library IN LISTS VC_RUNTIME_LIBRARIES)
        require_file("${library}" "A VC++ runtime library")
    endforeach()
endfunction()

# The Android player template, by what the editor asks of one before it deploys (editor/core/deploy/android.cpp).
function(check_android_template)
    if(NOT IS_DIRECTORY "${root}/${ANDROID_TEMPLATE}")
        add_problem("The Android player template is missing: ${ANDROID_TEMPLATE}/")
        return()
    endif()
    require_stamp("${ANDROID_TEMPLATE}/template.stamp" "The Android player template's stamp")
    read_stamp_value(abi "${ANDROID_TEMPLATE}/template.stamp" abi)
    read_stamp_value(framework "${ANDROID_TEMPLATE}/template.stamp" dotnet_framework)
    if(abi AND framework)
        require_file("${ANDROID_TEMPLATE}/lib/${abi}/libmain.so" "The Android player")
        require_file("${ANDROID_TEMPLATE}/symbols/${abi}/libmain.so.sym" "The Android player's symbol table")
        require_file("${ANDROID_TEMPLATE}/dotnet/lib/${framework}/System.Private.CoreLib.dll" "The Android player's .NET framework")
    else()
        add_problem("The Android player template's stamp names no abi or no .NET framework: ${ANDROID_TEMPLATE}/template.stamp")
    endif()
    require_file("${ANDROID_TEMPLATE}/dotnet/native/libcoreclr.so" "The Android player's .NET runtime")
    require_file("${ANDROID_TEMPLATE}/project/gradle/wrapper/gradle-wrapper.jar" "The Android player's Gradle project")
    require_folder("${ANDROID_TEMPLATE}/sdl/java" "The folder of SDL's Java sources for the Android player")
endfunction()

# The browser player template, by what the editor asks of one before it deploys (editor/core/deploy/web.cpp).
function(check_web_template)
    if(NOT IS_DIRECTORY "${root}/${WEB_TEMPLATE}")
        add_problem("The browser player template is missing: ${WEB_TEMPLATE}/")
        return()
    endif()
    require_stamp("${WEB_TEMPLATE}/template.stamp" "The browser player template's stamp")
    require_file("${WEB_TEMPLATE}/wwwroot/index.html" "The browser player's page")
    require_file("${WEB_TEMPLATE}/wwwroot/_framework/dotnet.js" "The browser player's .NET runtime")
endfunction()

# The licence of the engine, and the third-party notices with the engine version where the text names it.
function(check_licences)
    require_file("LICENSE.txt" "The licence")
    require_file("THIRD_PARTY_NOTICES.txt" "The file of the third-party notices")
    if(NOT EXISTS "${root}/THIRD_PARTY_NOTICES.txt")
        return()
    endif()
    file(STRINGS "${root}/THIRD_PARTY_NOTICES.txt" placeholders REGEX "@ENGINE_VERSION@")
    if(placeholders)
        add_problem("The third-party notices name no engine version: THIRD_PARTY_NOTICES.txt")
    endif()
endfunction()

# What a build puts beside the editor that no package ships: the test runner with its data, the tools and the
# install files of dependencies, and static libraries.
function(check_leftovers)
    refuse_entry("unravel-tests${executable_suffix}" "The test runner")
    refuse_entry("data/tests" "The test runner's data")
    foreach(tool IN LISTS DEPENDENCY_TOOLS)
        refuse_entry("${tool}${executable_suffix}" "A tool of a dependency")
    endforeach()
    foreach(folder IN LISTS DEPENDENCY_INSTALL_FOLDERS)
        refuse_entry("${folder}" "An install folder of dependencies")
    endforeach()
    file(GLOB_RECURSE libraries RELATIVE "${root}" "${root}/*.a" "${root}/*.lib")
    list(LENGTH libraries library_count)
    if(library_count EQUAL 0)
        return()
    endif()
    set(listed "${libraries}")
    set(more "")
    if(library_count GREATER LISTED_LIBRARIES_MAX)
        list(SUBLIST libraries 0 ${LISTED_LIBRARIES_MAX} listed)
        math(EXPR more_count "${library_count} - ${LISTED_LIBRARIES_MAX}")
        set(more " and ${more_count} more")
    endif()
    string(REPLACE ";" ", " listed "${listed}")
    add_problem("Static libraries are in the package: ${listed}${more}")
endfunction()

foreach(path_argument PACKAGE_ARCHIVE PACKAGE_DIR RUNTIME_DIR WORK_DIR)
    if(DEFINED ${path_argument})
        file(TO_CMAKE_PATH "${${path_argument}}" ${path_argument})
    endif()
endforeach()
if(NOT DEFINED ANDROID_PLAYER)
    set(ANDROID_PLAYER ON)
endif()
if(NOT DEFINED WEB_PLAYER)
    set(WEB_PLAYER ON)
endif()
if(NOT PLATFORM)
    string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" PLATFORM)
endif()
if(PLATFORM STREQUAL "windows")
    set(executable_suffix ".exe")
elseif(PLATFORM STREQUAL "linux")
    set(executable_suffix "")
else()
    message(FATAL_ERROR "Package check: PLATFORM is windows or linux, not \"${PLATFORM}\".")
endif()

set(is_package TRUE)
if(PACKAGE_ARCHIVE)
    if(NOT WORK_DIR)
        get_filename_component(archive_folder "${PACKAGE_ARCHIVE}" ABSOLUTE)
        get_filename_component(archive_folder "${archive_folder}" DIRECTORY)
        set(WORK_DIR "${archive_folder}/package_check")
    endif()
    unpack_package_archive()
elseif(PACKAGE_DIR)
    get_filename_component(root "${PACKAGE_DIR}" ABSOLUTE)
elseif(RUNTIME_DIR)
    get_filename_component(root "${RUNTIME_DIR}" ABSOLUTE)
    set(is_package FALSE)
else()
    message(FATAL_ERROR "Package check: name what to check with -DPACKAGE_ARCHIVE=, -DPACKAGE_DIR= or -DRUNTIME_DIR=.")
endif()
if(NOT IS_DIRECTORY "${root}")
    message(FATAL_ERROR "Package check: ${root} is not a folder.")
endif()
if(NOT PACKAGE_ARCHIVE)
    set(checked_name "${root}")
endif()

# The players are checked against the version the desktop player's stamp names, which the editor's own
# configure writes, when none is given.
if(NOT ENGINE_VERSION)
    read_stamp_value(ENGINE_VERSION "unravel-player${executable_suffix}.stamp" engine_version)
endif()

check_editor()
if(ANDROID_PLAYER)
    check_android_template()
endif()
if(WEB_PLAYER)
    check_web_template()
endif()
if(is_package)
    check_licences()
    check_leftovers()
endif()

if(PACKAGE_ARCHIVE)
    file(REMOVE_RECURSE "${WORK_DIR}")
endif()
get_property(problems GLOBAL PROPERTY package_problems)
if(problems)
    list(LENGTH problems problem_count)
    string(REPLACE ";" "\n  " problem_lines "${problems}")
    message(FATAL_ERROR "Package check: ${checked_name} fails ${problem_count} check(s):\n  ${problem_lines}")
endif()
message(STATUS "Package check: ${checked_name} holds what the editor and its deploys need (${PLATFORM}, engine version ${ENGINE_VERSION}).")
