# knxd core library for ESP32
# Sources come from knxd-esp32 worktree via symlink (knxd-src -> knxd-esp32/src)

set(KNXD ${CMAKE_CURRENT_SOURCE_DIR}/knxd/src)

set(KNXD_LIBSERVER_SRCS
    ${KNXD}/libserver/apdu.cpp
    ${KNXD}/libserver/cemi.cpp
    ${KNXD}/libserver/client.cpp
    ${KNXD}/libserver/cm_ip.cpp
    ${KNXD}/libserver/cm_tp1.cpp
    ${KNXD}/libserver/common.cpp
    ${KNXD}/libserver/eibnetip.cpp
    ${KNXD}/libserver/eibnetserver.cpp
    ${KNXD}/libserver/emi.cpp
    ${KNXD}/libserver/inetserver.cpp
    ${KNXD}/libserver/ipsecure.cpp
    ${KNXD}/libserver/ipsupport.cpp
    ${KNXD}/libserver/link.cpp
    ${KNXD}/libserver/llserial.cpp
    ${KNXD}/libserver/lltcp.cpp
    ${KNXD}/libserver/lowlatency.cpp
    ${KNXD}/libserver/lowlevel.cpp
    ${KNXD}/libserver/lpdu.cpp
    ${KNXD}/libserver/npdu.cpp
    ${KNXD}/libserver/retry.cpp
    ${KNXD}/libserver/router.cpp
    ${KNXD}/libserver/server.cpp
    ${KNXD}/libserver/systemdserver.cpp
    ${KNXD}/libserver/tcptunserver.cpp
    ${KNXD}/libserver/tpdu.cpp
    ${KNXD}/libserver/trace.cpp
    ${KNXD}/libserver/tunchannel.cpp
)

set(KNXD_COMMON_SRCS
    ${KNXD}/common/inifile.cpp
    ${KNXD}/common/inih.c
    ${KNXD}/common/iobuf.cpp
)

set(KNXD_BACKEND_SRCS
    ${KNXD}/backend/tpuart.cpp
    ${KNXD}/backend/ncn5120.cpp
    ${KNXD}/backend/dummy.cpp
    ${KNXD}/backend/log.cpp
    ${KNXD}/backend/nat.cpp
    ${KNXD}/backend/fpace.cpp
    ${KNXD}/backend/fqueue.cpp
)

idf_component_register(
    SRCS ${KNXD_LIBSERVER_SRCS} ${KNXD_COMMON_SRCS} ${KNXD_BACKEND_SRCS}
         "${CMAKE_CURRENT_SOURCE_DIR}/esp32_stubs.c"
    INCLUDE_DIRS "${KNXD}/libserver" "${KNXD}/common" "${KNXD}/backend" "${KNXD}/include" "." "fmt"
    REQUIRES libev_shim mbedtls lwip esp_event esp_driver_uart driver esp_netif freertos
)

# Our config.h and fmt shim take priority over autotools-generated ones
target_include_directories(${COMPONENT_LIB} BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fmt"
    "${KNXD}/backend"
    "${KNXD}/include"
)


target_compile_options(${COMPONENT_LIB} PRIVATE
    -fexceptions -frtti
    -Wno-error
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -Wno-sign-compare
    -Wno-reorder
    -Wno-format
    -Wno-switch
    -Wno-unused-label
    # Force-include FreeRTOS before any other header to prevent
    # include-order issues with queue.h/semphr.h in C++ files
    -include "freertos/FreeRTOS.h"
)
