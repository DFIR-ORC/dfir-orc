//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//

#include "stdafx.h"
#include "RegFindConfig.h"

#include "ConfigItem.h"

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

    if (item[CONFIG_HIVE_FILEFIND].Status == ConfigItem::PRESENT)
    {
        for (const auto& configItem : item[CONFIG_HIVE_FILEFIND].NodeList)
        {
            std::shared_ptr<FileFind::SearchTerm> term = FileFind::GetSearchTermFromConfig(configItem, _L_);

            if (term != nullptr)
            {
                HRESULT hr = E_FAIL;
                if (FAILED(hr = HivesFind.AddTerm(term)))
                {
                    log::Error(_L_, hr, L"Failed to add registry search term\r\n");
                }
                FileFindTerms.push_back(term);
            }
        }
    }

    if (FAILED(hr = reg.AddRegFindFromConfig(item[CONFIG_HIVE_REGFIND].NodeList)))
    {
        log::Error(_L_, hr, L"Error in specific registry find parsing in config file\r\n");
        return hr;
    }

    if (FAILED(hr = reg.AddRegFindFromTemplate(item[CONFIG_HIVE_TEMPLATE].NodeList)))
    {
        log::Error(_L_, hr, L"Error in specific registry find parsing in template file\r\n");
        return hr;
    }

    if (item[CONFIG_HIVE_FILE].Status == ConfigItem::PRESENT)
    {
        for (const auto& configItem : item[CONFIG_HIVE_FILE].NodeList)
        {
            HiveFiles.push_back(configItem.strData);
        }
    }

    return S_OK;
}

RegFindConfig::~RegFindConfig(void) {}
