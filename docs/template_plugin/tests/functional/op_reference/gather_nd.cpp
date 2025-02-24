// Copyright (C) 2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include <gtest/gtest.h>

#include "openvino/op/gather_nd.hpp"
#include "base_reference_test.hpp"

using namespace reference_tests;
using namespace ov;

namespace {
struct GatherNDParams {
    GatherNDParams(
        const Tensor& dataTensor, const Tensor& indicesTensor, int64_t batchDims,
        const Tensor& expectedTensor, const std::string& testcaseName = "") :
        dataTensor(dataTensor), indicesTensor(indicesTensor), batchDims(batchDims),
        expectedTensor(expectedTensor), testcaseName(testcaseName) {}

    Tensor dataTensor;
    Tensor indicesTensor;
    int32_t batchDims;
    Tensor expectedTensor;
    std::string testcaseName;
};

class ReferenceGatherNDTest : public testing::TestWithParam<GatherNDParams>, public CommonReferenceTest {
public:
    void SetUp() override {
        auto params = GetParam();
        function = CreateFunction(params);
        inputData = {params.dataTensor.data, params.indicesTensor.data};
        refOutData = {params.expectedTensor.data};
    }

    static std::string getTestCaseName(const testing::TestParamInfo<GatherNDParams>& obj) {
        auto param = obj.param;
        std::ostringstream result;
        result << "dType=" << param.dataTensor.type;
        result << "_dShape=" << param.dataTensor.shape;
        result << "_aType=" << param.indicesTensor.type;
        result << "_aShape=" << param.indicesTensor.shape;
        result << "_bDims=" << param.batchDims;
        result << "_eType=" << param.expectedTensor.type;
        if (param.testcaseName != "") {
            result << "_eShape=" << param.expectedTensor.shape;
            result << "_=" << param.testcaseName;
        } else {
            result << "_eShape=" << param.expectedTensor.shape;
        }
        return result.str();
    }

private:
    static std::shared_ptr<Function> CreateFunction(const GatherNDParams& params) {
        std::shared_ptr<Function> function;
        const auto data = std::make_shared<op::v0::Parameter>(params.dataTensor.type,
                                                              PartialShape{params.dataTensor.shape});
        const auto indices = std::make_shared<op::v0::Parameter>(params.indicesTensor.type,
                                                                 PartialShape{params.indicesTensor.shape});
        std::shared_ptr<op::v5::GatherND> gatherElement;
        if (params.batchDims == 0) {
            gatherElement = std::make_shared<op::v5::GatherND>(data, indices);
        } else {
            gatherElement = std::make_shared<op::v5::GatherND>(data, indices, params.batchDims);
        }
        function = std::make_shared<ov::Function>(NodeVector {gatherElement}, ParameterVector {data, indices});
        return function;
    }
};

TEST_P(ReferenceGatherNDTest, CompareWithRefs) {
    Exec();
}

template <element::Type_t IN_ET>
std::vector<GatherNDParams> generateParams() {
    using T = typename element_type_traits<IN_ET>::value_type;
    std::vector<GatherNDParams> params {
        GatherNDParams(
            Tensor(IN_ET, {3, 3}, std::vector<T>{10, 11, 12, 13, 14, 15, 16, 17, 18}),
            Tensor(element::i32, {2}, std::vector<int32_t>{1, 2}),
            0,
            Tensor(IN_ET, {}, std::vector<T>{15}),
            "gather_nd_single_indices"),
        GatherNDParams(
            Tensor(IN_ET, {2, 2}, std::vector<T>{10, 11, 12, 13}),
            Tensor(element::i32, {2, 2}, std::vector<int32_t>{0, 0, 1, 1}),
            0,
            Tensor(IN_ET, {2}, std::vector<T>{10, 13}),
            "gather_nd_scalar_from_2d"),
        GatherNDParams(
            Tensor(IN_ET, {2, 2}, std::vector<T>{10, 11, 12, 13}),
            Tensor(element::i32, {2, 1}, std::vector<int32_t>{1, 0}),
            0,
            Tensor(IN_ET, {2, 2}, std::vector<T>{12, 13, 10, 11}),
            "gather_nd_1d_from_2d"),
        GatherNDParams(
            Tensor(IN_ET, {2, 2, 2}, std::vector<T>{10, 11, 12, 13, 20, 21, 22, 23}),
            Tensor(element::i32, {2, 3}, std::vector<int32_t>{0, 0, 1, 1, 0, 1}),
            0,
            Tensor(IN_ET, {2}, std::vector<T>{11, 21}),
            "gather_nd_scalar_from_3d"),
        GatherNDParams(
            Tensor(IN_ET, {2, 2, 2}, std::vector<T>{10, 11, 12, 13, 20, 21, 22, 23}),
            Tensor(element::i32, {2, 2}, std::vector<int32_t>{0, 1, 1, 0}),
            0,
            Tensor(IN_ET, {2, 2}, std::vector<T>{12, 13, 20, 21}),
            "gather_nd_1d_from_3d"),
        GatherNDParams(
            Tensor(IN_ET, {2, 2, 2}, std::vector<T>{10, 11, 12, 13, 20, 21, 22, 23}),
            Tensor(element::i32, {1, 1}, std::vector<int32_t>{1}),
            0,
            Tensor(IN_ET, {1, 2, 2}, std::vector<T>{20, 21, 22, 23}),
            "gather_nd_2d_from_3d"),
        GatherNDParams(
            Tensor(IN_ET, {2, 2}, std::vector<T>{10, 11, 12, 13}),
            Tensor(element::i32, {2, 1, 2}, std::vector<int32_t>{0, 0, 0, 1}),
            0,
            Tensor(IN_ET, {2, 1}, std::vector<T>{10, 11}),
            "gather_nd_batch_scalar_from_2d"),
        GatherNDParams(
            Tensor(IN_ET, {2, 2}, std::vector<T>{10, 11, 12, 13}),
            Tensor(element::i32, {2, 1, 1}, std::vector<int32_t>{1, 0}),
            0,
            Tensor(IN_ET, {2, 1, 2}, std::vector<T>{12, 13, 10, 11}),
            "gather_nd_batch_1d_from_2d"),
        GatherNDParams(
            Tensor(IN_ET, {2, 2, 2}, std::vector<T>{10, 11, 12, 13, 20, 21, 22, 23}),
            Tensor(element::i32, {2, 2, 3}, std::vector<int32_t>{0, 0, 1, 1, 0, 1, 0, 1, 1, 1, 1, 0}),
            0,
            Tensor(IN_ET, {2, 2}, std::vector<T>{11, 21, 13, 22}),
            "gather_nd_batch_scalar_from_3d"),
        GatherNDParams(
            Tensor(IN_ET, {2, 2, 2}, std::vector<T>{10, 11, 12, 13, 20, 21, 22, 23}),
            Tensor(element::i32, {2, 2, 2}, std::vector<int32_t>{0, 1, 1, 0, 0, 0, 1, 1}),
            0,
            Tensor(IN_ET, {2, 2, 2}, std::vector<T>{12, 13, 20, 21, 10, 11, 22, 23}),
            "gather_nd_batch_1d_from_3d"),
        GatherNDParams(
            Tensor(IN_ET, {2, 2, 2}, std::vector<T>{10, 11, 12, 13, 20, 21, 22, 23}),
            Tensor(element::i32, {2, 2, 2}, std::vector<int32_t>{0, -1, -1, 0, 0, 0, 1, 1}),
            0,
            Tensor(IN_ET, {2, 2, 2}, std::vector<T>{12, 13, 20, 21, 10, 11, 22, 23}),
            "gather_nd_batch_1d_from_3d_negative"),
        GatherNDParams(
            Tensor(IN_ET, {2, 2, 2}, std::vector<T>{10, 11, 12, 13, 20, 21, 22, 23}),
            Tensor(element::i32, {2, 1, 1}, std::vector<int32_t>{1, 0}),
            0,
            Tensor(IN_ET, {2, 1, 2, 2}, std::vector<T>{20, 21, 22, 23, 10, 11, 12, 13}),
            "gather_nd_batch_2d_from_3d"),
        GatherNDParams(
            Tensor(IN_ET, {2, 3, 4}, std::vector<T>{1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                                    13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24}),
            Tensor(element::i32, {2, 1}, std::vector<int32_t>{1, 0}),
            1,
            Tensor(IN_ET, {2, 4}, std::vector<T>{5, 6, 7, 8, 13, 14, 15, 16}),
            "gather_nd_batch_dims1"),
        GatherNDParams(
            Tensor(IN_ET, {2, 3, 4, 2}, std::vector<T>{
                1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
                33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48}),
            Tensor(element::i32, {2, 3, 3, 2}, std::vector<int32_t>{
                1, 0, 3, 1, 2, 1, 0, 1, 1, 1, 2, 0, 3, 0, 3, 1, 2, 1,
                2, 0, 1, 1, 3, 1, 1, 1, 2, 0, 2, 0, 0, 0, 3, 1, 3, 1}),
            2,
            Tensor(IN_ET, {6, 3}, std::vector<T>{
                3, 8, 6, 10, 12, 13, 23, 24, 22, 29, 28, 32, 36, 37, 37, 41, 48, 48}),
            "gather_nd_batch_dims2"),
        GatherNDParams(
            Tensor(IN_ET, {2, 3, 4}, std::vector<T>{1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                                    13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24}),
            Tensor(element::i32, {2, 3, 1, 1}, std::vector<int32_t>{1, 0, 2, 0, 2, 2}),
            2,
            Tensor(IN_ET, {6, 1}, std::vector<T>{2, 5, 11, 13, 19, 23}),
            "gather_nd_batch_dims2_lead_dims"),
    };
    return params;
}

std::vector<GatherNDParams> generateCombinedParams() {
    const std::vector<std::vector<GatherNDParams>> generatedParams {
        generateParams<element::Type_t::i8>(),
        generateParams<element::Type_t::i16>(),
        generateParams<element::Type_t::i32>(),
        generateParams<element::Type_t::i64>(),
        generateParams<element::Type_t::u8>(),
        generateParams<element::Type_t::u16>(),
        generateParams<element::Type_t::u32>(),
        generateParams<element::Type_t::u64>(),
        generateParams<element::Type_t::bf16>(),
        generateParams<element::Type_t::f16>(),
        generateParams<element::Type_t::f32>(),
        generateParams<element::Type_t::f64>(),
    };
    std::vector<GatherNDParams> combinedParams;

    for (const auto& params : generatedParams) {
        combinedParams.insert(combinedParams.end(), params.begin(), params.end());
    }
    return combinedParams;
}

INSTANTIATE_TEST_SUITE_P(smoke_GatherND_With_Hardcoded_Refs, ReferenceGatherNDTest,
    testing::ValuesIn(generateCombinedParams()), ReferenceGatherNDTest::getTestCaseName);
} // namespace