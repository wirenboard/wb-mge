cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_definitions(FW_VERSION_STRING="${FW_VERSION_STRING}")

# Enable lwip statistics
add_compile_definitions(MIB2_STATS=1)

# Custom lwip hook
idf_build_set_property(INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/main/lwip_hook" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS ESP_IDF_LWIP_HOOK_FILENAME=\"lwip_hook.h\" APPEND)

# Set default project name if TARGET_GIT_INFO is not provided
if(NOT DEFINED TARGET_GIT_INFO)
    set(TARGET_GIT_INFO "mge")
endif()

project(${TARGET_GIT_INFO})
