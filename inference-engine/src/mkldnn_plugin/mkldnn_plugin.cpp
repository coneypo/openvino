// Copyright (C) 2018-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "ie_metric_helpers.hpp"
#include "mkldnn_plugin.h"
#include "mkldnn_extension_mngr.h"
#include "mkldnn_weights_cache.hpp"
#include "mkldnn_itt.h"
#include "mkldnn_serialize.h"

#include <threading/ie_executor_manager.hpp>
#include <memory>
#include <ie_plugin_config.hpp>
#include <cpp_interfaces/interface/ie_internal_plugin_config.hpp>
#include <ie_icore.hpp>
#include <fstream>
#include <vector>
#include <tuple>
#include <unordered_set>
#include <ie_system_conf.h>
#include <nodes/list.hpp>
#include <ie_ngraph_utils.hpp>

#include <transformations/opset_conversions/convert_opset3_to_opset2.hpp>
#include <transformations/opset_conversions/convert_opset2_to_opset1.hpp>

#include <transformations/common_optimizations/common_optimizations.hpp>
#include <transformations/common_optimizations/weights_dequantize_to_fake_quantize.hpp>
#include "transformations/common_optimizations/convert_quantize_dequantize.hpp"
#include <transformations/common_optimizations/nop_elimination.hpp>
#include <transformations/op_conversions/convert_depth_to_space.hpp>
#include <transformations/op_conversions/convert_shuffle_channels3.hpp>
#include <transformations/op_conversions/convert_space_to_depth.hpp>
#include <transformations/op_conversions/convert_gelu.hpp>
#include <transformations/op_conversions/convert_gather_downgrade.hpp>
#include <transformations/op_conversions/convert_gather_upgrade.hpp>
#include <transformations/op_conversions/gelu7_downgrade.hpp>
#include <transformations/op_conversions/hswish_decomposition.hpp>
#include <transformations/op_conversions/hsigmoid_decomposition.hpp>
#include <transformations/op_conversions/mvn6_decomposition.hpp>
#include <transformations/op_conversions/normalize_l2_decomposition.hpp>
#include <transformations/op_conversions/reduce_l1_decomposition.hpp>
#include <transformations/op_conversions/reduce_l2_decomposition.hpp>
#include <transformations/op_conversions/softplus_decomposition.hpp>
#include <transformations/op_conversions/convert_space_to_batch.hpp>
#include <transformations/op_conversions/convert_batch_to_space.hpp>
#include <transformations/op_conversions/convert_sequences_to_tensor_iterator.hpp>
#include <transformations/op_conversions/convert_subtract.hpp>
#include <transformations/op_conversions/softmax_decomposition.hpp>
#include <transformations/control_flow/unroll_tensor_iterator.hpp>
#include <transformations/op_conversions/convert_mod.hpp>
#include <transformations/op_conversions/convert_ti_to_sequences.hpp>
#include <transformations/op_conversions/lstm_cell_decomposition.hpp>
#include <transformations/op_conversions/rnn_cell_decomposition.hpp>
#include <transformations/op_conversions/gru_cell_decomposition.hpp>
#include <transformations/op_conversions/log_softmax_decomposition.hpp>
#include <transformations/op_conversions/convert_interpolate1_to_interpolate4.hpp>
#include <transformations/op_conversions/simplify_ctc_greedy_decoder_seq_len.hpp>
#include <transformations/op_conversions/convert_previous_nms_to_nms_5.hpp>
#include <transformations/op_conversions/convert_nms_to_nms_ie_internal.hpp>
#include <transformations/op_conversions/convert_multiclass_nms_to_multiclass_nms_ie.hpp>
#include <transformations/op_conversions/convert_matrix_nms_to_matrix_nms_ie.hpp>
#include <transformations/op_conversions/convert_deformable_conv_v8_to_v1.hpp>
#include <transformations/smart_reshape/matmul_sr.hpp>
#include <transformations/op_conversions/convert_minimum_to_power_and_max.hpp>
#include <transformations/convert_precision.hpp>
#include <transformations/init_node_info.hpp>
#include <transformations/rt_info/fused_names_attribute.hpp>
#include <transformations/op_conversions/fq_decomposition.hpp>
#include <transformations/utils/utils.hpp>

#include <ngraph/opsets/opset1.hpp>
#include <ngraph/opsets/opset2.hpp>
#include <ngraph/opsets/opset3.hpp>
#include <ngraph/opsets/opset4.hpp>
#include <ngraph/opsets/opset6.hpp>
#include <ngraph/op/util/op_types.hpp>
#include <ngraph/pass/manager.hpp>
#include <ngraph/graph_util.hpp>

#include <transformations/common_optimizations/lin_op_sequence_fusion.hpp>

#include <transformations/low_precision/disable_convert_constant_folding_on_const_path.hpp>
#include <low_precision/common/operation_per_tensor_quantization_restriction.hpp>
#include <low_precision/convert_subtract_constant.hpp>
#include <low_precision/convolution.hpp>
#include <low_precision/convolution_backprop_data.hpp>
#include <low_precision/layer_transformation.hpp>
#include <low_precision/low_precision.hpp>
#include <low_precision/multiply_to_group_convolution.hpp>
#include <low_precision/network_helper.hpp>
#include "openvino/runtime/core.hpp"

#include <ie_algorithm.hpp>
#include "performance_heuristics.hpp"

#include "nodes/mkldnn_mvn_node.h"
#include "nodes/mkldnn_fake_quantize_node.h"
#include "nodes/mkldnn_normalize_node.h"
#include "ngraph_transformations/convert_to_cpu_specific_opset.hpp"
#include "transformations/smart_reshape/smart_reshape.hpp"

#if !defined(__arm__) && !defined(_M_ARM) && !defined(__aarch64__) && !defined(_M_ARM64)
# ifdef _WIN32
#  include <intrin.h>
#  include <windows.h>
# else
#  include <cpuid.h>
# endif
#endif

using namespace MKLDNNPlugin;
using namespace InferenceEngine;

Engine::Engine() {
    _pluginName = "CPU";
    extensionManager->AddExtension(std::make_shared<Extensions::Cpu::MKLDNNExtensions>());
}

Engine::~Engine() {
    ExecutorManager::getInstance()->clear("CPU");
    ExecutorManager::getInstance()->clear("CPUStreamsExecutor");
    ExecutorManager::getInstance()->clear("CPUCallbackExecutor");
}

static void TransformationUpToCPUSpecificOpSet(std::shared_ptr<ngraph::Function> nGraphFunc, const bool _enableLPT) {
    ngraph::pass::Manager manager;
    manager.register_pass<ngraph::pass::InitNodeInfo>();

    const bool useLpt =
            _enableLPT &&
        ngraph::pass::low_precision::LowPrecision::isFunctionQuantized(nGraphFunc);
    if (useLpt) {
        manager.register_pass<ngraph::pass::DisableConvertConstantFoldingOnConstPath>(
            std::vector<ngraph::element::Type>{ ngraph::element::i8, ngraph::element::u8, ngraph::element::i4, ngraph::element::u4 });
    }

    auto get_convert_precisions = []() {
        precisions_array array = {
            {ngraph::element::i64,     ngraph::element::i32},
            {ngraph::element::u64,     ngraph::element::i32},
            {ngraph::element::i16,     ngraph::element::i32},
            {ngraph::element::u16,     ngraph::element::i32},
            {ngraph::element::u32,     ngraph::element::i32},
            {ngraph::element::f64,     ngraph::element::f32},
            {ngraph::element::f16,     ngraph::element::f32},
            {ngraph::element::boolean, ngraph::element::u8},
            {ngraph::element::i4,      ngraph::element::i8},
            {ngraph::element::u4,      ngraph::element::u8}
        };

        if (!with_cpu_x86_avx512_core())
            array.push_back({ngraph::element::bf16, ngraph::element::f32});

        return array;
    };

    static const auto precisions = get_convert_precisions();

    manager.register_pass<ngraph::pass::CommonOptimizations>();
    manager.register_pass<ngraph::pass::ConvertRNNSequenceToTensorIterator>();
    manager.register_pass<ngraph::pass::ConvertGRUSequenceToTensorIterator>();
    manager.register_pass<ngraph::pass::ConvertLSTMSequenceToTensorIterator>();
    manager.register_pass<ngraph::pass::ConvertOpSet3ToOpSet2>();
    manager.register_pass<ngraph::pass::ConvertOpSet2ToOpSet1>();
    manager.register_pass<ngraph::pass::ConvertTensorIteratorToGRUSequence>();
    manager.register_pass<ngraph::pass::ConvertTensorIteratorToLSTMSequence>();
    manager.register_pass<ngraph::pass::ConvertTensorIteratorToRNNSequence>();
    manager.register_pass<ngraph::pass::LSTMCellDecomposition>();
    manager.register_pass<ngraph::pass::GRUCellDecomposition>();
    manager.register_pass<ngraph::pass::RNNCellDecomposition>();
    manager.register_pass<ngraph::pass::ConvertNMS1ToNMS5>();
    manager.register_pass<ngraph::pass::ConvertNMS3ToNMS5>();
    manager.register_pass<ngraph::pass::ConvertNMS4ToNMS5>();
    manager.register_pass<ngraph::pass::ConvertNMSToNMSIEInternal>();
    manager.register_pass<ngraph::pass::ConvertMulticlassNmsToMulticlassNmsIE>();
    manager.register_pass<ngraph::pass::ConvertMatrixNmsToMatrixNmsIE>();
    manager.register_pass<ngraph::pass::TransposeMatMul>();
    manager.register_pass<ngraph::pass::ConstantFolding>();

    if (useLpt) {
        manager.register_pass<ngraph::pass::low_precision::ConvertSubtractConstant>(
            std::vector<ngraph::element::Type>{ ngraph::element::i8, ngraph::element::u8, ngraph::element::i4, ngraph::element::u4 });
    }
    manager.register_pass<ngraph::pass::ConvertPrecision>(precisions);
    manager.register_pass<ngraph::pass::EliminateConvert>();

    auto pass_config = manager.get_pass_config();

    using const_node_ptr = const std::shared_ptr<const ngraph::Node>;

    // SpaceToDepth/ DepthToSpace node implementation supports only equal input/output tensors with rank <= 5
    pass_config->set_callback<ngraph::pass::ConvertSpaceToDepth,
            ngraph::pass::ConvertDepthToSpace>(
            [](const_node_ptr &node) -> bool {
                return node->input_value(0).get_shape().size() <= 5lu &&
                       node->input_value(0).get_shape().size() == node->get_output_shape(0).size();
            });

    pass_config->set_callback<ngraph::pass::ConvertBatchToSpace,
                              ngraph::pass::ConvertSpaceToBatch>(
            [](const_node_ptr &node) -> bool {
                const auto & rank = node->input(0).get_partial_shape().rank().get_length();
                return rank == 4lu || rank == 5lu;
            });

    auto isCellPrimitiveSupported = [](const_node_ptr &node) -> bool {
        if (const auto &rnn_cell = std::dynamic_pointer_cast<const ngraph::opset4::RNNCell>(node)) {
            return rnn_cell->get_clip() == 0.0f;
        } else if (const auto &gru_cell = std::dynamic_pointer_cast<const ngraph::opset4::GRUCell>(
                node)) {
            return gru_cell->get_clip() == 0.0f
                   && gru_cell->get_activations() == std::vector<std::string>{"sigmoid", "tanh"};
        } else if (const auto &lstm_cell = std::dynamic_pointer_cast<const ngraph::opset4::LSTMCell>(
                node)) {
            return lstm_cell->get_clip() == 0.0f &&
                   lstm_cell->get_activations() == std::vector<std::string>{"sigmoid", "tanh", "tanh"};
        } else if (const auto &lstm_cell_v1 = std::dynamic_pointer_cast<const ngraph::opset1::LSTMCell>(
                node)) {
            return lstm_cell_v1->get_clip() == 0.0f &&
                   lstm_cell_v1->get_activations() == std::vector<std::string>{"sigmoid", "tanh", "tanh"};
        }
        return false;
    };

    // Sequences supported by the plugin shouldn't be converted to TensorIterator.
    // sequence_length input is not supported in all Sequences, so if is_seq_len_provided() == true, we
    // should always convert to TensorIterator.
    // RNN/GRU/LSTM Sequences are supported with clip == 0, and with default activations.
    auto isSequencePrimitiveSupported = [](const_node_ptr &node) -> bool {
        const auto& data = node->input(0);
        const auto& data_pshape = data.get_partial_shape();
        if (data_pshape.rank().is_static() && data_pshape.rank().get_length() > 1 && !data_pshape[1].is_static())
            return false;
        auto max_seq_len = data.get_shape().at(1);
        if (const auto &rnn_seq = std::dynamic_pointer_cast<const ngraph::opset6::RNNSequence>(node)) {
            return rnn_seq->get_clip() == 0.0f &&
                   !ngraph::op::util::is_seq_len_provided(rnn_seq->get_input_node_shared_ptr(2),
                                                          max_seq_len);
        } else if (const auto &gru_seq = std::dynamic_pointer_cast<const ngraph::opset6::GRUSequence>(
                node)) {
            return gru_seq->get_clip() == 0.0f &&
                   gru_seq->get_activations() == std::vector<std::string>{"sigmoid", "tanh"} &&
                   !ngraph::op::util::is_seq_len_provided(gru_seq->get_input_node_shared_ptr(2),
                                                          max_seq_len);
        } else if (const auto &lstm_seq = std::dynamic_pointer_cast<const ngraph::opset6::LSTMSequence>(
                node)) {
            return lstm_seq->get_clip() == 0.0f &&
                   lstm_seq->get_activations() == std::vector<std::string>{"sigmoid", "tanh", "tanh"} &&
                   !ngraph::op::util::is_seq_len_provided(lstm_seq->get_input_node_shared_ptr(3),
                                                          max_seq_len);
        }
        return false;
    };

    pass_config->set_callback<ngraph::pass::ConvertRNNSequenceToTensorIterator,
                              ngraph::pass::ConvertGRUSequenceToTensorIterator,
                              ngraph::pass::ConvertLSTMSequenceToTensorIterator>(
            [isSequencePrimitiveSupported](const_node_ptr &node) -> bool {
                return isSequencePrimitiveSupported(node);
            });

    pass_config->set_callback<ngraph::pass::RNNCellDecomposition, ngraph::pass::GRUCellDecomposition,
            ngraph::pass::LSTMCellDecomposition>(
            [isCellPrimitiveSupported](const_node_ptr &node) -> bool {
                return isCellPrimitiveSupported(node);
            });

    pass_config->set_callback<ngraph::pass::ConvertTensorIteratorToRNNSequence,
                              ngraph::pass::ConvertTensorIteratorToLSTMSequence,
                              ngraph::pass::ConvertTensorIteratorToGRUSequence>(
            [isCellPrimitiveSupported](const_node_ptr &node) -> bool {
                if (const auto& ti_op = std::dynamic_pointer_cast<const ngraph::op::TensorIterator>(node)) {
                    size_t count_rnn = 0;
                    for (const auto &op : ti_op->get_body()->get_ops())
                        count_rnn += isCellPrimitiveSupported(op);
                    return count_rnn != 1;
                }
                return true;
            });

    pass_config->set_callback<ngraph::pass::MVN6Decomposition>(
            [](const_node_ptr &node) -> bool {
                std::string errorMessage;
                return MKLDNNMVNNode::isSupportedOperation(node, errorMessage);
            });

    pass_config->set_callback<ngraph::pass::NormalizeL2Decomposition>(
            [](const_node_ptr &node) -> bool {
                std::string errorMsg;
                return MKLDNNNormalizeL2Node::isSupportedOperation(node, errorMsg);
            });

    pass_config->enable<ngraph::pass::SoftmaxDecomposition>();
    pass_config->set_callback<ngraph::pass::SoftmaxDecomposition>(
            [](const_node_ptr &node) -> bool {
                return node->input_value(0).get_partial_shape().rank().get_length() <= 5;
            });

    // TODO [DS NMS]: remove when nodes from models where nms is not last node in model supports DS
    pass_config->set_callback<ngraph::pass::ConvertNMSToNMSIEInternal>(
            [](const_node_ptr &node) -> bool {
                for (size_t i = 0; i < node->get_output_size(); i++) {
                    const auto outputs = node->get_output_target_inputs(i);
                    for (const auto &out : outputs) {
                        if (out.get_node()->get_type_info() != ngraph::op::v0::Result::get_type_info_static()) {
                            return false;
                        }
                    }
                }
                return true;
            });

    // List of enabled/disabled transformations
    pass_config->disable<ngraph::pass::ConvertGELU>();
    pass_config->disable<ngraph::pass::ConvertShuffleChannels3>();
    pass_config->disable<ngraph::pass::Gelu7Downgrade>();
    pass_config->disable<ngraph::pass::HSwishDecomposition>();
    pass_config->disable<ngraph::pass::ReduceL1Decomposition>();
    pass_config->disable<ngraph::pass::ReduceL2Decomposition>();
    pass_config->disable<ngraph::pass::SoftPlusDecomposition>();
    pass_config->disable<ngraph::pass::HSigmoidDecomposition>();
    pass_config->disable<ngraph::pass::ConvertMod>();
    pass_config->disable<ngraph::pass::LogSoftmaxDecomposition>();
    pass_config->disable<ngraph::pass::ConvertShuffleChannels3>();
    pass_config->disable<ngraph::pass::WeightsDequantizeToFakeQuantize>();
    pass_config->disable<ngraph::pass::SimplifyCTCGreedyDecoderSeqLen>();
    pass_config->disable<ngraph::pass::ConvertGather7ToGather1>();
    pass_config->disable<ngraph::pass::ConvertMinimum>();

    pass_config->enable<ngraph::pass::NormalizeL2Decomposition>();
    pass_config->enable<ngraph::pass::ConvertInterpolate1ToInterpolate4>();
    pass_config->enable<ngraph::pass::ConvertGather1ToGather7>();
    pass_config->enable<ngraph::pass::ConvertGather8ToGather7>();

    if (useLpt) {
        pass_config->set_callback<ngraph::pass::ConvertQuantizeDequantize>([](const_node_ptr &node) -> bool {
            return ngraph::pass::low_precision::NetworkHelper::areQuantizeAndDequantizeSupportedForMultiply(node);
        });

        pass_config->set_callback<ngraph::pass::ConvertSubtract>([](const_node_ptr &node) -> bool {
            return ngraph::pass::low_precision::NetworkHelper::areQuantizeAndDequantizeSupportedForSubtract(node);
        });
    }

    manager.run_passes(nGraphFunc);

    using namespace ngraph::pass::low_precision;
    if (useLpt) {
        OV_ITT_SCOPE(FIRST_INFERENCE, MKLDNNPlugin::itt::domains::MKLDNN_LT, "LowPrecisionTransformations");

        auto supportedPrecisions = std::vector<OperationPrecisionRestriction>({
            OperationPrecisionRestriction::create<ngraph::opset1::Convolution>({
                {0, {ngraph::element::u8}},
                {1, {ngraph::element::i8}},
            }),
            OperationPrecisionRestriction::create<ngraph::opset1::ConvolutionBackpropData>({
                {0, {ngraph::element::u8, ngraph::element::i8}},
                {1, {ngraph::element::i8}}
            }),
            OperationPrecisionRestriction::create<ngraph::opset1::GroupConvolution>({
                {0, {ngraph::element::u8}},
                {1, {ngraph::element::i8}}
            }),
            OperationPrecisionRestriction::create<ngraph::opset1::Multiply>({
                {0, {ngraph::element::u8}},
                {1, {ngraph::element::i8}},
            }),
            OperationPrecisionRestriction::create<ngraph::opset1::MatMul>({
                {0, {ngraph::element::u8, ngraph::element::i8}},
                {1, {ngraph::element::i8}}
            }),
        });

        auto perTensorQuantization = std::vector<OperationPerTensorQuantizationRestriction>({
            OperationPerTensorQuantizationRestriction::create<ngraph::opset1::Convolution>({0}),
            OperationPerTensorQuantizationRestriction::create<ngraph::opset1::ConvolutionBackpropData>({0})
        });

        // for GNA networks reference execution
        bool updatePrecision = true;
        bool hasINT16orINT32Levels = ngraph::pass::low_precision::LowPrecision::isFQLevelsPresent(
                nGraphFunc,
                {65535, 65536, 4294967295, 4294967296});
        if (hasINT16orINT32Levels) {
            updatePrecision = false;
            LowPrecision::setDefaultPrecisions({
                ngraph::element::u8,  ngraph::element::i8,
                ngraph::element::u16, ngraph::element::i16,
                ngraph::element::u32, ngraph::element::i32,
            });

            supportedPrecisions = std::vector<OperationPrecisionRestriction>({});
        }

        ngraph::pass::Manager lptManager;
        lptManager.register_pass<ngraph::pass::low_precision::LowPrecision>(supportedPrecisions, perTensorQuantization,
                                                                            LayerTransformation::Params(updatePrecision));
        lptManager.get_pass_config()->set_callback<ngraph::pass::low_precision::MarkupPrecisions>([](const_node_ptr& node) -> bool {
            if (const auto mulitply = std::dynamic_pointer_cast<const ngraph::opset1::Multiply>(node)) {
                return !MultiplyToGroupConvolutionTransformation::canBeTransformedToGroupConvolution(mulitply);
            }
            return false;
        });
        lptManager.get_pass_config()->set_callback<ngraph::pass::low_precision::ConvolutionBackpropDataTransformation>([](const_node_ptr& node) -> bool {
            return LayerTransformation::isAsymmetricQuantization(node) || WeightableLayerTransformation::isAsymmetricOnWeights(node);
        });
        lptManager.get_pass_config()->set_callback<ngraph::pass::low_precision::MultiplyToGroupConvolutionTransformation>([](const_node_ptr& node) -> bool {
            return MultiplyToGroupConvolutionTransformation::isDynamicOrScalar(node);
        });
        lptManager.run_passes(nGraphFunc);
    }

    ngraph::pass::Manager postLPTPassManager;
    postLPTPassManager.register_pass<ngraph::pass::FakeQuantizeDecomposition>();
    postLPTPassManager.register_pass<ngraph::pass::UnrollTensorIterator>();

    postLPTPassManager.get_pass_config()->set_callback<ngraph::pass::FakeQuantizeDecomposition>([](const_node_ptr &node) -> bool {
        std::string errMsg;
        return MKLDNNFakeQuantizeNode::isSupportedOperation(node, errMsg);
    });
    postLPTPassManager.get_pass_config()->set_callback<ngraph::pass::UnrollTensorIterator>([](const_node_ptr &node) -> bool {
        // UnrollTI transformation is disabled by default, is turned on by LowLatency transformation
        return node->get_rt_info().count("UNROLL_TI") == 0;
    });

    postLPTPassManager.run_passes(nGraphFunc);
}

static void Transformation(CNNNetwork& clonedNetwork, const bool _enableLPT) {
    auto nGraphFunc = clonedNetwork.getFunction();
    TransformationUpToCPUSpecificOpSet(nGraphFunc, _enableLPT);
    ConvertToCPUSpecificOpset(nGraphFunc);
}

InferenceEngine::IExecutableNetworkInternal::Ptr
Engine::LoadExeNetworkImpl(const InferenceEngine::CNNNetwork &network, const std::map<std::string, std::string> &orig_config) {
    OV_ITT_SCOPED_TASK(itt::domains::MKLDNNPlugin, "Engine::LoadExeNetworkImpl");

    // verification of supported input
    InferenceEngine::InputsDataMap _networkInputs = network.getInputsInfo();
    for (const auto &ii : _networkInputs) {
        auto input_precision = ii.second->getPrecision();
        if (input_precision != InferenceEngine::Precision::FP32 &&
            input_precision != InferenceEngine::Precision::I32 &&
            input_precision != InferenceEngine::Precision::U16 &&
            input_precision != InferenceEngine::Precision::I16 &&
            input_precision != InferenceEngine::Precision::I8 &&
            input_precision != InferenceEngine::Precision::U8 &&
            input_precision != InferenceEngine::Precision::BF16 &&
            input_precision != InferenceEngine::Precision::BOOL &&
            input_precision != InferenceEngine::Precision::I64 &&
            input_precision != InferenceEngine::Precision::U64) {
            IE_THROW(NotImplemented)
                               << "Input image format " << input_precision << " is not supported yet...";
        }
    }

    auto config = orig_config;
    CNNNetwork clonedNetwork = InferenceEngine::details::cloneNetwork(network);
    const auto& lptProp = config.find(InferenceEngine::PluginConfigInternalParams::KEY_LP_TRANSFORMS_MODE);
    const bool enableLPT = (lptProp != config.end() && lptProp->second == PluginConfigParams::YES) /* enabled in the orig_config*/
            || Config::LPTransformsMode::On == engConfig.lpTransformsMode /* or already enabled for the plugin */;
    auto nGraphFunc = clonedNetwork.getFunction();
    TransformationUpToCPUSpecificOpSet(nGraphFunc, enableLPT);

    // Here the OV perf modes are turned into specific settings (as we need the network for better params selection)
    const auto& mode = config.find(PluginConfigParams::KEY_PERFORMANCE_HINT);
    // the mode may have just arrived to the LoadNetwork, or was set with the plugins' SetConfig
    if (mode != config.end() || !engConfig.perfHintsConfig.ovPerfHint.empty()) {
        const auto mode_name = (mode != config.end())
                               ? PerfHintsConfig::CheckPerformanceHintValue(mode->second) : engConfig.perfHintsConfig.ovPerfHint;
        //checking streams (to avoid overriding what user might explicitly set in the incoming config or previously via SetConfig)
        const auto streams = config.find(PluginConfigParams::KEY_CPU_THROUGHPUT_STREAMS);
        if (streams == config.end() && !streamsSet) {
            if (mode_name == CONFIG_VALUE(LATENCY)) {
                config[PluginConfigParams::KEY_CPU_THROUGHPUT_STREAMS] = CONFIG_VALUE(CPU_THROUGHPUT_NUMA);
            } else if (mode_name == CONFIG_VALUE(THROUGHPUT)) {
                const auto isa = dnnl::get_effective_cpu_isa();
                float isaSpecificThreshold = 1.0f;
                switch (isa) {
                    case dnnl::cpu_isa::sse41 :
                        isaSpecificThreshold = 0.5f;
                        break;
                    case dnnl::cpu_isa::avx2:
                    case dnnl::cpu_isa::avx512_core:
                        isaSpecificThreshold = 1.0f;
                        break;
                    case dnnl::cpu_isa::avx512_core_vnni:
                    case dnnl::cpu_isa::avx2_vnni:
                        isaSpecificThreshold = 2.0f;
                        break;
                    case dnnl::cpu_isa::avx512_core_amx:
                        isaSpecificThreshold = 4.0f;
                        break;
                    default:
                        isaSpecificThreshold = 1.0f;
                }
                // the more "capable" the CPU in general, the more streams we may want to keep to keep it utilized
                const float memThresholdAssumeLimitedForISA = ov::MemBandwidthPressure::LIMITED/isaSpecificThreshold;
                const float L2_cache_size = mkldnn::utils::get_cache_size(2 /*level*/, true /*per core */);
                const float L3_cache_size = mkldnn::utils::get_cache_size(3, false);
                ov::MemBandwidthPressure networkToleranceForLowCache = ov::MemBandwidthPressureTolerance(
                        clonedNetwork.getFunction(),
                        L2_cache_size, L3_cache_size,
                        memThresholdAssumeLimitedForISA);
                // num of phys CPU cores (most aggressive value for #streams)
                const auto num_cores = getNumberOfCPUCores();
                // less aggressive
                const auto num_streams_less_aggressive = num_cores / 2;
                // default #streams value (most conservative)
                const auto default_num_streams = IStreamsExecutor::Config::GetDefaultNumStreams();
                int num_streams = default_num_streams;
                if (networkToleranceForLowCache.max_mem_tolerance == ov::MemBandwidthPressure::UNKNOWN) {
                    if ((networkToleranceForLowCache.ratio_compute_convs == ov::MemBandwidthPressure::ALL)
                        || (networkToleranceForLowCache.ratio_compute_deconvs == ov::MemBandwidthPressure::ALL)) {
                        // all relevant layers (convs, etc) are compute-limited, the most aggressive val for #streams
                        num_streams = num_cores;
                    }   // otherwise (no recognized layers) falling back to the default value
                } else if (networkToleranceForLowCache.max_mem_tolerance > memThresholdAssumeLimitedForISA) {
                    // network is below the ISA-specific threshold
                    num_streams = num_cores;
                } else if (networkToleranceForLowCache.max_mem_tolerance > ov::MemBandwidthPressure::LIMITED) {
                    // network is below general threshold
                    num_streams = std::max(default_num_streams, num_streams_less_aggressive);
                }
                auto num_requests = config.find(PluginConfigParams::KEY_PERFORMANCE_HINT_NUM_REQUESTS);
                if (engConfig.perfHintsConfig.ovPerfHintNumRequests)  // set thru SetConfig to the plugin
                    num_streams = std::min(engConfig.perfHintsConfig.ovPerfHintNumRequests,
                            engConfig.perfHintsConfig.ovPerfHintNumRequests);
                if (num_requests != config.end())   // arrived with config to the LoadNetwork (and thus higher pri)
                    num_streams = std::min(num_streams,
                            PerfHintsConfig::CheckPerformanceHintRequestValue(num_requests->second));
                config[PluginConfigParams::KEY_CPU_THROUGHPUT_STREAMS] = std::to_string(num_streams);
           }
        }
    }
    ConvertToCPUSpecificOpset(nGraphFunc);

    // update the props after the perf mode translated to configs
    // TODO: Clarify the behavior of SetConfig method. Skip eng_config or not?
    Config conf = engConfig;
    conf.readProperties(config);
    if (conf.enableDynamicBatch) {
        conf.batchLimit = static_cast<int>(network.getBatchSize());
    }

    return std::make_shared<MKLDNNExecNetwork>(clonedNetwork, conf, extensionManager, weightsSharing);
}

void Engine::SetConfig(const std::map<std::string, std::string> &config) {
    // accumulate config parameters on engine level
    streamsSet = (config.find(PluginConfigParams::KEY_CPU_THROUGHPUT_STREAMS) != config.end());
    engConfig.readProperties(config);
}

Parameter Engine::GetConfig(const std::string& name, const std::map<std::string, Parameter>& /*options*/) const {
    Parameter result;
    auto option = engConfig._config.find(name);
    if (option != engConfig._config.end()) {
        result = option->second;
    } else {
        IE_THROW() << "Unsupported config key " << name;
    }
    return result;
}

static bool hasAVX512() {
#if !defined(__arm__) && !defined(_M_ARM) && !defined(__aarch64__) && !defined(_M_ARM64)
    unsigned int regs[4] = {7, 0, 0, 0};
#ifdef _WIN32
    __cpuid(reinterpret_cast<int*>(regs), regs[0]);
#else
    __cpuid_count(regs[0], regs[1], regs[0], regs[1], regs[2], regs[3]);
#endif
    if (regs[1] & (1U << 16))
        return true;
#endif
    return false;
}

Parameter Engine::GetMetric(const std::string& name, const std::map<std::string, Parameter>& /*options*/) const {
    if (name == METRIC_KEY(SUPPORTED_METRICS)) {
        std::vector<std::string> metrics = {
            METRIC_KEY(AVAILABLE_DEVICES),
            METRIC_KEY(SUPPORTED_METRICS),
            METRIC_KEY(FULL_DEVICE_NAME),
            METRIC_KEY(OPTIMIZATION_CAPABILITIES),
            METRIC_KEY(SUPPORTED_CONFIG_KEYS),
            METRIC_KEY(RANGE_FOR_ASYNC_INFER_REQUESTS),
            METRIC_KEY(RANGE_FOR_STREAMS),
            METRIC_KEY(IMPORT_EXPORT_SUPPORT),
        };
        IE_SET_METRIC_RETURN(SUPPORTED_METRICS, metrics);
    } else if (name == METRIC_KEY(FULL_DEVICE_NAME)) {
        std::string brand_string;
#if !defined(__arm__) && !defined(_M_ARM) && !defined(__aarch64__) && !defined(_M_ARM64)
        unsigned int addr_list[3] = { 0x80000002, 0x80000003, 0x80000004 };
        unsigned int regs[4];
        for (auto addr : addr_list) {
            regs[0] = addr;
#ifdef _WIN32
            __cpuid(reinterpret_cast<int*>(regs), regs[0]);
#else
            __get_cpuid(regs[0], &regs[0], &regs[1], &regs[2], &regs[3]);
#endif
            char *ch = reinterpret_cast<char*>(&regs[0]);
            for (size_t j = 0; j < sizeof(regs); j++)
                brand_string += ch[j];
        }
#else
        brand_string = "Non Intel Architecture";
#endif
        IE_SET_METRIC_RETURN(FULL_DEVICE_NAME, brand_string);
    } else if (name == METRIC_KEY(AVAILABLE_DEVICES)) {
        std::vector<std::string> availableDevices = { "" };
        IE_SET_METRIC_RETURN(AVAILABLE_DEVICES, availableDevices);
    } else if (name == METRIC_KEY(OPTIMIZATION_CAPABILITIES)) {
        std::vector<std::string> capabilities;
        if (with_cpu_x86_bfloat16())
            capabilities.push_back(METRIC_VALUE(BF16));
        if (hasAVX512())
            capabilities.push_back(METRIC_VALUE(WINOGRAD));
        capabilities.push_back(METRIC_VALUE(FP32));
        capabilities.push_back(METRIC_VALUE(FP16));
        capabilities.push_back(METRIC_VALUE(INT8));
        capabilities.push_back(METRIC_VALUE(BIN));
        IE_SET_METRIC_RETURN(OPTIMIZATION_CAPABILITIES, capabilities);
    } else if (name == METRIC_KEY(SUPPORTED_CONFIG_KEYS)) {
        std::vector<std::string> configKeys;
        for (auto && opt : engConfig._config)
            configKeys.push_back(opt.first);
        IE_SET_METRIC_RETURN(SUPPORTED_CONFIG_KEYS, configKeys);
    } else if (name == METRIC_KEY(RANGE_FOR_ASYNC_INFER_REQUESTS)) {
        std::tuple<unsigned int, unsigned int, unsigned int> range = std::make_tuple(1, 1, 1);
        IE_SET_METRIC_RETURN(RANGE_FOR_ASYNC_INFER_REQUESTS, range);
    } else if (name == METRIC_KEY(RANGE_FOR_STREAMS)) {
        std::tuple<unsigned int, unsigned int> range = std::make_tuple(1, parallel_get_max_threads());
        IE_SET_METRIC_RETURN(RANGE_FOR_STREAMS, range);
    } else if (name == METRIC_KEY(IMPORT_EXPORT_SUPPORT)) {
        IE_SET_METRIC_RETURN(IMPORT_EXPORT_SUPPORT, true);
    } else {
        IE_THROW() << "Unsupported metric key " << name;
    }
}

void Engine::AddExtension(const InferenceEngine::IExtensionPtr& extension) {
    extensionManager->AddExtension(extension);
}

QueryNetworkResult Engine::QueryNetwork(const CNNNetwork& network, const std::map<std::string, std::string>& config) const {
    QueryNetworkResult res;

    MKLDNNWeightsSharing::Ptr fake_w_cache;
    auto function = network.getFunction();
    if (function != nullptr) {
        std::unordered_set<std::string> originalOps;
        for (auto&& node : function->get_ops()) {
            originalOps.emplace(node->get_friendly_name());
        }

        // TODO: Clarify the behavior of SetConfig method. Skip eng_config or not?
        Config conf = engConfig;
        conf.readProperties(config);

        if (conf.enableDynamicBatch) {
            conf.batchLimit = static_cast<int>(network.getBatchSize());
        }

        auto clonedNetwork = InferenceEngine::details::cloneNetwork(network);
        const auto& lptProp = config.find(InferenceEngine::PluginConfigInternalParams::KEY_LP_TRANSFORMS_MODE);
        const bool enableLPT = (lptProp != config.end() && lptProp->second == PluginConfigParams::YES) /* enabled in the orig_config*/
                               || Config::LPTransformsMode::On == engConfig.lpTransformsMode /* or already enabled */;
        Transformation(clonedNetwork, enableLPT);
        auto ops = clonedNetwork.getFunction()->get_ordered_ops();
        std::unordered_set<std::string> supported;
        std::unordered_set<std::string> unsupported;
        for (auto op : ops) {
            auto layerIsSupported = [&] {
                std::unique_ptr<MKLDNNNode> ptr;
                try {
                    ptr.reset(MKLDNNNode::factory().create(op, {mkldnn::engine::kind::cpu, 0}, extensionManager, fake_w_cache));
                } catch (InferenceEngine::Exception&) {
                    return false;
                }
                return true;
            } ();
            for (auto&& fusedLayerName : ngraph::getFusedNamesVector(op)) {
                if (InferenceEngine::details::contains(originalOps, fusedLayerName)) {
                    if (layerIsSupported) {
                        supported.emplace(fusedLayerName);
                    } else {
                        unsupported.emplace(fusedLayerName);
                    }
                }
            }
        }
        for (auto&& unsupportedNode : unsupported) {
            supported.erase(unsupportedNode);
        }
        for (auto&& node : function->get_ops()) {
            if (InferenceEngine::details::contains(supported, node->get_friendly_name())) {
                for (auto&& inputNodeOutput : node->input_values()) {
                    if (ngraph::op::is_constant(inputNodeOutput.get_node()) || ngraph::op::is_parameter(inputNodeOutput.get_node())) {
                        supported.emplace(inputNodeOutput.get_node()->get_friendly_name());
                    }
                }
                for (auto&& outputs : node->outputs()) {
                    for (auto&& outputNodeInput : outputs.get_target_inputs()) {
                        if (ngraph::op::is_output(outputNodeInput.get_node())) {
                            supported.emplace(outputNodeInput.get_node()->get_friendly_name());
                        }
                    }
                }
            }

            if (ngraph::op::is_constant(node) || ngraph::op::is_parameter(node)) {
                if (!InferenceEngine::details::contains(supported, node->output(0).get_target_inputs().begin()->get_node()->get_friendly_name())) {
                    supported.erase(node->get_friendly_name());
                }
            } else if (ngraph::op::is_output(node)) {
                if (!InferenceEngine::details::contains(supported, node->input_values().begin()->get_node()->get_friendly_name())) {
                    supported.erase(node->get_friendly_name());
                }
            }
        }

        for (auto&& layerName : supported) {
            res.supportedLayersMap.emplace(layerName, GetName());
        }
    } else {
        IE_THROW() << "CPU plug-in doesn't support not ngraph-based model!";
    }

    return res;
}

InferenceEngine::IExecutableNetworkInternal::Ptr Engine::ImportNetwork(std::istream& networkModel,
                                            const std::map<std::string, std::string>& config) {
    OV_ITT_SCOPE(FIRST_INFERENCE, itt::domains::MKLDNN_LT, "ImportNetwork");

    CNNNetworkDeserializer deserializer(networkModel,
        [this](const std::string& model, const Blob::CPtr& weights) {
            return GetCore()->ReadNetwork(model, weights);
        });

    CNNNetwork cnnnetwork;
    deserializer >> cnnnetwork;

    Config conf = engConfig;
    conf.readProperties(config);

    if (conf.enableDynamicBatch) {
        conf.batchLimit = static_cast<int>(cnnnetwork.getBatchSize());
    }

    auto execNetwork = std::make_shared<MKLDNNExecNetwork>(cnnnetwork, conf, extensionManager, weightsSharing);

    execNetwork->setNetworkInputs(cnnnetwork.getInputsInfo());
    execNetwork->setNetworkOutputs(cnnnetwork.getOutputsInfo());
    SetExeNetworkInfo(execNetwork, cnnnetwork.getFunction());

    return execNetwork;
}

static const Version version = {{2, 1}, CI_BUILD_NUMBER, "MKLDNNPlugin"};
IE_DEFINE_PLUGIN_CREATE_FUNCTION(Engine, version)
