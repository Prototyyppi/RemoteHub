cmake_minimum_required(VERSION 3.0.2)

set(MBEDTLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls)
set(CJSON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cJSON)

execute_process(COMMAND git submodule update --init ${MBEDTLS_DIR}
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		RESULT_VARIABLE GIT_EXIT_CODE)
if(NOT GIT_EXIT_CODE EQUAL "0")
    message(FATAL_ERROR "git submodule update (mbedtls) failed")
endif()

execute_process(COMMAND git submodule update --init ${CJSON_DIR}
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		RESULT_VARIABLE GIT_EXIT_CODE)
if(NOT GIT_EXIT_CODE EQUAL "0")
    message(FATAL_ERROR "git submodule update (cJSON) failed")
endif()

set(ENABLE_PROGRAMS OFF CACHE STRING "Do not compile mbedtls programs")
set(ENABLE_TESTING OFF CACHE STRING "Do not compile mbedtls tests")
set(ENABLE_PROGRAMS OFF)
set(ENABLE_TESTING OFF)

add_subdirectory(${MBEDTLS_DIR})

set(mbed_tls_libs
    mbedcrypto
    mbedx509
    mbedtls
)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static only" FORCE)

add_subdirectory(${CJSON_DIR})
