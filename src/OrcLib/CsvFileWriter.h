//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "TableOutputWriter.h"

#include "OutputSpec.h"
#include "WideAnsi.h"
#include "CriticalSection.h"

#pragma managed(push, off)

namespace Orc::TableOutput::CSV {

class WriterTermination;

class Column : public ::Orc::TableOutput::Column
{
public:
    Column(const ::Orc::TableOutput::Column& base)
        : ::Orc::TableOutput::Column(base) {};
    std::wstring FormatColumn;

    virtual ~Column() override final {};
};

class ORCLIB_API Writer
    : public ::Orc::TableOutput::Writer
    , public ::Orc::TableOutput::IStreamWriter
{
public:
    static std::shared_ptr<Writer> MakeNew(std::unique_ptr<TableOutput::Options>&& options);

    Writer(const Writer&) = delete;
    Writer(Writer&& other) noexcept
    {
        std::swap(m_pTermination, other.m_pTermination);
        wcscpy_s(m_szFileName, other.m_szFileName);
        std::swap(m_buffer, other.m_buffer);
        std::swap(m_bufferUtf8, other.m_bufferUtf8);
        std::swap(m_Options, other.m_Options);
        std::swap(m_bBOMWritten, other.m_bBOMWritten);
        std::swap(m_pByteStream, other.m_pByteStream);
        std::swap(m_bCloseStream, other.m_bCloseStream);
        std::swap(m_dwColumnCounter, other.m_dwColumnCounter);
        std::swap(m_dwColumnNumber, other.m_dwColumnNumber);
        std::swap(m_dwPageSize, other.m_dwPageSize);
    }

    std::shared_ptr<ByteStream> GetStream() const { return m_pByteStream; };

    ITableOutput& GetTableOutput() { return static_cast<ITableOutput&>(*this); }

    STDMETHOD(SetSchema)(const Schema& columns) override final;
    ;

    virtual DWORD GetCurrentColumnID() override final { return m_dwColumnCounter; };

    virtual const ::Orc::TableOutput::Column& GetCurrentColumn() override final
    {
        if (m_Schema)
            return m_Schema[m_dwColumnCounter];
        else
            throw "No Schema define for columns";
    }

    STDMETHOD(WriteToFile)(const std::filesystem::path& path) override final;
    STDMETHOD(WriteToFile)(const WCHAR* szFileName) override final;
    STDMETHOD(WriteToStream)
    (const std::shared_ptr<ByteStream>& pStream,
     bool bCloseStream = true) override final;  // bCloseStream close stream when closing writer

    STDMETHOD(Flush)() override final;
    STDMETHOD(Close)() override final;

    STDMETHOD(WriteNothing)() override final;

    STDMETHOD(WriteString)(const std::string& strString) override final
    {
        if (strString.empty())
        {
            return WriteNothing();
        }

        auto [hr, wstr] = AnsiToWide(strString);
        if (FAILED(hr))
        {
            return hr;
        }

        return WriteColumn(wstr);
    }
    STDMETHOD(WriteString)(const std::string_view& strString) override final
    {
        if (strString.empty())
        {
            return WriteNothing();
        }

        auto [hr, wstr] = AnsiToWide(strString);
        if (FAILED(hr))
        {
            return hr;
        }

        return WriteColumn(wstr);
    }

    STDMETHOD(WriteString)(const CHAR* szString) override final { return WriteString(std::string_view(szString)); }

    STDMETHOD(WriteCharArray)(const CHAR* szArray, DWORD dwCharCount) override final
    {
        return WriteString(std::string_view(szArray, dwCharCount));
    }

    STDMETHOD(WriteString)(const std::wstring& strString) override final
    {
        if (strString.empty())
        {
            return WriteNothing();
        }

        return WriteColumn(strString);
    }

    STDMETHOD(WriteString)(const std::wstring_view& strString) override final
    {
        if (strString.empty())
        {
            return WriteNothing();
        }

        return WriteColumn(strString);
    }

    STDMETHOD(WriteString)(const WCHAR* szString) override final { return WriteString(std::wstring_view(szString)); }

    STDMETHOD(WriteCharArray)(const WCHAR* szArray, DWORD dwCharCount) override final
    {
        return WriteString(std::wstring_view(szArray, dwCharCount));
    }

protected:
    STDMETHOD(WriteFormated_)(const std::wstring_view& szFormat, IOutput::wformat_args args) override final;
    STDMETHOD(WriteFormated_)(const std::string_view& szFormat, IOutput::format_args args) override final;

public:
    STDMETHOD(WriteAttributes)(DWORD dwAttibutes) override final;
    STDMETHOD(WriteFileTime)(FILETIME fileTime) override final;
    STDMETHOD(WriteFileTime)(LONGLONG fileTime) override final;
    STDMETHOD(WriteTimeStamp)(time_t tmStamp) override final;
    STDMETHOD(WriteTimeStamp)(tm tmStamp) override final;

    STDMETHOD(WriteFileSize)(LARGE_INTEGER fileSize) override final;
    STDMETHOD(WriteFileSize)(ULONGLONG fileSize) override final { return WriteColumn(fileSize); }
    STDMETHOD(WriteFileSize)(DWORD nFileSizeHigh, DWORD nFileSizeLow) override final;

    STDMETHOD(WriteInteger)(DWORD dwInteger) override final { return WriteColumn(dwInteger); }
    STDMETHOD(WriteInteger)(LONGLONG dw64Integer) override final { return WriteColumn(dw64Integer); }
    STDMETHOD(WriteInteger)(ULONGLONG dw64Integer) override final { return WriteColumn(dw64Integer); }

    STDMETHOD(WriteBytes)(const BYTE pSHA1[], DWORD dwLen) override final;
    STDMETHOD(WriteBytes)(const CBinaryBuffer& Buffer) override final;

    STDMETHOD(WriteBool)(bool bBoolean) override final;

    STDMETHOD(WriteEnum)(DWORD dwEnum) override final;
    STDMETHOD(WriteEnum)(DWORD dwEnum, const WCHAR* EnumValues[]) override final;

    STDMETHOD(WriteFlags)(DWORD dwFlags) override final;
    STDMETHOD(WriteFlags)(DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator) override final;

    STDMETHOD(WriteExactFlags)(DWORD dwFlags) override final;
    STDMETHOD(WriteExactFlags)(DWORD dwFlags, const FlagsDefinition FlagValues[]) override final;

    STDMETHOD(WriteGUID)(const GUID& guid) override final;

    STDMETHOD(WriteXML)(const WCHAR* szString) override final;
    STDMETHOD(WriteXML)(const CHAR* szString) override final;
    STDMETHOD(WriteXML)(const WCHAR* szArray, DWORD dwCharCount) override final;
    STDMETHOD(WriteXML)(const CHAR* szArray, DWORD dwCharCount) override final;

    STDMETHOD(AbandonRow)() override final;
    STDMETHOD(AbandonColumn)() override final;

    virtual HRESULT WriteEndOfLine() override final;

    ~Writer(void);

protected:
    STDMETHOD(WriteHeaders)(const ::Orc::TableOutput::Schema& columns);

    fmt::wmemory_buffer m_buffer;

    std::shared_ptr<WriterTermination> m_pTermination;

    WCHAR m_szFileName[MAX_PATH] = {0};

    std::vector<char> m_bufferUtf8;

    bool m_bBOMWritten = false;
    std::shared_ptr<ByteStream> m_pByteStream = nullptr;
    bool m_bCloseStream = true;
    CriticalSection m_cs;

    DWORD m_dwColumnCounter = 0L;
    DWORD m_dwColumnNumber = 0L;

    std::unique_ptr<Options> m_Options;

    // Taille d'une page (en octets)
    DWORD m_dwPageSize = 0L;
    DWORD PageSize()
    {
        if (!m_dwPageSize)
        {
            SYSTEM_INFO sysinfo;
            GetSystemInfo(&sysinfo);
            m_dwPageSize = sysinfo.dwPageSize;
        }
        return m_dwPageSize;
    }

    Writer(std::unique_ptr<Options>&& options);

    //
    // Workaround: 'Unescaped double quote characters in csv files #13 (github)'
    //
    // Double all quotes for escaping with CSV format but ignore first and last
    // ones.
    //
    // A more complete approach is possible by specializing fmt::formatter but
    // it could be less cpu friendly.
    //
    template <typename T>
    struct EscapeQuoteInserter
    {
        using value_type = typename T::value_type;

        EscapeQuoteInserter(T& wrapped)
            : m_wrapped(wrapped)
            , previousWasQuote(false)
            , quoteCount(0)
        {
        }

        void push_back(const wchar_t& u)
        {
            if (previousWasQuote)
            {
                quoteCount++;

                if (quoteCount > 1)
                {
                    m_wrapped.push_back(L'"');
                }
            }

            previousWasQuote = (u == L'"');

            m_wrapped.push_back(u);
        }

        T& m_wrapped;
        bool previousWasQuote;
        size_t quoteCount;
    };

    template <typename... Args>
    HRESULT FormatToBuffer(const std::wstring_view& strFormat, Args&&... args)
    {
        try
        {
            if (strFormat.find(L"\"{}\"") != std::wstring::npos)
            {
                auto escapedBuffer = EscapeQuoteInserter(m_buffer);
                fmt::format_to(std::back_inserter(escapedBuffer), strFormat, args...);
            }
            else
            {
                fmt::format_to(m_buffer, strFormat, args...);
            }
        }
        catch (const fmt::format_error& error)
        {
            spdlog::error("fmt::format_error: {}", error.what());
            return E_INVALIDARG;
        }

        if (m_buffer.size() > (80 * m_buffer.capacity() / 100))
        {
            // Flush cache buffer 'm_pBuffer' first then process the one allocated for formatting
            if (auto hr = Flush(); FAILED(hr))
            {
                return hr;
            }
        }

        return S_OK;
    }

    template <typename... Args>
    HRESULT FormatColumn(Args&&... args)
    {

        auto pCol = static_cast<const Column*>(&m_Schema[m_dwColumnCounter]);

        return FormatToBuffer(pCol->FormatColumn, std::forward<Args>(args)...);
    }

    template <typename... Args>
    HRESULT WriteColumn(Args&&... args)
    {

        auto pCol = static_cast<const Column*>(&m_Schema[m_dwColumnCounter]);

        if (auto hr = FormatToBuffer(pCol->FormatColumn, std::forward<Args>(args)...); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }
        AddColumnAndCheckNumbers();
        return S_OK;
    }

    HRESULT AddColumnAndCheckNumbers();

    STDMETHOD(InitializeBuffer)(DWORD dwBufferSize);

    STDMETHOD(WriteBOM)();
};
}  // namespace Orc::TableOutput::CSV

#pragma managed(pop)
