//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "GetThis.h"

#include <string>

#include "SystemDetails.h"
#include "LogFileWriter.h"
#include "FileFind.h"
#include "ToolVersion.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::GetThis;

void Main::PrintUsage()
{
    log::Info(
        _L_,
        L"\r\n"
        L"Usage: DFIR-Orc.exe GetThis [/out=<Cabinet.cab|Archive.zip|Archive.7z>] [/XOR=0xBADF00D0] "
        L"\t[/sample=<SampleFile>]\r\n"
        L"\t[/config=<ConfigFile>] <foldername>...\r\n"
        L"\r\n"
        L"\t/out=<Folder>|<F.csv>|<F.cab>|<F.zip>|<F.7z>:\r\n"
        L"\t                              Files will be added to existing folder, to cabinet or zip file\r\n"
        L"\t\r\n\r\n"
        L"\t/sample=<FileName>          : Name of the file to copy\r\n"
        L"\t/sample=<FileName>:<Stream> : Name of the Altername Data Stream to copy\r\n"
        L"\t/sample=<FileName>#<EAName> : Name of the Extended Attribute to copy\r\n"
        L"\t/content=(data|strings|raw) : Data to collect (raw is the compressed NTFS stream)"
        L"\t/config=<FileName>          : Config should be loaded from this file\r\n"
        L"\t/flushregistry              : Flushes registry hives (using RegFlushKey)\r\n"
        L"\t<foldername>...             : List of locations where to look for samples (and sub folders)\r\n"
        L"\t/verbose                    : Turns on verbose logging\r\n"
        L"\t/debug                      : Adds debug information (Source File Name, Line number) to output, outputs to "
        L"debugger (OutputDebugString)\r\n"
        L"\t/noconsole                  : Turns off console logging\r\n"
        L"\t/logfile=<FileName>         : All output is duplicated to logfile <FileName>\r\n"
        L"\t/nolimits                   : Ignore all limits, overrides default values\r\n"
        L"\t/reportall                  : Add information about rejected samples (due to limits) to CSV\r\n"
        L"\t/xor=0xBADF00D0             : Pattern used to XOR sample files (optional)\r\n"
        L"\t/hash=<MD5|SHA1|SHA256>    : List hash values stored in GetThis.csv\r\n"
        L"\t/fuzzyhash=<SSDeep|TLSH>    : List fuzzy hash values stored in GetThis.csv\r\n"
        L"\r\n"
        L"Note: config file settings are superseded by command line options\r\n"
        L"\r\n"
        L"\r\n"
        L"\r\n"
        L"\tExtraction syntax:  GetThis.exe [/extract=<Cabinet.cab>] [/out=<Folder>] \r\n"
        L"\r\n"
        L"\t/extract=<Cabinet.cab> : Cabinet file <Cabinet.cab> is to be extracted\r\n"
        L"\t/out=<Folder>          : Files will be extracted into <Folder>\r\n"
        L"\t/utf8,/utf16           : Select utf8 or utf16 encoding (default is utf8)\r\n"
        L"\r\n"
        L"\t/Yara=<Rules.Yara>           : Comma separared list of yara sources\r\n");
}

void Main::PrintParameters()
{
    PrintComputerName();
    PrintOperatingSystem();
    wstring strDesc;

    log::Info(_L_, L"\r\n");

    PrintOutputOption(config.Output);

    PrintBooleanOption(L"Report All", config.bReportAll);
    PrintHashAlgorithmOption(L"Hash", config.CryptoHashAlgs);
    PrintHashAlgorithmOption(L"Fuzzy Hash", config.FuzzyHashAlgs);

    log::Info(_L_, L"\r\nVolumes, Folders to parse:\r\n");

    config.Locations.PrintLocations(true);

    log::Info(_L_, L"\r\n");

    if (config.limits.bIgnoreLimits)
    {
        log::Info(_L_, L"\r\n\tWarning: No limits are imposed to the size and number of collected samples\r\n");
    }
    else
    {
        log::Info(_L_, L"\r\nGlobal limits imposed on collection:\r\n");
        if (config.limits.dwlMaxBytesPerSample == INFINITE)
        {
            log::Info(_L_, L"\tMaximum bytes per sample  = Unlimited\r\n");
        }
        else
        {
            log::Info(_L_, L"\tMaximum bytes per sample  = %I64d\r\n", config.limits.dwlMaxBytesPerSample);
        }
        if (config.limits.dwlMaxBytesTotal == INFINITE)
        {
            log::Info(_L_, L"\tMaximum bytes collected   = Unlimited\r\n");
        }
        else
        {
            log::Info(_L_, L"\tMaximum bytes collected   = %I64d\r\n", config.limits.dwlMaxBytesTotal);
        }
        if (config.limits.dwMaxSampleCount == INFINITE)
        {
            log::Info(_L_, L"\tMaximum number of samples = Unlimited\r\n");
        }
        else
        {
            log::Info(_L_, L"\tMaximum number of samples = %d\r\n", config.limits.dwMaxSampleCount);
        }
    }

    switch (config.content.Type)
    {
        case DATA:
            log::Info(_L_, L"\tDefault content copied is attribute's data\r\n");
            break;
        case STRINGS:
            log::Info(
                _L_,
                L"\tDefault content copied is attribute's strings (min=%d,max=%d)\r\n",
                config.content.MinChars,
                config.content.MaxChars);
            break;
        case RAW:
            log::Info(_L_, L"\tDefault content copied is attribute's raw data on disk\r\n");
            break;
        default:
            break;
    }

    if (!config.listofSpecs.empty())
    {
        log::Info(_L_, L"\r\nSamples looked after:\r\n\r\n");

        for (const auto& aSpec : config.listofSpecs)
        {
            if (!config.limits.bIgnoreLimits)
            {
                log::Info(_L_, L"   Sample: %s", aSpec.Name.c_str());
                if (aSpec.PerSampleLimits.dwlMaxBytesPerSample != INFINITE)
                {
                    log::Info(_L_, L" (max %I64d bytes per sample)", aSpec.PerSampleLimits.dwlMaxBytesPerSample);
                }
                if (aSpec.PerSampleLimits.dwlMaxBytesTotal != INFINITE)
                {
                    log::Info(_L_, L" (max %I64d bytes for all samples)", aSpec.PerSampleLimits.dwlMaxBytesTotal);
                }
                if (aSpec.PerSampleLimits.dwMaxSampleCount != INFINITE)
                {
                    log::Info(_L_, L" (max %d samples)", aSpec.PerSampleLimits.dwMaxSampleCount);
                }
            }
            else
            {
                log::Info(_L_, L"   Sample: %s", aSpec.Name.c_str());
            }
            switch (aSpec.Content.Type)
            {
                case DATA:
                {
                    log::Info(_L_, L" (copy data)\r\n", aSpec.Content.MinChars, aSpec.Content.MaxChars);
                }
                break;
                case STRINGS:
                {
                    log::Info(
                        _L_, L" (copy strings:min=%d,max=%d)\r\n", aSpec.Content.MinChars, aSpec.Content.MaxChars);
                }
                break;
                case RAW:
                {
                    log::Info(_L_, L" (copy raw data)\r\n", aSpec.Content.MinChars, aSpec.Content.MaxChars);
                }
                break;
                default:
                    break;
            }
            log::Info(_L_, L"\r\n");

            for (const auto& item : aSpec.Terms)
            {
                log::Info(_L_, L"      %s\r\n", item->GetDescription().c_str());
            }

            log::Info(_L_, L"\r\n");
        }
    }

    if (!config.listOfExclusions.empty())
    {
        log::Info(_L_, L"\r\nSamples excluded:\r\n\r\n");

        std::for_each(
            begin(config.listOfExclusions),
            end(config.listOfExclusions),
            [this](const std::shared_ptr<FileFind::SearchTerm>& item) {
                auto desc = item->GetDescription();
                log::Info(_L_, L"      %s\r\n", desc.c_str());
            });

        log::Info(_L_, L"\r\n");
    }

    SaveAndPrintStartTime();
}

void Main::PrintFooter()
{
    PrintExecutionTime();
}
