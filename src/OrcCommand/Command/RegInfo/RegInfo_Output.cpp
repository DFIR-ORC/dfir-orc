//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//

#include "stdafx.h"

#include "RegInfo.h"

#include "SystemDetails.h"

#include "ToolVersion.h"

#include "Text/Print/LocationSet.h"
#include "Text/Print/OutputSpec.h"

#include "Usage.h"

using namespace Orc::Command::RegInfo;
using namespace Orc::Text;
using namespace Orc;

void Main::PrintUsage()
{
    auto usageNode = m_console.OutputTree();

    Usage::PrintHeader(
        usageNode,
        "Usage: DFIR-Orc.exe RegInfo [/config=<ConfigFile>] [/out=<Folder|Outfile.csv|Archive.7z>]",
        "RegInfo is a registry parser which can analyze either online or offline registry files.");

    Usage::PrintOutputParameters(usageNode);

    constexpr std::array kCustomMiscParameters = {Usage::kMiscParameterComputer};
    Usage::PrintMiscellaneousParameters(usageNode, kCustomMiscParameters);
}

void Main::PrintParameters()
{
    auto root = m_console.OutputTree();
    auto node = root.AddNode("Parameters");

    PrintCommonParameters(node);

    PrintValue(node, L"Output", config.Output);
    PrintValues(node, L"Locations", config.m_HiveQuery.m_HivesLocation.GetParsedLocations());

    for (size_t i = 0; i < config.m_HiveQuery.m_Queries.size(); ++i)
    {
        const auto& query = config.m_HiveQuery.m_Queries[i];

        auto queryNode = node.AddNode("Query #{}:", i);

        auto matchingHives = queryNode.AddNode("Matching hives:");
        for (const auto& [searchTerm, searchQuery] : config.m_HiveQuery.m_FileFindMap)
        {
            if (query == searchQuery)
            {
                matchingHives.Add(searchTerm->GetDescription());
            }
        }

        if (config.m_HiveQuery.m_FileNameMap.size() > 0)
        {
            auto parsedHivesNode = queryNode.AddNode("Parsed hives:");
            for (const auto& [name, searchQuery] : config.m_HiveQuery.m_FileNameMap)
            {
                if (query == searchQuery)
                {
                    parsedHivesNode.Add(name);
                }
            }
        }

        if (config.Information != REGINFO_NONE)
        {
            auto registryInfoNode = queryNode.AddNode("Registry information to dump:");

            const RegInfoDescription* pCur = _InfoDescription;
            while (pCur->Type != REGINFO_NONE)
            {
                if (pCur->Type & config.Information)
                {
                    registryInfoNode.Add(L"{} ({})", pCur->ColumnName, pCur->Description);
                }

                pCur++;
            }
        }

        query->QuerySpec.PrintSpecs(queryNode);
    }

    m_console.PrintNewLine();
}

void Main::PrintFooter()
{
    m_console.PrintNewLine();

    auto root = m_console.OutputTree();
    auto node = root.AddNode("Statistics");
    PrintCommonFooter(node);

    m_console.PrintNewLine();
}
