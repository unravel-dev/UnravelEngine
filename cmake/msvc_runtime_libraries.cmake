# The VC++ runtime DLLs (msvcp140*, vcruntime140*, concrt140) for clang targeting the MSVC ABI.
#
# InstallRequiredSystemLibraries finds them only when CMake's MSVC is true, which it is not for clang
# in GNU-driver mode. They come from the redist folder of the Visual Studio whose MSVC headers clang
# compiles against, so they are never older than the STL the binaries were built with.
function(unravel_find_msvc_runtime_libraries out_var)
    set(${out_var} "" PARENT_SCOPE)
    # clang -v prints its include search path, which names the MSVC toolset it picked.
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -v -E -x c++ "${CMAKE_ROOT}/Modules/CMakeCXXCompilerABI.cpp"
                    OUTPUT_QUIET
                    ERROR_VARIABLE compiler_output
                    RESULT_VARIABLE compiler_result)
    # A search path line: " <Visual Studio>\VC\Tools\MSVC\<version>\include".
    string(REGEX MATCH "\n ([^\n\"]+)[/\\]VC[/\\]Tools[/\\]MSVC[/\\][0-9.]+[/\\]include" toolset_include "${compiler_output}")
    if(NOT compiler_result EQUAL 0 OR NOT toolset_include)
        message(WARNING "The MSVC toolset of ${CMAKE_CXX_COMPILER} was not found, so the VC++ runtime DLLs are not "
                        "copied beside the binaries. Deployed games then need the VC++ redistributable installed.")
        return()
    endif()
    file(TO_CMAKE_PATH "${CMAKE_MATCH_1}" visual_studio_root)
    file(STRINGS "${visual_studio_root}/VC/Auxiliary/Build/Microsoft.VCRedistVersion.default.txt" redist_version LIMIT_COUNT 1)
    string(STRIP "${redist_version}" redist_version)
    string(TOLOWER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" architecture)
    set(crt_folder "${visual_studio_root}/VC/Redist/MSVC/${redist_version}/${architecture}")
    file(GLOB runtime_libraries
         "${crt_folder}/Microsoft.VC*.CRT/msvcp140*.dll"
         "${crt_folder}/Microsoft.VC*.CRT/vcruntime140*.dll"
         "${crt_folder}/Microsoft.VC*.CRT/concrt140.dll")
    if(NOT runtime_libraries)
        message(WARNING "No VC++ runtime DLLs in ${crt_folder}, so none are copied beside the binaries. Deployed "
                        "games then need the VC++ redistributable installed.")
        return()
    endif()
    set(${out_var} ${runtime_libraries} PARENT_SCOPE)
endfunction()
