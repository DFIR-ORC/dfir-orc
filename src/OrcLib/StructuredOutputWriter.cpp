//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "StructuredOutputWriter.h"


#include "OutputSpec.h"

#include "LogFileWriter.h"

#include "FileStream.h"

using namespace Orc;

using namespace std;
using namespace std::string_view_literals;

HRESULT Orc::StructuredOutput::Writer::WriteBuffer(_Buffer& buffer, ULONG32 dwValue, bool bInHex)
{

    fmt::format_to(std::back_inserter(buffer), bInHex ? L"0x{:08X}"sv : L"{}"sv, dwValue);

    return S_OK;
}

HRESULT Orc::StructuredOutput::Writer::WriteBuffer(_Buffer& buffer, ULONG64 ullValue, bool bInHex)
{
    fmt::format_to(std::back_inserter(buffer), bInHex ? L"0x{:016X}"sv : L"{}"sv, ullValue);
    return S_OK;
}

HRESULT Orc::StructuredOutput::Writer::WriteBuffer(_Buffer& buffer, LONG32 uiValue, bool bInHex)
{

    fmt::format_to(std::back_inserter(buffer), bInHex ? L"0x{:08X}"sv : L"{}"sv, uiValue);

    return S_OK;
}

HRESULT Orc::StructuredOutput::Writer::WriteBuffer(_Buffer& buffer, LONG64 llValue, bool bInHex)
{
    fmt::format_to(std::back_inserter(buffer), bInHex ? L"0x{:016X}"sv : L"{}"sv, llValue);
    return S_OK;
}

HRESULT Orc::StructuredOutput::Writer::WriteBuffer(_Buffer& buffer, LARGE_INTEGER ullValue, bool bInHex)
{
    fmt::format_to(std::back_inserter(buffer), bInHex ? L"0x{:016X}"sv : L"{}"sv, ullValue.QuadPart);
    return S_OK;
}

HRESULT Orc::StructuredOutput::Writer::WriteAttributesBuffer(_Buffer& buffer, DWORD dwFileAttributes)
{
    fmt::format_to(
        std::back_inserter(buffer),
        L"{}{}{}{}{}{}{}{}{}{}{}{}{}",
        dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE ? L'A' : L'.',
        dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED ? L'C' : L'.',
        dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? L'D' : L'.',
        dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED ? L'E' : L'.',
        dwFileAttributes & FILE_ATTRIBUTE_HIDDEN ? L'H' : L'.',
        dwFileAttributes & FILE_ATTRIBUTE_NORMAL ? L'N' : L'.',
        dwFileAttributes & FILE_ATTRIBUTE_OFFLINE ? L'O' : L'.',
        dwFileAttributes & FILE_ATTRIBUTE_READONLY ? L'R' : L'.',
        dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ? L'?' : L'.',
        dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE ? L'?' : L'.',
        dwFileAttributes & FILE_ATTRIBUTE_SYSTEM ? L'S' : L'.',
        dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY ? L'T' : L'.',
        dwFileAttributes & FILE_ATTRIBUTE_VIRTUAL ? L'V' : L'.');
    return S_OK;
}


HRESULT Orc::StructuredOutput::Writer::WriteBuffer(_Buffer& buffer, FILETIME fileTime)
{
    HRESULT hr = E_FAIL;

    // Convert the Create time to System time.
    SYSTEMTIME stUTC;
    FileTimeToSystemTime(&fileTime, &stUTC);

    fmt::format_to(
        std::back_inserter(buffer),
        L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}"sv,
        stUTC.wYear,
        stUTC.wMonth,
        stUTC.wDay,
        stUTC.wHour,
        stUTC.wMinute,
        stUTC.wSecond,
        stUTC.wMilliseconds);
    return S_OK;
}

HRESULT Orc::StructuredOutput::Writer::WriteFileTimeBuffer(_Buffer& buffer, ULONGLONG fileTime)
{
    FILETIME ft;
    ft.dwLowDateTime  = ((LARGE_INTEGER*)&fileTime)->LowPart;
    ft.dwHighDateTime = ((LARGE_INTEGER*)&fileTime)->HighPart;
    return WriteBuffer(buffer,ft);
}

HRESULT Orc::StructuredOutput::Writer::WriteBuffer(_Buffer& buffer, const WCHAR* szArray, DWORD dwCharCount)
{
    buffer.resize(dwCharCount);

    fmt::format_to(std::back_inserter(buffer), L"{:.{}}", buffer.get(), dwCharCount);
    return S_OK;
}

HRESULT Orc::StructuredOutput::Writer::WriteBuffer(_Buffer& buffer, const BYTE pBytes[], DWORD dwLen, bool b0xPrefix)
{
    if (dwLen == 0)
    {
        buffer.assign(L"", 1);
        return S_OK;
    }

    Buffer<BYTE> bytes;
    bytes.view_of((BYTE*)pBytes, dwLen);
    bytes.use(dwLen);
    fmt::format_to(std::back_inserter(buffer), b0xPrefix ? L"0x{:02X}"sv : L"{:02X}"sv, bytes);
    return S_OK;
}

HRESULT Orc::StructuredOutput::Writer::WriteBuffer(_Buffer& buffer, const CBinaryBuffer& Buffer, bool b0xPrefix)
{
    if (Buffer.empty())
    {
        buffer.assign(L"", 1);
        return S_OK;
    }

    Orc::Buffer<BYTE> bytes;
    bytes.view_of((BYTE*)Buffer.GetData(), Buffer.GetCount());
    bytes.use(Buffer.GetCount());
    fmt::format_to(std::back_inserter(buffer), b0xPrefix ? L"0x{:02X}"sv : L"{:02X}"sv, bytes);
    return S_OK;
}

HRESULT Orc::StructuredOutput::Writer::WriteBuffer(
    _Buffer& buffer,
    DWORD dwFlags,
    const FlagsDefinition FlagValues[],
    WCHAR cSeparator)
{
    bool bFirst = true;

    int idx = 0;

    auto current = back_insert_iterator(buffer);
    while (FlagValues[idx].dwFlag != 0xFFFFFFFF)
    {
        if (dwFlags & FlagValues[idx].dwFlag)
        {
            if (bFirst)
            {
                bFirst = false;
                current = fmt::format_to(current, L"{}"sv, FlagValues[idx].szShortDescr);
            }
            else
            {
                current = fmt::format_to(current, L"{}{}"sv, cSeparator, FlagValues[idx].szShortDescr);
            }
        }
        idx++;
    }

    if (bFirst == true)  // None of the flags matched
        return WriteBuffer(buffer, (ULONG32) dwFlags, true);
    return S_OK;
}

HRESULT Orc::StructuredOutput::Writer::WriteBuffer(_Buffer& buffer, DWORD dwFlags, const FlagsDefinition FlagValues[])
{
    bool bFirst = true;

    int idx = 0;

    auto current = back_insert_iterator(buffer);
    while (FlagValues[idx].dwFlag != 0xFFFFFFFF)
    {
        if (dwFlags & FlagValues[idx].dwFlag)
        {
            fmt::format_to(back_insert_iterator(buffer), L"{}"sv, FlagValues[idx].szShortDescr);
            return S_OK;
        }
        idx++;
    }
    return WriteBuffer(buffer, (ULONG32) dwFlags);
}

HRESULT Orc::StructuredOutput::Writer::WriteBuffer(_Buffer& buffer, IN_ADDR& ip)
{
    fmt::format_to(
        back_insert_iterator(buffer),
        L"{}.{}.{}.{}"sv,
        ip.S_un.S_un_b.s_b1,
        ip.S_un.S_un_b.s_b2,
        ip.S_un.S_un_b.s_b3,
        ip.S_un.S_un_b.s_b4);
    return S_OK;
}

#include "XmlOutputWriter.h"
#include "JSONOutputWriter.h"

std::shared_ptr<StructuredOutput::IWriter> StructuredOutput::Writer::GetWriter(
    const logger& pLog,
    std::shared_ptr<ByteStream> stream,
    OutputSpec::Kind kindOfFile,
    std::unique_ptr<StructuredOutputOptions>&& pOptions)
{
    switch (kindOfFile)
    {
        case OutputSpec::Kind::None:
            return nullptr;
        case OutputSpec::Kind::File:
        case OutputSpec::Kind::StructuredFile:
        case OutputSpec::Kind::XML:
        case OutputSpec::Kind::StructuredFile | OutputSpec::Kind::XML: {

            auto retval =
                std::make_shared<XmlOutputWriter>(pLog, dynamic_unique_ptr_cast<XmlOutputOptions>(std::move(pOptions)));

            if (auto hr = retval->SetOutput(stream); FAILED(hr))
            {
                log::Error(pLog, hr, L"Failed to set output\r\n");
                return nullptr;
            }
            return retval;
        }
        break;
        case OutputSpec::Kind::JSON:
        case OutputSpec::Kind::StructuredFile | OutputSpec::Kind::JSON: {
            auto retval = StructuredOutput::JSON::GetWriter(pLog, stream, dynamic_unique_ptr_cast<JSONOutputOptions>(std::move(pOptions)));

            return retval;
        }
        break;
        case OutputSpec::Kind::Directory:
            break;
        default:
            log::Error(pLog, E_INVALIDARG, L"Invalid type of output to create StructuredOutputWriter\r\n");
            return nullptr;
    }
    return nullptr;
}

std::shared_ptr<StructuredOutput::IWriter> StructuredOutput::Writer::GetWriter(
    const logger& pLog,
    const OutputSpec& outFile,
    std::unique_ptr<StructuredOutputOptions>&& pOptions)
{
    auto stream = std::make_shared<FileStream>(pLog);

    if (auto hr = stream->WriteTo(outFile.Path.c_str()); FAILED(hr))
    {
        log::Error(pLog, hr, L"Failed to open file %s for writing\r\n", outFile.Path.c_str());
        return nullptr;
    }
    return GetWriter(pLog, stream, outFile.Type, std::move(pOptions));
}

std::shared_ptr<StructuredOutput::IWriter> StructuredOutput::Writer::GetWriter(
    const logger& pLog,
    const OutputSpec& outFile,
    const std::wstring& strPattern,
    const std::wstring& strName,
    std::unique_ptr<StructuredOutputOptions>&& pOptions)
{
    if (outFile.Type != OutputSpec::Kind::Directory)
    {
        log::Error(pLog, E_INVALIDARG, L"Invalid type of output to create StructuredOutputWriter\r\n");
        return nullptr;
    }

    std::wstring strFile;
    OutputSpec::ApplyPattern(strPattern, strName, strFile);


    WCHAR szOutputFile[MAX_PATH];
    StringCchPrintf(szOutputFile, MAX_PATH, L"%s\\%s", outFile.Path.c_str(), strFile.c_str());

    OutputSpec fileSpec;
    if (auto hr = fileSpec.Configure(pLog, OutputSpec::Kind::StructuredFile, szOutputFile)
        ; FAILED(hr))
    {
        log::Error(pLog, E_INVALIDARG, L"Failed to configure output file for path %s\r\n", szOutputFile);
        return nullptr;
    }

    return GetWriter(pLog, fileSpec, std::move(pOptions));
}
