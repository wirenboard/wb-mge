cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_definitions(${MODEL_DEFINE})
add_compile_definitions(DEVICE_SIGNATURE="${DEVICE_SIGNATURE}")
add_compile_definitions(FIRMWARE_GIT_INFO="${FIRMWARE_GIT_INFO}")
add_compile_definitions(FIRMWARE_VERSION="${FIRMWARE_VERSION}")
set(PROJECT_VER "${FIRMWARE_VERSION}")

# Enable lwip statistics
add_compile_definitions(MIB2_STATS=1)

# Custom lwip hook
idf_build_set_property(INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/main/lwip_hook" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS ESP_IDF_LWIP_HOOK_FILENAME=\"lwip_hook.h\" APPEND)

# Set project name and QEMU_BUILD define for QEMU build
if($ENV{CONFIG_ETH_USE_OPENETH})
    set(TARGET_PROJECT_NAME "qemu_mge")
    add_compile_definitions(QEMU_BUILD=1)
    message(STATUS "Building for QEMU, using project name 'qemu_mge' and setting QEMU_BUILD = 1")
else()
    add_compile_definitions(QEMU_BUILD=0)
    message(STATUS "Building for hardware, setting QEMU_BUILD = 0")
endif()

project(${TARGET_PROJECT_NAME})
