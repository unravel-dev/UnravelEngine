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
# The relink links what the Emscripten build lists as the game's link inputs (cmake/web_link_inputs.cmake).
#
# Nothing is redone while every input is as the last run left it: the relink takes minutes.
cmake_minimum_required(VERSION 3.16)

foreach(required DOTNET CSC REFERENCE_DIR RUNTIME_LIB SCRIPTS_DIR BRIDGE PLAYER_DIR WEB_BUILD EM_CACHE DOTNET_MAJOR
                 ENGINE_VERSION WORK_DIR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "web_player_template.cmake needs -D${required}=...")
    endif()
endforeach()

# A Visual Studio developer environment (vcvars, as CI sets up) defines Platform=x64, which MSBuild takes as a property:
# the browser player would target x64, which the browser-wasm runtime identifier refuses (NETSDK1032). It names the
# native toolchain's platform only, so the .NET steps run without it.
unset(ENV{Platform})

# C# the version the editor compiles the scripts with: the .NET major plus 4 (clrpp csharp_language_version).
math(EXPR language_version "${DOTNET_MAJOR} + 4")
set(stamp_file "${OUTPUT}/template.stamp")
set(stamp "engine_version=${ENGINE_VERSION}\n")
set(inputs_file "${WORK_DIR}/inputs.txt")
set(link_inputs_file "${WEB_BUILD}/web_link_inputs.txt")
set(link_inputs_project "${WORK_DIR}/link_inputs.props")

# The objects and static libraries the relink links. A library the game links that this build did not make is an
# error: the relink would fail on its symbols.
function(read_link_inputs out_var)
    if(NOT EXISTS "${link_inputs_file}")
        message(FATAL_ERROR "Web player: ${link_inputs_file} is missing; the Emscripten build writes it as it configures.")
    endif()
    file(STRINGS "${link_inputs_file}" link_inputs)
    foreach(link_input IN LISTS link_inputs)
        if(NOT EXISTS "${link_input}")
            message(FATAL_ERROR "Web player: the Emscripten build did not make ${link_input}, which the game links.")
        endif()
    endforeach()
    set(${out_var} ${link_inputs} PARENT_SCOPE)
endfunction()

# The project file the player's project imports: a NativeFileReference item for each link input.
function(write_link_inputs_project link_inputs)
    set(project "<Project>\n  <ItemGroup>\n")
    foreach(link_input IN LISTS link_inputs)
        string(APPEND project "    <NativeFileReference Include=\"${link_input}\" />\n")
    endforeach()
    string(APPEND project "  </ItemGroup>\n</Project>\n")
    file(WRITE "${link_inputs_project}" "${project}")
endfunction()

# Every input as path, size and time, so a changed file changes the list; the SDK's packs by their versioned folders.
function(describe_inputs out_var link_inputs)
    file(GLOB_RECURSE scripts "${SCRIPTS_DIR}/*.cs")
    file(GLOB_RECURSE player_files "${PLAYER_DIR}/*.cs" "${PLAYER_DIR}/*.csproj" "${PLAYER_DIR}/wwwroot/*")
    set(description "${stamp}${DOTNET}\n${CSC}\n${REFERENCE_DIR}\n${RUNTIME_LIB}\n${EM_CACHE}\n")
    foreach(file IN LISTS scripts link_inputs player_files BRIDGE CMAKE_CURRENT_LIST_FILE)
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

read_link_inputs(link_inputs)
describe_inputs(inputs "${link_inputs}")
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
write_link_inputs_project("${link_inputs}")
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
         -o "${WORK_DIR}/publish" "-p:UnravelLinkInputs=${link_inputs_project}" "-p:UnravelBridge=${BRIDGE}"
         "-p:UnravelCalliSignatures=${anchors}" "-p:WasmCachePath=${EM_CACHE}")

file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")
file(COPY "${WORK_DIR}/publish/wwwroot" DESTINATION "${OUTPUT}")
file(WRITE "${stamp_file}" "${stamp}")
file(WRITE "${inputs_file}" "${inputs}")
message(STATUS "Web player: ${OUTPUT} (${ENGINE_VERSION}).")
