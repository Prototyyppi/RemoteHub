add_library(remotehub_common remotehub.c logging.c event.c network.c network_usbip.c network_tcp.c network_tls.c)

include_directories(include)

install(TARGETS remotehub_common
    LIBRARY DESTINATION ${CMAKE_SOURCE_DIR}/lib/remotehub
    ARCHIVE DESTINATION ${CMAKE_SOURCE_DIR}/lib/remotehub
)

install(FILES include/remotehub.h include/logging.h
    DESTINATION remotehub/include
)
