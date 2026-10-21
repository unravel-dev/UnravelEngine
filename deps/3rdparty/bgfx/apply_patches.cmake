# Applies the patches of PATCH_DIR (*.patch, in file name order) to the git checkout SOURCE_DIR: the bgfx
# that bgfx.cmake fetches. The checkout is reset first, so a changed patch set never lands on an older one.
# The applied state (the checkout's HEAD and the patches' hash) is kept in its git directory: an unchanged
# state touches no file, and a configure does not rebuild bgfx.
cmake_minimum_required(VERSION 3.16)

if(NOT SOURCE_DIR OR NOT PATCH_DIR)
    message(FATAL_ERROR "apply_patches.cmake needs -DSOURCE_DIR=<checkout> -DPATCH_DIR=<patches>.")
endif()

file(GLOB patches LIST_DIRECTORIES false "${PATCH_DIR}/*.patch")
list(SORT patches)
set(patch_hashes "")
foreach(patch IN LISTS patches)
    file(SHA256 "${patch}" patch_hash)
    string(APPEND patch_hashes "${patch_hash}")
endforeach()
string(SHA256 patch_set "${patch_hashes}")

execute_process(COMMAND git rev-parse HEAD
                WORKING_DIRECTORY "${SOURCE_DIR}"
                OUTPUT_VARIABLE head
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "${SOURCE_DIR} is no git checkout: its patches cannot be applied.")
endif()
execute_process(COMMAND git rev-parse --absolute-git-dir
                WORKING_DIRECTORY "${SOURCE_DIR}"
                OUTPUT_VARIABLE git_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE)
set(state_file "${git_dir}/unravel-applied-patches")
set(state "${head} ${patch_set}")
if(EXISTS "${state_file}")
    file(READ "${state_file}" applied_state)
    if(applied_state STREQUAL state)
        return()
    endif()
endif()

execute_process(COMMAND git reset --hard --quiet
                WORKING_DIRECTORY "${SOURCE_DIR}"
                RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Could not reset ${SOURCE_DIR} before applying its patches.")
endif()
foreach(patch IN LISTS patches)
    execute_process(COMMAND git apply --whitespace=nowarn "${patch}"
                    WORKING_DIRECTORY "${SOURCE_DIR}"
                    RESULT_VARIABLE result
                    ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Could not apply ${patch} to ${SOURCE_DIR}:\n${error}")
    endif()
    get_filename_component(patch_name "${patch}" NAME)
    message(STATUS "Applied ${patch_name}.")
endforeach()
file(WRITE "${state_file}" "${state}")
