// Copyright (C) 2018-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "ie_plugin_config.hpp"
#include "behavior/config.hpp"

using namespace BehaviorTestsDefinitions;
namespace {
    const std::vector<InferenceEngine::Precision> netPrecisions = {
            InferenceEngine::Precision::FP32,
            InferenceEngine::Precision::FP16
    };

    const std::vector<std::map<std::string, std::string>> conf = {
            {}
    };

    const std::vector<std::map<std::string, std::string>> Configs = {
            {},
            {{InferenceEngine::PluginConfigParams::KEY_CPU_THROUGHPUT_STREAMS, InferenceEngine::PluginConfigParams::CPU_THROUGHPUT_AUTO}},
            {{InferenceEngine::PluginConfigParams::KEY_CPU_THROUGHPUT_STREAMS, InferenceEngine::PluginConfigParams::CPU_THROUGHPUT_NUMA}},
            {{InferenceEngine::PluginConfigParams::KEY_CPU_THROUGHPUT_STREAMS, "8"}},
            {{InferenceEngine::PluginConfigParams::KEY_CPU_BIND_THREAD, InferenceEngine::PluginConfigParams::NO}},
            {{InferenceEngine::PluginConfigParams::KEY_CPU_BIND_THREAD, InferenceEngine::PluginConfigParams::YES}},
            {{InferenceEngine::PluginConfigParams::KEY_DYN_BATCH_LIMIT, "10"}}
    };

    const std::vector<std::map<std::string, std::string>> IncorrectConfigs = {
            {{InferenceEngine::PluginConfigParams::KEY_CPU_THROUGHPUT_STREAMS, "OFF"}},
            {{InferenceEngine::PluginConfigParams::KEY_CPU_BIND_THREAD, "OFF"}},
            {{InferenceEngine::PluginConfigParams::KEY_DYN_BATCH_LIMIT, "NAN"}}
    };

    const std::vector<std::map<std::string, std::string>> MultiCorrectConf = {
            {{InferenceEngine::MultiDeviceConfigParams::KEY_MULTI_DEVICE_PRIORITIES , CommonTestUtils::DEVICE_CPU}}
    };

    const std::vector<std::map<std::string, std::string>> MultiIncorrectConfigs = {
            {{InferenceEngine::MultiDeviceConfigParams::KEY_MULTI_DEVICE_PRIORITIES , CommonTestUtils::DEVICE_CPU},
                    {InferenceEngine::PluginConfigParams::KEY_CPU_THROUGHPUT_STREAMS, "OFF"}},
            {{InferenceEngine::MultiDeviceConfigParams::KEY_MULTI_DEVICE_PRIORITIES , CommonTestUtils::DEVICE_CPU},
                    {InferenceEngine::PluginConfigParams::KEY_CPU_BIND_THREAD, "OFF"}},
            {{InferenceEngine::MultiDeviceConfigParams::KEY_MULTI_DEVICE_PRIORITIES , CommonTestUtils::DEVICE_CPU},
                    {InferenceEngine::PluginConfigParams::KEY_DYN_BATCH_LIMIT, "NAN"}}
    };

    INSTANTIATE_TEST_SUITE_P(smoke_BehaviorTests, CorrectConfigTests,
            ::testing::Combine(
            ::testing::ValuesIn(netPrecisions),
            ::testing::Values(CommonTestUtils::DEVICE_CPU),
            ::testing::ValuesIn(Configs)),
            CorrectConfigTests::getTestCaseName);

    INSTANTIATE_TEST_SUITE_P(smoke_Multi_BehaviorTests, CorrectConfigTests,
            ::testing::Combine(
            ::testing::ValuesIn(netPrecisions),
            ::testing::Values(CommonTestUtils::DEVICE_MULTI),
            ::testing::ValuesIn(MultiCorrectConf)),
            CorrectConfigTests::getTestCaseName);

    INSTANTIATE_TEST_SUITE_P(smoke_Auto_BehaviorTests, CorrectConfigTests,
            ::testing::Combine(
            ::testing::ValuesIn(netPrecisions),
            ::testing::Values(CommonTestUtils::DEVICE_AUTO),
            ::testing::ValuesIn(MultiCorrectConf)),
            CorrectConfigTests::getTestCaseName);

    INSTANTIATE_TEST_SUITE_P(smoke_BehaviorTests, CorrectConfigAPITests,
            ::testing::Combine(
            ::testing::ValuesIn(netPrecisions),
            ::testing::Values(CommonTestUtils::DEVICE_CPU),
            ::testing::ValuesIn(conf)),
            CorrectConfigAPITests::getTestCaseName);

    INSTANTIATE_TEST_SUITE_P(smoke_Multi_BehaviorTests, CorrectConfigAPITests,
            ::testing::Combine(
            ::testing::ValuesIn(netPrecisions),
            ::testing::Values(CommonTestUtils::DEVICE_MULTI),
            ::testing::ValuesIn(MultiCorrectConf)),
            CorrectConfigAPITests::getTestCaseName);

    INSTANTIATE_TEST_SUITE_P(smoke_BehaviorTests, IncorrectConfigTests,
            ::testing::Combine(
            ::testing::ValuesIn(netPrecisions),
            ::testing::Values(CommonTestUtils::DEVICE_CPU),
            ::testing::ValuesIn(IncorrectConfigs)),
            IncorrectConfigTests::getTestCaseName);

    INSTANTIATE_TEST_SUITE_P(smoke_Multi_BehaviorTests, IncorrectConfigTests,
            ::testing::Combine(
            ::testing::ValuesIn(netPrecisions),
            ::testing::Values(CommonTestUtils::DEVICE_MULTI),
            ::testing::ValuesIn(MultiIncorrectConfigs)),
            IncorrectConfigTests::getTestCaseName);

    INSTANTIATE_TEST_SUITE_P(smoke_Auto_BehaviorTests, IncorrectConfigTests,
            ::testing::Combine(
            ::testing::ValuesIn(netPrecisions),
            ::testing::Values(CommonTestUtils::DEVICE_AUTO),
            ::testing::ValuesIn(MultiIncorrectConfigs)),
            IncorrectConfigTests::getTestCaseName);

    INSTANTIATE_TEST_SUITE_P(smoke_BehaviorTests, IncorrectConfigAPITests,
            ::testing::Combine(
            ::testing::ValuesIn(netPrecisions),
            ::testing::Values(CommonTestUtils::DEVICE_CPU),
            ::testing::ValuesIn(IncorrectConfigs)),
            IncorrectConfigAPITests::getTestCaseName);

    INSTANTIATE_TEST_SUITE_P(smoke_Multi_BehaviorTests, IncorrectConfigAPITests,
            ::testing::Combine(
            ::testing::ValuesIn(netPrecisions),
            ::testing::Values(CommonTestUtils::DEVICE_MULTI),
            ::testing::ValuesIn(MultiIncorrectConfigs)),
            IncorrectConfigAPITests::getTestCaseName);

    INSTANTIATE_TEST_SUITE_P(smoke_Auto_BehaviorTests, IncorrectConfigAPITests,
                            ::testing::Combine(
                                ::testing::ValuesIn(netPrecisions),
                                ::testing::Values(CommonTestUtils::DEVICE_AUTO),
                                ::testing::ValuesIn(MultiIncorrectConfigs)),
                            IncorrectConfigAPITests::getTestCaseName);

} // namespace