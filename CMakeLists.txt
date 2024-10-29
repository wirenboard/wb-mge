cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_definitions(FW_VERSION_STRING="${FW_VERSION_STRING}")

project(${TARGET_GIT_INFO})