//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "Usage.h"

#include "Text/Print.h"

namespace Orc {

namespace Command {
namespace Usage {

void PrintColumnSelectionParameter(
    Orc::Text::Tree& root,
    const Orc::ColumnNameDef* pColumnNames,
    const Orc::ColumnNameDef* pAliases)
{
    {
        constexpr std::array kColumnsParameters = {
            Usage::Parameter {"/(+|-)<ColumnSelection,...>:<Filter>", "Custom list of columns to include or exclude"},
            Usage::Parameter {
                "/<DefaultColumnSelection>,...",
                "Default, All, DeepScan, Details, Hashes, Fuzzy, PeHashes, Dates, RefNums, Authenticode"}};

        Usage::PrintParameters(root, "COLUMNS PARAMETERS", kColumnsParameters);
    }

    {
        auto columnSyntaxNode = root.AddNode("COLUMNS SELECTION SYNTAX");
        columnSyntaxNode.AddEOL();

        auto filterSyntaxNode = columnSyntaxNode.AddNode("Filter specification: [/(+|-)<ColSelection>:<Filter>]");
        filterSyntaxNode.Add("'+' include columns");
        filterSyntaxNode.Add("'-' exclude columns ");
        filterSyntaxNode.Add("<ColSelection> is a comma separated list of columns or aliases");
        filterSyntaxNode.AddEOL();
    }

    {
        auto columnDescriptionNode = root.AddNode("COLUMNS DESCRIPTION");
        const ColumnNameDef* pCurCol = pColumnNames;
        while (pCurCol->dwIntention != Intentions::FILEINFO_NONE)
        {
            const auto columnName = fmt::format(L"{}:", pCurCol->szColumnName);
            columnDescriptionNode.Add("{:<33} {}", columnName, pCurCol->szColumnDesc);
            pCurCol++;
        }

        columnDescriptionNode.AddEOL();
    }

    {
        auto columnAliasesNode = root.AddNode("COLUMNS ALIASES");
        columnAliasesNode.AddEOL();
        columnAliasesNode.Add("Alias names can be used to specify more than one column.");
        columnAliasesNode.AddEOL();

        const ColumnNameDef* pCurAlias = pAliases;

        while (pCurAlias->dwIntention != Intentions::FILEINFO_NONE)
        {
            auto aliasNode = columnAliasesNode.AddNode("{}", pCurAlias->szColumnName);

            DWORD dwNumCol = 0;
            const ColumnNameDef* pCurCol = pColumnNames;
            while (pCurCol->dwIntention != Intentions::FILEINFO_NONE)
            {
                if (HasFlag(pCurAlias->dwIntention, pCurCol->dwIntention))
                {
                    PrintValue(aliasNode, pCurCol->szColumnName, pCurCol->szColumnDesc);
                    dwNumCol++;
                }
                pCurCol++;
            }

            aliasNode.AddEOL();
            pCurAlias++;
        }
    }

    root.AddEmptyLine();
}

}  // namespace Usage
}  // namespace Command
}  // namespace Orc
