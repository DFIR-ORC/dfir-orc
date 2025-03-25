//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "StructuredOutputWriter.h"

#include "OutputSpec.h"
#include "FileStream.h"
#include "BinaryBuffer.h"
#include "Filesystem/FileAttribute.h"

using namespace std::string_view_literals;
using namespace Orc;

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
    const auto s = ToIdentifiersW(static_cast<FileAttribute>(dwFileAttributes));
    std::copy(std::cbegin(s), std::cend(s), std::back_inserter(buffer));
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
    ft.dwLowDateTime = ((LARGE_INTEGER*)&fileTime)->LowPart;
    ft.dwHighDateTime = ((LARGE_INTEGER*)&fileTime)->HighPart;
    return WriteBuffer(buffer, ft);
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

    auto current = std::back_insert_iterator(buffer);
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
        return WriteBuffer(buffer, (ULONG32)dwFlags, true);
    return S_OK;
}

HRESULT Orc::StructuredOutput::Writer::WriteBuffer(_Buffer& buffer, DWORD dwFlags, const FlagsDefinition FlagValues[])
{
    int idx = 0;

    auto current = std::back_insert_iterator(buffer);
    while (FlagValues[idx].dwFlag != 0xFFFFFFFF)
    {
        if (dwFlags & FlagValues[idx].dwFlag)
        {
            fmt::format_to(std::back_insert_iterator(buffer), L"{}"sv, FlagValues[idx].szShortDescr);
            return S_OK;
        }
        idx++;
    }
    return WriteBuffer(buffer, (ULONG32)dwFlags);
}

HRESULT Orc::StructuredOutput::Writer::WriteBuffer(_Buffer& buffer, IN_ADDR& ip)
{
    fmt::format_to(
        std::back_insert_iterator(buffer),
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
    std::shared_ptr<ByteStream> stream,
    OutputSpec::Kind kindOfFile,
    std::unique_ptr<StructuredOutputOptions>&& options)
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
                std::make_shared<XmlOutputWriter>(dynamic_unique_ptr_cast<XmlOutputOptions>(std::move(options)));

            if (auto hr = retval->SetOutput(stream); FAILED(hr))
            {
                Log::Error(L"Failed to set output [{}]", SystemError(hr));
                return nullptr;
            }
            return retval;
        }
        break;
        case OutputSpec::Kind::JSON:
        case OutputSpec::Kind::StructuredFile | OutputSpec::Kind::JSON: {
            auto retval = StructuredOutput::JSON::GetWriter(
                stream, dynamic_unique_ptr_cast<JSONOutputOptions>(std::move(options)));

            return retval;
        }
        break;
        case OutputSpec::Kind::Directory:
            break;
        default:
            Log::Error(L"Invalid type of output to create StructuredOutputWriter");
            return nullptr;
    }
    return nullptr;
}

std::shared_ptr<StructuredOutput::IWriter>
StructuredOutput::Writer::GetWriter(const OutputSpec& outFile, std::unique_ptr<StructuredOutputOptions>&& pOptions)
{
    auto stream = std::make_shared<FileStream>();

    if (auto hr = stream->WriteTo(outFile.Path.c_str()); FAILED(hr))
    {
        Log::Error(L"Failed to open file '{}' for writing [{}]", outFile.Path, SystemError(hr));
        return nullptr;
    }
    return GetWriter(stream, outFile.Type, std::move(pOptions));
}

std::shared_ptr<StructuredOutput::IWriter> StructuredOutput::Writer::GetWriter(
    const OutputSpec& outFile,
    const std::wstring& strPattern,
    const std::wstring& strName,
    std::unique_ptr<StructuredOutputOptions>&& pOptions)
{
    if (outFile.Type != OutputSpec::Kind::Directory)
    {
        Log::Error(L"Invalid type of output to create StructuredOutputWriter");
        return nullptr;
    }

    std::wstring strFile;
    OutputSpec::ApplyPattern(strPattern, strName, strFile);

    WCHAR szOutputFile[ORC_MAX_PATH];
    StringCchPrintf(szOutputFile, ORC_MAX_PATH, L"%s\\%s", outFile.Path.c_str(), strFile.c_str());

    OutputSpec fileSpec;
    auto hr = fileSpec.Configure(OutputSpec::Kind::StructuredFile, szOutputFile);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to configure output file for path '{}' [{}]", szOutputFile, SystemError(hr));
        return nullptr;
    }

    return GetWriter(fileSpec, std::move(pOptions));
}
