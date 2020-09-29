//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//
#pragma once

#include "Hive.h"
#include "RegFind.h"

#include <vector>
#include <unordered_map>
#include <string>

#pragma managed(push, off)

namespace Orc {

class HiveQuery
{
public:
    HiveQuery()
        : m_HivesLocation()
        , m_HivesFind()
    {
    }

    class SearchQuery
    {
    public:
        std::vector<Hive> StreamList;

        // Search query to apply
        RegFind QuerySpec;

        SearchQuery()
            : QuerySpec() {};
    };

    // search queries
    std::vector<std::shared_ptr<SearchQuery>> m_Queries;

    // Hive files to search with FileFind
    LocationSet m_HivesLocation;
    FileFind m_HivesFind;

    // Open hives for searching
    HRESULT BuildStreamList();

    std::vector<std::wstring> m_HivesFileList;

    typedef std::unordered_multimap<std::shared_ptr<FileFind::SearchTerm>, std::shared_ptr<SearchQuery>>
        HiveFileFindMap;
    typedef std::unordered_multimap<
        std::wstring,
        std::shared_ptr<SearchQuery>,
        CaseInsensitiveUnordered,
        CaseInsensitiveUnordered>
        HiveFileNameMap;

    HiveFileFindMap m_FileFindMap;
    HiveFileNameMap m_FileNameMap;
};

}  // namespace Orc

#pragma managed(pop)
