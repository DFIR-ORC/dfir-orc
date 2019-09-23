//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//
#include "stdafx.h"
#include "HiveQuery.h"
#include "FileStream.h"
#include "VolumeReader.h"

#include <sstream>

namespace Orc {

HRESULT HiveQuery::BuildStreamList(const logger& pLog)
{
    HRESULT hr = E_FAIL;

    if (FAILED(
            hr = m_HivesFind.Find(
                m_HivesLocation,
                [this, &pLog](const std::shared_ptr<FileFind::Match>& aMatch, bool& bStop) {
                    if (aMatch == nullptr)
                        return;

                    HiveFileFindMap::iterator iter;

                    for (auto it = std::begin(aMatch->MatchingAttributes); it != std::end(aMatch->MatchingAttributes);
                         ++it)
                    {
                        Hive aHive;
                        std::wstringstream tmpStream;
                        tmpStream << std::hex << aMatch->VolumeReader->VolumeSerialNumber();

                        aHive.FileName = tmpStream.str() + aMatch->MatchingNames.front().FullPathName;
                        aHive.Match = aMatch;
                        aHive.Stream = it->DataStream;

                        iter = m_FileFindMap.find(aMatch->Term);

                        if (iter != m_FileFindMap.end())
                        {
                            std::shared_ptr<SearchQuery> Query(iter->second);
                            Query->StreamList.push_back(std::move(aHive));
                        }
                    }
                },
                false)))
    {
        log::Error(pLog, hr, L"Failed to parse locations for hives\r\n");
    }

    std::for_each(
        std::begin(m_HivesFileList), std::end(m_HivesFileList), [this, &hr, &pLog](const std::wstring& aFileName) {
            Hive aHive;

            auto fileStream = std::make_shared<FileStream>(pLog);
            HiveFileNameMap::iterator it;

            if (FAILED(hr = fileStream->ReadFrom(aFileName.c_str())))
            {
                log::Error(pLog, hr, L"Failed to open stream for hive: %s\r\n", aFileName.c_str());
                return;
            }

            aHive.Stream = fileStream;
            aHive.FileName = aFileName;

            it = m_FileNameMap.find(aFileName);

            if (it != m_FileNameMap.end())
            {
                std::shared_ptr<SearchQuery> Query(it->second);
                Query->StreamList.push_back(std::move(aHive));
            }
        });

    return S_OK;
}

}  // namespace Orc
