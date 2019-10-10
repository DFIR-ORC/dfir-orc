//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "NTFSInfo.h"

#include "ToolVersion.h"
#include "LogFileWriter.h"

#include <string>

#include "SystemDetails.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::NTFSInfo;

void Main::PrintUsage()
{
    log::Info(
        _L_,
        L"\r\n"
        L"usage: DFIR-Orc.exe NTFSInfo [/config=<ConfigFile>] [/out=<Folder|Outfile.csv|Archive.7z>] "
        L"[/Walker=USN|MFT]\r\n"
        L"\t[/(+|-)<ColumnSelection,...>:<Filter>] [/<DefaultColumnSelection>,...]\r\n"
        L"\t<Dir1> <Dir2> ... <DirN> [/KnownLocations|/kl]\r\n"
        L"\r\n"
        L"\t/config=<ConfigFile>     : Specify a XML config file\r\n"
        L"\r\n"
        L"\t/utf8,/utf16			  : Select utf8 or utf16 encoding (default is utf8)\r\n"
        L"\r\n"
        L"\t/FileInfo=<OutputSpec>     : File information file output specification\r\n"
        L"\t/AttrInfo=<OutputSpec>     : Attribute information file output specification\r\n"
        L"\t/I30Info=<OutputSpec>      : I30 information file output specification\r\n"
        L"\t/Timeline=<OutputSpec>     : Timeline information file output specification\r\n"
        L"\t/SecDecr=<OutputSpec>      : Security Descriptor information for the volume\r\n"
        L"\t\tOutput specification can be one of:\r\n"
        L"\t\t\tA file that will contain output for all locations\r\n"
        L"\t\t\tA directory that will contain one file per location (<Output>_<Location identifier>.csv)\r\n"
        L"\r\n"
        L"\t/computer=<ComputerName> : Substitute computer name to GetComputerName()\r\n"
        L"\r\n"
        L"\t/ResurrectRecords    : Include records marked as \"not in use\" in enumeration. (they'll need the FILE "
        L"tag)\r\n"
        L"\t/Walker=USN|MFT      : Walks the file systems entries through MFT parsing or USN Journal enumeration "
        L"(default is USN)\r\n"
        L"\r\n"
        L"\t/KnownLocations|/kl  : Scan a set of locations known to be of interest\r\n"
        L"\t/Shadows             : Add Volume Shadows Copies for selected volumes to parse\r\n"
        L"\r\n"
        L"\t/<DefaultColumnSelection>,...: \r\n"
        L"\tSelects the columns to fill for each file system entry:"
        L"\r\n");

    PrintCommonUsage();

    const ColumnNameDef* pCurCol = NtfsFileInfo::g_NtfsColumnNames;

    while (pCurCol->dwIntention != FILEINFO_NONE)
    {
        log::Info(_L_, L"\t\t%s:\t%s\n", pCurCol->szColumnName, pCurCol->szColumnDesc);
        pCurCol++;
    }

    log::Info(_L_, L"\n\t\tAlias names can be used to specify more than one column:\r\n");

    const ColumnNameDef* pCurAlias = NtfsFileInfo::g_NtfsAliasNames;

    while (pCurAlias->dwIntention != FILEINFO_NONE)
    {
        log::Info(_L_, L"\t\t- %s:\r\n", pCurAlias->szColumnName);
        DWORD dwNumCol = 0;
        const ColumnNameDef* pCurCol = NtfsFileInfo::g_NtfsColumnNames;
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
        log::Info(_L_, (L"\r\n\r\n"));
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
    SaveAndPrintStartTime();

    if (!config.strComputerName.empty())
        SystemDetails::SetOrcComputerName(config.strComputerName);

    PrintComputerName();

    PrintOperatingSystem();

    if (!config.strWalker.empty())
        log::Info(_L_, L"\r\nWalker used           : %s\r\n", config.strWalker.c_str());

    PrintOutputOption(L"FileInfo", config.outFileInfo);
    PrintOutputOption(L"AttrInfo", config.outAttrInfo);
    PrintOutputOption(L"I30Info", config.outI30Info);
    PrintOutputOption(L"Timeline", config.outTimeLine);
    PrintOutputOption(L"SecDescr", config.outSecDescrInfo);

    log::Info(_L_, L"\r\nCSV Columns           :\r\n");
    const ColumnNameDef* pCurCol = NtfsFileInfo::g_NtfsColumnNames;
    DWORD dwNumCol = 0;
    while (pCurCol->dwIntention != FILEINFO_NONE)
    {
        if (config.ColumnIntentions & pCurCol->dwIntention)
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

    log::Info(_L_, L"\r\n\r\nDefault columns       :\r\n");

    pCurCol = NtfsFileInfo::g_NtfsColumnNames;
    dwNumCol = 0;
    while (pCurCol->dwIntention != FILEINFO_NONE)
    {
        if (config.DefaultIntentions & pCurCol->dwIntention)
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
    std::for_each(begin(config.Filters), end(config.Filters), [&bFirstFilt, this](const Filter& filter) {
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

        log::Info(_L_, (filter.bInclude ? L"include columns: \r\n" : L" exclude columns: \r\n"));

        const ColumnNameDef* pCurCol = NtfsFileInfo::g_NtfsColumnNames;
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

    config.locs.PrintLocations(true);

    log::Info(_L_, L"\r\n");
}

void Main::PrintFooter()
{
    log::Info(_L_, L"\r\nLines processed         : %u\r\n", dwTotalFileTreated);

    PrintExecutionTime();
    return;
}
