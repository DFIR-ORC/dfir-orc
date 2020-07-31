//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "TableOutputWriter.h"
#include "OutputSpec.h"
#include "CriticalSection.h"

#include "ApacheOrcMemoryPool.h"
#include "ApacheOrcStream.h"

#include "Convert.h"

#pragma warning(disable : 4521)
#include "orc/OrcFile.hh"
#pragma warning(default : 4521)

#include <variant>

namespace Orc::TableOutput::ApacheOrc {

using namespace std::string_literals;

class WriterTermination;
class Stream;

class Writer
    : public TableOutput::Writer
    , public TableOutput::IStreamWriter
{
    struct MakeSharedEnabler;
    friend struct MakeSharedEnabler;

public:
    static std::shared_ptr<Writer> MakeNew(logger pLog, std::unique_ptr<Options>&& options);

    Writer(const Writer&) = delete;
    Writer(Writer&& other) noexcept
    {
        std::swap(_L_, other._L_);
        std::swap(m_pTermination, other.m_pTermination);
        wcscpy_s(m_szFileName, other.m_szFileName);
    }

    ~Writer();

    std::shared_ptr<ByteStream> GetStream() const override final { return m_pByteStream; };

    STDMETHOD(WriteToFile)(const std::filesystem::path& path) override final;
    STDMETHOD(WriteToFile)(const WCHAR* szFileName) override final;
    STDMETHOD(WriteToStream)(const std::shared_ptr<ByteStream>& pStream, bool bCloseStream = true) override final;

    STDMETHOD(SetSchema)(const TableOutput::Schema& columns) override final;
    ;

    virtual DWORD GetCurrentColumnID() override final { return m_dwColumnCounter; };

    virtual const TableOutput::Column& GetCurrentColumn() override final
    {
        if (m_Schema)
            return m_Schema[m_dwColumnCounter];
        else
            throw L"No schema assicated with Parquet writer";
    }

    STDMETHOD(Flush)() override final;
    STDMETHOD(Close)() override final;

    STDMETHOD(WriteNothing)() override final;

    STDMETHOD(WriteString)(const std::string& szString) override final;
    STDMETHOD(WriteString)(const std::string_view& szString) override final;
    STDMETHOD(WriteString)(const CHAR* szString) override final;
    STDMETHOD(WriteCharArray)(const CHAR* szArray, DWORD dwCharCount) override final;

    STDMETHOD(WriteString)(const std::wstring& szString) override final;
    STDMETHOD(WriteString)(const std::wstring_view& szString) override final;
    STDMETHOD(WriteString)(const WCHAR* szString) override final;
    STDMETHOD(WriteCharArray)(const WCHAR* szArray, DWORD dwCharCount) override final;

protected:
    STDMETHOD(WriteFormated_)(const std::string_view& szFormat, IOutput::format_args args) override final;
    STDMETHOD(WriteFormated_)(const std::wstring_view& szFormat, IOutput::wformat_args args) override final;

public:
    STDMETHOD(WriteAttributes)(DWORD dwAttibutes) override final;
    STDMETHOD(WriteFileTime)(FILETIME fileTime) override final;
    STDMETHOD(WriteFileTime)(LONGLONG fileTime) override final;
    STDMETHOD(WriteTimeStamp)(time_t tmStamp) override final;
    STDMETHOD(WriteTimeStamp)(tm tmStamp) override final;

    STDMETHOD(WriteFileSize)(LARGE_INTEGER fileSize) override final;
    STDMETHOD(WriteFileSize)(ULONGLONG fileSize) override final;
    STDMETHOD(WriteFileSize)(DWORD nFileSizeHigh, DWORD nFileSizeLow) override final;

    STDMETHOD(WriteInteger)(DWORD dwInteger) override final;
    STDMETHOD(WriteInteger)(LONGLONG dw64Integer) override final;
    STDMETHOD(WriteInteger)(ULONGLONG dw64Integer) override final;

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

private:
    Writer(logger pLog, std::unique_ptr<Options>&& options);

    HRESULT AddColumnAndCheckNumbers();

    logger _L_;

    std::unique_ptr<Options> m_Options;
    std::shared_ptr<WriterTermination> m_pTermination;

    WCHAR m_szFileName[MAX_PATH] = {0};

    CriticalSection m_cs;

    std::unique_ptr<Stream> m_OrcStream;

    DWORD m_dwColumnCounter = 0L;
    DWORD m_dwColumnNumber = 0L;

    DWORD m_dwBatchSize = 1204L;
    DWORD m_dwBatchRow = 0L;

    DWORD m_dwRows = 0L;

    std::unique_ptr<MemoryPool> m_BatchPool;

    std::shared_ptr<ByteStream> m_pByteStream = nullptr;
    bool m_bCloseStream = true;

    std::unique_ptr<orc::Type> m_OrcSchema;
    std::unique_ptr<orc::Writer> m_Writer;
    std::unique_ptr<orc::ColumnVectorBatch> m_Batch;

    static constexpr auto UTC_zoneinfo =
        L"VFppZjIAAAAAAAAAAAAAAAAAAAAAAAABAAAAAQAAAAAAAAAAAAAAAQAAAAQAAAAAAABVVEMAAABUWmlmMgAAAAAAAAAAAAAAAAAAAAAAAAEAAAABAAAAAAAAAAEAAAABAAAABPgAAAAAAAAAAAAAAAAAAFVUQwAAAApVVEMwCg=="sv;
    static constexpr auto GMT_zoneinfo =
        L"VFppZjIAAAAAAAAAAAAAAAAAAAAAAAABAAAAAQAAAAAAAAAAAAAAAQAAAAQAAAAAAABHTVQAAABUWmlmMgAAAAAAAAAAAAAAAAAAAAAAAAEAAAABAAAAAAAAAAEAAAABAAAABPgAAAAAAAAAAAAAAAAAAEdNVAAAAApHTVQwCg=="sv;

    void AddZoneInfo(const std::string name, const std::wstring_view base64);
};

}  // namespace Orc::TableOutput::ApacheOrc
