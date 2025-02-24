// Copyright (C) 2018-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "preprocess_steps_impl.hpp"

#include "color_utils.hpp"
#include "ngraph/opsets/opset1.hpp"
#include "openvino/core/node.hpp"
#include "openvino/core/shape.hpp"
#include "openvino/op/nv12_to_bgr.hpp"
#include "openvino/op/nv12_to_rgb.hpp"

namespace ov {
namespace preprocess {

static Shape construct_mean_scale_shape(const Output<Node>& node,
                                        size_t values_size,
                                        const PreprocessingContext& context) {
    auto node_shape = node.get_partial_shape();
    auto node_rank = node_shape.rank();
    auto channels_index = get_and_check_channels_idx(context.layout(), node_shape);
    std::vector<std::size_t> v(node_rank.get_length(), 1);

    OPENVINO_ASSERT(node_shape[channels_index].is_dynamic() || node_shape[channels_index] == values_size,
                    "Number of channels and mean/values size mismatch: Channels = ",
                    node_shape[channels_index].get_length(),
                    ", mean/scale = ",
                    values_size);
    v[channels_index] = values_size;
    return {v};
}

void PreStepsList::add_scale_impl(const std::vector<float>& values) {
    m_actions.emplace_back([values](const std::vector<Output<Node>>& nodes,
                                    const std::shared_ptr<ov::Function>& function,
                                    PreprocessingContext& context) -> std::tuple<std::vector<Output<Node>>, bool> {
        OPENVINO_ASSERT(!nodes.empty(), "Internal error: Can't apply scale preprocessing for empty input.");
        OPENVINO_ASSERT(nodes.size() == 1,
                        "Can't apply scale preprocessing for multi-plane input. Suggesting to convert current "
                        "image to RGB/BGR color format using 'convert_color'");
        Shape shape;
        if (values.size() == 1) {
            shape = Shape{1};
        } else {
            shape = construct_mean_scale_shape(nodes[0].get_node_shared_ptr(), values.size(), context);
        }
        auto constant = op::v0::Constant::create(element::f32, shape, values);

        auto new_op = std::make_shared<op::v1::Divide>(nodes[0], constant);
        return std::make_tuple(std::vector<Output<Node>>{new_op}, false);
    });
}

void PreStepsList::add_mean_impl(const std::vector<float>& values) {
    m_actions.emplace_back([values](const std::vector<Output<Node>>& nodes,
                                    const std::shared_ptr<ov::Function>& function,
                                    PreprocessingContext& context) {
        OPENVINO_ASSERT(!nodes.empty(), "Internal error: Can't apply mean preprocessing for empty input.");
        OPENVINO_ASSERT(nodes.size() == 1,
                        "Can't apply scale preprocessing for multi-plane input. Suggesting to convert current "
                        "image to RGB/BGR color format using 'convert_color'");
        Shape shape;
        if (values.size() == 1) {
            shape = Shape{1};
        } else {
            shape = construct_mean_scale_shape(nodes[0], values.size(), context);
        }
        auto constant = op::v0::Constant::create(element::f32, shape, values);

        auto new_op = std::make_shared<op::v1::Subtract>(nodes[0], constant);
        return std::make_tuple(std::vector<Output<Node>>{new_op}, false);
    });
}

void PreStepsList::add_convert_impl(const element::Type& type) {
    m_actions.emplace_back([type](const std::vector<Output<Node>>& nodes,
                                  const std::shared_ptr<Function>& function,
                                  PreprocessingContext& ctxt) {
        OPENVINO_ASSERT(!nodes.empty(), "Internal error: Can't set element type for empty input.");
        std::vector<Output<Node>> res;
        element::Type t = type;
        if (t == element::Type{}) {
            t = ctxt.target_element_type();
        }
        bool convert_added = false;
        for (const auto& node : nodes) {
            OPENVINO_ASSERT(node.get_element_type().is_static(),
                            "Can't insert 'convert_element_type' for dynamic source tensor type.");
            if (t != node.get_element_type()) {
                auto convert = std::make_shared<op::v0::Convert>(node, t);
                res.emplace_back(convert);
                convert_added = true;
            } else {
                res.emplace_back(node);
            }
        }
        return std::make_tuple(res, convert_added);
    });
}

void PreStepsList::add_resize_impl(ResizeAlgorithm alg, int dst_height, int dst_width) {
    using InterpolateMode = op::v4::Interpolate::InterpolateMode;
    m_actions.emplace_back([alg, dst_width, dst_height](const std::vector<Output<Node>>& nodes,
                                                        const std::shared_ptr<Function>& function,
                                                        PreprocessingContext& ctxt) {
        OPENVINO_ASSERT(!nodes.empty(), "Internal error: Can't add resize for empty input.");
        OPENVINO_ASSERT(nodes.size() == 1,
                        "Can't resize multi-plane input. Suggesting to convert current image to "
                        "RGB/BGR color format using 'PreProcessSteps::convert_color'");
        auto to_mode = [](ResizeAlgorithm alg) -> InterpolateMode {
            switch (alg) {
            case ResizeAlgorithm::RESIZE_NEAREST:
                return InterpolateMode::NEAREST;
            case ResizeAlgorithm::RESIZE_CUBIC:
                return InterpolateMode::CUBIC;
            case ResizeAlgorithm::RESIZE_LINEAR:
            default:
                return InterpolateMode::LINEAR;
            }
        };
        auto node = nodes.front();
        auto layout = ctxt.layout();
        OPENVINO_ASSERT(ov::layout::has_height(layout) && ov::layout::has_width(layout),
                        "Can't add resize for layout without W/H specified. Use 'set_layout' API to define layout "
                        "of image data, like `NCHW`");
        auto node_rank = node.get_partial_shape().rank();
        OPENVINO_ASSERT(node_rank.is_static(), "Resize operation is not supported for fully dynamic shape");

        auto height_idx = static_cast<int64_t>(get_and_check_height_idx(layout, node.get_partial_shape()));
        auto width_idx = static_cast<int64_t>(get_and_check_width_idx(layout, node.get_partial_shape()));
        if (dst_height < 0 || dst_width < 0) {
            OPENVINO_ASSERT(ctxt.network_shape().rank().is_static(),
                            "Resize is not fully specified while target network shape is dynamic");
        }
        int new_image_width = dst_width < 0 ? static_cast<int>(ctxt.get_network_width_for_resize()) : dst_width;
        int new_image_height = dst_height < 0 ? static_cast<int>(ctxt.get_network_height_for_resize()) : dst_height;

        auto target_spatial_shape =
            op::v0::Constant::create<int64_t>(element::i64, Shape{2}, {new_image_height, new_image_width});
        auto scales = op::v0::Constant::create<float>(element::f32, Shape{2}, {1, 1});
        // In future consider replacing this to set of new OV operations like `getDimByName(node, "H")`
        // This is to allow specifying layout on 'evaluation' stage
        auto axes = op::v0::Constant::create<int64_t>(element::i64, Shape{2}, {height_idx, width_idx});

        op::v4::Interpolate::InterpolateAttrs attrs(to_mode(alg),
                                                    op::v4::Interpolate::ShapeCalcMode::SIZES,
                                                    {0, 0},
                                                    {0, 0});

        auto interp = std::make_shared<op::v4::Interpolate>(node, target_spatial_shape, scales, axes, attrs);
        return std::make_tuple(std::vector<Output<Node>>{interp}, true);
    });
}

void PreStepsList::add_convert_layout_impl(const Layout& layout) {
    m_actions.emplace_back([layout](const std::vector<Output<Node>>& nodes,
                                    const std::shared_ptr<Function>& function,
                                    PreprocessingContext& context) {
        OPENVINO_ASSERT(!nodes.empty(), "Internal error: Can't convert layout for empty input.");
        OPENVINO_ASSERT(nodes.size() == 1,
                        "Can't convert layout for multi-plane input. Suggesting to convert current image to "
                        "RGB/BGR color format using 'convert_color'");
        Layout dst_layout = layout.empty() ? context.target_layout() : layout;
        auto permutation = layout::find_permutation(context.layout(), nodes[0].get_partial_shape().rank(), dst_layout);
        if (permutation.empty()) {
            // No transpose is needed, just update layout
            if (!layout.empty()) {
                context.layout() = layout;
            }
            return std::make_tuple(nodes, false);
        }
        auto perm_constant = op::v0::Constant::create<int64_t>(element::i64, Shape{permutation.size()}, permutation);
        auto transpose = std::make_shared<op::v1::Transpose>(nodes[0], perm_constant);
        context.layout() = dst_layout;  // Update context's current layout
        return std::make_tuple(std::vector<Output<Node>>{transpose}, true);
    });
}

void PreStepsList::add_convert_layout_impl(const std::vector<uint64_t>& dims) {
    if (dims.empty()) {
        return;
    }
    m_layout_converts.emplace_front(dims);
    m_actions.emplace_back([dims](const std::vector<Output<Node>>& nodes,
                                  const std::shared_ptr<Function>& function,
                                  PreprocessingContext& context) {
        OPENVINO_ASSERT(!nodes.empty(), "Internal error: Can't convert layout for empty input.");
        OPENVINO_ASSERT(nodes.size() == 1,
                        "Can't convert layout for multi-plane input. Suggesting to convert current image to "
                        "RGB/BGR color format using 'convert_color'");
        auto new_layout = layout::apply_permutation(context.layout(), dims);
        auto perm_constant = op::v0::Constant::create<uint64_t>(element::u64, Shape{dims.size()}, dims);
        auto transpose = std::make_shared<op::v1::Transpose>(nodes[0], perm_constant);
        auto res = std::make_tuple(std::vector<Output<Node>>{transpose}, true);
        context.layout() = std::move(new_layout);  // Update context's current layout
        return res;
    });
}

void PreStepsList::add_convert_color_impl(const ColorFormat& dst_format) {
    m_actions.emplace_back([dst_format](const std::vector<Output<Node>>& nodes,
                                        const std::shared_ptr<Function>& function,
                                        PreprocessingContext& context) {
        if (context.color_format() == dst_format) {
            return std::make_tuple(nodes, false);
        }
        if (context.color_format() == ColorFormat::NV12_SINGLE_PLANE) {
            OPENVINO_ASSERT(nodes.size() == 1, "Internal error: single plane NV12 image can't have multiple inputs");
            std::shared_ptr<Node> convert;
            switch (dst_format) {
            case ColorFormat::RGB:
                convert = std::make_shared<op::v8::NV12toRGB>(nodes[0]);
                break;
            case ColorFormat::BGR:
                convert = std::make_shared<op::v8::NV12toBGR>(nodes[0]);
                break;
            default:
                OPENVINO_ASSERT(false,
                                "Unsupported conversion from NV12 to '",
                                color_format_name(dst_format),
                                "' format:");
            }
            context.color_format() = dst_format;
            return std::make_tuple(std::vector<Output<Node>>{convert}, true);
        } else if (context.color_format() == ColorFormat::NV12_TWO_PLANES) {
            OPENVINO_ASSERT(nodes.size() == 2, "Internal error: two-plane NV12 image must have exactly two inputs");
            std::shared_ptr<Node> convert;
            switch (dst_format) {
            case ColorFormat::RGB:
                convert = std::make_shared<op::v8::NV12toRGB>(nodes[0], nodes[1]);
                break;
            case ColorFormat::BGR:
                convert = std::make_shared<op::v8::NV12toBGR>(nodes[0], nodes[1]);
                break;
            default:
                OPENVINO_ASSERT(false,
                                "Unsupported conversion from NV12 to '",
                                color_format_name(dst_format),
                                "' format:");
            }
            context.color_format() = dst_format;
            return std::make_tuple(std::vector<Output<Node>>{convert}, true);
        }
        if ((context.color_format() == ColorFormat::RGB || context.color_format() == ColorFormat::BGR) &&
            (dst_format == ColorFormat::RGB || dst_format == ColorFormat::BGR)) {
            auto res = reverse_channels(nodes, function, context);
            context.color_format() = dst_format;
            return res;
        }
        OPENVINO_ASSERT(false,
                        "Source color format '",
                        color_format_name(context.color_format()),
                        "' is not convertible to any other");
    });
}

void PreStepsList::add_reverse_channels() {
    m_actions.emplace_back([](const std::vector<Output<Node>>& nodes,
                              const std::shared_ptr<Function>& function,
                              PreprocessingContext& context) {
        return reverse_channels(nodes, function, context);
    });
}

std::tuple<std::vector<Output<Node>>, bool> PreStepsList::reverse_channels(const std::vector<Output<Node>>& nodes,
                                                                           const std::shared_ptr<Function>& function,
                                                                           PreprocessingContext& context) {
    OPENVINO_ASSERT(nodes.size() == 1, "Internal error: can't reverse channels for multi-plane inputs");
    OPENVINO_ASSERT(ov::layout::has_channels(context.layout()),
                    "Layout ",
                    context.layout().to_string(),
                    " doesn't have `channels` dimension");
    auto channels_idx = ov::layout::channels_idx(context.layout());
    // Get shape of user's input tensor (e.g. Tensor[1, 3, 224, 224] -> {1, 3, 224, 224})
    auto shape_of = std::make_shared<ov::op::v0::ShapeOf>(nodes[0]);  // E.g. {1, 3, 224, 224}

    auto constant_chan_idx = op::v0::Constant::create(element::i32, {}, {channels_idx});  // E.g. 1
    auto constant_chan_axis = op::v0::Constant::create(element::i32, {}, {0});
    // Gather will return scalar with number of channels (e.g. 3)
    auto gather_channels_num = std::make_shared<op::v8::Gather>(shape_of, constant_chan_idx, constant_chan_axis);

    // Create Range from channels_num-1 to 0 (e.g. {2, 1, 0})
    auto const_minus1 = op::v0::Constant::create(element::i64, {}, {-1});
    auto channels_num_minus1 = std::make_shared<op::v1::Add>(gather_channels_num, const_minus1);  // E.g. 3-1=2
    // Add range
    auto range_to = op::v0::Constant::create(element::i64, {}, {-1});
    auto range_step = op::v0::Constant::create(element::i64, {}, {-1});
    // E.g. {2, 1, 0}
    auto range = std::make_shared<op::v4::Range>(channels_num_minus1, range_to, range_step, element::i32);

    // Gather slices in reverse order (indexes are specified by 'range' operation)
    auto constant_axis = op::v0::Constant::create(element::i32, {1}, {channels_idx});
    auto convert = std::make_shared<op::v8::Gather>(nodes[0], range, constant_axis);
    return std::make_tuple(std::vector<Output<Node>>{convert}, false);
}

//------------- Post processing ------
void PostStepsList::add_convert_impl(const element::Type& type) {
    m_actions.emplace_back([type](const Output<Node>& node, PostprocessingContext& ctxt) {
        element::Type t = type;
        if (t == element::Type{}) {
            t = ctxt.target_element_type();
        }
        if (t == node.get_node()->get_element_type()) {
            return std::make_tuple(node, false);
        }
        OPENVINO_ASSERT(
            !t.is_dynamic() && t != element::undefined,
            "Can't convert to dynamic/unknown element type, consider using of InputTensorInfo::set_element_type");
        auto convert = std::make_shared<op::v0::Convert>(node, t);
        return std::make_tuple(Output<Node>(convert), true);
    });
}

void PostStepsList::add_convert_layout_impl(const Layout& layout) {
    m_actions.emplace_back([layout](const Output<Node>& node, PostprocessingContext& context) {
        Layout dst_layout = layout.empty() ? context.target_layout() : layout;
        auto permutation = layout::find_permutation(context.layout(), node.get_partial_shape().rank(), dst_layout);
        if (permutation.empty()) {
            // No transpose is needed, just update layout
            if (!layout.empty()) {
                context.layout() = layout;
            }
            return std::make_tuple(node, false);
        }
        auto perm_constant = op::v0::Constant::create<int64_t>(element::i64, Shape{permutation.size()}, permutation);
        auto transpose = std::make_shared<op::v1::Transpose>(node, perm_constant);
        context.layout() = dst_layout;  // Update context's current layout
        return std::make_tuple(Output<Node>(transpose), true);
    });
}

void PostStepsList::add_convert_layout_impl(const std::vector<uint64_t>& dims) {
    if (dims.empty()) {
        return;
    }
    m_actions.emplace_back([dims](const Output<Node>& node, PostprocessingContext& context) {
        auto perm_constant = op::v0::Constant::create<uint64_t>(element::u64, Shape{dims.size()}, dims);
        auto new_layout = layout::apply_permutation(context.layout(), dims);
        auto transpose = std::make_shared<op::v1::Transpose>(node, perm_constant);
        auto res = std::make_tuple(Output<Node>(transpose), true);
        context.layout() = std::move(new_layout);  // Update context's current layout
        return res;
    });
}

}  // namespace preprocess
}  // namespace ov
