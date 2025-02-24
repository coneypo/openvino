# Copyright (C) 2018-2021 Intel Corporation
# SPDX-License-Identifier: Apache-2.0
#

file(GLOB_RECURSE UTIL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")

add_library(ngraph_test_util STATIC ${UTIL_SRC})

if(COMMAND ie_faster_build)
    ie_faster_build(ngraph_test_util UNITY)
endif()

target_link_libraries(ngraph_test_util PUBLIC ngraph gtest gmock)
target_include_directories(ngraph_test_util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE all_util_src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
add_clang_format_target(ngraph_test_util_clang FOR_SOURCES ${all_util_src})

# developer package
openvino_developer_export_targets(COMPONENT ngraph TARGETS ngraph_test_util)
