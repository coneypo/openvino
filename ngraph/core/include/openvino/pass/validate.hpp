// Copyright (C) 2018-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "openvino/core/core_visibility.hpp"
#include "openvino/pass/pass.hpp"

namespace ov {
namespace pass {
/// \brief The Validate pass performs sanity checks on attributes and inputs, and
/// computes output shapes and element types for all computation nodes in a given
/// computation graph.
///
/// \details The verification and inference is done via invoking each node's specific
/// implementation of \link ov::Node::validate_and_infer_types() \endlink function.
///
/// By default, the \ref ov::pass::Manager runs this pass after executing every
/// optimization pass. This is to ensure that any update to the graph by an optimization
/// pass does not break the shape and data type requirement on a computation node.
/// This default validation run can be changed via calling the
/// \link ov::pass::Manager::set_per_pass_validation(bool) \endlink function.
class OPENVINO_API Validate : public FunctionPass {
public:
    OPENVINO_RTTI("ov::pass::Validate");

    Validate() : FunctionPass() {}
    bool run_on_function(std::shared_ptr<ov::Function> f) override;
};
}  // namespace pass
}  // namespace ov
