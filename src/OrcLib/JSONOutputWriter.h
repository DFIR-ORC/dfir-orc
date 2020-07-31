//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "StructuredOutputWriter.h"

#pragma managed(push, off)

#include "OutputSpec.h"
#include "ByteStream.h"

namespace Orc {

class LogFileWriter;
class ByteStream;

namespace StructuredOutput::JSON {


template <typename _Ch>
class Stream
{
public:

    using Ch = _Ch;
    
    Stream(std::shared_ptr<ByteStream> a_stream)
        : m_stream(std::move(a_stream))
    {
    }
    Stream(Stream&& rhs) noexcept = default;

    //! Write a character.
    void Put(_Ch c)
    {
        if (m_buffer.full())
        {
            Flush();
            if (!m_buffer.empty())
                throw Orc::Exception(Continue, E_OUTOFMEMORY, L"Failed to flush JSON's stream buffer");
        }
        m_buffer.push_back(c);
    }

    //! Flush the buffer.
    void Flush()
    {
        if (m_buffer.empty())
            return;

        auto BytesWritten = 0ULL;
        if (auto hr = m_stream->Write(m_buffer.get(), m_buffer.elt_size() * m_buffer.size(), &BytesWritten); FAILED(hr))
            throw Orc::Exception(Continue, hr, L"Failed to write JSON's buffer to stream");
        if (BytesWritten != m_buffer.elt_size() * m_buffer.size())
            throw Orc::Exception(Continue, E_NOT_VALID_STATE, L"Failed to write JSON's entire buffer to stream");

        m_buffer.clear();
    }

    ~Stream() { m_stream->Close(); }

private:
    std::shared_ptr<ByteStream> m_stream;
    Buffer<_Ch, 1024> m_buffer;
};


template <class _RapidWriter, typename _Ch>
class Writer : public StructuredOutput::Writer
{
private:

    Stream<_Ch> m_Stream;
    _RapidWriter rapidWriter;

public:
    Writer(logger pLog, std::shared_ptr<ByteStream> stream, std::unique_ptr<Options>&& pOptions);
    Writer(const Writer&) = delete;

    virtual HRESULT Close() override final;

    virtual HRESULT BeginElement(LPCWSTR szElement) override final;
    virtual HRESULT EndElement(LPCWSTR szElement) override final;

    virtual HRESULT BeginCollection(LPCWSTR szCollection) override final;
    virtual HRESULT EndCollection(LPCWSTR szCollection) override final;

    virtual HRESULT WriteFormated(const WCHAR* szFormat, ...) override final;
    virtual HRESULT WriteNamedFormated(LPCWSTR szName, const WCHAR* szFormat, ...) override final;

    virtual HRESULT Write(LPCWSTR szString) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, const WCHAR* szValue) override final;

    virtual HRESULT Write(bool bBoolean) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, bool bBoolean) override final;

    virtual HRESULT Write(ULONG32 dwValue, bool bInHex = false) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, ULONG32 dwValue, bool bInHex = false) override final;

    virtual HRESULT Write(LONG32 dwValue, bool bInHex = false) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, LONG32 dwValue, bool bInHex = false) override final;

    virtual HRESULT Write(ULONG64 ullValue, bool bInHex = false) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, ULONG64 ullValue, bool bInHex = false) override final;

    virtual HRESULT Write(LONG64 ullValue, bool bInHex = false) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, LONG64 ullValue, bool bInHex = false) override final;

    virtual HRESULT Write(LARGE_INTEGER ullValue, bool bInHex = false) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, LARGE_INTEGER ullValue, bool bInHex = false) override final;

    virtual HRESULT WriteFileTime(ULONGLONG fileTime) override final;
    virtual HRESULT WriteNamedFileTime(LPCWSTR szName, ULONGLONG fileTime) override final;

    virtual HRESULT WriteAttributes(DWORD dwAttibutes) override final;
    virtual HRESULT WriteNamedAttributes(LPCWSTR szName, DWORD dwAttibutes) override final;

    virtual HRESULT Write(FILETIME fileTime) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, FILETIME fileTime) override final;

    virtual HRESULT Write(const WCHAR* szArray, DWORD dwCharCount) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, const WCHAR* szArray, DWORD dwCharCount) override final;

    virtual HRESULT Write(const BYTE pSHA1[], DWORD dwLen, bool b0xPrefix) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, const BYTE pSHA1[], DWORD dwLen, bool b0xPrefix) override final;

    virtual HRESULT Write(const CBinaryBuffer& Buffer, bool b0xPrefix) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, const CBinaryBuffer& Buffer, bool b0xPrefix) override final;

    virtual HRESULT Write(DWORD dwEnum, const WCHAR* EnumValues[]) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, DWORD dwEnum, const WCHAR* EnumValues[]) override final;
    ;

    virtual HRESULT Write(DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator) override final;
    virtual HRESULT
    WriteNamed(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator) override final;
    ;

    virtual HRESULT Write(DWORD dwFlags, const FlagsDefinition FlagValues[]) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, DWORD dwFlags, const FlagsDefinition FlagValues[]) override final;
    ;

    virtual HRESULT Write(IN_ADDR& ip) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, IN_ADDR& ip) override final;
    ;

    virtual HRESULT Write(IN6_ADDR& ip) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, IN6_ADDR& ip) override final;
    ;

    virtual HRESULT Write(const GUID& guid) override final;
    virtual HRESULT WriteNamed(LPCWSTR szName, const GUID& guid) override final;
    ;

    virtual HRESULT WriteComment(LPCWSTR szComment) override final;

    ~Writer();


private:
    template <typename... Args>
    HRESULT WriteNamed_(LPCWSTR szName, Args&&... args);
};

std::shared_ptr<StructuredOutput::IWriter> GetWriter(const logger& pLog, std::shared_ptr<ByteStream> stream, std::unique_ptr<Options>&& pOptions);


}  // namespace StructuredOutput::JSON

using JSONOutputOptions = StructuredOutput::JSON::Options;


}  // namespace Orc
