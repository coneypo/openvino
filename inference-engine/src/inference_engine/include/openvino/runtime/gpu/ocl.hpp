// Copyright (C) 2018-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

/**
 * @brief a header that defines wrappers for internal GPU plugin-specific
 * OpenCL context and OpenCL shared memory tensors
 *
 * @file openvino/runtime/gpu/ocl.hpp
 */
#pragma once

#include <memory>
#include <string>

#include "gpu/gpu_params.hpp"
#include "openvino/runtime/core.hpp"
#include "openvino/runtime/gpu/ocl_wrapper.hpp"
#include "openvino/runtime/remote_context.hpp"
#include "openvino/runtime/remote_tensor.hpp"

namespace ov {
namespace runtime {
namespace gpu {

/**
 * @brief Shortcut for defining a handle parameter
 */
using gpu_handle_param = void*;

/**
 * @brief This class represents an abstraction for GPU plugin remote tensor
 * which can be shared with user-supplied OpenCL buffer.
 * The plugin object derived from this class can be obtained with ClContext::create_tensor() call.
 * @note User can obtain OpenCL buffer handle from this class.
 */
class ClBufferTensor : public RemoteTensor {
public:
    /**
     * @brief Checks that type defined runtime paramters are presented in remote object
     * @param tensor a tensor to check
     */
    static void type_check(const Tensor& tensor) {
        RemoteTensor::type_check(
            tensor,
            {{GPU_PARAM_KEY(MEM_HANDLE), {}},
             {GPU_PARAM_KEY(SHARED_MEM_TYPE), {GPU_PARAM_VALUE(OCL_BUFFER), GPU_PARAM_VALUE(DX_BUFFER)}}});
    }

    /**
     * @brief Returns the underlying OpenCL memory object handle.
     * @return underlying OpenCL memory object handle
     */
    cl_mem get() {
        return static_cast<cl_mem>(get_params().at(GPU_PARAM_KEY(MEM_HANDLE)).as<gpu_handle_param>());
    }

    /**
     * @brief OpenCL memory handle conversion operator.
     * @return `cl_mem`
     */
    operator cl_mem() {
        return get();
    }

    /**
     * @brief Standard Khronos cl::Buffer wrapper conversion operator.
     * @return `cl::Buffer` object
     */
    operator cl::Buffer() {
        return cl::Buffer(get(), true);
    }
};

/**
 * @brief This class represents an abstraction for GPU plugin remote tensor
 * which can be shared with user-supplied OpenCL 2D Image.
 * The plugin object derived from this class can be obtained with ClContext::create_tensor() call.
 * @note User can obtain OpenCL image handle from this class.
 */
class ClImage2DTensor : public RemoteTensor {
public:
    /**
     * @brief Checks that type defined runtime paramters are presented in remote object
     * @param tensor a tensor to check
     */
    static void type_check(const Tensor& tensor) {
        RemoteTensor::type_check(
            tensor,
            {{GPU_PARAM_KEY(MEM_HANDLE), {}},
             {GPU_PARAM_KEY(SHARED_MEM_TYPE), {GPU_PARAM_VALUE(OCL_IMAGE2D), GPU_PARAM_VALUE(VA_SURFACE)}}});
    }

    /**
     * @brief Returns the underlying OpenCL memory object handle.
     * @return underlying OpenCL memory object handle
     */
    cl_mem get() {
        return static_cast<cl_mem>(get_params().at(GPU_PARAM_KEY(MEM_HANDLE)).as<gpu_handle_param>());
    }

    /**
     * @brief OpenCL memory handle conversion operator.
     * @return `cl_mem`
     */
    operator cl_mem() {
        return get();
    }

    /**
     * @brief Standard Khronos cl::Image2D wrapper conversion operator for the ClContext object.
     * @return `cl::Image2D` object
     */
    operator cl::Image2D() {
        return cl::Image2D(get(), true);
    }
};

/**
 * @brief This class represents an abstraction for GPU plugin remote context
 * which is shared with OpenCL context object.
 * The plugin object derived from this class can be obtained either with
 * ExecutableNetwork::get_context() or Core::create_context() calls.
 */
class ClContext : public RemoteContext {
    using RemoteContext::create_tensor;
    static constexpr const char* device_name = "GPU";

public:
    /**
     * @brief Checks that type defined runtime paramters are presented in remote object
     * @param remote_context remote context to check
     */
    static void type_check(const RemoteContext& remote_context) {
        RemoteContext::type_check(remote_context,
                                  {{GPU_PARAM_KEY(OCL_CONTEXT), {}},
                                   {GPU_PARAM_KEY(CONTEXT_TYPE), {GPU_PARAM_VALUE(OCL), GPU_PARAM_VALUE(VA_SHARED)}}});
    }

    /**
     * @brief Constructs context object from user-supplied OpenCL context handle
     * @param core A reference to OpenVINO Runtime Core object
     * @param ctx A OpenCL context to be used to create shared remote context
     */
    ClContext(Core& core, cl_context ctx) {
        ParamMap context_params = {{GPU_PARAM_KEY(CONTEXT_TYPE), GPU_PARAM_VALUE(OCL)},
                                   {GPU_PARAM_KEY(OCL_CONTEXT), static_cast<gpu_handle_param>(ctx)}};
        *this = core.create_context(device_name, context_params);
    }

    /**
     * @brief Constructs context object from user-supplied OpenCL context handle
     * @param core A reference to OpenVINO Runtime Core object
     * @param queue An OpenCL queue to be used to create shared remote context. Queue will be reused inside the plugin.
     * @note Only latency mode is supported for such context sharing case.
     */
    ClContext(Core& core, cl_command_queue queue) {
        cl_context ctx;
        auto res = clGetCommandQueueInfo(queue, CL_QUEUE_CONTEXT, sizeof(cl_context), &ctx, nullptr);
        if (res != CL_SUCCESS)
            IE_THROW() << "Can't get context from given opencl queue";
        ParamMap context_params = {{GPU_PARAM_KEY(CONTEXT_TYPE), GPU_PARAM_VALUE(OCL)},
                                   {GPU_PARAM_KEY(OCL_CONTEXT), static_cast<gpu_handle_param>(ctx)},
                                   {GPU_PARAM_KEY(OCL_QUEUE), static_cast<gpu_handle_param>(queue)}};
        *this = core.create_context(device_name, context_params);
    }

    /**
     * @brief Returns the underlying OpenCL context handle.
     * @return `cl_context`
     */
    cl_context get() {
        return static_cast<cl_context>(get_params().at(GPU_PARAM_KEY(OCL_CONTEXT)).as<gpu_handle_param>());
    }

    /**
     * @brief OpenCL context handle conversion operator for the ClContext object.
     * @return `cl_context`
     */
    operator cl_context() {
        return get();
    }

    /**
     * @brief Standard Khronos cl::Context wrapper conversion operator for the ClContext object.
     * @return `cl::Context` object
     */
    operator cl::Context() {
        return cl::Context(get(), true);
    }

    /**
     * @brief This function is used to construct a NV12 compound tensor object from two cl::Image2D wrapper objects.
     * The resulting compound contains two remote tensors for Y and UV planes of the surface.
     * @param nv12_image_plane_y cl::Image2D object containing Y plane data.
     * @param nv12_image_plane_uv cl::Image2D object containing UV plane data.
     * @return A pair of remote tensors for each plane
     */
    std::pair<ClImage2DTensor, ClImage2DTensor> create_tensor_nv12(const cl::Image2D& nv12_image_plane_y,
                                                                   const cl::Image2D& nv12_image_plane_uv) {
        size_t width = nv12_image_plane_y.getImageInfo<CL_IMAGE_WIDTH>();
        size_t height = nv12_image_plane_y.getImageInfo<CL_IMAGE_HEIGHT>();
        ParamMap tensor_params = {{GPU_PARAM_KEY(SHARED_MEM_TYPE), GPU_PARAM_VALUE(OCL_IMAGE2D)},
                                  {GPU_PARAM_KEY(MEM_HANDLE), static_cast<gpu_handle_param>(nv12_image_plane_y.get())}};
        auto y_tensor = create_tensor(element::u8, {1, 1, height, width}, tensor_params);
        tensor_params[GPU_PARAM_KEY(MEM_HANDLE)] = static_cast<gpu_handle_param>(nv12_image_plane_uv.get());
        auto uv_tensor = create_tensor(element::u8, {1, 2, height / 2, width / 2}, tensor_params);
        return std::make_pair(y_tensor, uv_tensor);
    }

    /**
     * @brief This function is used to obtain remote tensor object from user-supplied cl_mem object
     * @param type Tensor element type
     * @param shape Tensor shape
     * @param buffer A cl_mem object wrapped by a remote tensor
     * @return A remote tensor instance
     */
    ClBufferTensor create_tensor(const element::Type type, const Shape& shape, const cl_mem buffer) {
        ParamMap params = {{GPU_PARAM_KEY(SHARED_MEM_TYPE), GPU_PARAM_VALUE(OCL_BUFFER)},
                           {GPU_PARAM_KEY(MEM_HANDLE), static_cast<gpu_handle_param>(buffer)}};
        return create_tensor(type, shape, params);
    }

    /**
     * @brief This function is used to obtain remote tensor object from user-supplied cl::Buffer object
     * @param type Tensor element type
     * @param shape Tensor shape
     * @param buffer A cl::Buffer object wrapped by a remote tensor
     * @return A remote tensor instance
     */
    ClBufferTensor create_tensor(const element::Type type, const Shape& shape, const cl::Buffer& buffer) {
        return create_tensor(type, shape, buffer.get());
    }

    /**
     * @brief This function is used to obtain remote tensor object from user-supplied cl::Image2D object
     * @param type Tensor element type
     * @param shape Tensor shape
     * @param image A cl::Image2D object wrapped by a remote tensor
     * @return A remote tensor instance
     */
    ClImage2DTensor create_tensor(const element::Type type, const Shape& shape, const cl::Image2D& image) {
        ParamMap params = {{GPU_PARAM_KEY(SHARED_MEM_TYPE), GPU_PARAM_VALUE(OCL_IMAGE2D)},
                           {GPU_PARAM_KEY(MEM_HANDLE), static_cast<gpu_handle_param>(image.get())}};
        return create_tensor(type, shape, params);
    }
};
}  // namespace gpu
}  // namespace runtime
}  // namespace ov
