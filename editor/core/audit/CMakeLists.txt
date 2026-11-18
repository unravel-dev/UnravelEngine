file(GLOB libsrc *.h *.cpp *.hpp)

# The project audit: its rules, the snapshot they read, reports and the project's audit settings. Linked by the
# editor and by the tests, never by the game.
set(target_name audit)

add_library(${target_name} STATIC ${libsrc})

target_include_directories(${target_name}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(${target_name}
    PUBLIC
        engine
        deploy
        asset_compiler
    PRIVATE
        subprocess
)

set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)

# The script analysis tool (code_audit_tool/), a .NET program on the Mono.Cecil the script bridge ships. Built with
# the .NET SDK the bridge builds with, into bin/code_audit beside the editor; a game never ships it.
if(DOTNET_EXECUTABLE)
    set(CODE_AUDIT_PROJECT ${CMAKE_CURRENT_SOURCE_DIR}/code_audit_tool/Unravel.CodeAudit.csproj)
    set(CODE_AUDIT_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/code_audit)
    set(CODE_AUDIT_DLL ${CODE_AUDIT_OUTPUT_DIR}/Unravel.CodeAudit.dll)
    file(GLOB CODE_AUDIT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/code_audit_tool/*.cs)

    add_custom_command(
        OUTPUT ${CODE_AUDIT_DLL}
        COMMAND ${DOTNET_EXECUTABLE} build ${CODE_AUDIT_PROJECT} -c Release -o ${CODE_AUDIT_OUTPUT_DIR}
                -p:CodeAuditDotnetVersion=${CLRPP_DOTNET_VERSION} --nologo -v minimal
        DEPENDS ${CODE_AUDIT_SOURCES} ${CODE_AUDIT_PROJECT}
        COMMENT "Building the project audit's script analysis tool (net${CLRPP_DOTNET_VERSION})"
        VERBATIM
    )
    add_custom_target(code_audit DEPENDS ${CODE_AUDIT_DLL})
    add_dependencies(${target_name} code_audit)

    if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        add_custom_command(TARGET code_audit POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${CODE_AUDIT_OUTPUT_DIR} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/code_audit
            COMMENT "Copying the script analysis tool into code_audit/ next to the executables"
            VERBATIM
        )
    endif()
else()
    message(WARNING "audit: dotnet executable not found - the script analysis tool will not be built")
endif()
