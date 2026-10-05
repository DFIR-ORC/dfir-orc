//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): DFIR-ORC Team
//
#include "stdafx.h"

#include "Configuration/UploadDestinationReader.h"

#include <optional>
#include <set>

#include <boost/algorithm/string.hpp>

#include "Configuration/ConfigItem.h"
#include "Log/Log.h"
#include "Utils/Result.h"

using namespace Orc;

namespace {

// The include/exclude in force at one level; absent means "inherit from above".
struct InheritedFilters
{
    std::optional<std::vector<std::wstring>> Include;
    std::optional<std::vector<std::wstring>> Exclude;
};

// Whether the attribute was written at all, as opposed to written empty. This is what makes
// include="" mean "deliberately empty, do not inherit": the reader marks an attribute PRESENT
// whatever its value.
bool HasAttribute(const ConfigItem& item, DWORD index)
{
    return index < item.SubItems.size() && item.SubItems[index].Status == ConfigItem::PRESENT;
}

// The filters this level contributes, falling back to what it inherited for each attribute it does
// not write itself.
InheritedFilters
ResolveFilters(const ConfigItem& item, DWORD includeIndex, DWORD excludeIndex, const InheritedFilters& parent)
{
    InheritedFilters resolved = parent;

    if (HasAttribute(item, includeIndex))
    {
        resolved.Include = SplitFilterPatterns((const std::wstring&)item.SubItems[includeIndex]);
    }

    if (HasAttribute(item, excludeIndex))
    {
        resolved.Exclude = SplitFilterPatterns((const std::wstring&)item.SubItems[excludeIndex]);
    }

    return resolved;
}

// Apply what an <upload> inherited. Configure() has already read the upload's own attributes, so only
// the ones it did not write are replaced.
void ApplyInheritedFilters(const ConfigItem& uploadItem, const InheritedFilters& parent, OutputSpec::Upload& upload)
{
    if (!HasAttribute(uploadItem, CONFIG_UPLOAD_FILTER_INC) && parent.Include)
    {
        upload.FilterInclude = *parent.Include;
    }

    if (!HasAttribute(uploadItem, CONFIG_UPLOAD_FILTER_EXC) && parent.Exclude)
    {
        upload.FilterExclude = *parent.Exclude;
    }
}

// A yes/no attribute that defaults to yes: only an explicit no turns it off, matching how the four
// run-artifact switches are read.
bool ReadYesByDefault(const ConfigItem& item, DWORD index)
{
    if (!HasAttribute(item, index))
    {
        return true;
    }

    const std::wstring& value = (const std::wstring&)item.SubItems[index];
    return !(boost::iequals(value, L"no") || boost::iequals(value, L"false") || value == L"0");
}

// A yes/no attribute that defaults to NO: only an explicit yes turns it on. Used for
// delete_when_done, where reading it the other way round meant any unrecognised value - "off", "n", a
// trailing space, an empty string - deleted the collected data. An evidence-destroying switch has to
// fail closed on anything it does not positively recognise.
bool ReadNoByDefault(const ConfigItem& item, DWORD index)
{
    if (!HasAttribute(item, index))
    {
        return false;
    }

    std::wstring value = (const std::wstring&)item.SubItems[index];
    boost::trim(value);
    return boost::iequals(value, L"yes") || boost::iequals(value, L"true") || value == L"1";
}

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
    // The embedded WolfLauncher config and /local are read by two different functions into this one
    // vector, so a named destination already here came from an <uploads> block in the other file: one
    // form or the other, never both, across files as well as within one.
    for (const auto& existing : destinations)
    {
        if (!existing.Name.empty())
        {
            Log::Error(
                L"A top-level <upload> list cannot be combined with the <uploads> block already read "
                L"(destination '{}'); use one form or the other",
                existing.Name);
            return E_INVALIDARG;
        }
    }

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

HRESULT Orc::Config::ReadUploadDestinations(
    const ConfigItem& uploadsNode,
    std::vector<OutputSpec::UploadDestination>& destinations,
    bool& deleteWhenDone)
{
    // Anything already in the vector came from the other config file (embedded config vs /local). A
    // legacy list there cannot be combined with this block: one form or the other, across files too.
    if (!destinations.empty())
    {
        Log::Error(
            L"An <uploads> block cannot be combined with the top-level <upload> list already read; use "
            L"one form or the other");
        return E_INVALIDARG;
    }

    const InheritedFilters globalFilters =
        ResolveFilters(uploadsNode, CONFIG_UPLOADS_FILTER_INC, CONFIG_UPLOADS_FILTER_EXC, {});

    // Explicit yes only: this deletes collected data, so an unrecognised value must not enable it.
    deleteWhenDone = ReadNoByDefault(uploadsNode, CONFIG_UPLOADS_DELETE_WHEN_DONE);

    const ConfigItem& destinationNodes = uploadsNode.SubItems[CONFIG_UPLOADS_DESTINATION];
    if (destinationNodes.NodeList.empty())
    {
        Log::Error(L"<uploads> declares no <destination>; refusing to run with no upload configured");
        return E_INVALIDARG;
    }

    std::set<std::wstring> seenNames;

    for (const ConfigItem& destinationNode : destinationNodes.NodeList)
    {
        OutputSpec::UploadDestination destination;
        destination.Name = (const std::wstring&)destinationNode.SubItems[CONFIG_DESTINATION_NAME];
        destination.Required = ReadYesByDefault(destinationNode, CONFIG_DESTINATION_REQUIRED);
        destination.Artifacts = ReadYesByDefault(destinationNode, CONFIG_DESTINATION_ARTIFACTS);

        // The schema marks 'name' MANDATORY, but CheckConfig only tests that the attribute is present:
        // an empty value gets through and leaves a destination no log line can identify, which also
        // collides with the empty name a legacy list uses.
        boost::trim(destination.Name);
        if (destination.Name.empty())
        {
            Log::Error(L"An upload <destination> has an empty 'name'; every destination must be named");
            return E_INVALIDARG;
        }

        // Case-insensitive: two destinations differing only in case are indistinguishable in every
        // log line and every refusal message.
        std::wstring key = destination.Name;
        boost::to_lower(key);
        if (!seenNames.insert(key).second)
        {
            Log::Error(L"Duplicate upload destination name '{}'", destination.Name);
            return E_INVALIDARG;
        }

        const InheritedFilters destinationFilters = ResolveFilters(
            destinationNode, CONFIG_DESTINATION_FILTER_INC, CONFIG_DESTINATION_FILTER_EXC, globalFilters);

        const ConfigItem& uploadNode = destinationNode.SubItems[CONFIG_DESTINATION_UPLOAD];
        std::vector<size_t> sourceIndices;
        if (auto hr = ReadUploadChain(uploadNode, destination.Chain, sourceIndices); FAILED(hr))
        {
            // A required destination that cannot be configured at all is refused, not dropped:
            // dropping it would silently reduce the redundancy the operator asked for, and the run
            // would then end Info with one copy where two were configured.
            if (destination.Required)
            {
                Log::Error(
                    L"Required upload destination '{}' has no usable <upload>; refusing to run [{}]",
                    destination.Name,
                    SystemError(hr));
                return hr;
            }

            Log::Error(
                L"Optional upload destination '{}' has no usable <upload>, dropped [{}]",
                destination.Name,
                SystemError(hr));
            continue;
        }

        // sourceIndices, not the loop counter: entries that failed Configure were skipped, so the
        // chain position and the NodeList position diverge once anything was dropped.
        for (size_t i = 0; i < destination.Chain.size(); ++i)
        {
            ApplyInheritedFilters(uploadNode.NodeList[sourceIndices[i]], destinationFilters, *destination.Chain[i]);
        }

        WarnOnAsyncBeforeFallback(destination.Chain);
        destinations.push_back(std::move(destination));
    }

    if (destinations.empty())
    {
        Log::Error(L"No upload destination in <uploads> could be configured; refusing to run");
        return E_INVALIDARG;
    }

    return S_OK;
}
