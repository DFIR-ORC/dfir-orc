//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

// WOLFLAUNCHER
#include "Configuration/ConfigFile_Common.h"
#include "ConfigFile_WOLFLauncher.h"
#include "CaseInsensitive.h"
#include "Log/UtilitiesLoggerConfiguration.h"
#include "Command/WolfLauncher/ConsoleConfiguration.h"

// <TempDir>%Temp%\WorkingTemp</TempDir>

// <Execute name="NTFSInfo.exe" resource="7z:#ToolCab|ntfsinfo.exe" />

using namespace Orc;

HRESULT wolf_execute(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNode(L"execute", dwIndex, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name", WOLFLAUNCHER_EXENAME, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"run", WOLFLAUNCHER_EXERUN, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"run32", WOLFLAUNCHER_EXERUN32, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"run64", WOLFLAUNCHER_EXERUN64, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

// <Output name="ntfsinfo.csv" source="File" argument="/out={Output}" />
HRESULT wolf_output(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"output", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name", WOLFLAUNCHER_OUTNAME, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"source", WOLFLAUNCHER_OUTSOURCE, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"argument", WOLFLAUNCHER_OUTARGUMENT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"filematch", WOLFLAUNCHER_OUTFILEMATCH, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

// <Input name="ntfsinfo_config.xml" argument="/config={Input}" source="res:ntfsinfo_config" />
HRESULT wolf_input(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"input", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name", WOLFLAUNCHER_INNAME, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"source", WOLFLAUNCHER_INSOURCE, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"argument", WOLFLAUNCHER_INARGUMENT, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

// <Command keyword="NTFSInfo">
HRESULT wolf_command(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"command", dwIndex, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(wolf_execute, WOLFLAUNCHER_COMMAND_EXECUTE)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(wolf_input, WOLFLAUNCHER_COMMAND_INPUT)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(wolf_output, WOLFLAUNCHER_COMMAND_OUTPUT)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChildNodeList(L"argument", WOLFLAUNCHER_COMMAND_ARGUMENT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"keyword", WOLFLAUNCHER_COMMAND_CMDKEYWORD, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"winver", WOLFLAUNCHER_COMMAND_WINVER, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"systemtype", WOLFLAUNCHER_COMMAND_SYSTEMTYPE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"queue", WOLFLAUNCHER_COMMAND_QUEUE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"optional", WOLFLAUNCHER_COMMAND_OPTIONAL, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"timeout", WOLFLAUNCHER_COMMAND_TIMEOUT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"diskfree", WOLFLAUNCHER_COMMAND_DISKFREE, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT wolf_restrictions(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNode(L"restrictions", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"JobMemoryLimit", WOLFLAUNCHER_JOBMEMORY, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent[dwIndex].AddAttribute(L"ProcessMemoryLimit", WOLFLAUNCHER_PROCESSMEMORY, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"ElapsedTimeLimit", WOLFLAUNCHER_ELAPSEDTIME, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(
            hr = parent[dwIndex].AddAttribute(L"ProcessCPULimit", WOLFLAUNCHER_PERPROCESSUSERTIME, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"JobCPULimit", WOLFLAUNCHER_JOBUSERTIME, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"CpuRate", WOLFLAUNCHER_CPU_RATE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"CpuWeight", WOLFLAUNCHER_CPU_WEIGHT, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT wolf_archive(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"archive", dwIndex, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name", WOLFLAUNCHER_ARCHIVE_NAME, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"keyword", WOLFLAUNCHER_ARCHIVE_KEYWORD, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent[dwIndex].AddAttribute(L"concurrency", WOLFLAUNCHER_ARCHIVE_CONCURRENCY, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"compression", WOLFLAUNCHER_ARCHIVE_COMPRESSION, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"repeat", WOLFLAUNCHER_ARCHIVE_REPEAT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(wolf_command, WOLFLAUNCHER_ARCHIVE_COMMAND)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(wolf_restrictions, WOLFLAUNCHER_ARCHIVE_RESTRICTIONS)))
        return hr;
    if (FAILED(
            hr =
                parent[dwIndex].AddAttribute(L"command_timeout", WOLFLAUNCHER_ARCHIVE_CMD_TIMEOUT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"archive_timeout", WOLFLAUNCHER_ARCHIVE_TIMEOUT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"optional", WOLFLAUNCHER_ARCHIVE_OPTIONAL, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"childdebug", WOLFLAUNCHER_ARCHIVE_CHILDDEBUG, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"diskfree", WOLFLAUNCHER_ARCHIVE_DISKFREE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent[dwIndex].AddAttribute(
                L"physicalmemory", WOLFLAUNCHER_ARCHIVE_PHYSICALMEMORY, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Wolf::recipient(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"recipient", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name", WOLFLAUNCHER_RECIPIENT_NAME, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"archive", WOLFLAUNCHER_RECIPIENT_ARCHIVE, ConfigItem::MANDATORY)))
        return hr;
    return S_OK;
}

// The Wolf <upload> list is the shared OrcLib schema. Kept as a named entry point because both root
// schemas call it by name, and because OrcLibTest links OrcLib only: the registration itself has to
// live there to be unit-testable, and one copy of it is the only way the two forms cannot drift.
HRESULT Orc::Config::Wolf::upload(ConfigItem& parent, DWORD dwIndex)
{
    return Orc::Config::Common::upload_list(parent, dwIndex);
}

HRESULT Orc::Config::Wolf::root(ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    item.strName = L"wolf";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = 0L;
    item.Status = ConfigItem::MISSING;

    if (FAILED(hr = item.AddChild(wolf_archive, WOLFLAUNCHER_ARCHIVE)))
        return hr;
    if (FAILED(hr = item.AddChild(recipient, WOLFLAUNCHER_RECIPIENT)))
        return hr;
    if (FAILED(hr = item.AddChild(Orc::Command::UtilitiesLoggerConfiguration::Register, WOLFLAUNCHER_LOG)))
        return hr;
    if (FAILED(hr = item.AddChild(Orc::Command::ConsoleConfiguration::Register, WOLFLAUNCHER_CONSOLE)))
        return hr;
    if (FAILED(hr = item.AddChild(L"outline", Orc::Config::Common::output, WOLFLAUNCHER_OUTLINE)))
        return hr;
    if (FAILED(hr = item.AddChild(L"outcome", Orc::Config::Common::output, WOLFLAUNCHER_OUTCOME)))
        return hr;
    if (FAILED(
            hr = item[WOLFLAUNCHER_OUTLINE].AddAttribute(L"encrypt", WOLFLAUNCHER_OUTLINE_ENCRYPT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = item[WOLFLAUNCHER_OUTCOME].AddAttribute(L"encrypt", WOLFLAUNCHER_OUTCOME_ENCRYPT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item[WOLFLAUNCHER_LOG].AddAttribute(L"encrypt", WOLFLAUNCHER_LOG_ENCRYPT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = item[WOLFLAUNCHER_CONSOLE].AddAttribute(L"encrypt", WOLFLAUNCHER_CONSOLE_ENCRYPT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = item[WOLFLAUNCHER_OUTLINE].AddAttribute(L"upload", WOLFLAUNCHER_OUTLINE_UPLOAD, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = item[WOLFLAUNCHER_OUTCOME].AddAttribute(L"upload", WOLFLAUNCHER_OUTCOME_UPLOAD, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item[WOLFLAUNCHER_LOG].AddAttribute(L"upload", WOLFLAUNCHER_LOG_UPLOAD, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = item[WOLFLAUNCHER_CONSOLE].AddAttribute(L"upload", WOLFLAUNCHER_CONSOLE_UPLOAD, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"childdebug", WOLFLAUNCHER_CHILDDEBUG, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"command_timeout", WOLFLAUNCHER_GLOBAL_CMD_TIMEOUT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"archive_timeout", WOLFLAUNCHER_GLOBAL_ARCHIVE_TIMEOUT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"werdontshowui", WOLFLAUNCHER_WERDONTSHOWUI, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = Orc::Config::Wolf::upload(item, WOLFLAUNCHER_UPLOAD)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"priority", WOLFLAUNCHER_PRIORITY, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"MultipleInstance", WOLFLAUNCHER_MULTIPLE_INSTANCE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"InstanceId", WOLFLAUNCHER_INSTANCE_ID, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = Orc::Config::Common::uploads(item, WOLFLAUNCHER_UPLOADS)))
        return hr;

    return S_OK;
}

// Declared in the header beside the 'encrypt'/'upload' attributes it reads; see there for why it is
// shared rather than copied into each reader.
bool Orc::Config::Wolf::IsExplicitNo(const std::wstring& value)
{
    using namespace std::string_view_literals;

    return equalCaseInsensitive(L"No"sv, value) || equalCaseInsensitive(L"false"sv, value)
        || equalCaseInsensitive(L"0"sv, value);
}
