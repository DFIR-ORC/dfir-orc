//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//
#pragma once

#include "OrcLib.h"

#include "FileFind.h"
#include "RegFind.h"

#include "CaseInsensitive.h"

#pragma managed(push, off)

namespace Orc {

class ConfigItem;

class ORCLIB_API RegFindConfig
{
public:
    RegFindConfig(logger pLog)
        : _L_(std::move(pLog))
    {
    }

    HRESULT GetConfiguration(
        const ConfigItem& item,
        LocationSet& HivesLocation,
        FileFind& HivesFind,
        RegFind& reg,
        std::vector<std::wstring>& HiveFiles,
        std::vector<std::shared_ptr<FileFind::SearchTerm>>& FileFindterms);

    ~RegFindConfig(void);

private:
    logger _L_;
};

}  // namespace Orc

#pragma managed(pop)
