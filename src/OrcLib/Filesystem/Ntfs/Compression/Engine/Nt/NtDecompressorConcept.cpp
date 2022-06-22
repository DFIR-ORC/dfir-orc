//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "NtDecompressorConcept.h"

#include "Filesystem/Ntfs/Compression/Engine/Nt/NtApi.h"
#include "Log/Log.h"
#include "Utils/BufferView.h"

using namespace Orc::Ntfs;
using namespace Orc;

namespace {

ULONG GetWorkspaceSize(NtAlgorithm algorithm, std::error_code& ec)
{
    ULONG workspaceSize;
    ULONG CompressFragmentWorkSpaceSize;

    RtlGetCompressionWorkSpaceSize(
        std::underlying_type_t<NtAlgorithm>(algorithm), &workspaceSize, &CompressFragmentWorkSpaceSize, ec);
    if (ec)
    {
        Log::Error("Failed RtlGetCompressionWorkSpaceSize [{}]", ec);
        return 0;
    }

    return workspaceSize;
}

std::vector<uint8_t> GetWorkspace(NtAlgorithm algorithm, std::error_code& ec)
{
    const auto workspaceSize = GetWorkspaceSize(algorithm, ec);
    if (ec)
    {
        Log::Debug("Failed to get workspace size [{}]", ec);
        return {};
    }

    std::vector<uint8_t> workspace;
    workspace.resize(workspaceSize);
    return workspace;
}

}  // namespace

namespace Orc {

NtDecompressorConcept::NtDecompressorConcept(Ntfs::WofAlgorithm algorithm, std::error_code& ec)
    : m_ntAlgorithm(NtAlgorithm::kUnknown)
    , m_workspace()
{
    m_ntAlgorithm = ToNtAlgorithm(algorithm);
    if (m_ntAlgorithm == NtAlgorithm::kUnknown)
    {
        Log::Debug("Failed to convert WofAlgorithm to NtAlgorithm (value: {})", ToString(algorithm));
        ec = std::make_error_code(std::errc::operation_not_supported);
        return;
    }

    m_workspace = ::GetWorkspace(m_ntAlgorithm, ec);
    if (ec)
    {
        Log::Debug("Failed to get workspace [{}]", ec);
        return;
    }
}

NtDecompressorConcept::NtDecompressorConcept(NtAlgorithm algorithm, std::error_code& ec)
    : m_ntAlgorithm(algorithm)
    , m_workspace()
{
    m_workspace = ::GetWorkspace(algorithm, ec);
    if (ec)
    {
        Log::Debug("Failed to get workspace [{}]", ec);
        return;
    }
}

size_t NtDecompressorConcept::Decompress(BufferView input, gsl::span<uint8_t> output, std::error_code& ec)
{
    if (input.size() == output.size())
    {
        std::copy(std::cbegin(input), std::cend(input), std::begin(output));
        return output.size();
    }

    ULONG processed = 0;
    switch (m_ntAlgorithm)
    {
        case NtAlgorithm::kLznt1:
        case NtAlgorithm::kXpress: {
            RtlDecompressBuffer(
                std::underlying_type_t<NtAlgorithm>(m_ntAlgorithm),
                output.data(),
                output.size(),
                const_cast<PUCHAR>(input.data()),
                input.size(),
                &processed,
                ec);
            break;
        }
        case NtAlgorithm::kXpressHuffman: {
            // Windows >= 8
            RtlDecompressBufferEx(
                std::underlying_type_t<NtAlgorithm>(m_ntAlgorithm),
                output.data(),
                output.size(),
                const_cast<PUCHAR>(input.data()),
                input.size(),
                &processed,
                m_workspace.data(),
                ec);
            break;
        }
        default:
            ec = std::make_error_code(std::errc::function_not_supported);
    }

    if (ec)
    {
        Log::Error("Failed to decompress buffer [{}]", ec);
        return 0;
    }

    return processed;
}

}  // namespace Orc
