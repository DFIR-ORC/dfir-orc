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
#include "WideAnsi.h"

#include "RegistryWalker.h"

using namespace Orc;
using namespace Orc::Command::RegInfo;

// Characters banned from output (break csv file...)
// end the table with a null character
constexpr WCHAR kOutputBadChars[] = {'\n', '"', '\0'};

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

HRESULT Main::WriteComputerName(ITableOutput& output)
{
    if (config.Information & REGINFO_COMPUTERNAME)
    {
        if (m_utilitiesConfig.strComputerName.empty())
        {
            SystemDetails::WriteComputerName(output);
        }
        else
        {
            output.WriteString(m_utilitiesConfig.strComputerName);
        }
    }
    else
    {
        output.WriteNothing();
    }

    return S_OK;
}

HRESULT Main::WriteTermName(ITableOutput& output, const std::shared_ptr<RegFind::SearchTerm>& term)
{
    if (config.Information & REGINFO_TERMNAME)
    {
        output.WriteString(term->GetTermName());
    }
    else
    {
        output.WriteNothing();
    }

    return S_OK;
}

HRESULT Main::WriteSearchDescription(ITableOutput& output, const std::shared_ptr<RegFind::SearchTerm>& term)
{
    if (config.Information & REGINFO_TERMDESCRIPTION)
    {
        output.WriteInteger((DWORD)term->m_criteriaRequired);
    }
    else
    {
        output.WriteNothing();
    }

    return S_OK;
}

HRESULT Main::WriteKeyInformation(ITableOutput& output, const RegFind::Match::KeyNameMatch& match)
{
    if (config.Information & REGINFO_LASTMODDATE)
    {
        output.WriteFileTime(match.LastModificationTime);
    }
    else
    {
        output.WriteNothing();
    }

    if (config.Information & REGINFO_KEYNAME)
    {
        output.WriteString(match.ShortKeyName);
    }
    else
    {
        output.WriteNothing();
    }

    if (config.Information & REGINFO_KEYTREE)
    {
        output.WriteString(match.KeyName);
    }
    else
    {
        output.WriteNothing();
    }

    return S_OK;
}

inline bool ContainsBadChars(LPWSTR pString, size_t strLength)
{
    DWORD i = 0;
    size_t j = 0;

    while (kOutputBadChars[i] != '\0')
    {
        for (j = 0; j < strLength; j++)
        {
            if (pString[j] == kOutputBadChars[i])
            {
                return true;
            }
        }

        i++;
    }

    return false;
}

HRESULT DumpValue(const RegFind::Match::ValueNameMatch& match, std::wstring outputPath)
{
    FileStream dataDumpFile;

    HRESULT hr = dataDumpFile.WriteTo(outputPath.c_str());
    if (FAILED(hr))
    {
        Log::Error(L"Can't open file '{}' [{}]", outputPath, SystemError(hr));
        return hr;
    }

    ULONGLONG ulWritten = 0;
    const size_t dwLen = match.DatasLength;
    dataDumpFile.Write(match.Datas.get(), match.DatasLength, &ulWritten);
    if (ulWritten != dwLen)
    {
        Log::Error("Can't write content of value: '{}' with key: '{}'", match.ValueName, match.KeyName);
    }

    dataDumpFile.Close();
    return hr;
}

std::wstring GetFullPath(
    const std::string& keyName,
    const std::string& valueName,
    const std::wstring& namePrefix,
    std::wstring& outputPath)
{
    std::string dataDumpFileName(keyName + "_" + valueName);
    std::wstring dataDumpFileNameW;
    AnsiToWide(dataDumpFileName, dataDumpFileNameW);
    std::wstring fullName = namePrefix + L"_" + dataDumpFileNameW + L".ddmp";
    std::replace(fullName.begin(), fullName.end(), '\\', '_');
    std::replace(fullName.begin(), fullName.end(), ':', '_');
    std::replace(fullName.begin(), fullName.end(), '/', '_');
    std::replace(fullName.begin(), fullName.end(), '*', '_');
    std::replace(fullName.begin(), fullName.end(), '?', '_');
    std::replace(fullName.begin(), fullName.end(), '>', '_');
    std::replace(fullName.begin(), fullName.end(), '<', '_');
    std::replace(fullName.begin(), fullName.end(), '|', '_');
    return outputPath + L"\\" + fullName;
}

HRESULT Main::WriteValueInformation(
    ITableOutput& output,
    const RegFind::Match::ValueNameMatch& match,
    const std::wstring& fileNamePrefix,
    size_t csvMaxSize)
{
    HRESULT hr = S_OK;

    if (config.Information & REGINFO_LASTMODDATE)
    {
        output.WriteFileTime(match.LastModificationTime);
    }
    else
    {
        output.WriteNothing();
    }

    if (config.Information & REGINFO_KEYNAME)
    {
        output.WriteString(match.ShortKeyName);
    }
    else
    {
        output.WriteNothing();
    }

    if (config.Information & REGINFO_KEYTREE)
    {
        output.WriteString(match.KeyName);
    }
    else
    {
        output.WriteNothing();
    }

    if (config.Information & REGINFO_VALUENAME)
    {
        output.WriteString(match.ValueName);
    }
    else
    {
        output.WriteNothing();
    }

    if (config.Information & REGINFO_VALUETYPE)
    {
        DWORD i = 0;
        while (g_ValueTypeDefinitions[i].Type != match.ValueType && i < _countof(g_ValueTypeDefinitions))
        {
            i++;
        }

        if (i == _countof(g_ValueTypeDefinitions))
        {
            output.WriteString("REG_NO_TYPE (!)");
        }
        else
        {
            output.WriteString(g_ValueTypeDefinitions[i].szTypeName);
        }
    }
    else
    {
        output.WriteNothing();
    }

    if (config.Information & REGINFO_VALUESIZE)
    {
        output.WriteInteger((ULONGLONG)match.DatasLength);
    }
    else
    {
        output.WriteNothing();
    }

    if (config.Information & REGINFO_VALUEFLAG)
    {
        if (match.Datas.get() == nullptr)
        {
            output.WriteString("NOTINHIVE");
        }
        else
        {
            if (match.DatasLength > csvMaxSize)
            {
                output.WriteString("DUMPFILE");
            }
            else if (
                ((match.ValueType == REG_SZ) || (match.ValueType == REG_MULTI_SZ) || (match.ValueType == REG_EXPAND_SZ))
                && (ContainsBadChars((LPWSTR)(match.Datas.get()), match.DatasLength / sizeof(WCHAR))))
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
    {
        output.WriteNothing();
    }

    if (config.Information & REGINFO_VALUE)
    {
        if (match.Datas.get() == nullptr)
        {
            output.WriteNothing();
            output.WriteNothing();
        }
        else
        {
            size_t dwLen = match.DatasLength;
            if (dwLen > csvMaxSize)
            {
                // Do not write value into CSV
                output.WriteNothing();
                if (config.Information & REGINFO_VALUEDUMPFILE)
                {
                    std::wstring wstrFullPath =
                        GetFullPath(match.KeyName, match.ValueName, fileNamePrefix, config.Output.Path);
                    hr = DumpValue(match, wstrFullPath);

                    output.WriteString(wstrFullPath.c_str());
                }
                else
                {
                    output.WriteNothing();
                }
            }
            else
            {
                CBinaryBuffer dataBuffer(match.Datas.get(), dwLen);
                std::wstring* wstrTmp;
                LPWSTR lpCurrent;

                switch (match.ValueType)
                {
                    case REG_BINARY:
                    case REG_DWORD_BIG_ENDIAN:
                        output.WriteBytes(dataBuffer);
                        output.WriteNothing();
                        break;
                        // endianness...
                    case REG_DWORD:
                    case REG_QWORD: {
                        BYTE* pDataBuffer = new BYTE[match.DatasLength];
                        for (dwLen = match.DatasLength; dwLen > 0; dwLen--)
                        {
                            pDataBuffer[match.DatasLength - dwLen] = match.Datas.get()[dwLen - 1];
                        }

                        dataBuffer.SetData(pDataBuffer, match.DatasLength);
                        delete[] pDataBuffer;

                        output.WriteBytes(dataBuffer);
                        output.WriteNothing();
                        break;
                    }
                    case REG_EXPAND_SZ:
                    case REG_SZ:
                        if (ContainsBadChars((LPWSTR)(match.Datas.get()), match.DatasLength / sizeof(WCHAR)))
                        {
                            std::wstring wstrFullPath =
                                GetFullPath(match.KeyName, match.ValueName, fileNamePrefix, config.Output.Path);
                            hr = DumpValue(match, wstrFullPath);
                            output.WriteNothing();
                            output.WriteString(wstrFullPath.c_str());
                        }
                        else
                        {
                            output.WriteString((LPWSTR)(match.Datas.get()));
                            output.WriteNothing();
                        }
                        break;
                    case REG_MULTI_SZ:

                        if (ContainsBadChars((LPWSTR)(match.Datas.get()), match.DatasLength / sizeof(WCHAR)))
                        {
                            std::wstring wstrFullPath =
                                GetFullPath(match.KeyName, match.ValueName, fileNamePrefix, config.Output.Path);
                            hr = DumpValue(match, wstrFullPath);

                            output.WriteNothing();
                            output.WriteString(wstrFullPath.c_str());
                        }
                        else
                        {
                            std::wstring wstrData;

                            dwLen = 0;
                            lpCurrent = (LPWSTR)match.Datas.get();
                            while (dwLen < match.DatasLength)
                            {
                                wstrTmp = new std::wstring(lpCurrent);
                                dwLen += (wcslen(lpCurrent) + 1) * sizeof(WCHAR);
                                lpCurrent += wcslen(lpCurrent) + 1;
                                wstrData += *wstrTmp + L" <> ";
                                delete wstrTmp;
                            }
                            output.WriteString(wstrData);
                            output.WriteNothing();
                        }
                        break;
                    default:
                        output.WriteNothing();
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
    HRESULT hr = LoadWinTrust();
    if (FAILED(hr))
    {
        Log::Critical("Failed LoadWinTrust [{}]", SystemError(hr));
        return hr;
    }

    // Flush key before runnning
    // Useless if MFT parser not used but done every time so nobody will forget to mention it in configuration...
    DWORD dwGLE = 0L;

    Log::Debug("Flushing HKEY_LOCAL_MACHINE");
    dwGLE = RegFlushKey(HKEY_LOCAL_MACHINE);
    if (dwGLE != ERROR_SUCCESS)
    {
        Log::Error("Flushing HKEY_LOCAL_MACHINE failed [{}]", Win32Error(dwGLE));
    }

    Log::Debug("Flushing HKEY_USERS");
    dwGLE = RegFlushKey(HKEY_USERS);
    if (dwGLE != ERROR_SUCCESS)
    {
        Log::Error("Flushing HKEY_USERS failed [{}]", Win32Error(dwGLE));
    }

    hr = config.m_HiveQuery.BuildStreamList();
    if (FAILED(hr))
    {
        Log::Error("Failed to build hive stream list[{}]", SystemError(hr));
        return hr;
    }

    for (const auto& query : config.m_HiveQuery.m_Queries)
    {
        GetSystemTimeAsFileTime(&m_collectionDate);

        std::shared_ptr<TableOutput::IWriter> pRegInfoWriter;

        if (HasFlag(config.Output.Type, OutputSpec::Kind::TableFile))
        {
            pRegInfoWriter = GetRegInfoWriter(config.Output);
            if (nullptr == pRegInfoWriter)
            {
                Log::Error("Failed to create output file");
                return E_FAIL;
            }
        }

        auto root = m_console.OutputTree();
        for (const auto& hive : query->StreamList)
        {
            auto node = root.AddNode("Parsing hive '{}'", hive.FileName);

            if (HasFlag(config.Output.Type, OutputSpec::Kind::Directory))
            {
                std::wstring fileName(hive.FileName);
                std::replace(fileName.begin(), fileName.end(), L'\\', L'_');
                std::replace(fileName.begin(), fileName.end(), L':', L'_');

                pRegInfoWriter = GetRegInfoWriter(config.Output, fileName);
                if (nullptr == pRegInfoWriter)
                {
                    Log::Error("Failed to create output file information file");
                    continue;
                }
            }

            if (!hive.Stream)
            {
                Log::Error(L"Can't open hive '{}'", hive.FileName);
                continue;
            }

            hr = query->QuerySpec.Find(hive.Stream, nullptr, nullptr);
            if (FAILED(hr))
            {
                Log::Error(L"Failed to search into hive '{}' [{}]", hive.FileName, SystemError(hr));
                continue;
            }

            auto& output = *pRegInfoWriter;
            for (const auto& [searchTerm, result] : query->QuerySpec.Matches())
            {
                for (const auto& key : result->MatchingKeys)
                {
                    WriteComputerName(output);
                    WriteTermName(output, searchTerm);
                    WriteSearchDescription(output, searchTerm);
                    WriteKeyInformation(output, key);
                    output.WriteNothing();
                    output.WriteNothing();
                    output.WriteNothing();
                    output.WriteNothing();
                    output.WriteNothing();
                    output.WriteNothing();
                    output.WriteEndOfLine();
                }

                for (const auto& value : result->MatchingValues)
                {
                    WriteComputerName(output);
                    WriteTermName(output, searchTerm);
                    WriteSearchDescription(output, searchTerm);
                    WriteValueInformation(output, value, hive.FileName, config.CsvValueLengthLimit);
                    output.WriteEndOfLine();
                }
            }
        }

        query->QuerySpec.ClearMatches();
    }

    return hr;
}
