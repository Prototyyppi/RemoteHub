
add_library(remotehub_client
    tasks/beacon.c
    tasks/timer.c
    tasks/interface.c
    tasks/manager.c
    util/cli_network.c
    util/cli_network_tcp.c
    util/cli_network_tls.c
    util/command.c
    util/client.c
    util/vhci.c
)

set(CMAKE_BUILD_TYPE Debug)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/common/include)
include_directories(include)
include_directories(${CMAKE_SOURCE_DIR}/dependency/cJSON)

target_link_libraries(remotehub_client Threads::Threads)
target_link_libraries(remotehub_client usb-1.0)
target_link_libraries(remotehub_client remotehub_common)
target_link_libraries(remotehub_client mbedcrypto mbedx509 mbedtls)
target_link_libraries(remotehub_client cjson)

install(TARGETS remotehub_client
    LIBRARY DESTINATION ${CMAKE_SOURCE_DIR}/lib/remotehub
    ARCHIVE DESTINATION ${CMAKE_SOURCE_DIR}/lib/remotehub
)

install(DIRECTORY include
    DESTINATION remotehub
    FILES_MATCHING PATTERN "client*.h"
)

add_executable(rh_client
    executable/rh_client.c
)

target_link_libraries(rh_client remotehub_client remotehub_common)

install(TARGETS rh_client RUNTIME DESTINATION ${CMAKE_SOURCE_DIR}/bin)
