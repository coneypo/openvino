// Copyright (C) 2018-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include <gtest/gtest.h>

#include "common_test_utils/test_common.hpp"
#include <ngraph/function.hpp>
#include "common_test_utils/ngraph_test_utils.hpp"

#include <transformations/init_node_info.hpp>
#include "ngraph/opsets/opset1.hpp"
#include "ngraph/opsets/opset5.hpp"
#include "ngraph/opsets/opset8.hpp"
#include <ngraph/pass/constant_folding.hpp>

using namespace testing;
using namespace std;
using namespace ngraph;

// If doesn't have evaluate methods
TEST(TransformationTests, DISABLED_if_constant_folding) {
    std::shared_ptr<ngraph::Function> fun(nullptr);
    {
        auto cond = std::make_shared<ngraph::opset5::Constant>(element::boolean, Shape{ 1 }, false);
        auto A1 = std::make_shared<ngraph::opset5::Constant>(element::f32, Shape{ 1 }, 37.0);
        auto A2 = std::make_shared<ngraph::opset5::Constant>(element::f32, Shape{ 1 }, 45.0);
        auto B1 = std::make_shared<ngraph::opset5::Constant>(element::f32, Shape{ 1 }, 10.0);
        auto B2 = std::make_shared<ngraph::opset5::Constant>(element::f32, Shape{ 1 }, 3.0);
        auto Xt = make_shared<op::Parameter>(element::f32, PartialShape::dynamic());
        auto Yt = make_shared<op::Parameter>(element::f32, PartialShape::dynamic());
        auto Xe = make_shared<op::Parameter>(element::f32, PartialShape::dynamic());
        auto Ye = make_shared<op::Parameter>(element::f32, PartialShape::dynamic());
        auto a_add = std::make_shared<op::v1::Add>(Xt, Yt);
        auto b_pow = std::make_shared<op::v1::Power>(Xe, Ye);
        auto then_res = std::make_shared<op::Result>(a_add);
        auto then_body = make_shared<ngraph::Function>(OutputVector{ then_res }, ParameterVector{ Xt, Yt });
        auto else_res = std::make_shared<op::Result>(b_pow);
        auto else_body = make_shared<ngraph::Function>(OutputVector{ else_res }, ParameterVector{ Xe, Ye });
        auto if_op = make_shared<op::v8::If>(cond);
        if_op->set_then_body(then_body);
        if_op->set_else_body(else_body);
        if_op->set_input(A1, Xt, nullptr);
        if_op->set_input(A2, Yt, nullptr);
        if_op->set_input(B1, nullptr, Xe);
        if_op->set_input(B2, nullptr, Ye);
        auto if_res = if_op->set_output(then_res, else_res);
        auto param_add = make_shared<op::Parameter>(element::f32, Shape{ 1 });
        auto add = make_shared<op::v1::Add>(if_res, param_add);
        auto add_res = make_shared<op::Result>(add);
        fun = make_shared<Function>(OutputVector{ add_res }, ParameterVector{ param_add });
        ngraph::pass::ConstantFolding().run_on_function(fun);
    }
    std::shared_ptr<ngraph::Function> f_ref(nullptr);
    {
        auto constant_folding_if = make_shared<ngraph::opset5::Constant>(element::f32, Shape{ 1 }, 1000.0f);
        auto param_add = make_shared<op::Parameter>(element::f32, Shape{ 1 });
        auto add = make_shared<op::v1::Add>(constant_folding_if, param_add);
        auto add_res = make_shared<op::Result>(add);
        f_ref = std::make_shared<ngraph::Function>(ngraph::NodeVector{ add_res }, ngraph::ParameterVector{ param_add });
    }

    auto res = compare_functions(fun, f_ref);
    ASSERT_TRUE(res.first) << res.second;
}
