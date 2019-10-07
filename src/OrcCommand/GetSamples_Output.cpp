//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "GetSamples.h"
#include "LogFileWriter.h"
#include "ToolVersion.h"

#include <string>

using namespace std;

using namespace Orc;
using namespace Orc::Command::GetSamples;

void Main::PrintUsage()
{
    log::Info(
        _L_,
        L"\n"
        L"usage: DFIR-Orc.exe GetSamples [/Config=GetSamplesConfig.xml] [/Out=GetSample.7z]\r\n"
        L"                               [/GetThis=GetThisConfig.xml] [/Autoruns[=<AutoRuns.xml>]]\r\n"
        L"                               [/TempDir=c:\\temp]\r\n"
        L"\r\n\r\n"
        L"\t/GetThis=GetThisConfig.xml       : Output result config file for GetThis into GetThisConfig.xml\r\n"
        L"\t/GetThisArgs=\"/nolimits ...\"   : Arguments to be forwared to 'GetThis'\r\n"
        L"\t/SampleInfo=GetSamplesInfo.csv   : Collect sample related information into GetSamplesInfo.csv\r\n"
        L"\t/TimeLine=Timeline.csv           : Collect timeline related information into Timeline.csv\r\n"
        L"\t/Out=GetSamples.7z               : Run GetThis using generated config file and collect samples into "
        L"GetSample.7z\r\n"
        L"\t/Compression=<CompressionLevel>  : Set archive compression level\r\n"
        L"\t/Password=<aPassword>            : For archive formats supporting it, set archive password\r\n"
        L"\t/Autoruns=Autoruns.xml           : Path to existing autoruns.xml file -> File is loaded instead of running "
        L"autoruns\r\n"
        L"\t                                   If file does not exist, autorunsc.exe is run and output placed in the "
        L"file\r\n"
        L"\t/Autoruns                        : Extract and run autorunsc.exe to collect ASE information\r\n"
        L"\t/TempDir=c:\\temp                 : Use c:\\temp to store temporary files\r\n"
        L"\t/NoSigCheck                      : Do not check all sample signatures (only signatures in autoruns output "
        L"will be used)\r\n"
        L"\r\n");
}

void Main::PrintParameters()
{
    PrintComputerName();
    PrintOperatingSystem();

    log::Info(_L_, L"\r\nRunning with options:\r\n");
    if (config.bRunAutoruns)
    {
        log::Info(_L_, L"\tEmbedded Autoruns enabled\r\n");
        if (config.bKeepAutorunsXML && !config.autorunsOutput.Type != OutputSpec::None)
            log::Info(_L_, L"\tAutoruns output saved in %s\r\n", config.autorunsOutput.Path.c_str());
    }
    else if (config.autorunsOutput.Type != OutputSpec::None)
    {
        log::Info(_L_, L"\tUsing Autoruns XML %s\r\n", config.autorunsOutput.Path.c_str());
    }
    else
    {
        log::Info(_L_, L"\tNot using Autoruns ASE information\r\n", config.autorunsOutput.Path.c_str());
    }

    if (config.criteriasConfig.Type == OutputSpec::File)
    {
        log::Info(_L_, L"\tCreate GetThis configuration in %s\r\n", config.criteriasConfig.Path.c_str());
    }

    PrintOutputOption(config.samplesOutput);

    if (config.sampleinfoOutput.Type != OutputSpec::None)
    {
        log::Info(_L_, L"\tCollect sample related information in %s\r\n", config.sampleinfoOutput.Path.c_str());
    }

    if (config.timelineOutput.Type != OutputSpec::None)
    {
        log::Info(_L_, L"\tCollect timeline information in %s\r\n", config.timelineOutput.Path.c_str());
    }

    if (config.tmpdirOutput.Type != OutputSpec::None)
    {
        log::Info(_L_, L"\tCreate temporary files in %s\r\n", config.tmpdirOutput.Path.c_str());
    }

    if (config.bNoSigCheck)
    {
        log::Info(_L_, L"\tDo not check sample signatures\r\n");
    }

    SaveAndPrintStartTime();
}

void Main::PrintFooter()
{
    PrintExecutionTime();
}
