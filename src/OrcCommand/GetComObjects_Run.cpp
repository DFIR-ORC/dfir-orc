//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "GetComObjects.h"
#include "LogFileWriter.h"
#include "TableOutputWriter.h"

#include <memory>
#include <boost\scope_exit.hpp>

using namespace Orc;
using namespace Orc::Command::GetComObjects;

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    DWORD dwGLE = 0L;

    dwGLE = RegFlushKey(HKEY_LOCAL_MACHINE);
    if (dwGLE != ERROR_SUCCESS)
        log::Error(_L_, HRESULT_FROM_WIN32(dwGLE), L"Flushing HKEY_LOCAL_MACHINE failed.\r\n");

    dwGLE = RegFlushKey(HKEY_USERS);
    if (dwGLE != ERROR_SUCCESS)
        log::Error(_L_, HRESULT_FROM_WIN32(dwGLE), L"Flushing HKEY_USERS failed.\r\n");

    if (FAILED(hr = m_Config.m_HiveQuery.BuildStreamList(_L_)))
    {
        log::Error(_L_, hr, L"Failed to build hive stream list\r\n");
        return hr;
    }

    SYSTEMTIME CollectionTime;
    GetSystemTime(&CollectionTime);
    SystemTimeToFileTime(&CollectionTime, &m_CollectionDate);

    auto pWriter = TableOutput::GetWriter(_L_, m_Config.m_Output);

    if (pWriter == nullptr)
    {
        log::Error(
            _L_, E_FAIL, L"Failed to create writer for sample information to %s\r\n", m_Config.m_Output.Path.c_str());
        return E_FAIL;
    }

    std::for_each(
        m_Config.m_HiveQuery.m_Queries.begin(),
        m_Config.m_HiveQuery.m_Queries.end(),
        [this](std::shared_ptr<HiveQuery::SearchQuery> Query) {
            HRESULT hr = E_FAIL;

            Query->QuerySpec.PrintSpecs();

            std::for_each(std::begin(Query->StreamList), std::end(Query->StreamList), [this, Query](Hive& aHive) {
                HRESULT hr = E_FAIL;

                log::Info(_L_, L"\tParsing hive %s\r\n", aHive.FileName.c_str());

                if (aHive.Stream)
                {
                    if (FAILED(hr = Query->QuerySpec.Find(aHive.Stream, nullptr, nullptr)))
                    {
                        log::Error(_L_, E_FAIL, L"Failed to search into hive \"%s\"\r\n", aHive.FileName.c_str());
                    }
                    else
                    {
                        auto& Results = Query->QuerySpec.Matches();

                        // matching elements
                        std::for_each(
                            Results.begin(),
                            Results.end(),
                            [this, &aHive](
                                std::pair<std::shared_ptr<RegFind::SearchTerm>, std::shared_ptr<RegFind::Match>> elt) {
                                std::for_each(
                                    elt.second->MatchingValues.begin(),
                                    elt.second->MatchingValues.end(),
                                    [this, elt, &aHive](RegFind::Match::ValueNameMatch& value) {
                                        if (value.ValueType == REG_EXPAND_SZ || value.ValueType == REG_SZ)
                                        {
                                            log::Info(_L_, L"Value : %ls\r\n", (LPTSTR)(value.Datas.get()));
                                        }
                                    });
                            });
                    }
                }
                else
                {
                    log::Error(_L_, E_FAIL, L"Can't open hive \"%s\"\r\n", aHive.FileName.c_str());
                }

                Query->QuerySpec.ClearMatches();
            });
        });

    log::Info(_L_, L"\r\n\r\n");

    return S_OK;
}
