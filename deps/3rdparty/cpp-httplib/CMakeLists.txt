# set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/openssl)
# set(OPENSSL_ROOT_DIR "C:/Program Filesn/FireDaemon OpenSSL 3" CACHE PATH "")
project(cpp-httplib)
set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)

add_subdirectory(cpp-httplib)

#target_include_directories(httplib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
