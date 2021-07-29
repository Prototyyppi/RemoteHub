
add_library(remotehub_server
    util/forwarding.c
    util/server.c
    tasks/usb.c
    tasks/host.c
    tasks/timer.c
    tasks/beacon.c
    tasks/interface.c
    util/srv_network.c
    util/srv_network_tcp.c
    util/srv_network_tls.c
)

set(CMAKE_BUILD_TYPE Debug)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/common/include)
include_directories(include)
include_directories(${CMAKE_SOURCE_DIR}/dependency/cJSON)

target_link_libraries(remotehub_server Threads::Threads)
target_link_libraries(remotehub_server usb-1.0)
target_link_libraries(remotehub_server remotehub_common)
target_link_libraries(remotehub_server cjson)
target_link_libraries(remotehub_server mbedcrypto mbedx509 mbedtls)

install(TARGETS remotehub_server
    LIBRARY DESTINATION ${CMAKE_SOURCE_DIR}/lib/remotehub
    ARCHIVE DESTINATION ${CMAKE_SOURCE_DIR}/lib/remotehub
)

install(DIRECTORY include
    DESTINATION remotehub
    FILES_MATCHING PATTERN "server*.h"
)

add_executable(rh_server
    executable/rh_server.c
)

target_link_libraries(rh_server remotehub_server remotehub_common)

install(TARGETS rh_server RUNTIME DESTINATION ${CMAKE_SOURCE_DIR}/bin)
