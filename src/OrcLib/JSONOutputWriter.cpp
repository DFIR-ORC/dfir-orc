//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "JSONOutputWriter.h"

#include "WideAnsi.h"
#include "BinaryBuffer.h"
#include "Buffer.h"

#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/stream.h"

using namespace std;
using namespace std::string_view_literals;
using namespace Orc;

namespace Orc::StructuredOutput::JSON {

template <class _RapidWriter, typename _Ch>
Writer<_RapidWriter, _Ch>::Writer(std::shared_ptr<ByteStream> stream, std::unique_ptr<Options>&& pOptions)
    : StructuredOutputWriter(std::move(pOptions))
    , m_Stream(std::move(stream))
    , rapidWriter(m_Stream)
{
    rapidWriter.StartObject();
}

std::shared_ptr<StructuredOutput::IWriter>
GetWriter(std::shared_ptr<ByteStream> stream, std::unique_ptr<Options>&& options)
{
    if (options == nullptr)
        return std::make_shared<Writer<
            rapidjson::PrettyWriter<Stream<rapidjson::UTF8<>::Ch>, rapidjson::UTF16<>, rapidjson::UTF8<>>,
            rapidjson::UTF8<>::Ch>>(stream, std::move(options));

    if (options->bPrettyPrint && options->Encoding == OutputSpec::Encoding::UTF8)
        return std::make_shared<Writer<
            rapidjson::PrettyWriter<Stream<rapidjson::UTF8<>::Ch>, rapidjson::UTF16<>, rapidjson::UTF8<>>,
            rapidjson::UTF8<>::Ch>>(stream, std::move(options));
    if (!options->bPrettyPrint && options->Encoding == OutputSpec::Encoding::UTF8)
        return std::make_shared<Writer<
            rapidjson::Writer<Stream<rapidjson::UTF8<>::Ch>, rapidjson::UTF16<>, rapidjson::UTF8<>>,
            rapidjson::UTF8<>::Ch>>(stream, std::move(options));
    if (options->bPrettyPrint && options->Encoding == OutputSpec::Encoding::UTF16)
        return std::make_shared<Writer<
            rapidjson::PrettyWriter<Stream<rapidjson::UTF16<>::Ch>, rapidjson::UTF16<>, rapidjson::UTF16<>>,
            rapidjson::UTF16<>::Ch>>(stream, std::move(options));
    if (!options->bPrettyPrint && options->Encoding == OutputSpec::Encoding::UTF16)
        return std::make_shared<Writer<
            rapidjson::Writer<Stream<rapidjson::UTF16<>::Ch>, rapidjson::UTF16<>, rapidjson::UTF16<>>,
            rapidjson::UTF16<>::Ch>>(stream, std::move(options));

    return std::make_shared<Writer<
        rapidjson::PrettyWriter<Stream<rapidjson::UTF8<>::Ch>, rapidjson::UTF16<>, rapidjson::UTF8<>>,
        rapidjson::UTF8<>::Ch>>(stream, std::move(options));
}

template <class _RapidWriter, typename _Ch>
template <typename... Args>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed_(LPCWSTR szName, Args&&... args)
{
    rapidWriter.Key(szName);
    return Write(std::forward<Args>(args)...);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Close()
{
    rapidWriter.EndObject();
    m_Stream.Flush();
    m_Stream.Close();
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::BeginElement(LPCWSTR szElement)
{
    if (szElement)
        rapidWriter.Key(szElement);
    rapidWriter.StartObject();
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::EndElement(LPCWSTR szElement)
{
    rapidWriter.EndObject();
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::BeginCollection(LPCWSTR szCollection)
{
    if (szCollection)
        rapidWriter.Key(szCollection);
    rapidWriter.StartArray();
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::EndCollection(LPCWSTR szCollection)
{
    rapidWriter.EndArray();
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteFormated_(
    std::wstring_view szFormat,
    fmt::wformat_args args)
{
    Buffer<WCHAR, ORC_MAX_PATH> buffer;
    auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);

    std::wstring_view result_string = buffer.empty() ? L""sv : std::wstring_view(buffer.get(), buffer.size());

    rapidWriter.String(result_string.data(), result_string.size());
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT
Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteFormated_(std::string_view szFormat, fmt::format_args args)
{
    Buffer<CHAR, ORC_MAX_PATH> buffer;
    auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);

    std::string_view result_string = buffer.empty() ? ""sv : std::string_view(buffer.get(), buffer.size());
    auto [hr, wstr] = Orc::AnsiToWide(result_string);
    if (FAILED(hr))
        return hr;

    rapidWriter.String(wstr.data(), wstr.size());
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamedFormated_(
    LPCWSTR szName,
    std::wstring_view szFormat,
    fmt::wformat_args args)
{
    rapidWriter.Key(szName);
    WriteFormated_(szFormat, args);
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamedFormated_(
    LPCWSTR szName,
    std::string_view szFormat,
    fmt::format_args args)
{
    rapidWriter.Key(szName);
    WriteFormated_(szFormat, args);
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(LPCWSTR szValue)
{
    rapidWriter.String(szValue);
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(LPCWSTR szName, const WCHAR* szValue)
{
    return WriteNamed_(szName, szValue);
}

template <class _RapidWriter, typename _Ch>
HRESULT Writer<_RapidWriter, _Ch>::Write(const std::wstring_view str)
{
    rapidWriter.String(str.data(), str.length());
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Writer<_RapidWriter, _Ch>::WriteNamed(LPCWSTR szName, const std::wstring_view str)
{
    return WriteNamed_(szName, str);
}

template <class _RapidWriter, typename _Ch>
HRESULT Writer<_RapidWriter, _Ch>::Write(const std::wstring& str)
{
    rapidWriter.String(str.data(), str.length());
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Writer<_RapidWriter, _Ch>::WriteNamed(LPCWSTR szName, const std::wstring& str)
{
    return WriteNamed_(szName, str);
}

template <class _RapidWriter, typename _Ch>
HRESULT Writer<_RapidWriter, _Ch>::Write(std::string_view str)
{
    if (auto [hr, wstr] = Orc::AnsiToWide(str); FAILED(hr))
        return hr;
    else
        rapidWriter.String(wstr.data(), wstr.length());
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Writer<_RapidWriter, _Ch>::WriteNamed(LPCWSTR szName, std::string_view str)
{
    if (auto [hr, wstr] = Orc::AnsiToWide(str); FAILED(hr))
        return hr;
    else
        return WriteNamed(szName, wstr);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(uint32_t dwValue, bool bInHex)
{
    if (bInHex)
    {
        StructuredOutput::Writer::_Buffer buffer;
        WriteBuffer(buffer, dwValue, bInHex);
        rapidWriter.String(buffer.get());
    }
    else
        rapidWriter.Uint(dwValue);

    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(int32_t uiValue, bool bInHex)
{
    if (bInHex)
    {
        StructuredOutput::Writer::_Buffer buffer;
        WriteBuffer(buffer, uiValue, bInHex);
        rapidWriter.String(buffer.get());
    }
    else
        rapidWriter.Int(uiValue);

    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(LPCWSTR szName, int32_t lValue, bool bInHex)
{
    return WriteNamed_(szName, lValue, bInHex);
}

template <class _RapidWriter, typename _Ch>
HRESULT
Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(LPCWSTR szName, uint32_t dwValue, bool bInHex)
{
    return WriteNamed_(szName, dwValue, bInHex);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(uint64_t ullValue, bool bInHex)
{
    if (bInHex)
    {
        StructuredOutput::Writer::_Buffer buffer;
        WriteBuffer(buffer, ullValue, bInHex);
        rapidWriter.String(buffer.get());
    }
    else
        rapidWriter.Uint64(ullValue);

    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(int64_t llValue, bool bInHex)
{
    if (bInHex)
    {
        StructuredOutput::Writer::_Buffer buffer;
        WriteBuffer(buffer, llValue, bInHex);
        rapidWriter.String(buffer.get());
    }
    else
        rapidWriter.Int64(llValue);

    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT
Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(LPCWSTR szName, uint64_t ullValue, bool bInHex)
{
    return WriteNamed_(szName, ullValue, bInHex);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(LPCWSTR szName, int64_t llValue, bool bInHex)
{
    return WriteNamed_(szName, llValue, bInHex);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(LARGE_INTEGER ullValue, bool bInHex)
{
    if (bInHex)
    {
        StructuredOutput::Writer::_Buffer buffer;
        WriteBuffer(buffer, ullValue, bInHex);
        rapidWriter.String(buffer.get());
    }
    else
        rapidWriter.Int64(ullValue.QuadPart);
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT
Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(LPCWSTR szName, LARGE_INTEGER ullValue, bool bInHex)
{
    return WriteNamed_(szName, ullValue, bInHex);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteAttributes(DWORD dwFileAttributes)
{
    StructuredOutput::Writer::_Buffer buffer;
    WriteAttributesBuffer(buffer, dwFileAttributes);
    rapidWriter.String(buffer.get());
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT
Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamedAttributes(LPCWSTR szName, DWORD dwFileAttributes)
{
    rapidWriter.Key(szName);
    return WriteAttributes(dwFileAttributes);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteFileTime(ULONGLONG fileTime)
{
    StructuredOutput::Writer::_Buffer buffer;
    WriteFileTimeBuffer(buffer, fileTime);
    rapidWriter.String(buffer.get());
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamedFileTime(LPCWSTR szName, ULONGLONG fileTime)
{
    rapidWriter.Key(szName);
    return WriteFileTime(fileTime);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(FILETIME fileTime)
{
    StructuredOutput::Writer::_Buffer buffer;
    WriteBuffer(buffer, fileTime);
    rapidWriter.String(buffer.get());
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(LPCWSTR szName, FILETIME fileTime)
{
    return WriteNamed_(szName, fileTime);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(const WCHAR* szArray, DWORD dwCharCount)
{
    StructuredOutput::Writer::_Buffer buffer;
    WriteBuffer(buffer, szArray, dwCharCount);
    rapidWriter.String(buffer.get());
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(
    LPCWSTR szName,
    const WCHAR* szArray,
    DWORD dwCharCount)
{
    return WriteNamed_(szName, szArray, dwCharCount);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(const BYTE pBytes[], DWORD dwLen, bool b0xPrefix)
{
    if (dwLen == 0)
    {
        rapidWriter.String(L"");
        return S_OK;
    }

    StructuredOutput::Writer::_Buffer buffer;
    WriteBuffer(buffer, pBytes, dwLen, b0xPrefix);
    rapidWriter.String(buffer.get());
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(
    LPCWSTR szName,
    const BYTE pSHA1[],
    DWORD dwLen,
    bool b0xPrefix)
{
    return WriteNamed_(szName, pSHA1, dwLen, b0xPrefix);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(const CBinaryBuffer& Buffer, bool b0xPrefix)
{
    return Write(Buffer.GetData(), (DWORD)Buffer.GetCount(), b0xPrefix);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(
    LPCWSTR szName,
    const CBinaryBuffer& Buffer,
    bool b0xPrefix)
{
    return WriteNamed_(szName, Buffer, b0xPrefix);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(bool bBoolean)
{
    rapidWriter.Bool(bBoolean);
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(LPCWSTR szName, bool bBoolean)
{
    return WriteNamed_(szName, bBoolean);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(DWORD dwEnum, const WCHAR* EnumValues[])
{
    unsigned int i = 0;
    const WCHAR* szValue = NULL;

    while (((EnumValues[i]) != NULL) && i <= dwEnum)
    {
        if (i == dwEnum)
        {
            szValue = EnumValues[i];
            break;
        }
        i++;
    }

    if (szValue == NULL)
        szValue = L"IllegalEnumValue";

    rapidWriter.String(szValue);
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(
    LPCWSTR szName,
    DWORD dwEnum,
    const WCHAR* EnumValues[])
{
    return WriteNamed_(szName, dwEnum, EnumValues);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(
    DWORD dwFlags,
    const FlagsDefinition FlagValues[],
    WCHAR cSeparator)
{
    StructuredOutput::Writer::_Buffer buffer;
    WriteBuffer(buffer, dwFlags, FlagValues, cSeparator);
    rapidWriter.String(buffer.get());
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(
    LPCWSTR szName,
    DWORD dwFlags,
    const FlagsDefinition FlagValues[],
    WCHAR cSeparator)
{
    return WriteNamed_(szName, dwFlags, FlagValues, cSeparator);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(DWORD dwFlags, const FlagsDefinition FlagValues[])
{
    HRESULT hr = E_FAIL;
    unsigned int idx = 0;
    const WCHAR* szValue = NULL;

    while (FlagValues[idx].dwFlag != 0xFFFFFFFF)
    {
        if (dwFlags == FlagValues[idx].dwFlag)
        {
            szValue = FlagValues[idx].szShortDescr;
            break;
        }
        idx++;
    }

    if (szValue == NULL)
    {
        // No flags where recognised, write value in Hex
        if (FAILED(Write((uint32_t)dwFlags, true)))
            return hr;
        return S_OK;
    }
    else
        return Write(szValue);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(
    LPCWSTR szName,
    DWORD dwFlags,
    const FlagsDefinition FlagValues[])
{
    return WriteNamed_(szName, dwFlags, FlagValues);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(IN_ADDR& ip)
{
    return WriteFormated(
        L"%d.%d.%d.%d", ip.S_un.S_un_b.s_b1, ip.S_un.S_un_b.s_b2, ip.S_un.S_un_b.s_b3, ip.S_un.S_un_b.s_b4);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(LPCWSTR szName, IN_ADDR& ip)
{
    return WriteNamed_(szName, ip);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(IN6_ADDR& ip)
{
    return Write(ip.u.Byte, 16, false);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(LPCWSTR szName, IN6_ADDR& ip)
{
    return WriteNamed_(szName, ip);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::Write(const GUID& guid)
{
    WCHAR szCLSID[MAX_GUID_STRLEN];
    if (StringFromGUID2(guid, szCLSID, MAX_GUID_STRLEN))
    {
        return Write(szCLSID);
    }
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteNamed(LPCWSTR szName, const GUID& guid)
{
    return WriteNamed_(szName, guid);
}

template <class _RapidWriter, typename _Ch>
HRESULT Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::WriteComment(LPCWSTR szComment)
{
    return S_OK;
}

template <class _RapidWriter, typename _Ch>
Orc::StructuredOutput::JSON::Writer<_RapidWriter, _Ch>::~Writer()
{
}

}  // namespace Orc::StructuredOutput::JSON
