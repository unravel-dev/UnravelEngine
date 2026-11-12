# Assembles the Android player template (see android_player.cmake). Run with cmake -P by the
# android_player target after the arm64 sub-build. OUTPUT ends up holding the files listed below and
# nothing else, so a file an earlier template had does not stay. Files are copied only when they
# changed, so the Gradle builds that read the template stay incremental. The stamp is removed before
# the first file changes and written last: a stamp means a complete template of the version it names.
#
# Layout of OUTPUT:
#   lib/<abi>/libmain.so    the player, stripped
#   symbols/<abi>/libmain.so.sym  the player's symbol table, which store packages upload beside the game
#   project/                the Gradle project (game/android)
#   sdl/java/               SDL's Java sources, from the SDL that libmain.so links
#   dotnet/native/          the .NET runtime's native libraries (libcoreclr.so, libclrjit.so, ...) and the Java
#                           classes its cryptography library calls (a .jar)
#   dotnet/lib/<framework>/ the .NET runtime's managed framework
#   template.stamp          key=value lines: engine_version, configuration, abi, dotnet_runtime,
#                           dotnet_framework, symbols

foreach(required LIBRARY STRIP GRADLE_PROJECT SDL_JAVA DOTNET DOTNET_RUNTIME_VERSION ENGINE_VERSION ABI WORK_DIR OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "android_player_template.cmake needs -D${required}=...")
    endif()
endforeach()
if(NOT DEFINED CONFIGURATION)
    set(CONFIGURATION "")
endif()

# The template's files: where each comes from, and its path in OUTPUT.
set(template_sources "")
set(template_files "")

# Adds the file `source` to the template's folder `folder`, a path relative to OUTPUT.
function(add_template_file source folder)
    get_filename_component(name "${source}" NAME)
    list(APPEND template_sources "${source}")
    list(APPEND template_files "${folder}/${name}")
    set(template_sources "${template_sources}" PARENT_SCOPE)
    set(template_files "${template_files}" PARENT_SCOPE)
endfunction()

# Adds the files of the folder `source` to the template's folder `folder`, in their folders. INCLUDE and EXCLUDE
# are regular expressions on a file's path relative to `source`.
function(add_template_folder source folder)
    cmake_parse_arguments(filter "" "INCLUDE;EXCLUDE" "" ${ARGN})
    file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${source}" "${source}/*")
    if(filter_INCLUDE)
        list(FILTER files INCLUDE REGEX "${filter_INCLUDE}")
    endif()
    if(filter_EXCLUDE)
        list(FILTER files EXCLUDE REGEX "${filter_EXCLUDE}")
    endif()
    foreach(file IN LISTS files)
        get_filename_component(file_folder "${folder}/${file}" DIRECTORY)
        add_template_file("${source}/${file}" "${file_folder}")
    endforeach()
    set(template_sources "${template_sources}" PARENT_SCOPE)
    set(template_files "${template_files}" PARENT_SCOPE)
endfunction()

# Removes the folders of OUTPUT that hold nothing, the deepest first.
function(remove_empty_template_folders)
    file(GLOB_RECURSE entries LIST_DIRECTORIES true "${OUTPUT}/*")
    list(SORT entries ORDER DESCENDING)
    foreach(entry IN LISTS entries)
        if(IS_DIRECTORY "${entry}")
            file(GLOB children "${entry}/*")
            if(NOT children)
                file(REMOVE_RECURSE "${entry}")
            endif()
        endif()
    endforeach()
endfunction()

# Makes OUTPUT hold the template's files and nothing else. What does not belong goes first: on a file system that
# ignores letter case, a file whose name changed in case alone would keep its earlier name under the copy.
function(write_template_files)
    file(GLOB_RECURSE others LIST_DIRECTORIES false RELATIVE "${OUTPUT}" "${OUTPUT}/*")
    if(others)
        list(REMOVE_ITEM others ${template_files})
    endif()
    foreach(other IN LISTS others)
        file(REMOVE "${OUTPUT}/${other}")
        if(EXISTS "${OUTPUT}/${other}")
            message(FATAL_ERROR "The Android player template holds ${other}, which is no file of it and cannot be removed.")
        endif()
    endforeach()
    remove_empty_template_folders()
    list(LENGTH template_files file_count)
    math(EXPR last_index "${file_count} - 1")
    foreach(index RANGE ${last_index})
        list(GET template_sources ${index} source)
        list(GET template_files ${index} file)
        get_filename_component(folder "${OUTPUT}/${file}" DIRECTORY)
        file(COPY "${source}" DESTINATION "${folder}")
    endforeach()
endfunction()

# The player, stripped of its debug information. The unstripped library stays in the sub-build for
# symbolizing crashes.
if(NOT EXISTS "${LIBRARY}")
    message(FATAL_ERROR "The Android player library ${LIBRARY} was not built.")
endif()
set(stripped "${WORK_DIR}/lib/${ABI}/libmain.so")
if(NOT EXISTS "${stripped}" OR "${LIBRARY}" IS_NEWER_THAN "${stripped}")
    file(MAKE_DIRECTORY "${WORK_DIR}/lib/${ABI}")
    execute_process(COMMAND "${STRIP}" --strip-unneeded -o "${stripped}" "${LIBRARY}" RESULT_VARIABLE strip_result)
    if(NOT strip_result EQUAL 0)
        message(FATAL_ERROR "Stripping ${LIBRARY} failed (${strip_result}).")
    endif()
endif()
add_template_file("${stripped}" "lib/${ABI}")

# The symbol table alone, without the debug information: function names in the store's crash reports,
# for every editor package, which does not carry the far larger unstripped library.
set(symbol_table "${WORK_DIR}/symbols/${ABI}/libmain.so.sym")
if(NOT EXISTS "${symbol_table}" OR "${LIBRARY}" IS_NEWER_THAN "${symbol_table}")
    file(MAKE_DIRECTORY "${WORK_DIR}/symbols/${ABI}")
    execute_process(COMMAND "${STRIP}" --strip-debug --only-keep-debug -o "${symbol_table}" "${LIBRARY}" RESULT_VARIABLE symbols_result)
    if(NOT symbols_result EQUAL 0)
        message(FATAL_ERROR "Extracting the symbol table of ${LIBRARY} failed (${symbols_result}).")
    endif()
endif()
add_template_file("${symbol_table}" "symbols/${ABI}")

# The Gradle project without what Gradle and Android Studio write into it.
add_template_folder("${GRADLE_PROJECT}" "project"
                    EXCLUDE "(^|/)(build|\\.gradle|\\.kotlin|\\.idea)/|\\.iml$|(^|/)local\\.properties$")
add_template_folder("${SDL_JAVA}" "sdl/java")

# The .NET runtime pack, restored into the NuGet global packages folder when it is not there yet.
string(REGEX REPLACE "\\.[0-9]+$" "" dotnet_major_minor "${DOTNET_RUNTIME_VERSION}")
set(dotnet_framework "net${dotnet_major_minor}")
execute_process(COMMAND "${DOTNET}" nuget locals global-packages --list
                OUTPUT_VARIABLE packages_listing RESULT_VARIABLE packages_result)
string(REGEX MATCH "global-packages: *([^\r\n]+)" packages_line "${packages_listing}")
string(STRIP "${CMAKE_MATCH_1}" packages_dir)
file(TO_CMAKE_PATH "${packages_dir}" packages_dir)
string(REGEX REPLACE "/+$" "" packages_dir "${packages_dir}")
if(NOT packages_result EQUAL 0 OR NOT packages_dir)
    message(FATAL_ERROR "dotnet did not name its NuGet packages folder (${packages_result}): ${packages_listing}")
endif()
set(runtime_dir "${packages_dir}/microsoft.netcore.app.runtime.android-arm64/${DOTNET_RUNTIME_VERSION}/runtimes/android-arm64")
if(NOT IS_DIRECTORY "${runtime_dir}/lib/${dotnet_framework}")
    set(restore_project "${WORK_DIR}/dotnet-restore/restore.csproj")
    file(WRITE "${restore_project}"
"<Project Sdk=\"Microsoft.NET.Sdk\">
  <PropertyGroup>
    <TargetFramework>${dotnet_framework}</TargetFramework>
  </PropertyGroup>
  <ItemGroup>
    <PackageDownload Include=\"Microsoft.NETCore.App.Runtime.android-arm64\" Version=\"[${DOTNET_RUNTIME_VERSION}]\" />
  </ItemGroup>
</Project>
")
    execute_process(COMMAND "${DOTNET}" restore "${restore_project}" RESULT_VARIABLE restore_result)
    if(NOT restore_result EQUAL 0 OR NOT IS_DIRECTORY "${runtime_dir}/lib/${dotnet_framework}")
        message(FATAL_ERROR "Restoring the .NET ${DOTNET_RUNTIME_VERSION} android-arm64 runtime pack failed (${restore_result}).")
    endif()
endif()
# The pack's static archives are for linking it into an executable; the debugger's libraries
# (mscordaccore, mscordbi) are left out. The .jar holds the Java classes of the cryptography library, which the
# Gradle project compiles into the package.
add_template_folder("${runtime_dir}/native" "dotnet/native" INCLUDE "\\.(so|jar)$" EXCLUDE "(^|/)libmscord")
add_template_folder("${runtime_dir}/lib/${dotnet_framework}" "dotnet/lib/${dotnet_framework}")

# symbols: the unstripped library's folder, for symbolizing crashes on the machine that built it.
get_filename_component(symbols_dir "${LIBRARY}" DIRECTORY)
set(stamp "engine_version=${ENGINE_VERSION}
configuration=${CONFIGURATION}
abi=${ABI}
dotnet_runtime=${DOTNET_RUNTIME_VERSION}
dotnet_framework=${dotnet_framework}
symbols=${symbols_dir}
")
set(stamp_file "${OUTPUT}/template.stamp")
file(REMOVE "${stamp_file}")
write_template_files()
file(WRITE "${stamp_file}" "${stamp}")
