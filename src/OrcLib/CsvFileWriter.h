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
    , private ITableOutput
    , public ::Orc::TableOutput::IStreamWriter
{
    struct MakeSharedEnabler;
    friend struct MakeSharedEnabler;

public:
    static std::shared_ptr<Writer> MakeNew(logger pLog, std::unique_ptr<TableOutput::Options>&& options);

    Writer(const Writer&) = delete;
    Writer(Writer&& other) noexcept
    {
        std::swap(_L_, other._L_);
        std::swap(m_pTermination, other.m_pTermination);
        wcscpy_s(m_szFileName, other.m_szFileName);
        std::swap(m_dwBufferSize, other.m_dwBufferSize);
        std::swap(m_pBuffer, other.m_pBuffer);
        std::swap(m_Options, other.m_Options);
        std::swap(m_pUTF8Buffer, other.m_pUTF8Buffer);
        std::swap(m_dwUTF8BufferSize, other.m_dwUTF8BufferSize);
        std::swap(m_bBOMWritten, other.m_bBOMWritten);
        std::swap(m_pByteStream, other.m_pByteStream);
        std::swap(m_bCloseStream, other.m_bCloseStream);
        std::swap(m_pCurrent, other.m_pCurrent);
        std::swap(m_dwColumnCounter, other.m_dwColumnCounter);
        std::swap(m_dwCount, other.m_dwCount);
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

        auto [hr, wstr] = AnsiToWide(_L_, strString);
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

        auto [hr, wstr] = AnsiToWide(_L_, strString);
        if (FAILED(hr))
        {
            return hr;
        }

        return WriteColumn(wstr);
    }

    STDMETHOD(WriteString)(const CHAR* szString) override final
    {
        return WriteString(std::string_view(szString));
    }

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

    STDMETHOD(WriteString)(const std::wstring_view& strString) override final {
        if (strString.empty())
        {
            return WriteNothing();
        }

        return WriteColumn(strString);
    }

    STDMETHOD(WriteString)(const WCHAR* szString) override final {
        return WriteString(std::wstring_view(szString));
    }

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

private:
    STDMETHOD(WriteHeaders)(const ::Orc::TableOutput::Schema& columns);

    logger _L_;

    std::shared_ptr<WriterTermination> m_pTermination;

    WCHAR m_szFileName[MAX_PATH] = {0};

    DWORD m_dwBufferSize = 0L;  // in bytes
    LPWSTR m_pBuffer = nullptr;

    LPSTR m_pUTF8Buffer = nullptr;
    DWORD m_dwUTF8BufferSize = 0L;  // in CHARs

    bool m_bBOMWritten = false;
    std::shared_ptr<ByteStream> m_pByteStream = nullptr;
    bool m_bCloseStream = true;
    CriticalSection m_cs;

    WCHAR* m_pCurrent = nullptr;
    DWORD m_dwColumnCounter = 0L;
    DWORD m_dwCount = 0L;  // in bytes

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

    Writer(logger pLog, std::unique_ptr<Options>&& options);

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
            , quoteCount(0)
            , previousWasQuote(false)
        {
        }

        template <typename U>
        void push_back(U u)
        {
            if (previousWasQuote)
            {
                quoteCount++;

                if (quoteCount > 1)
                {
                    m_wrapped.push_back('"');
                }
            }

            previousWasQuote = (u == '"');

            m_wrapped.push_back(u);
        }

        T& m_wrapped;
        bool previousWasQuote;
        size_t quoteCount;
    };

    template <typename... Args>
    HRESULT FormatToBuffer(const std::wstring_view& strFormat, Args&&... args)
    {
        using char_type = WCHAR;
        using buffer_type = Buffer<char_type>;
        using wformat_iterator = std::back_insert_iterator<buffer_type>;
        using wformat_args = fmt::format_args_t<wformat_iterator, char_type>;
        using context = fmt::basic_format_context<wformat_iterator, char_type>;

        DWORD remaining = (m_dwBufferSize - m_dwCount) / sizeof(char_type);

        buffer_type buffer;
        buffer.view_of(m_pCurrent, remaining);

        try
        {
            if (strFormat.find(L"\"{}\"") != std::wstring::npos)
            {
                fmt::format_to(std::back_inserter(EscapeQuoteInserter(buffer)), strFormat, args...);
            }
            else
            {
                fmt::format_to(std::back_inserter(buffer), strFormat, args...);
            }

            if (!buffer.is_view())
            {
                // if buffer is no longer a non owning view on the reserved data, we need to flush
                if (auto hr = Flush(); FAILED(hr))
                    return hr;

                buffer_type new_buffer;
                new_buffer.view_of(m_pCurrent, (m_dwBufferSize - m_dwCount) / sizeof(char_type));
                new_buffer.append(buffer);
                std::swap(buffer, new_buffer);
            }
            m_dwCount += buffer.size() * sizeof(char_type);
            m_pCurrent += buffer.size();
        }
        catch (const fmt::format_error& error)
        {
            const auto [hr, errorMsg] = AnsiToWide(_L_, error.what());
            log::Error(_L_, E_INVALIDARG, L"fmt::format_error: %s\r\n", errorMsg);
            return E_INVALIDARG;
        }
        catch (const fmt::windows_error& system_error)
        {
            const auto [hr, errorMsg] = AnsiToWide(_L_, system_error.what());
            log::Error(_L_, HRESULT_FROM_WIN32(system_error.error_code()), L"fmt::windows_error: %s\r\n", errorMsg);
            return HRESULT_FROM_WIN32(system_error.error_code());
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
