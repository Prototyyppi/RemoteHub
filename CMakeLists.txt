cmake_minimum_required(VERSION 3.0.2)
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/lib CACHE PATH "Install to lib folder")
project(RemoteHub)

include_directories(${CMAKE_SOURCE_DIR}/dependency/mbedtls/include)

add_compile_options(-Wall -Wextra -Werror)

if(${CMAKE_VERSION} VERSION_LESS "3.12.0")
  add_definitions(-DMBEDTLS_THREADING_C -DMBEDTLS_THREADING_PTHREAD)
else()
  add_compile_definitions(MBEDTLS_THREADING_C)
  add_compile_definitions(MBEDTLS_THREADING_PTHREAD)
endif()

add_subdirectory(dependency)

add_subdirectory(common)
add_subdirectory(server)
add_subdirectory(client)
