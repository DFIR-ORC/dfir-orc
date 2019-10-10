//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//

#include "stdafx.h"

#include "RegInfo.h"

#include "SystemDetails.h"

#include "LogFileWriter.h"

#include "ToolVersion.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::RegInfo;

void Main::PrintUsage()
{
    log::Info(
        _L_,
        L"\n"
        L"	usage: DFIR-Orc.exe RegInfo\r\n"
        L"\r\n"
        L"\t/config=<ConfigFile> : Specify a XML config file\r\n"
        L"\r\n"
        L"\t/Out=<OutputSpec>     : Timeline information file output specification\r\n"
        L"\t\tOutput specification can be one of:\r\n"
        L"\t\t\tA file that will contain output for all locations\r\n"
        L"\t\t\tA directory that will contain one file per location (<Output>_<Location identifier>.csv)\r\n"
        L"\t\t\tA SQL connection string and table name to import into (<connectionstring>#<tablename>)\r\n"
        L"\r\n"
        L"\t/utf8,/utf16       : Select utf8 or utf16 encoding (default is utf8)\r\n");
    PrintCommonUsage();
    return;
}

void Main::PrintParameters()
{
    SaveAndPrintStartTime();

    PrintComputerName();

    PrintOperatingSystem();

    PrintOutputOption(config.Output);

    log::Info(_L_, L"**********************\r\n");
    log::Info(_L_, L"***** Locations ******\r\n");
    log::Info(_L_, L"**********************\r\n");

    if (config.m_HiveQuery.m_HivesLocation.GetLocations().size() > 0)
    {
        log::Info(_L_, L"\r\nVolumes, Folders to parse:\r\n");

        config.m_HiveQuery.m_HivesLocation.PrintLocations(true, L"\t");

        log::Info(_L_, L"\r\n");
    }

    log::Info(_L_, L"**********************\r\n");
    log::Info(_L_, L"***** /Locations *****\r\n");
    log::Info(_L_, L"**********************\r\n");

    log::Info(_L_, L"**********************\r\n");
    log::Info(_L_, L"** Query list start ** \r\n");
    log::Info(_L_, L"**********************\r\n");
    std::for_each(
        begin(config.m_HiveQuery.m_Queries),
        end(config.m_HiveQuery.m_Queries),
        [this](shared_ptr<HiveQuery::SearchQuery> Query) {
            log::Info(_L_, L"\r\n++++++ Query ++++++\r\n");
            log::Info(_L_, L"Registry hives matching:\r\n");

            for_each(
                config.m_HiveQuery.m_FileFindMap.begin(),
                config.m_HiveQuery.m_FileFindMap.end(),
                [this,
                 Query](std::pair<std::shared_ptr<FileFind::SearchTerm>, std::shared_ptr<HiveQuery::SearchQuery>> p) {
                    if (p.second.get() == Query.get())
                    {
                        log::Info(_L_, (p.first->GetDescription().c_str()));
                        log::Info(_L_, L"\r\n");
                    }
                });
            log::Info(_L_, L"\r\n");
            log::Info(_L_, L"Registry hives to parse:\r\n");
            for_each(
                config.m_HiveQuery.m_FileNameMap.begin(),
                config.m_HiveQuery.m_FileNameMap.end(),
                [this, Query](std::pair<std::wstring, std::shared_ptr<HiveQuery::SearchQuery>> p) {
                    if (p.second.get() == Query.get())
                    {

                        log::Info(_L_, L"%s\r\n", p.first.c_str());
                    }
                });

            if (config.Information != REGINFO_NONE)
            {
                log::Info(_L_, L"\r\nRegistry information to dump:\r\n");

                const RegInfoDescription* pCur = _InfoDescription;

                while (pCur->Type != REGINFO_NONE)
                {
                    if (pCur->Type & config.Information)
                    {
                        log::Info(_L_, L"\t%s (%s)\r\n", pCur->ColumnName, pCur->Description);
                    }
                    pCur++;
                }
                log::Info(_L_, L"\r\n");
            }

            Query->QuerySpec.PrintSpecs();
            log::Info(_L_, L"\r\n++++++ /Query +++++\r\n");
        });

    log::Info(_L_, L"**********************\r\n");
    log::Info(_L_, L"**  Query list end  ** \r\n");
    log::Info(_L_, L"**********************\r\n\r\n");
}

void Main::PrintFooter()
{
    PrintExecutionTime();

    return;
}
