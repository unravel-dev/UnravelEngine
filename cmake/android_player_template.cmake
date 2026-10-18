# Assembles the Android player template (see android_player.cmake). Run with cmake -P by the
# android_player target after the arm64 sub-build. Files are copied only when they changed, so the
# Gradle builds that read the template stay incremental, and the stamp is written last: a stamp
# means a complete template.
#
# Layout of OUTPUT:
#   lib/<abi>/libmain.so    the player, stripped
#   project/                the Gradle project (game/android)
#   sdl/java/               SDL's Java sources, from the SDL that libmain.so links
#   dotnet/native/          the .NET runtime's native libraries (libcoreclr.so, libclrjit.so, ...)
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
file(COPY "${stripped}" DESTINATION "${OUTPUT}/lib/${ABI}")

file(COPY "${GRADLE_PROJECT}/" DESTINATION "${OUTPUT}/project"
     PATTERN "build" EXCLUDE
     PATTERN ".gradle" EXCLUDE
     PATTERN ".kotlin" EXCLUDE
     PATTERN ".idea" EXCLUDE
     PATTERN "*.iml" EXCLUDE
     PATTERN "local.properties" EXCLUDE
     PATTERN "run_on_device.py" EXCLUDE)
file(COPY "${SDL_JAVA}/" DESTINATION "${OUTPUT}/sdl/java")

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
# (mscordaccore, mscordbi) are left out.
file(COPY "${runtime_dir}/native/" DESTINATION "${OUTPUT}/dotnet/native"
     FILES_MATCHING PATTERN "*.so"
     PATTERN "libmscord*" EXCLUDE)
file(COPY "${runtime_dir}/lib/${dotnet_framework}" DESTINATION "${OUTPUT}/dotnet/lib")

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
set(previous_stamp "")
if(EXISTS "${stamp_file}")
    file(READ "${stamp_file}" previous_stamp)
endif()
if(NOT previous_stamp STREQUAL stamp)
    file(WRITE "${stamp_file}" "${stamp}")
endif()
