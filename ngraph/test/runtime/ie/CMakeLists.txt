# Copyright (C) 2018-2021 Intel Corporation
# SPDX-License-Identifier: Apache-2.0
#

set(SRC
    ie_backend.cpp
    ie_backend.hpp
    ie_backend_visibility.hpp
    ie_tensor.cpp
    ie_tensor.hpp
    ie_executable.cpp
    ie_executable.hpp
)

add_library(ie_backend SHARED ${SRC})

if(COMMAND ie_faster_build)
    ie_faster_build(ie_backend
        UNITY
    )
endif()

if(COMMAND ie_add_vs_version_file)
    ie_add_vs_version_file(NAME ie_backend
                           FILEDESCRIPTION "nGraph IE backend library")
endif()

add_dependencies(ie_backend inference_engine)
target_link_libraries(ie_backend PUBLIC ngraph_backend inference_engine)

install(TARGETS ie_backend
        RUNTIME DESTINATION ${IE_CPACK_RUNTIME_PATH} COMPONENT tests EXCLUDE_FROM_ALL
        LIBRARY DESTINATION ${IE_CPACK_LIBRARY_PATH} COMPONENT tests EXCLUDE_FROM_ALL)
