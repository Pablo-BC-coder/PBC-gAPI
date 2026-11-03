# PBC-gAPI — property and copyright authority: Pablo B. C.
cmake_minimum_required(VERSION 3.20)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pbc_esp32_example)
