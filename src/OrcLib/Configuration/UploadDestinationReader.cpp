//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): DFIR-ORC Team
//
#include "stdafx.h"

#include "Configuration/UploadDestinationReader.h"

#include "Configuration/ConfigItem.h"
#include "Log/Log.h"
#include "Utils/Result.h"

using namespace Orc;

namespace {

// Read one <upload> NodeList into a chain. Returns the last failure when nothing usable was read, so
// the caller can tell "some entries were dropped" from "this chain has nothing".
//
// 'sourceIndices' receives the NodeList index each kept entry came from. Entries that fail Configure
// are skipped, so the chain position and the NodeList position diverge, and a caller that reads the
// attributes Configure does not own needs the original item, not the chain position.
HRESULT ReadUploadChain(
    const ConfigItem& uploadNode,
    std::vector<std::shared_ptr<OutputSpec::Upload>>& chain,
    std::vector<size_t>& sourceIndices)
{
    HRESULT lastFailure = S_OK;

    for (size_t i = 0; i < uploadNode.NodeList.size(); ++i)
    {
        auto upload = std::make_shared<OutputSpec::Upload>();

        if (auto hr = upload->Configure(uploadNode.NodeList[i]); FAILED(hr))
        {
            Log::Error("Error in upload section #{} in config file, ignored [{}]", i + 1, SystemError(hr));
            lastFailure = hr;
            continue;
        }

        chain.push_back(upload);
        sourceIndices.push_back(i);
    }

    if (chain.empty())
    {
        return FAILED(lastFailure) ? lastFailure : E_INVALIDARG;
    }

    return S_OK;
}

// An asynchronous entry hands off to BITS, whose outcome cannot be confirmed in-process, so the
// fallbacks after it are attempted against a file that may already be gone. Warn where it is
// configured rather than where it bites.
void WarnOnAsyncBeforeFallback(const std::vector<std::shared_ptr<OutputSpec::Upload>>& chain)
{
    if (chain.size() <= 1)
    {
        return;
    }

    for (size_t i = 0; i + 1 < chain.size(); ++i)
    {
        if (chain[i]->Mode == OutputSpec::UploadMode::Asynchronous)
        {
            Log::Warn(
                L"Upload #{} is configured with asynchronous mode but has {} following fallback upload(s). "
                L"Fallbacks will be attempted but their outcome is unpredictable: the file may already "
                L"be gone if the async upload succeeded.",
                i + 1,
                chain.size() - i - 1);
        }
    }
}

}  // namespace

HRESULT Orc::Config::ReadLegacyUploadChain(
    const ConfigItem& uploadNode,
    std::vector<OutputSpec::UploadDestination>& destinations)
{
    std::vector<std::shared_ptr<OutputSpec::Upload>> chain;
    std::vector<size_t> sourceIndices;  // the legacy list has no inherited attributes to resolve

    if (auto hr = ReadUploadChain(uploadNode, chain, sourceIndices); FAILED(hr))
    {
        Log::Error(
            "None of the {} upload section(s) in config file could be configured [{}]",
            uploadNode.NodeList.size(),
            SystemError(hr));
        return hr;
    }

    // Two legacy lists concatenate into ONE chain, which is what they did before <uploads> existed:
    // the second file's entries are further fallbacks of the same destination, not a second
    // destination that would get its own copy of the collection.
    if (!destinations.empty())
    {
        auto& existing = destinations.front().Chain;
        existing.insert(existing.end(), chain.begin(), chain.end());
        WarnOnAsyncBeforeFallback(existing);
        return S_OK;
    }

    OutputSpec::UploadDestination destination;
    destination.Chain = std::move(chain);
    WarnOnAsyncBeforeFallback(destination.Chain);
    destinations.push_back(std::move(destination));
    return S_OK;
}
