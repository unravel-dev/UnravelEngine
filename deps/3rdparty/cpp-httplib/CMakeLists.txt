# set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/openssl)
# set(OPENSSL_ROOT_DIR "C:/Program Filesn/FireDaemon OpenSSL 3" CACHE PATH "")
project(cpp-httplib)

# Local MCP HTTP only — skip optional TLS/compression. Upstream's zstd
# find_package path often sets zstd_FOUND without creating zstd::libzstd
# (Ubuntu libzstd-dev), which then breaks target_link_libraries.
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(cpp-httplib)

# Upstream defaults HTTPLIB_USE_NON_BLOCKING_GETADDRINFO=ON, which uses
# getaddrinfo_a / gai_* on Linux. Those live in libanl (glibc); the stock
# httplib CMake target does not pull it in, so link it here for consumers.
if(UNIX AND NOT APPLE AND HTTPLIB_USE_NON_BLOCKING_GETADDRINFO)
  target_link_libraries(httplib INTERFACE anl)
endif()

#target_include_directories(httplib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
