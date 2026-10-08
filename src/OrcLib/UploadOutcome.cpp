//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): DFIR-ORC Team
//
#include "stdafx.h"

#include "UploadOutcome.h"

#include <fmt/format.h>
#include <fmt/xchar.h>

#include "Log/Log.h"

using namespace Orc;

Orc::UploadVerdict Orc::GradeUploadOutcomes(
    const std::vector<UploadOutcome>& outcomes,
    std::vector<std::wstring>& failedRequired,
    std::vector<std::wstring>& failedOptional)
{
    for (const auto& outcome : outcomes)
    {
        if (outcome.Succeeded || outcome.WantedNothing)
        {
            continue;
        }

        if (outcome.Required)
        {
            failedRequired.push_back(outcome.Name);
        }
        else
        {
            failedOptional.push_back(outcome.Name);
        }
    }

    // A single unnamed destination is a legacy top-level <upload> list. Graded before the others so a
    // configuration that predates <uploads> keeps its historical Critical verbatim.
    if (outcomes.size() == 1 && outcomes.front().Name.empty())
    {
        return failedRequired.empty() ? UploadVerdict::AllSucceeded : UploadVerdict::LegacyChainFailed;
    }

    if (!failedRequired.empty())
    {
        return UploadVerdict::RequiredFailed;
    }

    if (!failedOptional.empty())
    {
        return UploadVerdict::OptionalFailed;
    }

    return UploadVerdict::AllSucceeded;
}

std::wstring Orc::UploadLabel(const std::wstring& destinationName, size_t entry, size_t chainSize)
{
    if (destinationName.empty())
    {
        return fmt::format(L"Upload #{}", entry + 1);
    }

    return fmt::format(L"Upload '{}' #{}/{}", destinationName, entry + 1, chainSize);
}

bool Orc::DestinationWantsFile(
    const OutputSpec::UploadDestination& destination,
    const std::wstring& remoteName,
    bool isArtifact)
{
    if (destination.Chain.empty())
    {
        return false;
    }

    // A run artifact carries no include/exclude choice: it is the collection's own record, and
    // artifacts="no" is the only thing that keeps one away from a destination.
    if (isArtifact)
    {
        return destination.Artifacts;
    }

    // Any entry, not just the primary: each one applies its own filters when it runs, so a destination
    // whose fallback asks for everything does want this file even if its primary drops it.
    for (const auto& upload : destination.Chain)
    {
        if (upload->IsFileUploaded(remoteName))
        {
            return true;
        }
    }

    return false;
}

bool Orc::ShouldDeleteAfterUpload(
    const std::wstring& localPath,
    const std::wstring& remoteName,
    bool isArtifact,
    const std::vector<OutputSpec::UploadDestination>& destinations,
    const std::map<std::wstring, std::set<std::wstring>>& confirmedByDestination)
{
    bool wantedBySomeRequired = false;

    for (const auto& destination : destinations)
    {
        if (!destination.Required)
        {
            continue;
        }

        // Judged across the whole chain: a required destination whose fallback asked for this file did
        // want it, so its missing confirmation must still hold the file back.
        if (!DestinationWantsFile(destination, remoteName, isArtifact))
        {
            continue;
        }

        wantedBySomeRequired = true;

        const auto confirmed = confirmedByDestination.find(destination.Name);
        if (confirmed == confirmedByDestination.end() || confirmed->second.find(localPath) == confirmed->second.end())
        {
            Log::Debug(L"Not deleting '{}': required destination '{}' did not confirm it", localPath, destination.Name);
            return false;
        }
    }

    // Vacuous truth is the dangerous case: with no required destination, or none that wanted this file,
    // the loop above agrees with everything and nothing was ever uploaded.
    if (!wantedBySomeRequired)
    {
        Log::Debug(L"Not deleting '{}': no required destination wanted it", localPath);
        return false;
    }

    return true;
}
