//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#include "stdafx.h"

#include "FatInfo.h"
#include "FatFileInfo.h"

#include "LogFileWriter.h"

#include "ToolVersion.h"

using namespace Orc;
using namespace Orc::Command::FatInfo;

void Main::PrintUsage()
{
    // Display the tool's usage
    log::Info(
        _L_,
        L"\r\n"
        L"	usage: DFIR-Orc.exe FatInfo [/config=<ConfigFile>] \r\n"
        L"\t[/(+|-)<ColumnSelection,...>:<Filter>] [/<DefaultColumnSelection>,...]\r\n"
        L"\t<Dir1> <Dir2> ... <DirN>\r\n"
        L"\r\n"
        L"\t/utf8,/utf16			  : Select utf8 or utf16 encoding (default is utf8)\r\n"
        L"\r\n"
        L"\t/out=<OutputSpec>       : Output file or archive\r\n"
        L"\r\n"
        L"\t\tOutput specification can be one of:\r\n"
        L"\t\t\tA file that will contain output for all locations\r\n"
        L"\t\t\tA directory that will contain one file per location (<Output>_<Location identifier>.csv)\r\n"
        L"\t\t\tA SQL connection string and table name to import into (<connectionstring>#<tablename>)\r\n"
        L"\r\n"
        L"\t/computer=<ComputerName> : Substitute computer name to GetComputerName()\r\n"
        L"\r\n"
        L"\t/errorcodes        : Columns in error will have \"Error=0x00000000\" reporting the error code\r\n"
        L"\t/low                : Runs with lowered priority\r\n"
        L"\t/verbose            : Turns on verbose logging\r\n"
        L"\t/debug              : Adds debug information (Source File Name, Line number) to output, outputs to "
        L"debugger (OutputDebugString)\r\n"
        L"\t/noconsole          : Turns off console logging\r\n"
        L"\t/logfile=<FileName> : All output is duplicated to logfile <FileName>\r\n"
        L"\t/ResurrectRecords   : Include records marked as \"not in use\" in enumeration.\r\n"
        L"\t/PopSysObj           : Populate system objects in locations (true by default).\r\n"
        L"\t/<DefaultColumnSelection>,...: \r\n"
        L"\tSelects the columns to fill for each file system entry:");

    const ColumnNameDef* pCurCol = FatFileInfo::g_FatColumnNames;

    while (pCurCol->dwIntention != FILEINFO_NONE)
    {
        log::Info(_L_, L"\t\t%s:\t%s\n", pCurCol->szColumnName, pCurCol->szColumnDesc);
        pCurCol++;
    }

    log::Info(_L_, L"\n\t\tAlias names can be used to specify more than one column:\r\n");

    const ColumnNameDef* pCurAlias = FatFileInfo::g_FatAliasNames;

    while (pCurAlias->dwIntention != FILEINFO_NONE)
    {
        log::Info(_L_, L"\t\t- %s:\r\n", pCurAlias->szColumnName);
        DWORD dwNumCol = 0;
        const ColumnNameDef* pCurCol = FatFileInfo::g_FatColumnNames;
        while (pCurCol->dwIntention != FILEINFO_NONE)
        {
            if (pCurAlias->dwIntention & pCurCol->dwIntention)
            {
                if (dwNumCol % 3)
                {
                    log::Info(_L_, L" %s ", pCurCol->szColumnName);
                }
                else
                {
                    log::Info(_L_, L"\r\n\t\t\t%s ", pCurCol->szColumnName);
                }
                dwNumCol++;
            }
            pCurCol++;
        }
        log::Info(_L_, L"\r\n\r\n");
        pCurAlias++;
    }
    log::Info(
        _L_,
        L"\t[/(+|-)<ColSelection>:<Filter>]: Filter specification where:\r\n\r\n"
        L"\t\t+ will add selected columns\r\n"
        L"\t\t- will remove selected columns\r\n\r\n"
        L"\t\t<ColSelection> is a comma separated list of columns or aliases\r\n\r\n"
        L"\t\t<Filter> is one of:\r\n"
        L"\t\t  HasVersionInfo: if file has a VERSION_INFO resource\r\n"
        L"\t\t  HasPE: if file has a PE header\r\n"
        L"\t\t  ExtBinary: if file has an executable extension\r\n"
        L"\t\t  ExtScript: if file has a script extension\r\n"
        L"\t\t  ExtArchive: if file has an archive extension\r\n"
        L"\t\t  Ext=.Ext1,.Ext2,...: if file has extension in .Ext1,.Ext2,...\r\n\r\n"
        L"\t\t  SizeLT=size, SizeGT=size: if file is bigger or smaller than\r\n"
        L"\t\t       size can be expressed in KBs (i.e. SizeGT=25K...)\r\n"
        L"\t\t       or in MBs (i.e. SizeLT=5M etc...)\r\n");
    return;
}

void Main::PrintParameters()
{
    // Parameters are displayed when the configuration is complete and checked

    SaveAndPrintStartTime();

    PrintComputerName();

    PrintOperatingSystem();

    if (!m_Config.strComputerName.empty())
        log::Info(_L_, L"\r\nComputer logged     : %s\r\n", m_Config.strComputerName.c_str());

    log::Info(_L_, L"\r\nCSV Columns         :\r\n");
    const ColumnNameDef* pCurCol = FatFileInfo::g_FatColumnNames;
    DWORD dwNumCol = 0;
    while (pCurCol->dwIntention != FILEINFO_NONE)
    {
        if (m_Config.ColumnIntentions & pCurCol->dwIntention)
        {
            if (dwNumCol % 3)
            {
                log::Info(_L_, L"%s ", pCurCol->szColumnName);
            }
            else
            {
                log::Info(_L_, L"\r\n\t%s ", pCurCol->szColumnName);
            }

            dwNumCol++;
        }
        pCurCol++;
    }

    log::Info(_L_, L"\r\n\r\nDefault columns     :\r\n");

    pCurCol = FatFileInfo::g_FatColumnNames;
    dwNumCol = 0;
    while (pCurCol->dwIntention != FILEINFO_NONE)
    {
        if (m_Config.DefaultIntentions & pCurCol->dwIntention)
        {
            if (dwNumCol % 3)
            {
                log::Info(_L_, L"%s ", pCurCol->szColumnName);
            }
            else
            {
                log::Info(_L_, L"\r\n\t%s ", pCurCol->szColumnName);
            }
            dwNumCol++;
        }
        pCurCol++;
    }
    log::Info(_L_, L"\r\n");

    bool bFirstFilt = true;
    std::for_each(begin(m_Config.Filters), end(m_Config.Filters), [&bFirstFilt, this](const Filter& filter) {
        if (bFirstFilt)
        {
            log::Info(_L_, L"\r\n\r\nFilters:\r\n");
            bFirstFilt = false;
        }
        switch (filter.type)
        {
            case FILEFILTER_EXTBINARY:
                log::Info(_L_, L"\r\n\tif file has binary extension ");
                break;
            case FILEFILTER_EXTSCRIPT:
                log::Info(_L_, L"\r\n\tif file has script extension ");
                break;
            case FILEFILTER_EXTCUSTOM:
            {
                log::Info(_L_, L"\r\n\tif file extension is one of (");
                WCHAR** pCurExt = filter.filterdata.extcustom;
                while (*pCurExt)
                {
                    if (*(pCurExt + 1))
                    {
                        log::Info(_L_, L"%s, ", *pCurExt);
                    }
                    else
                    {
                        log::Info(_L_, L"%s) ", *pCurExt);
                    }
                    pCurExt++;
                }
            }
            break;
            case FILEFILTER_EXTARCHIVE:
                log::Info(_L_, L"\r\n\tif file has archive extension ");
                break;
            case FILEFILTER_PEHEADER:
                log::Info(_L_, L"\r\n\tif file has valid PE header ");
                break;
            case FILEFILTER_VERSIONINFO:
                log::Info(_L_, L"\r\n\tif file has valid VERSION_INFO resource ");
                break;
            case FILEFILTER_SIZELESS:
                log::Info(_L_, L"\r\n\tif file is smaller than %u bytes ", filter.filterdata.size.LowPart);
                break;
            case FILEFILTER_SIZEMORE:
                log::Info(_L_, L"\r\n\tif file is bigger than %u bytes ", filter.filterdata.size.LowPart);
                break;
        }

        log::Info(_L_, filter.bInclude ? L"include columns: \r\n" : L" exclude columns: \r\n");

        const ColumnNameDef* pCurCol = FatFileInfo::g_FatColumnNames;
        DWORD dwNumCol = 0;
        while (pCurCol->dwIntention != FILEINFO_NONE)
        {
            if (pCurCol->dwIntention & filter.intent)
            {
                if (dwNumCol % 3)
                {
                    log::Info(_L_, L"%s ", pCurCol->szColumnName);
                }
                else
                {
                    log::Info(_L_, L"\r\n\t\t%s ", pCurCol->szColumnName);
                }
                dwNumCol++;
            }
            pCurCol++;
        }
        log::Info(_L_, L"\r\n");
    });

    log::Info(_L_, L"\r\nVolumes, Folders to parse:\r\n");

    m_Config.locs.PrintLocations(true);

    log::Info(_L_, L"\r\n");

    return;
}

void Main::PrintFooter()
{
    // Footer is displayed when the Run exits
    PrintExecutionTime();
}
