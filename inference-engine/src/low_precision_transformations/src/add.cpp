﻿// Copyright (C) 2018-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "low_precision/add.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ngraph/pattern/op/wrap_type.hpp>
#include "ngraph_ops/type_relaxed.hpp"

#include "low_precision/common/ie_lpt_exception.hpp"
#include "low_precision/network_helper.hpp"

namespace ngraph {
namespace pass {
namespace low_precision {

NGRAPH_RTTI_DEFINITION(AddTransformation, "AddTransformation", 0);

namespace {

std::shared_ptr<opset1::Subtract> replaceToSubtract(const std::shared_ptr<Node>& op) {
    // TODO: separate this part to standalone transformation: AddToSubtractTransformation
    // motivation:
    //    - single responsibility
    //    - keep AddTransformation and AddToSubtractTransformation transformations independent and optional
    const auto add = ov::as_type_ptr<opset1::Add>(op);
    if (add == nullptr) {
        return nullptr;
    }

    // TODO: use general way from getDequantization: is eltwise with Constant
    const int constBranchIndex = ov::is_type<opset1::Constant>(add->get_input_node_ptr(0)) ?
        0 :
        (ov::is_type<opset1::Constant>(add->get_input_node_ptr(1)) ? 1 : -1);
    if (constBranchIndex == -1) {
        return nullptr;
    }
    const size_t dataBranchIndex = constBranchIndex == 0 ? 1ul : 0;

    const auto parent = add->get_input_node_shared_ptr(dataBranchIndex);
    if (ov::is_type<opset1::Convolution>(parent) ||
        ov::is_type<opset1::GroupConvolution>(parent) ||
        ov::is_type<opset1::ConvolutionBackpropData>(parent) ||
        (ov::is_type<opset1::MatMul>(parent) &&
        (ov::is_type<opset1::Constant>(parent->get_input_node_ptr(0)) || ov::is_type<opset1::Constant>(parent->get_input_node_ptr(1))))) {
        return nullptr;
    }

    auto constant = fold<opset1::Negative>(add->input_value(constBranchIndex));
    auto constOutput = constant->output(0);

    const auto subtract = std::make_shared<op::TypeRelaxed<opset1::Subtract>>(
        std::vector<element::Type>{element::f32, element::f32},
        std::vector<element::Type>{ op->get_output_element_type(0) },
        ngraph::op::TemporaryReplaceOutputType(add->input_value(dataBranchIndex), element::f32).get(),
        ngraph::op::TemporaryReplaceOutputType(constOutput, element::f32).get(),
        add->get_autob());

    NetworkHelper::copyInfo(add, subtract);

    replace_node(add, subtract);
    return subtract;
}

std::shared_ptr<opset1::Subtract> fuseWithSubtract(const std::shared_ptr<Node>& op) {
    const auto add = ov::as_type_ptr<opset1::Add>(op);
    if ((add == nullptr) ||
        !ov::is_type<opset1::Subtract>(add->get_input_node_shared_ptr(0)) ||
        // TODO: use general way from getDequantization: is eltwise with Constant
        !ov::is_type<opset1::Constant>(add->get_input_node_shared_ptr(0)->get_input_node_shared_ptr(1))) {
        return nullptr;
    }

    const auto newSubConst = fold<opset1::Subtract>(
        add->get_input_node_shared_ptr(0)->input_value(1),
        add->input_value(1));

    const auto newSubtract = std::make_shared<op::TypeRelaxed<opset1::Subtract>>(
        std::vector<element::Type>{element::f32, element::f32},
        std::vector<element::Type>{ op->get_output_element_type(0) },
        ngraph::op::TemporaryReplaceOutputType(add->get_input_node_shared_ptr(0)->input_value(0), element::f32).get(),
        ngraph::op::TemporaryReplaceOutputType(newSubConst, element::f32).get());
    NetworkHelper::copyInfo(add, newSubtract);

    replace_node(add, newSubtract);
    return newSubtract;
}

} // namespace

AddTransformation::AddTransformation(const Params& params) : EltwiseBaseTransformation(params) {
    auto matcher = ngraph::pattern::wrap_type<opset1::Add>();

    ngraph::graph_rewrite_callback callback = [this](pattern::Matcher& m) {
        auto op = m.get_match_root();
        if (transformation_callback(op)) {
            return false;
        }
        return transform(*context, m);
    };

    auto m = std::make_shared<ngraph::pattern::Matcher>(matcher, "AddTransformation");
    this->register_matcher(m, callback);
}

bool AddTransformation::transform(TransformationContext& context, ngraph::pattern::Matcher &m) {
    std::shared_ptr<opset1::Add> op = ov::as_type_ptr<opset1::Add>(m.get_match_root());
    if ((op == nullptr) || (!canBeTransformed(context, op))) {
        return false;
    }

    NetworkHelper::normalizeDequantization(NetworkHelper::getDequantization(op, 0));
    NetworkHelper::normalizeDequantization(NetworkHelper::getDequantization(op, 1));

    std::shared_ptr<Node> addNode = NetworkHelper::separateInStandaloneBranch(op);
    std::shared_ptr<opset1::Add> add = ov::as_type_ptr<opset1::Add>(addNode);

    const int fullPathIndex = getNotEmpty(add);
    std::shared_ptr<Node> newMultiply;
    std::shared_ptr<Node> newAddOrSubtract;

    if (fullPathIndex == -1) {
        // swap constant multiply and add and possibly fuse to subtract
        const auto multiplyBranch = getMultiplyConstBranch(add);
        if (multiplyBranch.first != -1) {
            NetworkHelper::foldDequantization(add, multiplyBranch.first == 0 ? 1 : 0);
        } else {
            // constant folding on dequantization ops (for example: Convert on Subtract)
            NetworkHelper::foldDequantization(addNode, 0);
            NetworkHelper::foldDequantization(addNode, 1);
            return false;
        }

        newMultiply = NetworkHelper::swapMultiplyAndAdd(add, multiplyBranch.first);
        ngraph::copy_runtime_info({ add, newMultiply }, newMultiply);
        if (ov::is_type<opset1::Add>(newMultiply->get_input_node_shared_ptr(0))) {
            newAddOrSubtract = newMultiply->get_input_node_shared_ptr(0);

            auto subtract = fuseWithSubtract(newAddOrSubtract);
            if (subtract != nullptr) {
                newAddOrSubtract = subtract;
            }

            subtract = replaceToSubtract(newAddOrSubtract);
            if (subtract != nullptr) {
                newAddOrSubtract = subtract;
            }
        } else {
            newAddOrSubtract = newMultiply;
        }
    } else {
        // low precision with dequantization operations on at least one branch
        const int emptyPathIndex = fullPathIndex == 0 ? 1 : 0;

        if (updatePrecisions) {
            const FakeQuantizeDequantization dequantizationEmptyPath = NetworkHelper::getDequantization(add, emptyPathIndex);
            if (!dequantizationEmptyPath.empty() && !dequantizationEmptyPath.isLowPrecision()) {
                return false;
            }
        }

        const FakeQuantizeDequantization dequantizationEmptyPath = NetworkHelper::foldDequantization(addNode, emptyPathIndex);
        std::shared_ptr<Node> subtractEmptyPathValues;
        std::shared_ptr<Node> multiplyEmptyPathValues;
        std::tie(subtractEmptyPathValues, multiplyEmptyPathValues) = NetworkHelper::createEmptyValues(dequantizationEmptyPath, deqPrecision);

        const FakeQuantizeDequantization dequantizationFullPath = NetworkHelper::foldDequantization(addNode, fullPathIndex);
        std::shared_ptr<Node> subtractFullPathValues;
        std::shared_ptr<Node> multiplyFullPathValues;
        std::tie(subtractFullPathValues, multiplyFullPathValues) = NetworkHelper::createEmptyValues(dequantizationFullPath, deqPrecision);

        // calculation
        // before: Y = (SC1 * (X1 - SH1)) + (SC2 * (X2 - SH2))
        // after : Y = SC2 * ( SC1' * (X1 - SH1') + X2 ) , where :
        //         SC1' = SC1 / SC2
        //         SH1' = SH1 + SC2 * SH2 / SC1
        auto newSubtractFullPathValues = fold<opset1::Add>(
            subtractFullPathValues,
            fold<opset1::Divide>(
                fold<opset1::Multiply>(subtractEmptyPathValues, multiplyEmptyPathValues),
                multiplyFullPathValues));

        auto newMultiplyFullPathValues = fold<opset1::Divide>(multiplyFullPathValues, multiplyEmptyPathValues);

        if (NetworkHelper::isZeroConst(newSubtractFullPathValues)) {
            newSubtractFullPathValues = nullptr;
        }

        // graph update
        OutputVector inputs{ {}, {} };
        auto fullPathInput = dequantizationFullPath.convert == nullptr ? dequantizationFullPath.data : dequantizationFullPath.convert;

        // inputs[0]    inputs[1]
        //     \          /
        //      \        /
        //   newAddOrSubtract
        //          |
        //     newMultiply

        inputs[emptyPathIndex] = dequantizationEmptyPath.data;
        inputs[fullPathIndex] = std::make_shared<opset1::Multiply>(
            newSubtractFullPathValues == nullptr ?
                (fullPathInput.get_element_type() != newMultiplyFullPathValues->get_element_type() ?
                     std::make_shared<opset1::Convert>(fullPathInput, newMultiplyFullPathValues->get_element_type()) :
                     fullPathInput) :
                std::make_shared<opset1::Subtract>(
                    // precision on branch with dequantization operations can be different with dequantization precision,
                    // for example: FP16 model with FP32 dequantization
                    fullPathInput.get_element_type() != newSubtractFullPathValues->get_element_type() ?
                        std::make_shared<opset1::Convert>(fullPathInput, newSubtractFullPathValues->get_element_type()) :
                        fullPathInput,
                    newSubtractFullPathValues),
            newMultiplyFullPathValues);

        newAddOrSubtract = std::make_shared<op::TypeRelaxed<opset1::Add>>(
            std::vector<element::Type>{element::f32, element::f32}, std::vector<element::Type>{ element::f32 },
            ngraph::op::TemporaryReplaceOutputType(inputs[0], element::f32).get(),
            ngraph::op::TemporaryReplaceOutputType(inputs[1], element::f32).get());
        newMultiply = std::make_shared<op::TypeRelaxed<opset1::Multiply>>(
            std::vector<element::Type>{element::f32, element::f32}, std::vector<element::Type>{ add->get_output_element_type(0) },
            ngraph::op::TemporaryReplaceOutputType(newAddOrSubtract, element::f32).get(),
            ngraph::op::TemporaryReplaceOutputType(multiplyEmptyPathValues, element::f32).get());

        replace_node(add, newMultiply);
        NetworkHelper::copyInfo(add, newAddOrSubtract);
        ngraph::copy_runtime_info({ add, newMultiply }, newMultiply);
    }

    updateOutput(context, newMultiply, newAddOrSubtract);

    if (fullPathIndex != -1) {
        std::shared_ptr<Node> node = add;
        NetworkHelper::foldDequantization(node, fullPathIndex);
    }

    return true;
}

bool AddTransformation::canBeTransformed(const TransformationContext& context, std::shared_ptr<Node> layer) const {
    const FakeQuantizeDequantization dequantization1 = pass::low_precision::NetworkHelper::getDequantization(layer, 0ul);
    if (dequantization1.multiplyHasZeroOrDenormal()) {
        return false;
    }

    const FakeQuantizeDequantization dequantization2 = pass::low_precision::NetworkHelper::getDequantization(layer, 1ul);
    if (dequantization2.multiplyHasZeroOrDenormal()) {
        return false;
    }

    return EltwiseBaseTransformation::canBeTransformed(context, layer);
}

} // namespace low_precision
} // namespace pass
} // namespace ngraph
