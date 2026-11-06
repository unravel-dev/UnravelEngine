# Assembles the browser player template from the engine's Emscripten build (cmake/web_player.cmake):
#
#   template.stamp   key=value lines: engine_version; written last, so an interrupted build leaves a template the
#                    editor refuses
#   wwwroot/         what a web server serves: the page, the .NET browser runtime with the engine linked into its
#                    wasm module, and the player's assemblies
#
# The .NET interpreter calls native code through a stub per signature that the relink makes, for the signatures of
# DllImports only: the engine scripts' native calls (calli, after the bridge weaves them) get one placeholder
# DllImport each (game/web/tools/CalliSignatures). The scripts are compiled here as the editor compiles them (a
# library, unsafe, no defines) for the scan only; deploys ship the editor's own build of them.
#
# Nothing is redone while every input is as the last run left it: the relink takes minutes.
cmake_minimum_required(VERSION 3.16)

foreach(required DOTNET CSC REFERENCE_DIR RUNTIME_LIB SCRIPTS_DIR BRIDGE PLAYER_DIR WEB_BUILD EM_CACHE DOTNET_MAJOR
                 ENGINE_VERSION WORK_DIR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "web_player_template.cmake needs -D${required}=...")
    endif()
endforeach()

# C# the version the editor compiles the scripts with: the .NET major plus 4 (clrpp csharp_language_version).
math(EXPR language_version "${DOTNET_MAJOR} + 4")
set(stamp_file "${OUTPUT}/template.stamp")
set(stamp "engine_version=${ENGINE_VERSION}\n")
set(inputs_file "${WORK_DIR}/inputs.txt")

# Every input as path, size and time, so a changed file changes the list.
function(describe_inputs out_var)
    file(GLOB_RECURSE scripts "${SCRIPTS_DIR}/*.cs")
    file(GLOB libraries "${WEB_BUILD}/lib/*.a")
    file(GLOB_RECURSE objects "${WEB_BUILD}/game/game/CMakeFiles/game.dir/*.o")
    file(GLOB_RECURSE player_files "${PLAYER_DIR}/*.cs" "${PLAYER_DIR}/*.csproj" "${PLAYER_DIR}/wwwroot/*")
    set(description "${stamp}${DOTNET}\n")
    foreach(file IN LISTS scripts libraries objects player_files BRIDGE)
        file(SIZE "${file}" size)
        file(TIMESTAMP "${file}" time "%Y%m%d%H%M%S" UTC)
        string(APPEND description "${file} ${size} ${time}\n")
    endforeach()
    set(${out_var} "${description}" PARENT_SCOPE)
endfunction()

function(run_step name)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Web player: ${name} failed (${result}).")
    endif()
endfunction()

# The engine scripts, compiled into the work folder for the scan (clrpp build_csc_args).
function(compile_engine_scripts assembly)
    file(GLOB_RECURSE scripts "${SCRIPTS_DIR}/*.cs")
    file(GLOB references "${REFERENCE_DIR}/*.dll")
    set(response "-nologo\n-nostdlib+\n-langversion:${language_version}\n-target:library\n-unsafe\n-optimize\n")
    foreach(reference IN LISTS references)
        string(APPEND response "-r:\"${reference}\"\n")
    endforeach()
    string(APPEND response "-out:\"${assembly}\"\n")
    foreach(script IN LISTS scripts)
        string(APPEND response "\"${script}\"\n")
    endforeach()
    file(WRITE "${WORK_DIR}/engine-script.rsp" "${response}")
    run_step("compiling the engine scripts" "${DOTNET}" exec "${CSC}" "@${WORK_DIR}/engine-script.rsp")
endfunction()

describe_inputs(inputs)
set(previous_inputs "")
if(EXISTS "${inputs_file}")
    file(READ "${inputs_file}" previous_inputs)
endif()
set(previous_stamp "")
if(EXISTS "${stamp_file}")
    file(READ "${stamp_file}" previous_stamp)
endif()
if(inputs STREQUAL previous_inputs AND previous_stamp STREQUAL stamp)
    message(STATUS "Web player: up to date.")
    return()
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(scripts_assembly "${WORK_DIR}/scripts/engine-script.dll")
file(MAKE_DIRECTORY "${WORK_DIR}/scripts")
compile_engine_scripts("${scripts_assembly}")
set(anchors "${WORK_DIR}/CalliSignatures.g.cs")
run_step("finding the native call signatures"
         "${DOTNET}" run --project "${PLAYER_DIR}/tools/CalliSignatures" -c Release --artifacts-path "${WORK_DIR}/tools"
         -- "${anchors}" "${RUNTIME_LIB}" --weave-with "${BRIDGE}" "${scripts_assembly}" "${BRIDGE}")
# The relink skips its interop tables when only its inputs' contents changed; they are made afresh every time.
file(REMOVE_RECURSE "${WORK_DIR}/obj/UnravelWebPlayer/release/wasm" "${WORK_DIR}/publish")
run_step("relinking the .NET browser runtime with the engine"
         "${DOTNET}" publish "${PLAYER_DIR}/UnravelWebPlayer.csproj" -c Release --artifacts-path "${WORK_DIR}"
         -o "${WORK_DIR}/publish" "-p:UnravelWebBuild=${WEB_BUILD}" "-p:UnravelBridge=${BRIDGE}"
         "-p:UnravelCalliSignatures=${anchors}" "-p:WasmCachePath=${EM_CACHE}")

file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")
file(COPY "${WORK_DIR}/publish/wwwroot" DESTINATION "${OUTPUT}")
file(WRITE "${stamp_file}" "${stamp}")
file(WRITE "${inputs_file}" "${inputs}")
message(STATUS "Web player: ${OUTPUT} (${ENGINE_VERSION}).")
