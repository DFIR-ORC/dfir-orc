//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#include "stdafx.h"

#include "FatInfo.h"
#include "FatWalker.h"
#include "FatFileInfo.h"
#include "FileInfoCommon.h"
#include "LogFileWriter.h"

#include <boost\scope_exit.hpp>

using namespace Orc;
using namespace Orc::Command::FatInfo;

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    LoadWinTrust();

    const auto& locs = m_Config.locs.GetAltitudeLocations();
    std::vector<std::shared_ptr<Location>> locations;
    std::vector<std::shared_ptr<Location>> allLocations;

    std::copy_if(begin(locs), end(locs), back_inserter(locations), [](const std::shared_ptr<Location>& loc) {
        if (loc == nullptr)
            return false;

        return (loc->GetParse() && (loc->IsFAT12() || loc->IsFAT16() || loc->IsFAT32()));
    });

    std::copy_if(begin(locs), end(locs), back_inserter(allLocations), [](const std::shared_ptr<Location>& loc) {
        if (loc == nullptr)
            return false;

        return (loc->IsFAT12() || loc->IsFAT16() || loc->IsFAT32());
    });

    if (m_Config.output.Type == OutputSpec::Kind::Archive || m_Config.output.Type == OutputSpec::Kind::Directory)
    {
        if (m_Config.output.Type == OutputSpec::Kind::Archive)
        {
            if (FAILED(hr = m_FileInfoOutput.Prepare(m_Config.output)))
            {
                log::Error(_L_, hr, L"Failed to prepare archive for %s\r\n", m_Config.output.Path.c_str());
                return hr;
            }
        }
        m_FileInfoOutput.WriteVolStats(m_Config.volumesStatsOutput, allLocations);
    }

    BOOST_SCOPE_EXIT(&m_Config, &m_FileInfoOutput) { m_FileInfoOutput.CloseAll(m_Config.output); }
    BOOST_SCOPE_EXIT_END;

    if (FAILED(hr = m_FileInfoOutput.GetWriters(m_Config.output, L"FatInfo", locations)))
    {
        log::Error(_L_, hr, L"Failed to create file information writers\r\n");
        return hr;
    }

    m_FileInfoOutput.ForEachOutput(
        m_Config.output, [this](const MultipleOutput<LocationOutput>::OutputPair& dir) -> HRESULT {
            log::Info(_L_, L"\r\nParsing %s\r\n", dir.first.GetIdentifier().c_str());

            FatWalker::Callbacks callBacks;
            callBacks.m_FileEntryCall = [this, &dir](
                                            const std::shared_ptr<VolumeReader>& volreader,
                                            const WCHAR* szFullName,
                                            const std::shared_ptr<FatFileEntry>& fileEntry) {
                HRESULT hr = E_FAIL;

                FatFileInfo fi(
                    _L_,
                    m_Config.strComputerName.c_str(),
                    m_Config.DefaultIntentions,
                    m_Config.Filters,
                    szFullName,
                    (DWORD)wcslen(szFullName),
                    volreader,
                    fileEntry,
                    m_CodeVerifier);

                try
                {
                    if (FAILED(
                            hr = fi.WriteFileInformation(
                                _L_, FatFileInfo::g_FatColumnNames, *dir.second, m_Config.Filters)))
                    {
                        log::Error(_L_, hr, L"\r\nCould not WriteFileInformation for %s\r\n", szFullName);
                        return hr;
                    }
                }
                catch (WCHAR* e)
                {
                    log::Error(_L_, E_FAIL, L"\r\nCould not WriteFileInformation for %s : %s\r\n", szFullName, e);
                    return E_FAIL;
                }
                return S_OK;
            };

            FatWalker walker(_L_);
            walker.Init(dir.first.m_pLoc, (bool)m_Config.bResurrectRecords);
            walker.Process(callBacks);

            return S_OK;
        });

    return S_OK;
}
