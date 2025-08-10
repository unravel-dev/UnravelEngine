cmake_minimum_required(VERSION 3.16)

project (Unravel)

# ————————————————————————
# Gather detailed host info
# ————————————————————————
# OS name (e.g. "Windows")
cmake_host_system_information(RESULT OS_NAME      QUERY OS_NAME)
# OS sub‐type / edition (e.g. "Professional")
cmake_host_system_information(RESULT OS_RELEASE   QUERY OS_RELEASE)
# OS build / version string
cmake_host_system_information(RESULT OS_VERSION   QUERY OS_VERSION)
# Host processor "platform" (duplicates CMAKE_HOST_SYSTEM_PROCESSOR,
# e.g. "AMD64" on 64-bit Windows, "x86_64" on Linux)
cmake_host_system_information(RESULT OS_PLATFORM  QUERY OS_PLATFORM)
# Whether the CPU is 64-bit (1=yes, 0=no)
cmake_host_system_information(RESULT IS_64BIT     QUERY IS_64BIT)

cmake_host_system_information(RESULT DISTRO_NAME  QUERY  DISTRIB_NAME)

cmake_host_system_information(RESULT DISTRO_VERSION QUERY  DISTRIB_VERSION_ID)

message(STATUS
  "Host system: ${OS_NAME} ${OS_RELEASE} "
  "(build ${OS_VERSION}), processor=${OS_PLATFORM}, 64-bit=${IS_64BIT}"
)


set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/utility")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/git")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sanitizers")


if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
endif()
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
endif()
if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
endif()

message(STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "CMAKE_ARCHIVE_OUTPUT_DIRECTORY=${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
message(STATUS "CMAKE_LIBRARY_OUTPUT_DIRECTORY=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")

include(ECMEnableSanitizers)
include(GetGitRevisionDescription)

# Include the module for checking compiler flags
include(CheckCXXCompilerFlag)

git_describe(GIT_VERSION --tags)

#parse the version information into pieces.
string(REGEX REPLACE "^v([0-9]+)\\..*" "\\1" GIT_VERSION_MAJOR "${GIT_VERSION}")
string(REGEX REPLACE "^v[0-9]+\\.([0-9]+).*" "\\1" GIT_VERSION_MINOR "${GIT_VERSION}")
string(REGEX REPLACE "^v[0-9]+\\.[0-9]+\\.([0-9]+).*" "\\1" GIT_VERSION_PATCH "${GIT_VERSION}")
string(REGEX REPLACE "^v[0-9]+\\.[0-9]+\\.[0-9]+(.*)" "\\1" GIT_VERSION_SHA1 "${GIT_VERSION}")
set(VERSION_SHORT "${GIT_VERSION_MAJOR}.${GIT_VERSION_MINOR}.${GIT_VERSION_PATCH}${GIT_VERSION_SHA1}")

message(STATUS "Version ${GIT_VERSION}")
message(STATUS "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")

option(BUILD_ENGINE_SHARED "Build as a shared library." ON)
option(BUILD_ENGINE_TESTS "Build the tests" OFF)
option(BUILD_ENGINE_WITH_CODE_STYLE_CHECKS "Build with code style checks." OFF)

set(BUILD_ENGINE_SHARED OFF CACHE BOOL "" FORCE)
if(BUILD_ENGINE_SHARED)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
else()
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()

if(BUILD_ENGINE_WITH_CODE_STYLE_CHECKS)
	set(BUILD_WITH_CODE_STYLE_CHECKS ON)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
#set(CMAKE_SUPPORT_WINDOWS_EXPORT_ALL_SYMBOLS ON)
if(MSVC)
    add_compile_options(/Zc:__cplusplus)
endif()

if(MSVC OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND WIN32))
    add_definitions("-D_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS")
    add_definitions("-D_CRT_SECURE_NO_WARNINGS")
endif()

# Add bigobj to allow larger object files
#set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /OPT:REF")

if(MINGW)
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--export-all-symbols" )
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--export-all-symbols" )
   #add_compile_options("-Wl,--export-all-symbols")
endif()

# Function to check if a compiler flag is supported and add it if so
function(add_flag_if_supported flag)
  # Create a unique variable name for the flag
  string(REPLACE "-" "_" flag_var "${flag}")
  string(REPLACE "," "_" flag_var "${flag_var}")
  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" flag_var "${flag_var}")
  set(flag_var "FLAG_SUPPORTED${flag_var}")

  # Check if the compiler supports the flag
  check_cxx_compiler_flag("${flag}" ${flag_var})
  if (${flag_var})
    # If supported, add the flag
    add_compile_options("${flag}")
    message(STATUS "Added compiler flag ${flag}")
  else()
    message(STATUS "Compiler does not support flag ${flag}")
  endif()
endfunction()

# Set compiler flags based on the compiler and platform
if (MSVC)
  # For Microsoft Visual C++, use /bigobj
  add_flag_if_supported("/bigobj")
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # For GCC
  if (WIN32)
    # For MinGW or GCC on Windows, use -Wa,-mbig-obj
    add_flag_if_supported("-Wa,-mbig-obj")
  endif()
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  # For Clang
  if (WIN32)
    # For Clang on Windows, try /bigobj
    add_flag_if_supported("/bigobj")
  endif()
endif()

# Set the sanitizers you want to enable
#set(ECM_ENABLE_SANITIZERS "address")

# Call the function to enable sanitizers for the current compiler
#enable_sanitizers_for_compiler()


set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
include(InstallRequiredSystemLibraries)

if(MINGW)
    get_filename_component( Mingw_Path ${CMAKE_CXX_COMPILER} PATH )
    file(GLOB MINGW_SYSTEM_RUNTIME_LIBS "${Mingw_Path}/*.dll")
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} ${MINGW_SYSTEM_RUNTIME_LIBS})
endif()
message(STATUS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})

set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)
#include(cmake/modules.cmake)

include(cmake/utility/target_ide_support.cmake)

if (MSVC)
    # Disable iterator debugging in Debug mode
    add_compile_definitions($<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=1>)

endif()

set(UNRAVEL_UNITY_BUILD ON)

add_subdirectory(deps)
#add_subdirectory(version)
add_subdirectory(engine)
add_subdirectory(editor)
add_subdirectory(game)


file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/engine_data/data/shaders/* ${PROJECT_SOURCE_DIR}/engine_data/data/scripts/*)
add_custom_target(engine_data
                  ALL
                  COMMAND ${CMAKE_COMMAND} -E copy ${BGFX_DIR}/src/bgfx_shader.sh ${PROJECT_SOURCE_DIR}/engine_data/data/shaders
                  COMMAND ${CMAKE_COMMAND} -E copy ${BGFX_DIR}/src/bgfx_compute.sh ${PROJECT_SOURCE_DIR}/engine_data/data/shaders
                  COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/engine_data ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data/engine

                  SOURCES ${SOURCES}
)



file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/editor_data/data/shaders/* )
add_custom_target(editor_data
                  ALL
                  COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/editor_data ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data/editor

                  SOURCES
                  ${SOURCES}
)



if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    add_custom_target(copy_system_runtime_libs ALL
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        )
endif()


function(get_target_filename target outvar)
    set(outname "${target}")

    get_target_property(prop_outname "${target}" OUTPUT_NAME)
    if(NOT ("${prop_outname}" STREQUAL "prop_outname-NOTFOUND"))
        set(outname "${prop_outname}")
    endif()

    get_target_property(prop_cfg_outname "${target}" "${OUTPUT_NAME}_${CMAKE_BUILD_TYPE}")
    if(NOT ("${prop_cfg_outname}" STREQUAL "prop_cfg_outname-NOTFOUND"))
        set(outname "${prop_cfg_outname}")
    endif()

    get_target_property(prop_archive_outname "${target}" ARCHIVE_OUTPUT_NAME)
    get_target_property(prop_library_outname "${target}" LIBRARY_OUTPUT_NAME)
    get_target_property(prop_runtime_outname "${target}" RUNTIME_OUTPUT_NAME)
    get_target_property(prop_archive_cfg_outname "${target}" "${ARCHIVE_OUTPUT_NAME}_${CMAKE_BUILD_TYPE}")
    get_target_property(prop_library_cfg_outname "${target}" "${LIBRARY_OUTPUT_NAME}_${CMAKE_BUILD_TYPE}")
    get_target_property(prop_runtime_cfg_outname "${target}" "${RUNTIME_OUTPUT_NAME}_${CMAKE_BUILD_TYPE}")
    if(NOT ("${prop_archive_cfg_outname}" STREQUAL "prop_archive_cfg_outname-NOTFOUND"))
        set(prop_archive_outname "${prop_archive_cfg_outname}")
    endif()
    if(NOT ("${prop_library_cfg_outname}" STREQUAL "prop_library_cfg_outname-NOTFOUND"))
        set(prop_library_outname "${prop_library_cfg_outname}")
    endif()
    if(NOT ("${prop_runtime_cfg_outname}" STREQUAL "prop_runtime_cfg_outname-NOTFOUND"))
        set(prop_runtime_outname "${prop_runtime_cfg_outname}")
    endif()


    get_target_property(prop_type "${target}" TYPE)
    get_target_property(prop_is_framework "${target}" FRAMEWORK)
    if("${prop_is_framework}")
        set(filename "${outname}")
    elseif(prop_type STREQUAL "STATIC_LIBRARY")
        if(NOT ("${prop_archive_outname}" STREQUAL "prop_archive_outname-NOTFOUND"))
            set(outname "${prop_archive_outname}")
        endif()
        set(prefix "${CMAKE_STATIC_LIBRARY_PREFIX}")
        set(suffix "${CMAKE_STATIC_LIBRARY_SUFFIX}")
    elseif(prop_type STREQUAL "MODULE_LIBRARY")
        if(NOT ("${prop_library_outname}" STREQUAL "prop_library_outname-NOTFOUND"))
            set(outname "${prop_library_outname}")
        endif()
        set(prefix "${CMAKE_SHARED_MODULE_LIBRARY_PREFIX}")
        set(suffix "${CMAKE_SHARED_MODULE_LIBRARY_SUFFIX}")
    elseif(prop_type STREQUAL "SHARED_LIBRARY")
        if(WIN32)
            if(NOT ("${prop_runtime_outname}" STREQUAL "prop_runtime_outname-NOTFOUND"))
                set(outname "${prop_runtime_outname}")
            endif()
        else()
            if(NOT ("${prop_library_outname}" STREQUAL "prop_library_outname-NOTFOUND"))
                set(outname "${prop_library_outname}")
            endif()
        endif()
        set(prefix "${CMAKE_SHARED_LIBRARY_PREFIX}")
        set(suffix "${CMAKE_SHARED_LIBRARY_SUFFIX}")
    elseif(prop_type STREQUAL "EXECUTABLE")
        if(NOT ("${prop_runtime_outname}" STREQUAL "prop_runtime_outname-NOTFOUND"))
            set(outname "${prop_runtime_outname}")
        endif()
        set(prefix "${CMAKE_EXECUTABLE_PREFIX}")
        set(suffix "${CMAKE_EXECUTABLE_SUFFIX}")
    else()
        message(FATAL_ERROR "target \"${target}\" is not of type STATIC_LIBRARY, MODULE_LIBRARY, SHARED_LIBRARY, or EXECUTABLE.")
    endif()

    get_target_property(prop_prefix "${target}" PREFIX)
    if(NOT ("${prop_prefix}" STREQUAL "prop_prefix-NOTFOUND"))
        set(prefix "${prop_prefix}")
    endif()
    get_target_property(prop_suffix "${target}" PREFIX)
    if(NOT ("${prop_suffix}" STREQUAL "prop_suffix-NOTFOUND"))
        set(suffix "${prop_suffix}")
    endif()

    set("${outvar}" "${prefix}${outname}${suffix}" PARENT_SCOPE)
endfunction()

function (_get_all_cmake_targets out_var current_dir)
    get_property(targets DIRECTORY ${current_dir} PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subdirs DIRECTORY ${current_dir} PROPERTY SUBDIRECTORIES)

    foreach(subdir ${subdirs})
        _get_all_cmake_targets(subdir_targets ${subdir})
        list(APPEND targets ${subdir_targets})
    endforeach()

    set(${out_var} ${targets} PARENT_SCOPE)
endfunction()

# Run at end of top-level CMakeLists
_get_all_cmake_targets(all_targets ${CMAKE_CURRENT_LIST_DIR})

# Collect all executable targets defined in your project.
#get_property(all_targets GLOBAL PROPERTY TARGETS)

set(exec_targets "")
foreach(target ${all_targets})
  get_target_property(target_type ${target} TYPE)

  if(target_type STREQUAL "EXECUTABLE")
    list(APPEND exec_targets ${target})
  endif()
endforeach()

# Now install the remaining files from the directory, skipping those executables.
install(DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/
        DESTINATION .
        FILES_MATCHING PATTERN "*")

# Install each executable using its generator expression for its file path.
foreach(exec ${exec_targets})

  install(PROGRAMS "$<TARGET_FILE:${exec}>"
          DESTINATION .)
endforeach()


# Basic settings

set(CPACK_PACKAGE_NAME "UnravelEngine")
set(CPACK_PACKAGE_VERSION "${VERSION_SHORT}")


# 3) Override the *file* name for all generators (NSIS, ZIP, DEB, RPM, etc):
#    <name>-<version>-<distro>-<system>.<ext>
set(CPACK_PACKAGE_FILE_NAME
   "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}"
)


# Override the *file* name for all generators (NSIS, ZIP, DEB, RPM, etc):
#    <name>-<version>-<distro>-<system>.<ext>
set(CPACK_PACKAGE_FILE_NAME
   "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}"
)


if(DISTRO_NAME)
    set(CPACK_PACKAGE_FILE_NAME
        "${CPACK_PACKAGE_FILE_NAME}-${DISTRO_NAME}"
    )
endif()

if(DISTRO_VERSION)
    set(CPACK_PACKAGE_FILE_NAME
        "${CPACK_PACKAGE_FILE_NAME}-${DISTRO_VERSION}"
    )
endif()

message(STATUS "PACKAGE_FILE_NAME ${CPACK_PACKAGE_FILE_NAME}")

set(CPACK_PACKAGE_CONTACT "Your Name <you@example.com>")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")
set(CPACK_PACKAGE_DESCRIPTION "UnravelEngine is a great tool for developing games.")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "UnravelEngine-${CPACK_PACKAGE_VERSION}")
set(CPACK_THREADS 0)
if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}\\\\rc\\\\UnravelEngine.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "${CMAKE_SOURCE_DIR}\\\\rc\\\\UnravelEngine.ico")
    set(CPACK_WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/rc/UnravelEngine.ico")
endif()

# For macOS DMG:
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
endif()

# For Linux DEB or RPM:
if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "ZIP;DEB;RPM")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/unravel-engine")
endif()
# Include CPack
include(CPack)

