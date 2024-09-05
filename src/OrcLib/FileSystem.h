//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#pragma once

#include "FatFileEntry.h"

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>

#include <string>
#include <memory>

#pragma managed(push, off)

namespace Orc {

using name_key = boost::multi_index::composite_key<
    FatFileEntry,
    boost::multi_index::member<FatFileEntry, const FatFileEntry*, &FatFileEntry::m_ParentFolder>,
    boost::multi_index::member<FatFileEntry, std::wstring, &FatFileEntry::m_Name>>;

using size_key = boost::multi_index::composite_key<
    FatFileEntry,
    boost::multi_index::member<FatFileEntry, const FatFileEntry* const, &FatFileEntry::m_ParentFolder>,
    boost::multi_index::member<FatFileEntry, DWORD, &FatFileEntry::m_ulSize>>;

using FatFileSystem = boost::multi_index::multi_index_container<
    std::shared_ptr<FatFileEntry>,
    boost::multi_index::
        indexed_by<boost::multi_index::ordered_unique<name_key>, boost::multi_index::ordered_non_unique<size_key>>>;

using FatFileSystemByName = boost::multi_index::nth_index<FatFileSystem, 0>::type;
using FatFileSystemBySize = boost::multi_index::nth_index<FatFileSystem, 1>::type;

}  // namespace Orc

#pragma managed(pop)
