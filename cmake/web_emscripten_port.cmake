# Fetches an external Emscripten port (Dawn's emdawnwebgpu) into an Emscripten cache before a build links with it.
#
# Emscripten downloads an external port the first time a link names it (--use-port), in one attempt: a download
# server that answers with an error fails that link. This fetch retries, checks the port's SHA-512 and unpacks it
# the way Emscripten does (tools/ports/__init__.py, fetch_port_artifact): the port's folder under <cache>/ports with
# an .emscripten_url marker holding the download's URL, which Emscripten then takes as up to date. Emscripten would
# also drop the port's built library from the cache; emdawnwebgpu names that library after a hash of its sources, so
# a new version builds a library of its own.

set(UNRAVEL_EMSCRIPTEN_PORT_DOWNLOAD_ATTEMPTS 5)
set(UNRAVEL_EMSCRIPTEN_PORT_RETRY_SECONDS 15)

# The download URL and SHA-512 of the external port <name> that <emscripten>/tools/ports/<name>.py describes, in
# <url_var> and <hash_var>; empty when that file has none.
function(unravel_read_emscripten_external_port emscripten name url_var hash_var)
    set(url "")
    set(hash "")
    set(port_file "${emscripten}/tools/ports/${name}.py")
    if(EXISTS "${port_file}")
        file(READ "${port_file}" port)
        if(port MATCHES "\nEXTERNAL_PORT = f?'([^']+)'")
            set(url "${CMAKE_MATCH_1}")
            if(port MATCHES "\n_VERSION = '([^']+)'")
                string(REPLACE "{_VERSION}" "${CMAKE_MATCH_1}" url "${url}")
            endif()
        endif()
        if(port MATCHES "\nSHA512 = '([0-9a-f]+)'")
            set(hash "${CMAKE_MATCH_1}")
        endif()
    endif()
    if(url MATCHES "[{}]")
        set(url "")
    endif()
    set(${url_var} "${url}" PARENT_SCOPE)
    set(${hash_var} "${hash}" PARENT_SCOPE)
endfunction()

# Whether the port <name> under <ports> is the one <url> downloads, as Emscripten's marker says.
function(unravel_is_emscripten_port_fetched ports name url result_var)
    set(fetched FALSE)
    set(marker "${ports}/${name}/.emscripten_url")
    if(EXISTS "${marker}")
        file(STRINGS "${marker}" marker_url LIMIT_COUNT 1)
        if(marker_url STREQUAL url)
            set(fetched TRUE)
        endif()
    endif()
    set(${result_var} ${fetched} PARENT_SCOPE)
endfunction()

# Downloads <url> to <archive>, retrying, and checks its SHA-512 against <hash>; <result_var> is the failure, empty on
# success.
function(unravel_download_emscripten_port url hash archive result_var)
    set(failure "")
    foreach(attempt RANGE 1 ${UNRAVEL_EMSCRIPTEN_PORT_DOWNLOAD_ATTEMPTS})
        file(DOWNLOAD "${url}" "${archive}" STATUS status TLS_VERIFY ON)
        list(GET status 0 code)
        list(GET status 1 failure)
        if(code EQUAL 0)
            file(SHA512 "${archive}" actual_hash)
            if(actual_hash STREQUAL hash)
                set(${result_var} "" PARENT_SCOPE)
                return()
            endif()
            set(failure "unexpected SHA-512 ${actual_hash}")
        endif()
        file(REMOVE "${archive}")
        message(STATUS "Download ${attempt} of ${UNRAVEL_EMSCRIPTEN_PORT_DOWNLOAD_ATTEMPTS} of ${url} failed: ${failure}.")
        if(attempt LESS UNRAVEL_EMSCRIPTEN_PORT_DOWNLOAD_ATTEMPTS)
            execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep ${UNRAVEL_EMSCRIPTEN_PORT_RETRY_SECONDS})
        endif()
    endforeach()
    set(${result_var} "${failure}" PARENT_SCOPE)
endfunction()

# Puts the external port <name> of the Emscripten at <emscripten> into the Emscripten cache <cache>, unless it is
# there. A port this Emscripten does not describe, or a download that keeps failing, is left to Emscripten.
function(unravel_fetch_emscripten_port emscripten cache name)
    unravel_read_emscripten_external_port("${emscripten}" "${name}" url hash)
    if(url STREQUAL "" OR hash STREQUAL "")
        message(STATUS "Emscripten port ${name}: no download found in tools/ports/${name}.py; Emscripten fetches it.")
        return()
    endif()
    set(ports "${cache}/ports")
    unravel_is_emscripten_port_fetched("${ports}" "${name}" "${url}" fetched)
    if(fetched)
        return()
    endif()
    file(MAKE_DIRECTORY "${ports}")
    # Emscripten's own name for the archive: the port's name and what follows the first dot of the file's name.
    get_filename_component(archive_name "${url}" NAME)
    string(FIND "${archive_name}" "." first_dot)
    math(EXPR extension_start "${first_dot} + 1")
    string(SUBSTRING "${archive_name}" ${extension_start} -1 archive_extension)
    set(archive "${ports}/${name}.${archive_extension}")
    unravel_download_emscripten_port("${url}" "${hash}" "${archive}" failure)
    if(NOT failure STREQUAL "")
        message(WARNING "Emscripten port ${name}: ${url} could not be downloaded (${failure}). Emscripten tries once "
                        "more when the player links.")
        return()
    endif()
    set(unpack_dir "${ports}/${name}.tmp")
    file(REMOVE_RECURSE "${unpack_dir}")
    file(MAKE_DIRECTORY "${unpack_dir}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${archive}"
                    WORKING_DIRECTORY "${unpack_dir}"
                    RESULT_VARIABLE unpack_result)
    if(NOT unpack_result EQUAL 0)
        file(REMOVE_RECURSE "${unpack_dir}")
        message(WARNING "Emscripten port ${name}: ${archive} could not be unpacked. Emscripten fetches it again when "
                        "the player links.")
        return()
    endif()
    file(WRITE "${unpack_dir}/.emscripten_url" "${url}\n")
    file(REMOVE_RECURSE "${ports}/${name}")
    file(RENAME "${unpack_dir}" "${ports}/${name}")
    message(STATUS "Emscripten port ${name}: fetched ${url}.")
endfunction()
