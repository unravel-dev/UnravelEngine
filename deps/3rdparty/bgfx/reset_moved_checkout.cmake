# Resets the bgfx checkout FetchContent keeps in SOURCE_DIR when its HEAD is not PIN, the commit the next update
# checks out. FetchContent's git update stashes the checkout's changes, checks out the new commit and pops them;
# the patches apply_patches.cmake left there do not pop where upstream changed the same lines, and the update stops
# with "Failed to unstash changes" on every configure. A clean checkout updates, and the patch step then applies
# the patch set on the new commit. The checkout holds only those patches: apply_patches.cmake resets it whenever
# the set changes.
cmake_minimum_required(VERSION 3.16)

if(NOT SOURCE_DIR OR NOT PIN)
    message(FATAL_ERROR "reset_moved_checkout.cmake needs -DSOURCE_DIR=<checkout> -DPIN=<commit>.")
endif()

if(NOT EXISTS "${SOURCE_DIR}/.git")
    return()
endif()

execute_process(COMMAND git rev-parse HEAD
                WORKING_DIRECTORY "${SOURCE_DIR}"
                OUTPUT_VARIABLE head
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    return()
endif()
# A pin the checkout has not fetched yet resolves to nothing, which is a move too.
execute_process(COMMAND git rev-parse --verify --quiet "${PIN}^{commit}"
                WORKING_DIRECTORY "${SOURCE_DIR}"
                OUTPUT_VARIABLE pin_commit
                OUTPUT_STRIP_TRAILING_WHITESPACE)
if(head STREQUAL pin_commit)
    return()
endif()

execute_process(COMMAND git status --porcelain --untracked-files=no
                WORKING_DIRECTORY "${SOURCE_DIR}"
                OUTPUT_VARIABLE changes)
if(changes STREQUAL "")
    return()
endif()

execute_process(COMMAND git reset --hard --quiet
                WORKING_DIRECTORY "${SOURCE_DIR}"
                RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Could not reset ${SOURCE_DIR} before bgfx moves to ${PIN}.")
endif()
message(STATUS "Reset the patched bgfx checkout before it moves to ${PIN}.")
