//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#include "stdafx.h"

#include <boost/scope_exit.hpp>

#include "FatInfo.h"
#include "FatWalker.h"
#include "FatFileInfo.h"
#include "FileInfoCommon.h"

#include "Output/Text/Print/Location.h"

using namespace Orc;
using namespace Orc::Command::FatInfo;

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    BOOST_SCOPE_EXIT(&m_Config, &m_FileInfoOutput) { m_FileInfoOutput.CloseAll(m_Config.output); }
    BOOST_SCOPE_EXIT_END;

    LoadWinTrust();

    const auto& locs = m_Config.locs.GetAltitudeLocations();

    if (m_Config.output.Type == OutputSpec::Kind::Archive || m_Config.output.Type == OutputSpec::Kind::Directory)
    {
        if (m_Config.output.Type == OutputSpec::Kind::Archive)
        {
            hr = m_FileInfoOutput.Prepare(m_Config.output);
            if (FAILED(hr))
            {
                Log::Error(L"Failed to prepare archive for '{}'", m_Config.output.Path);
                return hr;
            }
        }

        std::vector<std::shared_ptr<Location>> allLocations;
        std::copy_if(
            std::cbegin(locs),
            std::cend(locs),
            std::back_inserter(allLocations),
            [](const std::shared_ptr<Location>& loc) {
                assert(loc != nullptr);
                return loc->IsFAT12() || loc->IsFAT16() || loc->IsFAT32();
            });

        m_FileInfoOutput.WriteVolStats(m_Config.volumesStatsOutput, allLocations);
    }

    std::vector<std::shared_ptr<Location>> locations;
    std::copy_if(
        std::cbegin(locs), std::cend(locs), std::back_inserter(locations), [](const std::shared_ptr<Location>& loc) {
            assert(loc != nullptr);
            return loc->GetParse() && (loc->IsFAT12() || loc->IsFAT16() || loc->IsFAT32());
        });

    hr = m_FileInfoOutput.GetWriters(m_Config.output, L"FatInfo", locations);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to create file information writers (code: {:#x})", hr);
        return hr;
    }

    m_FileInfoOutput.ForEachOutput(
        m_Config.output, [this](const MultipleOutput<LocationOutput>::OutputPair& dir) -> HRESULT {
            auto locationNode = m_console.OutputTree().AddNode("Parsing:");
            Print(locationNode, dir.first.m_pLoc);

            FatWalker::Callbacks callBacks;
            callBacks.m_FileEntryCall = [this, &dir](
                                            const std::shared_ptr<VolumeReader>& volreader,
                                            const WCHAR* szFullName,
                                            const std::shared_ptr<FatFileEntry>& fileEntry) {
                try
                {
                    FatFileInfo fi(
                        m_utilitiesConfig.strComputerName.c_str(),
                        m_Config.DefaultIntentions,
                        m_Config.Filters,
                        szFullName,
                        (DWORD)wcslen(szFullName),
                        volreader,
                        fileEntry,
                        m_CodeVerifier);

                    HRESULT hr = fi.WriteFileInformation(FatFileInfo::g_FatColumnNames, *dir.second, m_Config.Filters);
                    if (FAILED(hr))
                    {
                        Log::Error(L"Could not WriteFileInformation for '{}' (code: {:#x})", szFullName, hr);
                        return hr;
                    }
                }
                catch (WCHAR* e)
                {
                    Log::Error(L"Could not WriteFileInformation for '{}': {}", szFullName, e);
                    return E_FAIL;
                }

                return S_OK;
            };

            FatWalker walker;
            walker.Init(dir.first.m_pLoc, static_cast<bool>(m_Config.bResurrectRecords));
            walker.Process(callBacks);

            return S_OK;
        });

    return S_OK;
}
