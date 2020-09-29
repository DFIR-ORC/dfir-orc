//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2020 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//
#include "stdafx.h"

#include "HiveQuery.h"

#include <sstream>

#include "FileStream.h"
#include "VolumeReader.h"

namespace Orc {

HRESULT HiveQuery::BuildStreamList()
{
    HRESULT hr = m_HivesFind.Find(
        m_HivesLocation,
        [this](const std::shared_ptr<FileFind::Match>& match, bool& bStop) {
            if (match == nullptr)
            {
                assert(match);
                return;
            }

            for (const auto& matchingAttribute : match->MatchingAttributes)
            {
                Hive hive;

                std::wstringstream tmpStream;
                tmpStream << std::hex << match->VolumeReader->VolumeSerialNumber();
                hive.FileName = tmpStream.str() + match->MatchingNames.front().FullPathName;

                hive.Match = match;
                hive.Stream = matchingAttribute.DataStream;

                auto it = m_FileFindMap.find(match->Term);
                if (it != m_FileFindMap.end())
                {
                    std::shared_ptr<SearchQuery> query(it->second);
                    query->StreamList.push_back(std::move(hive));
                }
            }
        },
        false);

    if (FAILED(hr))
    {
        Log::Error("Failed to parse locations for hives (code: {:#x})", hr);
        return hr;
    }

    for (const auto& fileName : m_HivesFileList)
    {
        auto fileStream = std::make_shared<FileStream>();
        hr = fileStream->ReadFrom(fileName.c_str());
        if (FAILED(hr))
        {
            Log::Error(L"Failed to open stream for hive: {} (code: {:#x})", fileName, hr);
            continue;
        }

        Hive hive;
        hive.Stream = fileStream;
        hive.FileName = fileName;

        auto it = m_FileNameMap.find(fileName);
        if (it != m_FileNameMap.end())
        {
            std::shared_ptr<SearchQuery> query(it->second);
            query->StreamList.push_back(std::move(hive));
        }
    }

    return S_OK;
}

}  // namespace Orc
