# set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/openssl)
# set(OPENSSL_ROOT_DIR "C:/Program Filesn/FireDaemon OpenSSL 3" CACHE PATH "")
project(cpp-httplib)
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)

add_subdirectory(cpp-httplib)

# Upstream defaults HTTPLIB_USE_NON_BLOCKING_GETADDRINFO=ON, which uses
# getaddrinfo_a / gai_* on Linux. Those live in libanl (glibc); the stock
# httplib CMake target does not pull it in, so link it here for consumers.
if(UNIX AND NOT APPLE AND HTTPLIB_USE_NON_BLOCKING_GETADDRINFO)
  target_link_libraries(httplib INTERFACE anl)
endif()

#target_include_directories(httplib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
