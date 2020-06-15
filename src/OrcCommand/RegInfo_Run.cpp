//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//

#include "stdafx.h"

#include <string>
#include <sstream>

#include "OrcLib.h"

#include "RegInfo.h"

#include "SystemDetails.h"
#include "FileFind.h"
#include "MFTWalker.h"
#include "Location.h"
#include "FileFind.h"
#include "NTFSStream.h"
#include "FileStream.h"

#include "TableOutputWriter.h"
#include "TableOutput.h"

#include "ParameterCheck.h"
#include "LogFileWriter.h"
#include "WideAnsi.h"

#include "RegistryWalker.h"

using namespace std;

using namespace Orc;
using namespace Orc::Command::RegInfo;

Main::RegInfoDescription Main::_InfoDescription[] = {
    {REGINFO_COMPUTERNAME, L"ComputerName", L"Name of computer"},

    {REGINFO_TERMNAME, L"TemplateName", L"Name of associated template"},
    {REGINFO_TERMDESCRIPTION, L"SearchTerm", L"Search Term description"},

    {REGINFO_LASTMODDATE, L"LastModificationDate", L"Date the _key_ was last modified"},

    {REGINFO_KEYNAME, L"KeyName", L"Key name"},
    {REGINFO_KEYTREE, L"KeyTree", L"Key full path"},

    {REGINFO_VALUENAME, L"ValueName", L"Name of value"},
    {REGINFO_VALUETYPE, L"ValueType", L"Type of value"},
    {REGINFO_VALUESIZE, L"ValueSize", L"Size of value"},
    {REGINFO_VALUEFLAG, L"ValueFlag", L"Flag of value"},
    {REGINFO_VALUE, L"ValueData", L"Bytes in value"},
    {REGINFO_VALUEDUMPFILE, L"ValueDumpFile", L"Dump of value"},

    {REGINFO_NONE, NULL, NULL}};

// aliases for other colums
Main::RegInfoDescription Main::_AliasDescription[] = {
    {REGINFO_TIMELINE, L"TimeLine", L"Timeline of reg key information (_not_ sorted)"},
    {REGINFO_ALL, L"All", L"All available information"},
    {REGINFO_NONE, NULL, NULL}};

HRESULT Main::BindColumns(
    const logger& pLog,
    Main::RegInfoType columns,
    const std::vector<TableOutput::Column>& sqlcolumns,
    const std::shared_ptr<TableOutput::IWriter>& pWriter)
{
    DWORD dwIndex = 0L;

    const RegInfoDescription* pCurCol = _InfoDescription;

    DWORD dwMaxColumns = 0L;

    auto pConnectedWriter = std::dynamic_pointer_cast<TableOutput::IConnectWriter>(pWriter);
    if (!pConnectedWriter)
    {
        log::Error(pLog, E_INVALIDARG, L"Table writer is not of connected type, no binding of columns");
        return E_INVALIDARG;
    }

    // NULL First column

    while (pCurCol->Type != REGINFO_NONE)
    {
        auto it =
            std::find_if(begin(sqlcolumns), end(sqlcolumns), [pCurCol](const TableOutput::Column& coldef) -> bool {
                return pCurCol->ColumnName == coldef.ColumnName;
            });

        if (it == end(sqlcolumns))
        {
            log::Error(pLog, E_UNEXPECTED, L"Column name %s is NOT listed in Schema... Binding to NULL\r\n");
            dwIndex++;
            pConnectedWriter->AddColumn(dwIndex, pCurCol->ColumnName, TableOutput::Nothing);
        }
        else
        {
            if (columns & pCurCol->Type)
            {
                dwIndex++;
                pConnectedWriter->AddColumn(dwIndex, pCurCol->ColumnName, it->Type, it->dwMaxLen);
            }
            else
            {
                dwIndex++;
                pConnectedWriter->AddColumn(dwIndex, pCurCol->ColumnName, TableOutput::Nothing);
            }
        }
        pCurCol++;
    }
    if (!dwIndex)
        return E_INVALIDARG;

    return S_OK;
}

HRESULT Main::WriteCompName(ITableOutput& output)
{
    HRESULT hr = E_FAIL;

    if (config.Information & REGINFO_COMPUTERNAME)
    {
        if (config.strComputerName.empty())
            SystemDetails::WriteComputerName(output);
        else
            output.WriteString(config.strComputerName.c_str());
    }
    else
        output.WriteNothing();

    return S_OK;
}

HRESULT Main::WriteTermName(ITableOutput& output, const std::shared_ptr<RegFind::SearchTerm>& Term)
{
    HRESULT hr = E_FAIL;

    if (config.Information & REGINFO_TERMNAME)
    {
        output.WriteString(Term->GetTermName().c_str());
    }
    else
        output.WriteNothing();

    return S_OK;
}

HRESULT Main::WriteSearchDescription(ITableOutput& output, const std::shared_ptr<RegFind::SearchTerm>& Term)
{
    HRESULT hr = E_FAIL;

    if (config.Information & REGINFO_TERMDESCRIPTION)
    {
        output.WriteInteger((DWORD)Term->m_criteriaRequired);
    }
    else
        output.WriteNothing();

    return S_OK;
}

HRESULT Main::WriteKeyInformation(ITableOutput& output, RegFind::Match::KeyNameMatch& Match)
{
    HRESULT hr = E_FAIL;

    if (config.Information & REGINFO_LASTMODDATE)
        output.WriteFileTime(Match.LastModificationTime);
    else
        output.WriteNothing();

    if (config.Information & REGINFO_KEYNAME)
        output.WriteString(Match.ShortKeyName.c_str());
    else
        output.WriteNothing();

    if (config.Information & REGINFO_KEYTREE)
    {
        output.WriteString(Match.KeyName.c_str());
    }
    else
        output.WriteNothing();

    return S_OK;
}

// helper function
inline bool ContainsBadChars(LPWSTR wstrString, size_t StrLen)
{
    DWORD i = 0;
    size_t j = 0;
    while (OutputBadChars[i] != '\0')
    {
        for (j = 0; j < StrLen; j++)
        {
            if (wstrString[j] == OutputBadChars[i])
            {
                return true;
            }
        }
        i++;
    }
    return false;
}

HRESULT DumpValue(const logger& pLog, RegFind::Match::ValueNameMatch& Match, std::wstring wstrFullPath)
{
    HRESULT hr;
    FileStream DataDumpFile(pLog);
    size_t dwLen = Match.DatasLength;

    if (FAILED(hr = DataDumpFile.WriteTo(wstrFullPath.c_str())))
    {
        log::Error(pLog, hr, L"Can't open file \"%S\" (reason : 0x%x)\r\n", wstrFullPath.c_str(), hr);
        return hr;
    }

    ULONGLONG ulWritten;
    DataDumpFile.Write(Match.Datas.get(), Match.DatasLength, &ulWritten);
    if (ulWritten != dwLen)
        log::Error(
            pLog, hr, L"Can't write content of value %s (key: %s).", Match.ValueName.c_str(), Match.KeyName.c_str());
    DataDumpFile.Close();

    return hr;
}

std::wstring GetFullPath(
    const logger& pLog,
    std::string& KeyName,
    std::string& ValueName,
    std::wstring& NamePrefix,
    std::wstring& OutputPath)
{

    std::string DataDumpFileName(KeyName + "_" + ValueName);
    std::wstring wstrDataDumpFileName;
    AnsiToWide(pLog, DataDumpFileName, wstrDataDumpFileName);
    wstring FullName = NamePrefix + L"_" + wstrDataDumpFileName + L".ddmp";
    replace(FullName.begin(), FullName.end(), '\\', '_');
    replace(FullName.begin(), FullName.end(), ':', '_');
    replace(FullName.begin(), FullName.end(), '/', '_');
    replace(FullName.begin(), FullName.end(), '*', '_');
    replace(FullName.begin(), FullName.end(), '?', '_');
    replace(FullName.begin(), FullName.end(), '>', '_');
    replace(FullName.begin(), FullName.end(), '<', '_');
    replace(FullName.begin(), FullName.end(), '|', '_');
    return std::wstring(OutputPath + L"\\" + FullName);
}

HRESULT Main::WriteValueInformation(
    const logger& pLog,
    ITableOutput& output,
    RegFind::Match::ValueNameMatch& Match,
    std::wstring& FileNamePrefix,
    size_t CvsMaxsize)
{
    HRESULT hr = S_OK;

    if (config.Information & REGINFO_LASTMODDATE)
        output.WriteFileTime(Match.LastModificationTime);
    else
        output.WriteNothing();

    if (config.Information & REGINFO_KEYNAME)
        output.WriteString(Match.ShortKeyName.c_str());
    else
        output.WriteNothing();

    if (config.Information & REGINFO_KEYTREE)
    {
        output.WriteString(Match.KeyName.c_str());
    }
    else
        output.WriteNothing();

    if (config.Information & REGINFO_VALUENAME)
    {
        output.WriteString(Match.ValueName.c_str());
    }
    else
        output.WriteNothing();

    if (config.Information & REGINFO_VALUETYPE)
    {
        DWORD i = 0;
        while (g_ValyeTypeDefinitions[i].Type != Match.ValueType && i < _countof(g_ValyeTypeDefinitions))
        {
            i++;
        }
        if (i == _countof(g_ValyeTypeDefinitions))
            output.WriteString("REG_NO_TYPE (!)");
        else
            output.WriteString(g_ValyeTypeDefinitions[i].szTypeName);
    }
    else
        output.WriteNothing();

    if (config.Information & REGINFO_VALUESIZE)
        output.WriteInteger((ULONGLONG)Match.DatasLength);
    else
        output.WriteNothing();

    if (config.Information & REGINFO_VALUEFLAG)
    {
        if (Match.Datas.get() == nullptr)
        {
            output.WriteString("NOTINHIVE");
        }
        else
        {
            if (Match.DatasLength > CvsMaxsize)
            {
                output.WriteString("DUMPFILE");
            }
            else if (
                ((Match.ValueType == REG_SZ) || (Match.ValueType == REG_MULTI_SZ) || (Match.ValueType == REG_EXPAND_SZ))
                && (ContainsBadChars((LPWSTR)(Match.Datas.get()), Match.DatasLength / sizeof(WCHAR))))
            {
                output.WriteString("HASBADCHARS");
            }
            else
            {
                output.WriteString("PRESENT");
            }
        }
    }
    else
        output.WriteNothing();

    if (config.Information & REGINFO_VALUE)
    {
        if (Match.Datas.get() == nullptr)
        {
            output.WriteNothing();
            output.WriteNothing();
        }
        else
        {
            size_t dwLen = Match.DatasLength;
            if (dwLen > CvsMaxsize)
            {
                // Do not write value into CSV
                output.WriteNothing();
                if (config.Information & REGINFO_VALUEDUMPFILE)
                {

                    wstring wstrFullPath =
                        GetFullPath(_L_, Match.KeyName, Match.ValueName, FileNamePrefix, config.Output.Path);
                    hr = DumpValue(pLog, Match, wstrFullPath);

                    output.WriteString(wstrFullPath.c_str());
                }
                else
                {
                    output.WriteNothing();
                }
            }
            else
            {
                CBinaryBuffer DataBuffer(Match.Datas.get(), dwLen);

                wstring wstrData;
                BYTE* pDataBuffer;
                wstring* wstrTmp;
                LPWSTR lpCurrent;

                switch (Match.ValueType)
                {

                    case REG_BINARY:
                    case REG_DWORD_BIG_ENDIAN:
                        output.WriteBytes(DataBuffer);
                        // No file dump
                        output.WriteNothing();
                        break;
                        // endianness...
                    case REG_DWORD:
                    case REG_QWORD:
                        pDataBuffer = new BYTE[Match.DatasLength];
                        for (dwLen = Match.DatasLength; dwLen > 0; dwLen--)
                        {
                            pDataBuffer[Match.DatasLength - dwLen] = Match.Datas.get()[dwLen - 1];
                        }
                        DataBuffer.SetData(pDataBuffer, Match.DatasLength);
                        delete[] pDataBuffer;

                        output.WriteBytes(DataBuffer);
                        // No file dump
                        output.WriteNothing();
                        break;

                    case REG_EXPAND_SZ:
                    case REG_SZ:
                        if (ContainsBadChars((LPWSTR)(Match.Datas.get()), Match.DatasLength / sizeof(WCHAR)))
                        {
                            wstring wstrFullPath =
                                GetFullPath(_L_, Match.KeyName, Match.ValueName, FileNamePrefix, config.Output.Path);
                            hr = DumpValue(pLog, Match, wstrFullPath);
                            output.WriteNothing();
                            output.WriteString(wstrFullPath.c_str());
                        }
                        else
                        {
                            output.WriteString((LPWSTR)(Match.Datas.get()));
                            // No file dump
                            output.WriteNothing();
                        }
                        break;
                    case REG_MULTI_SZ:

                        if (ContainsBadChars((LPWSTR)(Match.Datas.get()), Match.DatasLength / sizeof(WCHAR)))
                        {
                            wstring wstrFullPath =
                                GetFullPath(_L_, Match.KeyName, Match.ValueName, FileNamePrefix, config.Output.Path);
                            hr = DumpValue(pLog, Match, wstrFullPath);

                            output.WriteNothing();
                            output.WriteString(wstrFullPath.c_str());
                        }
                        else
                        {
                            dwLen = 0;
                            lpCurrent = (LPWSTR)Match.Datas.get();
                            while (dwLen < Match.DatasLength)
                            {
                                wstrTmp = new wstring(lpCurrent);
                                dwLen += (wcslen(lpCurrent) + 1) * sizeof(WCHAR);
                                lpCurrent += wcslen(lpCurrent) + 1;
                                wstrData += *wstrTmp + L" <> ";
                                delete wstrTmp;
                            }
                            output.WriteString(wstrData.c_str());
                            // No file dump
                            output.WriteNothing();
                        }
                        break;
                    default:
                        output.WriteNothing();
                        // No file dump
                        output.WriteNothing();
                        break;
                }
            }
        }
    }
    return hr;
}

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = LoadWinTrust()))
        return hr;

    // Flush key before runnning
    // Useless if MFT parser not used but done every time so nobody will forget to mention it in configuration...
    DWORD dwGLE = 0L;

    log::Info(_L_, L"Flushing HKEY_LOCAL_MACHINE\r\n");
    dwGLE = RegFlushKey(HKEY_LOCAL_MACHINE);
    if (dwGLE != ERROR_SUCCESS)
        log::Error(_L_, HRESULT_FROM_WIN32(dwGLE), L"Flushing HKEY_LOCAL_MACHINE failed.\r\n");

    log::Info(_L_, L"Flushing HKEY_USERS\r\n");
    dwGLE = RegFlushKey(HKEY_USERS);
    if (dwGLE != ERROR_SUCCESS)
        log::Error(_L_, HRESULT_FROM_WIN32(dwGLE), L"Flushing HKEY_USERS failed.\r\n");

    log::Info(_L_, L"Finding files required.\r\n");
    if (FAILED(hr = config.m_HiveQuery.BuildStreamList(_L_)))
    {
        log::Error(_L_, hr, L"Failed to build hive stream list\r\n");
        return hr;
    }

    std::for_each(
        config.m_HiveQuery.m_Queries.begin(),
        config.m_HiveQuery.m_Queries.end(),
        [this](shared_ptr<HiveQuery::SearchQuery> Query) {
            HRESULT hr = E_FAIL;

            GetSystemTimeAsFileTime(&CollectionDate);
            SystemDetails::GetOrcComputerName(ComputerName);

            std::shared_ptr<TableOutput::IWriter> pRegInfoWriter;

            if (config.Output.Type & OutputSpec::Kind::TableFile)
            {
                if (nullptr == (pRegInfoWriter = GetRegInfoWriter(config.Output)))
                {
                    log::Error(_L_, E_FAIL, L"Failed to create output file.\r\n");
                    return E_FAIL;
                }
            }

            log::Info(_L_, L"Hive parsing :\r\n");
            std::for_each(
                begin(Query->StreamList), end(Query->StreamList), [this, &pRegInfoWriter, Query](Hive& aHive) {
                    HRESULT hr = E_FAIL;

                    log::Info(_L_, L"\tParsing hive %s\r\n", aHive.FileName.c_str());

                    if (config.Output.Type & OutputSpec::Kind::Directory)
                    {
                        // generate log filename from hive path

                        std::wstring aFileName(aHive.FileName);

                        std::replace(aFileName.begin(), aFileName.end(), L'\\', L'_');
                        std::replace(aFileName.begin(), aFileName.end(), L':', L'_');

                        if (nullptr == (pRegInfoWriter = GetRegInfoWriter(config.Output, aFileName)))
                        {
                            log::Error(_L_, E_FAIL, L"Failed to create output file information file\r\n");
                            return;
                        }
                    }

                    auto& output = *pRegInfoWriter;

                    if (aHive.Stream)
                    {
                        if (FAILED(hr = Query->QuerySpec.Find(aHive.Stream, nullptr, nullptr)))
                        {
                            log::Error(_L_, E_FAIL, L"Failed to search into hive \"%s\"\r\n", aHive.FileName);
                        }
                        else
                        {
                            auto& Results = Query->QuerySpec.Matches();

                            // write matching elements
                            for_each(
                                Results.begin(),
                                Results.end(),
                                [this, &output, &aHive](
                                    std::pair<shared_ptr<RegFind::SearchTerm>, std::shared_ptr<RegFind::Match>> elt) {
                                    // write matching keys
                                    for_each(
                                        elt.second->MatchingKeys.begin(),
                                        elt.second->MatchingKeys.end(),
                                        [this, &output, elt](RegFind::Match::KeyNameMatch& key) {
                                            WriteCompName(output);
                                            WriteTermName(output, elt.first);
                                            WriteSearchDescription(output, elt.first);
                                            WriteKeyInformation(output, key);
                                            output.WriteNothing();
                                            output.WriteNothing();
                                            output.WriteNothing();
                                            output.WriteNothing();
                                            output.WriteNothing();
                                            output.WriteNothing();
                                            output.WriteEndOfLine();
                                        });

                                    // write matching values

                                    for_each(
                                        elt.second->MatchingValues.begin(),
                                        elt.second->MatchingValues.end(),
                                        [this, &output, elt, &aHive](RegFind::Match::ValueNameMatch& value) {
                                            WriteCompName(output);
                                            WriteTermName(output, elt.first);
                                            WriteSearchDescription(output, elt.first);
                                            WriteValueInformation(
                                                _L_, output, value, aHive.FileName, config.CsvValueLengthLimit);
                                            output.WriteEndOfLine();
                                        });
                                });
                        }
                    }
                    else
                    {
                        log::Error(_L_, E_FAIL, L"Can't open hive \"%s\"\r\n", aHive.FileName);
                    }
                    Query->QuerySpec.ClearMatches();
                });

            log::Info(_L_, L"\r\n\r\n");
            return hr;
        });
    return S_OK;
}
