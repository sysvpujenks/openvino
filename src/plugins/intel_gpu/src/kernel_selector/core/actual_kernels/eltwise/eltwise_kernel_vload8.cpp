﻿// Copyright (C) 2018-2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "eltwise_kernel_vload8.h"
#include "kernel_selector_utils.h"
#include <string>
#include <algorithm>

namespace kernel_selector {

ParamsKey EltwiseKernel_vload8::GetSupportedKey() const {
    ParamsKey k;
    k.EnableInputDataType(Datatype::F16);
    k.EnableInputDataType(Datatype::F32);
    k.EnableOutputDataType(Datatype::F16);
    k.EnableOutputDataType(Datatype::F32);
    k.EnableAllInputLayout();
    k.EnableAllOutputLayout();
    k.EnableBatching();
    return k;
}

JitConstants EltwiseKernel_vload8::GetJitConstants(const eltwise_params& params) const {
    return GetJitConstantsCommon(params, true);
}

bool EltwiseKernel_vload8::Validate(const Params& params, const optional_params& o) const {
    if (!EltwiseKernelBase::Validate(params, o)) {
        return false;
    }

    const auto& ewParams = static_cast<const eltwise_params&>(params);

        for (size_t i = 0; i < ewParams.inputs.size(); i++) {
            const auto input_layout = ewParams.inputs[i].GetLayout();
            const auto batch_size = ewParams.inputs[i].Batch().v;
            const auto feature_size = ewParams.inputs[i].Feature().v;
            if ((input_layout == DataLayout::b_fs_yx_fsv16 && feature_size % 16 != 0) ||
                (input_layout == DataLayout::b_fs_yx_fsv32 && feature_size % 32 != 0) ||
                (input_layout == DataLayout::b_fs_zyx_fsv16 && feature_size % 16 != 0) ||
                (input_layout == DataLayout::b_fs_yx_fsv4 && feature_size % 8 != 0) ||
                input_layout == DataLayout::fs_b_yx_fsv32 ||
                (input_layout == DataLayout::bs_fs_yx_bsv32_fsv16 && (feature_size % 16 != 0 || batch_size % 32 != 0)) ||
                (input_layout == DataLayout::bs_fs_yx_bsv32_fsv32 && (feature_size % 32 != 0 || batch_size % 32 != 0)))
                return false;
        }
        if ((ewParams.outputs[0].GetLayout() == DataLayout::b_fs_yx_fsv16 && ewParams.outputs[0].Feature().v % 16 != 0) ||
            (ewParams.outputs[0].GetLayout() == DataLayout::b_fs_yx_fsv32 && ewParams.outputs[0].Feature().v % 32 != 0) ||
            (ewParams.outputs[0].GetLayout() == DataLayout::b_fs_zyx_fsv16 && ewParams.outputs[0].Feature().v % 16 != 0) ||
            (ewParams.outputs[0].GetLayout() == DataLayout::b_fs_yx_fsv4 && ewParams.outputs[0].Feature().v % 8 != 0) ||
            ewParams.outputs[0].GetLayout() == DataLayout::fs_b_yx_fsv32 ||
            (ewParams.outputs[0].GetLayout() == DataLayout::bs_fs_yx_bsv32_fsv16 &&
                (ewParams.outputs[0].Feature().v % 16 != 0 || ewParams.outputs[0].Batch().v % 32 != 0)) ||
            (ewParams.outputs[0].GetLayout() == DataLayout::bs_fs_yx_bsv32_fsv32 &&
                (ewParams.outputs[0].Feature().v % 32 != 0 || ewParams.outputs[0].Batch().v % 32 != 0)))
            return false;

    const auto& output = ewParams.outputs[0];
    const auto count = output.PhysicalSize();

    const bool bSupportedCount = (count % 8) == 0;

    bool bCheckSizes = true;
    for (size_t i = 0; i < ewParams.inputs.size(); i++) {
        // allow only the same input sizes or scalars, without pitches
        if (ewParams.inputs[i].PitchesDifferFromLogicalDims() ||
            (!(ewParams.inputs[0] == ewParams.inputs[i] && ewParams.inputs[i] == ewParams.outputs[0]) &&
             ewParams.inputs[i].PhysicalSize() != 1))
            bCheckSizes = false;
    }

    // TODO: add support to this implementation when user requests input values updates
    bool bCheckUpdateInput = true;
    if (!ewParams.updateInputIds.empty())
        bCheckUpdateInput = false;

    // TODO: add support for reading from output buffer and using its values in computation
    bool bCheckUseOutput = true;
    for (size_t op = 0; op < ewParams.operations.size(); op++) {
        for (size_t input_idx = 0; input_idx < ewParams.operations[op].inputs.size(); input_idx++) {
            if (ewParams.operations[op].inputs[input_idx].mode == EltwiseInputMode::OUTPUT_BUFFER) {
                bCheckUseOutput = false;
                break;
            }
        }
    }

    if (IsUnsupportedModeForVecCode(ewParams))
        return false;

    if (!bCheckSizes || !bSupportedCount || !bCheckUpdateInput || !bCheckUseOutput) {
        return false;
    }

    return true;
}

KernelsData EltwiseKernel_vload8::GetKernelsData(const Params& params, const optional_params& options) const {
    if (!Validate(params, options)) {
        return {};
    }

    KernelData kd = KernelData::Default<eltwise_params>(params);
    eltwise_params& newParams = *static_cast<eltwise_params*>(kd.params.get());

    std::pair<std::string, std::string> jit;

    auto entry_point = GetEntryPoint(kernelName, newParams.layerID, params, options);

    try {
        auto cldnn_jit = GetJitConstants(newParams);
        jit = CreateJit(kernelName, cldnn_jit, entry_point);
    } catch (const std::runtime_error&) {
        return KernelsData();
    }

    auto& kernel = kd.kernels[0];
    kernel.params.workGroups.global = {std::max(newParams.inputs[0].LogicalSize() / 8, (size_t)1), 1, 1};
    kernel.params.workGroups.local = GetOptimalLocalWorkGroupSizes(kernel.params.workGroups.global, params.engineInfo);
    kernel.code.kernelString = GetKernelString(kernelName, jit, entry_point, params.engineInfo, DEFAULT);
    kernel.params.arguments = GetArgsDesc((uint32_t)newParams.inputs.size(), false, false);

    return {kd};
}

KernelsPriority EltwiseKernel_vload8::GetKernelsPriority(const Params& /*params*/, const optional_params& /*options*/) const {
    return FORCE_PRIORITY_8;
}
}  // namespace kernel_selector
