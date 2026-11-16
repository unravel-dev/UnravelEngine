file(GLOB_RECURSE libsrc *.h *.cpp *.hpp)

# What the editor's online checks share: the HTTPS GET (WinHTTP on Windows, wget or curl elsewhere), the GitHub
# addresses, when a check asks again and how it keeps its files. Used by the update check and the sample catalog;
# linked by the editor and by the tests, never by the game.
set(target_name https_client)

if(WIN32)
    list(FILTER libsrc EXCLUDE REGEX "/https_get_posix\\.cpp$")
else()
    list(FILTER libsrc EXCLUDE REGEX "/https_get_windows\\.cpp$")
endif()

add_library(${target_name} STATIC ${libsrc})

target_include_directories(${target_name}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(${target_name} PUBLIC engine PRIVATE subprocess)
if(WIN32)
    target_link_libraries(${target_name} PRIVATE winhttp)
endif()

set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)
