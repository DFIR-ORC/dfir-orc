//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//

#include "stdafx.h"
#include "RegFindConfig.h"

#include "Configuration/ConfigItem.h"

using namespace Orc;

HRESULT RegFindConfig::GetConfiguration(
    const ConfigItem& item,
    LocationSet& HivesLocation,
    FileFind& HivesFind,
    RegFind& reg,
    std::vector<std::wstring>& HiveFiles,
    std::vector<std::shared_ptr<FileFind::SearchTerm>>& FileFindTerms)
{
    HRESULT hr = E_FAIL;

    DBG_UNREFERENCED_PARAMETER(HivesLocation);

    if (item[CONFIG_HIVE_FILEFIND])
    {
        for (const auto& configItem : item[CONFIG_HIVE_FILEFIND].NodeList)
        {
            std::shared_ptr<FileFind::SearchTerm> term = FileFind::GetSearchTermFromConfig(configItem);

            if (term != nullptr)
            {
                HRESULT hr = E_FAIL;
                if (FAILED(hr = HivesFind.AddTerm(term)))
                {
                    Log::Error(L"Failed to add registry search term [{}]", SystemError(hr));
                }

                FileFindTerms.push_back(term);
            }
        }
    }

    if (FAILED(hr = reg.AddRegFindFromConfig(item[CONFIG_HIVE_REGFIND].NodeList)))
    {
        Log::Error(L"Error in specific registry find parsing in config file [{}]", SystemError(hr));
        return hr;
    }

    if (FAILED(hr = reg.AddRegFindFromTemplate(item[CONFIG_HIVE_TEMPLATE].NodeList)))
    {
        Log::Error(L"Error in specific registry find parsing in template file [{}]", SystemError(hr));
        return hr;
    }

    if (item[CONFIG_HIVE_FILE])
    {
        for (const auto& configItem : item[CONFIG_HIVE_FILE].NodeList)
        {
            HiveFiles.push_back(configItem);
        }
    }

    return S_OK;
}

RegFindConfig::~RegFindConfig(void) {}
