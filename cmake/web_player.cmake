# The browser player template: what the editor's web deploys copy beside the game's data.
#
# An Emscripten sub-build compiles the player with the Emscripten of the .NET SDK's wasm-tools workload. The
# web_player target then assembles <bin>/players/web beside the editor (web_player_template.cmake): it compiles and
# weaves the engine scripts to find the native call signatures they use, relinks the .NET browser runtime with the
# engine (game/web/UnravelWebPlayer.csproj), and writes the page, the runtime and a stamp. Editor packages carry it,
# so a downloaded editor deploys to the web without the workload.
#
# UNRAVEL_WEB_PLAYER: AUTO builds the template when the tools are found and says why not otherwise, ON fails the
# configure when a tool or the script bridge is missing, OFF skips it.
# UNRAVEL_WEB_DOTNET: the dotnet of a .NET SDK with the wasm-tools workload (dotnet workload install wasm-tools);
# empty: the dotnet the build found.
# UNRAVEL_WEB_PYTHON, UNRAVEL_WEB_NODE: the Python (3.10 or newer) and the Node.js Emscripten runs on; empty: the
# workload's (Windows), else the ones on the PATH.
# UNRAVEL_WEB_PLAYER_JOBS: the sub-build's parallel jobs.
# The workload's tools are taken for the .NET major below, a release before its previews.

set(UNRAVEL_WEB_PLAYER "AUTO" CACHE STRING
    "Build the browser player template beside the editor: AUTO (when a .NET SDK with wasm-tools is found), ON or OFF.")
set_property(CACHE UNRAVEL_WEB_PLAYER PROPERTY STRINGS AUTO ON OFF)
set(UNRAVEL_WEB_DOTNET "" CACHE FILEPATH
    "The dotnet of a .NET SDK with the wasm-tools workload for the browser player; empty: the build's dotnet.")
set(UNRAVEL_WEB_PLAYER_JOBS "3" CACHE STRING
    "Parallel jobs of the browser player sub-build (unity builds need about 3 GB per job).")
set(UNRAVEL_WEB_PYTHON "" CACHE FILEPATH
    "The Python (3.10 or newer) Emscripten runs on for the browser player; empty: the workload's, else the PATH's.")
set(UNRAVEL_WEB_NODE "" CACHE FILEPATH
    "The Node.js Emscripten runs on for the browser player; empty: the workload's, else the PATH's.")

# The .NET the browser player runs on (game/web/UnravelWebPlayer.csproj).
set(UNRAVEL_WEB_DOTNET_MAJOR "11")
# The oldest Python the Emscripten of that .NET's wasm-tools workload runs on (emcc.py checks it).
set(UNRAVEL_WEB_PYTHON_MIN "3.10")

# The newest folder or file `pattern` matches, by version-aware name order, a release before its previews (11.0.0
# before 11.0.0-rc.2); "" when none does.
function(unravel_find_newest out_var pattern)
    file(GLOB candidates LIST_DIRECTORIES true ${pattern})
    set(keyed "")
    foreach(candidate IN LISTS candidates)
        # A preview's "-" ranks below the "/" or "." that follows a release's version.
        string(REPLACE "-" "!" key "${candidate}")
        list(APPEND keyed "${key}|${candidate}")
    endforeach()
    if(NOT keyed)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    list(SORT keyed COMPARE NATURAL ORDER DESCENDING)
    list(GET keyed 0 newest)
    string(REGEX REPLACE "^[^|]*\\|" "" newest "${newest}")
    set(${out_var} "${newest}" PARENT_SCOPE)
endfunction()

# The tool the user named (`named`), else the newest the workload brings (`pattern`), else `names` on the PATH.
function(unravel_find_web_tool out_var named pattern)
    set(tool "${named}")
    if(NOT tool)
        unravel_find_newest(tool "${pattern}")
    endif()
    if(NOT tool)
        find_program(unravel_web_tool_on_path NAMES ${ARGN})
        if(unravel_web_tool_on_path)
            set(tool "${unravel_web_tool_on_path}")
        endif()
        unset(unravel_web_tool_on_path CACHE)
    endif()
    set(${out_var} "${tool}" PARENT_SCOPE)
endfunction()

# Sets web_player_reason to why the template cannot be built here, or to "" when it can, and the tools it found:
# web_dotnet, web_emscripten, web_llvm, web_binaryen, web_node, web_python, web_runtime_lib, web_reference_dir,
# web_csc, web_ninja.
function(unravel_check_web_player_tools)
    set(web_player_reason "" PARENT_SCOPE)
    if(CMAKE_CONFIGURATION_TYPES)
        set(web_player_reason "multi-configuration generators are not supported, use Ninja" PARENT_SCOPE)
        return()
    endif()
    set(dotnet "${UNRAVEL_WEB_DOTNET}")
    if(NOT dotnet)
        set(dotnet "${DOTNET_EXECUTABLE}")
    endif()
    if(NOT dotnet)
        set(web_player_reason "the .NET SDK (dotnet) was not found" PARENT_SCOPE)
        return()
    endif()
    # A dotnet on the PATH may be a link into the SDK's folder.
    get_filename_component(dotnet "${dotnet}" REALPATH)
    get_filename_component(root "${dotnet}" DIRECTORY)
    set(major "${UNRAVEL_WEB_DOTNET_MAJOR}")
    set(emscripten_packs "${root}/packs/Microsoft.NET.Runtime.Emscripten")
    unravel_find_newest(emscripten "${emscripten_packs}.*.Sdk.*/${major}.*/tools/emscripten")
    unravel_find_newest(runtime_lib "${root}/packs/Microsoft.NETCore.App.Runtime.Mono.browser-wasm/${major}.*/runtimes/browser-wasm/lib/net${major}.0")
    if(NOT emscripten OR NOT runtime_lib)
        set(web_player_reason
            "the .NET SDK at ${root} has no wasm-tools workload for .NET ${major} (dotnet workload install wasm-tools, or set UNRAVEL_WEB_DOTNET)"
            PARENT_SCOPE)
        return()
    endif()
    unravel_find_newest(reference_dir "${root}/packs/Microsoft.NETCore.App.Ref/${major}.*/ref/net${major}.0")
    unravel_find_newest(csc "${root}/sdk/${major}.*/Roslyn/bincore/csc.dll")
    if(NOT reference_dir OR NOT csc)
        set(web_player_reason "the .NET SDK at ${root} has no .NET ${major} SDK" PARENT_SCOPE)
        return()
    endif()
    get_filename_component(tools "${emscripten}" DIRECTORY)
    # The workload brings Node.js and Python on Windows only; elsewhere Emscripten runs on the system's.
    unravel_find_web_tool(node "${UNRAVEL_WEB_NODE}"
                          "${emscripten_packs}.*.Node.*/${major}.*/tools/bin/node${CMAKE_EXECUTABLE_SUFFIX}" node)
    unravel_find_web_tool(python "${UNRAVEL_WEB_PYTHON}"
                          "${emscripten_packs}.*.Python.*/${major}.*/tools/python${CMAKE_EXECUTABLE_SUFFIX}" python3 python)
    if(NOT node OR NOT python)
        set(web_player_reason "Node.js or Python for Emscripten was not found (set UNRAVEL_WEB_NODE or UNRAVEL_WEB_PYTHON)"
            PARENT_SCOPE)
        return()
    endif()
    execute_process(COMMAND "${python}" -c "import sys; print('%d.%d' % sys.version_info[:2])"
                    OUTPUT_VARIABLE python_version
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE python_result)
    if(NOT python_result EQUAL 0)
        set(web_player_reason "the Python for Emscripten, ${python}, does not run (set UNRAVEL_WEB_PYTHON)" PARENT_SCOPE)
        return()
    endif()
    if(python_version VERSION_LESS UNRAVEL_WEB_PYTHON_MIN)
        set(web_player_reason
            "Emscripten needs Python ${UNRAVEL_WEB_PYTHON_MIN} or newer, and ${python} is ${python_version} (set UNRAVEL_WEB_PYTHON)"
            PARENT_SCOPE)
        return()
    endif()
    if(CMAKE_GENERATOR STREQUAL "Ninja")
        set(ninja "${CMAKE_MAKE_PROGRAM}")
    else()
        find_program(UNRAVEL_WEB_NINJA ninja)
        set(ninja "${UNRAVEL_WEB_NINJA}")
    endif()
    if(NOT ninja)
        set(web_player_reason "Ninja was not found" PARENT_SCOPE)
        return()
    endif()
    set(web_dotnet "${dotnet}" PARENT_SCOPE)
    set(web_emscripten "${emscripten}" PARENT_SCOPE)
    set(web_llvm "${tools}/bin" PARENT_SCOPE)
    set(web_binaryen "${tools}" PARENT_SCOPE)
    set(web_node "${node}" PARENT_SCOPE)
    set(web_python "${python}" PARENT_SCOPE)
    set(web_runtime_lib "${runtime_lib}" PARENT_SCOPE)
    set(web_reference_dir "${reference_dir}" PARENT_SCOPE)
    set(web_csc "${csc}" PARENT_SCOPE)
    set(web_ninja "${ninja}" PARENT_SCOPE)
endfunction()

# Emscripten's configuration: the workload's toolchain and a cache of the build's own, which Emscripten writes the
# libraries and ports (Dawn's WebGPU) it builds into.
function(unravel_write_emscripten_config file cache)
    set(config "LLVM_ROOT = r'${web_llvm}'
BINARYEN_ROOT = r'${web_binaryen}'
NODE_JS = r'${web_node}'
CACHE = r'${cache}'
FROZEN_CACHE = False
COMPILER_ENGINE = NODE_JS
JS_ENGINES = [NODE_JS]
")
    set(previous "")
    if(EXISTS "${file}")
        file(READ "${file}" previous)
    endif()
    if(NOT previous STREQUAL config)
        file(WRITE "${file}" "${config}")
    endif()
endfunction()

if(NOT UNRAVEL_WEB_PLAYER STREQUAL "OFF")
    unravel_check_web_player_tools()
    if(web_player_reason)
        if(UNRAVEL_WEB_PLAYER STREQUAL "ON")
            message(FATAL_ERROR "Web player: ${web_player_reason}.")
        endif()
        message(STATUS "Web player: off, ${web_player_reason}.")
    elseif(NOT TARGET clrpp_managed)
        if(UNRAVEL_WEB_PLAYER STREQUAL "ON")
            message(FATAL_ERROR "Web player: the script bridge is not built here (BUILD_CLRPP_MANAGED).")
        endif()
        message(STATUS "Web player: off, the script bridge is not built here (BUILD_CLRPP_MANAGED).")
    else()
        message(STATUS "Web player: on (Emscripten at ${web_emscripten}).")
        include(ExternalProject)
        set(web_build_dir "${PROJECT_BINARY_DIR}/web")
        set(web_em_config "${web_build_dir}/emscripten_config.py")
        set(web_em_cache "${web_build_dir}/em_cache")
        file(MAKE_DIRECTORY "${web_em_cache}")
        unravel_write_emscripten_config("${web_em_config}" "${web_em_cache}")
        set(web_env "${CMAKE_COMMAND}" -E env "EM_CONFIG=${web_em_config}" "EMSDK_PYTHON=${web_python}")
        ExternalProject_Add(web_player_build
            SOURCE_DIR "${PROJECT_SOURCE_DIR}"
            BINARY_DIR "${web_build_dir}"
            CMAKE_GENERATOR Ninja
            CMAKE_COMMAND ${web_env} "${CMAKE_COMMAND}"
            CMAKE_ARGS
                "-DCMAKE_TOOLCHAIN_FILE=${web_emscripten}/cmake/Modules/Platform/Emscripten.cmake"
                "-DCMAKE_MAKE_PROGRAM=${web_ninja}"
                # The browser player is always a release build: it is downloaded, and runs interpreted scripts.
                "-DCMAKE_BUILD_TYPE=Release"
                "-DUNRAVEL_BUILD_EDITOR=OFF"
                "-DUNRAVEL_BUILD_TESTS=OFF"
                # Dependencies that FetchContent downloads are built per toolchain: the host tree's copies are for
                # the desktop.
                "-DFETCHCONTENT_BASE_DIR=${web_build_dir}/_deps"
            BUILD_COMMAND ${web_env} "${CMAKE_COMMAND}" --build "${web_build_dir}" --target game -j ${UNRAVEL_WEB_PLAYER_JOBS}
            INSTALL_COMMAND ""
            BUILD_ALWAYS TRUE
            USES_TERMINAL_CONFIGURE TRUE
            USES_TERMINAL_BUILD TRUE
        )
        if(UNRAVEL_PLAYERS_AFTER_EDITOR)
            # After the editor, so the two compiles do not compete for memory.
            add_dependencies(web_player_build editor)
        endif()
        # The sub-build globs its sources when it configures, and an external project configures once: it
        # configures again after every configure of this tree, so the files added since reach it.
        set(web_configure_stamp "${PROJECT_BINARY_DIR}/web_player_host_configure.stamp")
        file(TOUCH "${web_configure_stamp}")
        ExternalProject_Add_StepDependencies(web_player_build configure "${web_configure_stamp}")

        add_custom_target(web_player ALL
            COMMAND ${web_env} "${CMAKE_COMMAND}"
                "-DDOTNET=${web_dotnet}"
                "-DCSC=${web_csc}"
                "-DREFERENCE_DIR=${web_reference_dir}"
                "-DRUNTIME_LIB=${web_runtime_lib}"
                "-DSCRIPTS_DIR=${PROJECT_SOURCE_DIR}/engine_data/data/scripts"
                "-DBRIDGE=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CLRPP_MANAGED_DIR}/Clrpp.Managed.dll"
                "-DPLAYER_DIR=${PROJECT_SOURCE_DIR}/game/web"
                "-DWEB_BUILD=${web_build_dir}"
                "-DEM_CACHE=${web_em_cache}"
                "-DDOTNET_MAJOR=${UNRAVEL_WEB_DOTNET_MAJOR}"
                "-DENGINE_VERSION=${GIT_VERSION}"
                "-DWORK_DIR=${web_build_dir}/template"
                "-DOUTPUT=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/players/web"
                -P "${CMAKE_CURRENT_LIST_DIR}/web_player_template.cmake"
            COMMENT "Assembling the Web Player Template..."
            VERBATIM
        )
        add_dependencies(web_player web_player_build clrpp_managed)
    endif()
endif()
