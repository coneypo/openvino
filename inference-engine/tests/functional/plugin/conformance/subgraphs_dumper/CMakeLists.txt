# Copyright (C) 2018-2021 Intel Corporation
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME subgraphsDumper)

addIeTargetTest(
        NAME ${TARGET_NAME}
        ROOT ${CMAKE_CURRENT_SOURCE_DIR}
        INCLUDES
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            $<TARGET_PROPERTY:inference_engine,INTERFACE_INCLUDE_DIRECTORIES>
        DEPENDENCIES
            ir_ngraph_frontend
        LINK_LIBRARIES
            PRIVATE
                gflags
                inference_engine
                commonTestUtils
                pugixml::static
        ADD_CPPLINT
)

ie_faster_build(${TARGET_NAME} UNITY)
