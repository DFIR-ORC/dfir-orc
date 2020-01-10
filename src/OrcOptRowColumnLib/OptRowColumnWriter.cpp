//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "OptRowColumnWriter.h"
#include "OptRowColumnStream.h"

#include "FileStream.h"

#include "WideAnsi.h"
#include "Robustness.h"

#include "Buffer.h"
#include "Convert.h"

#include <WideAnsi.h>

#include <safeint.h>
#include <fmt/format.h>
#include <chrono>

#pragma warning(disable : 4521)
#include <orc/OrcFile.hh>
#pragma warning(default : 4521)

using namespace msl::utilities;

class Orc::TableOutput::OptRowColumn::WriterTermination : public TerminationHandler
{
public:
    WriterTermination(const std::wstring& strDescr, std::weak_ptr<Writer> pW)
        : TerminationHandler(strDescr, ROBUSTNESS_CSV)
        , m_pWriter(std::move(pW)) {};

    HRESULT operator()();

private:
    std::weak_ptr<Writer> m_pWriter;
};

HRESULT Orc::TableOutput::OptRowColumn::WriterTermination::operator()()
{
    if (auto pWriter = m_pWriter.lock(); pWriter)
    {
        pWriter->Close();
    }
    return S_OK;
}

struct Orc::TableOutput::OptRowColumn::Writer::MakeSharedEnabler : public Orc::TableOutput::OptRowColumn::Writer
{
    MakeSharedEnabler(logger pLog, std::unique_ptr<Options>&& options)
        : Writer(std::move(pLog), std::move(options))
    {
    }
};

std::shared_ptr<Orc::TableOutput::OptRowColumn::Writer>
Orc::TableOutput::OptRowColumn::Writer::MakeNew(logger pLog, std::unique_ptr<Options>&& options)
{
    auto retval = std::make_shared<MakeSharedEnabler>(std::move(pLog), std::move(options));

    std::wstring strDescr = L"Termination for ParquetWriter";
    retval->m_pTermination = std::make_shared<WriterTermination>(strDescr, retval);
    Robustness::AddTerminationHandler(retval->m_pTermination);
    return retval;
}

Orc::TableOutput::OptRowColumn::Writer::Writer(logger pLog, std::unique_ptr<Options>&& options)
    : _L_(std::move(pLog))
    , m_Options(std::move(options))
{

    if (m_Options && m_Options->BatchSize.has_value())
    {
        m_dwBatchSize = m_Options->BatchSize.value();
    }
}

Orc::TableOutput::OptRowColumn::Writer::~Writer() {}

// declarations in orc (Timezone.h) missing from the exported includes
namespace orc {

class TimezoneVariant;

class Timezone
{
public:
    virtual ~Timezone();

    /**
     * Get the variant for the given time (time_t).
     */
    virtual const TimezoneVariant& getVariant(int64_t clk) const = 0;

    /**
     * Get the number of seconds between the ORC epoch in this timezone
     * and Unix epoch.
     * ORC epoch is 1 Jan 2015 00:00:00 local.
     * Unix epoch is 1 Jan 1970 00:00:00 UTC.
     */
    virtual int64_t getEpoch() const = 0;

    /**
     * Print the timezone to the stream.
     */
    virtual void print(std::ostream&) const = 0;

    /**
     * Get the version of the zone file.
     */
    virtual uint64_t getVersion() const = 0;

    /**
     * Convert wall clock time of current timezone to UTC timezone
     */
    virtual int64_t convertToUTC(int64_t clk) const = 0;
};

std::unique_ptr<Timezone> getTimezone(const std::string& filename, const std::vector<unsigned char>& b);

}  // namespace orc

void Orc::TableOutput::OptRowColumn::Writer::AddZoneInfo(const std::string name, const std::wstring_view base64)
{
    auto data = Orc::ConvertBase64(base64);
    std::vector<unsigned char> bytes;

    bytes.resize(data.size());
    CopyMemory(bytes.data(), data.get(), data.size());

    auto timeZone = orc::getTimezone(name, bytes);
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::SetSchema(const TableOutput::Schema& columns)
{
    if (!columns)
        return E_INVALIDARG;

    m_Schema = columns;
    m_dwColumnNumber = static_cast<DWORD>(m_Schema.size());

    AddZoneInfo("GMT", GMT_zoneinfo);
    AddZoneInfo("UTC", UTC_zoneinfo);

    using namespace orc;
    m_OrcSchema = orc::createStructType();

    for (const auto& column : m_Schema)
    {
        auto [hr, strName] = WideToAnsi(_L_, column->ColumnName);
        if (FAILED(hr))
        {
            log::Error(_L_, hr, L"Invalid column name %s", column->ColumnName.c_str());
            break;
        }

        std::unique_ptr<orc::Type> type;

        switch (column->Type)
        {
            case Nothing:
                type = createPrimitiveType(TypeKind::BOOLEAN);
                break;
            case BoolType:
                type = createPrimitiveType(TypeKind::BOOLEAN);
                break;
            case UInt8Type:
                type = createPrimitiveType(TypeKind::BYTE);
                break;
            case Int8Type:
                type = createPrimitiveType(TypeKind::BYTE);
                break;
            case UInt16Type:
                type = createPrimitiveType(TypeKind::SHORT);
                break;
            case Int16Type:
                type = createPrimitiveType(TypeKind::SHORT);
                break;
            case UInt32Type:
                type = createPrimitiveType(TypeKind::INT);
                break;
            case Int32Type:
                type = createPrimitiveType(TypeKind::INT);
                break;
            case UInt64Type:
                type = createPrimitiveType(TypeKind::LONG);
                break;
            case Int64Type:
                type = createPrimitiveType(TypeKind::LONG);
                break;
            case TimeStampType:
                type = createPrimitiveType(TypeKind::TIMESTAMP);
                break;
            case UTF16Type:
                if (column->dwLen.has_value())
                    type = createCharType(TypeKind::CHAR, column->dwLen.value());
                else if (column->dwMaxLen.has_value())
                    type = createCharType(TypeKind::VARCHAR, column->dwMaxLen.value());
                else
                    type = createPrimitiveType(TypeKind::STRING);
                break;
            case UTF8Type:
                if (column->dwLen.has_value())
                    type = createCharType(TypeKind::CHAR, column->dwLen.value());
                else if (column->dwMaxLen.has_value())
                    type = createCharType(TypeKind::VARCHAR, column->dwMaxLen.value());
                else
                    type = createPrimitiveType(TypeKind::STRING);
                break;
            case BinaryType:
                type = createPrimitiveType(TypeKind::BINARY);
                break;
            case FixedBinaryType:
                type = createPrimitiveType(TypeKind::BINARY);
                break;
            case GUIDType:
                type = createCharType(TypeKind::CHAR, 16);
                break;
            case EnumType:
                type = createPrimitiveType(TypeKind::INT);
                break;
            case XMLType:
                type = createPrimitiveType(TypeKind::STRING);
                break;
            case FlagsType:
                type = createPrimitiveType(TypeKind::INT);
                break;
            default:
                log::Error(_L_, E_FAIL, L"Unupported (orc) column type for column %s", column->ColumnName.c_str());
        }

        if (type)
            m_OrcSchema->addStructField(strName.c_str(), std::move(type));
    }

    return S_OK;
}

HRESULT Orc::TableOutput::OptRowColumn::Writer::WriteToFile(const WCHAR* szFileName)
{
    HRESULT hr = E_FAIL;

    if (szFileName == NULL)
        return E_POINTER;

    auto pFileStream = std::make_shared<FileStream>(_L_);

    if (pFileStream == nullptr)
        return E_OUTOFMEMORY;

    // ... create it
    if (FAILED(hr = pFileStream->WriteTo(szFileName)))  // no attr.	template
        return hr;

    return WriteToStream(pFileStream, true);
}

STDMETHODIMP
Orc::TableOutput::OptRowColumn::Writer::WriteToStream(const std::shared_ptr<ByteStream>& pStream, bool bCloseStream)
{
    HRESULT hr = E_FAIL;

    if (m_pByteStream && m_bCloseStream)
    {
        m_pByteStream->Close();
    }

    m_bCloseStream = bCloseStream;
    m_pByteStream = pStream;

    m_OrcStream = std::make_unique<Stream>(_L_);

    if (auto hr = m_OrcStream->Open(pStream); FAILED(hr))
        return hr;

    orc::WriterOptions options;
    options.setFileVersion(orc::FileVersion(0, 11));
    options.setCompression(orc::CompressionKind::CompressionKind_ZLIB);

    m_Writer = orc::createWriter(*m_OrcSchema, m_OrcStream.get(), options);

    m_Batch = m_Writer->createRowBatch(m_dwBatchSize);

    m_BatchPool = std::make_unique<MemoryPool>(1024 * 1024 * 20);

    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::Flush()
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());

    if (root)
    {
        ScopedLock sl(m_cs);

        root->numElements = m_dwBatchRow;

        for (DWORD i = 0; i < m_dwColumnNumber; i++)
        {
            root->fields[m_dwColumnCounter]->numElements = m_dwBatchRow;
        }
    }
    m_Writer->add(*m_Batch);
    m_BatchPool = std::make_unique<MemoryPool>(20 * 1024 * 1024);
    m_dwBatchRow = 0;
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::Close()
{

    if (auto hr = Flush(); FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to flush arrow table\r\n");
        return hr;
    }

    m_Writer->close();

    if (m_pTermination)
    {
        ScopedLock sl(m_cs);
        Robustness::RemoveTerminationHandler(m_pTermination);
        m_pTermination = nullptr;
    }
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteNothing()
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::ColumnVectorBatch*>(root->fields[m_dwColumnCounter]);

        col->notNull[m_dwBatchRow] = false;
        col->hasNulls = true;
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::AbandonRow()
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    for (auto i = m_dwColumnCounter; i < m_dwColumnNumber; i++)
    {
        auto col = dynamic_cast<orc::ColumnVectorBatch*>(root->fields[i]);

        if (col)
        {
            col->notNull[m_dwBatchRow] = false;
            col->hasNulls = true;
        }
    }
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::AbandonColumn()
{
    if (auto hr = WriteNothing(); FAILED(hr))
        return hr;

    return E_FAIL;
}

HRESULT Orc::TableOutput::OptRowColumn::Writer::AddColumnAndCheckNumbers()
{
    m_dwColumnCounter++;
    if (m_dwColumnCounter > m_dwColumnNumber)
    {
        auto counter = m_dwColumnCounter;
        m_dwColumnCounter = 0L;
        throw Orc::Exception(
            ExceptionSeverity::Fatal,
            L"Too many columns written to Parquet (got %d, max is %d)",
            counter,
            m_dwColumnNumber);
    }
    return S_OK;
}

HRESULT Orc::TableOutput::OptRowColumn::Writer::WriteEndOfLine()
{
    if (m_dwColumnCounter == m_dwColumnNumber)
    {
        m_dwColumnCounter = 0L;
    }
    else if (m_dwColumnCounter < m_dwColumnNumber)
    {
        auto counter = m_dwColumnCounter;
        m_dwColumnCounter = 0L;
        throw Orc::Exception(
            ExceptionSeverity::Fatal,
            L"Too few columns written to OptRowColumn (got %d, max is %d)",
            counter,
            m_dwColumnNumber);
    }
    else if (m_dwColumnCounter > m_dwColumnNumber)
    {
        auto counter = m_dwColumnCounter;
        m_dwColumnCounter = 0L;
        throw Orc::Exception(
            ExceptionSeverity::Fatal,
            L"Too many columns written to OptRowColumn (got %d, max is %d)",
            counter,
            m_dwColumnNumber);
    }

    m_dwBatchRow++;
    m_dwRows++;

    if (m_dwBatchRow >= m_dwBatchSize)
    {
        return Flush();
    }
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteString(const std::wstring& strString)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::StringVectorBatch*>(root->fields[m_dwColumnCounter]);
        auto data = (char*)m_BatchPool->malloc(strString.size());
        if (data == nullptr)
        {
            AbandonColumn();
            return E_OUTOFMEMORY;
        }

        Buffer<CHAR, MAX_PATH> ansiString;

        if (auto hr = WideToAnsi(_L_, strString, ansiString); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }
        auto pStr = m_BatchPool->malloc(ansiString.size());
        if (pStr == nullptr)
        {
            AbandonColumn();
            return E_OUTOFMEMORY;
        }

        memcpy_s(pStr, ansiString.size(), ansiString.get(), ansiString.size());

        col->data[m_dwBatchRow] = pStr;
        col->length[m_dwBatchRow] = strString.size();
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteString(const std::wstring_view& strString)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::StringVectorBatch*>(root->fields[m_dwColumnCounter]);
        auto data = (char*)m_BatchPool->malloc(strString.size());
        if (data == nullptr)
        {
            AbandonColumn();
            return E_OUTOFMEMORY;
        }
        Buffer<CHAR, MAX_PATH> ansiString;

        if (auto hr = WideToAnsi(_L_, strString, ansiString); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }
        auto pStr = m_BatchPool->malloc(ansiString.size());
        if (pStr == nullptr)
        {
            AbandonColumn();
            return E_OUTOFMEMORY;
        }

        memcpy_s(pStr, ansiString.size(), ansiString.get(), ansiString.size());

        col->data[m_dwBatchRow] = pStr;
        col->length[m_dwBatchRow] = strString.size();
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteString(const WCHAR* szString)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto strSize = wcslen(szString);

        auto col = dynamic_cast<orc::StringVectorBatch*>(root->fields[m_dwColumnCounter]);
        auto data = (char*)m_BatchPool->malloc(strSize);
        if (data == nullptr)
        {
            AbandonColumn();
            return E_OUTOFMEMORY;
        }
        Buffer<CHAR, MAX_PATH> ansiString;

        if (auto hr = WideToAnsi(_L_, szString, static_cast<DWORD>(strSize), ansiString); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }

        auto pStr = m_BatchPool->malloc(ansiString.size());
        if (pStr == nullptr)
        {
            AbandonColumn();
            return E_OUTOFMEMORY;
        }
        memcpy_s(pStr, ansiString.size(), ansiString.get(), ansiString.size());

        col->data[m_dwBatchRow] = pStr;
        col->length[m_dwBatchRow] = strSize;
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteCharArray(const WCHAR* szString, DWORD dwCharCount)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::StringVectorBatch*>(root->fields[m_dwColumnCounter]);
        auto data = (char*)m_BatchPool->malloc(dwCharCount);
        if (data == nullptr)
        {
            AbandonColumn();
            return E_OUTOFMEMORY;
        }
        Buffer<CHAR, MAX_PATH> ansiString;

        if (auto hr = WideToAnsi(_L_, szString, dwCharCount, ansiString); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }

        auto pStr = m_BatchPool->malloc(ansiString.size());
        if (pStr == nullptr)
        {
            AbandonColumn();
            return E_OUTOFMEMORY;
        }

        memcpy_s(pStr, ansiString.size(), ansiString.get(), ansiString.size());

        col->data[m_dwBatchRow] = pStr;
        col->length[m_dwBatchRow] = dwCharCount;
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP
Orc::TableOutput::OptRowColumn::Writer::WriteFormated_(const std::wstring_view& szFormat, IOutput::wformat_args args)
{
    Buffer<WCHAR, MAX_PATH> buffer;

    auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);

    if (buffer.empty())
        return WriteNothing();
    else
        return WriteCharArray(buffer.get(), buffer.size());
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteString(const std::string& strString)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::StringVectorBatch*>(root->fields[m_dwColumnCounter]);
        auto data = (char*)m_BatchPool->malloc(strString.size());
        if (data == nullptr)
        {
            AbandonColumn();
            return E_OUTOFMEMORY;
        }
        memcpy(data, strString.data(), strString.size());

        col->data[m_dwBatchRow] = data;
        col->length[m_dwBatchRow] = strString.size();
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteString(const std::string_view& strString)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::StringVectorBatch*>(root->fields[m_dwColumnCounter]);
        auto data = (char*)m_BatchPool->malloc(strString.size());
        if (data == nullptr)
        {
            AbandonColumn();
            return E_OUTOFMEMORY;
        }

        memcpy(data, strString.data(), strString.size());

        col->data[m_dwBatchRow] = data;
        col->length[m_dwBatchRow] = strString.size();
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteString(const CHAR* szString)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::StringVectorBatch*>(root->fields[m_dwColumnCounter]);

        auto strSize = strlen(szString);

        auto data = (char*)m_BatchPool->malloc(strSize);
        if (data == nullptr)
        {
            AbandonColumn();
            return E_OUTOFMEMORY;
        }
        memcpy(data, szString, strSize);

        col->data[m_dwBatchRow] = data;
        col->length[m_dwBatchRow] = strSize;
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteCharArray(const CHAR* szString, DWORD dwCharCount)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::StringVectorBatch*>(root->fields[m_dwColumnCounter]);
        auto data = (char*)m_BatchPool->malloc(dwCharCount);
        if (data == nullptr)
        {
            AbandonColumn();
            return E_OUTOFMEMORY;
        }

        memcpy(data, szString, dwCharCount);

        col->data[m_dwBatchRow] = data;
        col->length[m_dwBatchRow] = dwCharCount;
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP
Orc::TableOutput::OptRowColumn::Writer::WriteFormated_(const std::string_view& szFormat, IOutput::format_args args)
{
    Buffer<CHAR, MAX_PATH> buffer;

    auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);

    if (buffer.empty())
        return WriteNothing();
    else
        return WriteCharArray(buffer.get(), buffer.size());
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteAttributes(DWORD dwFileAttributes)
{
    if (auto hr = WriteFormated(
            "{}{}{}{}{}{}{}{}{}{}{}{}{}",
            dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE ? 'A' : '.',
            dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED ? 'C' : '.',
            dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? 'D' : '.',
            dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED ? 'E' : '.',
            dwFileAttributes & FILE_ATTRIBUTE_HIDDEN ? 'H' : '.',
            dwFileAttributes & FILE_ATTRIBUTE_NORMAL ? 'N' : '.',
            dwFileAttributes & FILE_ATTRIBUTE_OFFLINE ? 'O' : '.',
            dwFileAttributes & FILE_ATTRIBUTE_READONLY ? 'R' : '.',
            dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ? 'L' : '.',
            dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE ? 'P' : '.',
            dwFileAttributes & FILE_ATTRIBUTE_SYSTEM ? 'S' : '.',
            dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY ? 'T' : '.',
            dwFileAttributes & FILE_ATTRIBUTE_VIRTUAL ? 'V' : '.');
        FAILED(hr))
    {
        AbandonColumn();
        return hr;
    }
    return S_OK;
}

HRESULT Orc::TableOutput::OptRowColumn::Writer::WriteFileTime(FILETIME fileTime)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::TimestampVectorBatch*>(root->fields[m_dwColumnCounter]);

        auto time_point = Orc::ConvertTo(fileTime);

        col->data[m_dwBatchRow] = std::chrono::system_clock::to_time_t(time_point);
        col->nanoseconds[m_dwBatchRow] = 0;

        AddColumnAndCheckNumbers();
        return S_OK;
    }
    else
        return AbandonColumn();
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteFileTime(LONGLONG fileTime)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::TimestampVectorBatch*>(root->fields[m_dwColumnCounter]);

        ULARGE_INTEGER uli;
        uli.QuadPart = fileTime;
        FILETIME ft;
        ft.dwHighDateTime = uli.HighPart;
        ft.dwLowDateTime = uli.LowPart;

        auto time_point = Orc::ConvertTo(ft);

        col->data[m_dwBatchRow] = std::chrono::system_clock::to_time_t(time_point);
        col->nanoseconds[m_dwBatchRow] = 0;

        AddColumnAndCheckNumbers();
        return S_OK;
    }
    else
        return AbandonColumn();
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteTimeStamp(time_t tmStamp)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::TimestampVectorBatch*>(root->fields[m_dwColumnCounter]);

        AddColumnAndCheckNumbers();
        return S_OK;
    }
    else
        return AbandonColumn();
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteTimeStamp(tm tmStamp)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::TimestampVectorBatch*>(root->fields[m_dwColumnCounter]);

        AddColumnAndCheckNumbers();
        return S_OK;
    }
    else
        return AbandonColumn();
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteFileSize(LARGE_INTEGER fileSize)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::LongVectorBatch*>(root->fields[m_dwColumnCounter]);

        col->data[m_dwBatchRow] = fileSize.QuadPart;
        AddColumnAndCheckNumbers();
        return S_OK;
    }
    else
        return AbandonColumn();
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteFileSize(ULONGLONG fileSize)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::LongVectorBatch*>(root->fields[m_dwColumnCounter]);

        col->data[m_dwBatchRow] = fileSize;
        AddColumnAndCheckNumbers();
        return S_OK;
    }
    else
        return AbandonColumn();
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteFileSize(DWORD nFileSizeHigh, DWORD nFileSizeLow)
{
    LARGE_INTEGER FileSize;

    FileSize.HighPart = nFileSizeHigh;
    FileSize.LowPart = nFileSizeLow;

    // File	size calculation only required for Very	big	files.
    return WriteFileSize(FileSize);
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteBool(bool bBoolean)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::LongVectorBatch*>(root->fields[m_dwColumnCounter]);

        col->data[m_dwBatchRow] = bBoolean;
        AddColumnAndCheckNumbers();
        return S_OK;
    }
    else
        return AbandonColumn();
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteEnum(DWORD dwEnum)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::LongVectorBatch*>(root->fields[m_dwColumnCounter]);

        col->data[m_dwBatchRow] = dwEnum;
        AddColumnAndCheckNumbers();
        return S_OK;
    }
    else
        return AbandonColumn();
}
STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteEnum(DWORD dwEnum, const WCHAR* EnumValues[])
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::LongVectorBatch*>(root->fields[m_dwColumnCounter]);

        col->data[m_dwBatchRow] = dwEnum;
        AddColumnAndCheckNumbers();
    }
    else
        return AbandonColumn();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteFlags(DWORD dwFlags)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::LongVectorBatch*>(root->fields[m_dwColumnCounter]);

        col->data[m_dwBatchRow] = dwFlags;
        AddColumnAndCheckNumbers();
    }
    else
        return AbandonColumn();
    return S_OK;
}

STDMETHODIMP
Orc::TableOutput::OptRowColumn::Writer::WriteFlags(DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator)
{
    return WriteFlags(dwFlags);
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteExactFlags(DWORD dwFlags)
{
    return WriteFlags(dwFlags);
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteExactFlags(DWORD dwFlags, const FlagsDefinition FlagValues[])
{
    return WriteExactFlags(dwFlags);
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteGUID(const GUID& guid)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::StringVectorBatch*>(root->fields[m_dwColumnCounter]);

        auto guidSize = sizeof(GUID);

        auto data = (char*)m_BatchPool->malloc(guidSize);
        if (data == nullptr)
            return E_OUTOFMEMORY;

        memcpy_s(data, 16, &guid, sizeof(GUID));
        col->data[m_dwBatchRow] = data;
        col->length[m_dwBatchRow] = guidSize;
        AddColumnAndCheckNumbers();
    }
    else
        return AbandonColumn();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteXML(const WCHAR* szString)
{
    return WriteString(szString);
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteXML(const WCHAR* szString, DWORD dwCharCount)
{
    return WriteString(std::wstring_view(szString, dwCharCount));
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteXML(const CHAR* szString)
{
    return WriteString(szString);
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteXML(const CHAR* szString, DWORD dwCharCount)
{
    return WriteString(std::string_view(szString, dwCharCount));
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteInteger(DWORD dwInteger)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::LongVectorBatch*>(root->fields[m_dwColumnCounter]);

        if (!SafeCast(dwInteger, col->data[m_dwBatchRow]))
        {
            AbandonRow();
            return E_FAIL;
        }
        AddColumnAndCheckNumbers();
    }
    else
        return AbandonColumn();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteInteger(LONGLONG llInteger)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::LongVectorBatch*>(root->fields[m_dwColumnCounter]);

        if (!SafeCast(llInteger, col->data[m_dwBatchRow]))
        {
            AbandonColumn();
            return E_FAIL;
        }
        AddColumnAndCheckNumbers();
    }
    else
        return AbandonColumn();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteInteger(ULONGLONG ullInteger)
{
    auto root = dynamic_cast<orc::StructVectorBatch*>(m_Batch.get());
    if (root)
    {
        auto col = dynamic_cast<orc::LongVectorBatch*>(root->fields[m_dwColumnCounter]);

        if (!SafeCast(ullInteger, col->data[m_dwBatchRow]))
        {
            AbandonColumn();
            return E_FAIL;
        }
        AddColumnAndCheckNumbers();
    }
    else
        return AbandonColumn();

    return S_OK;
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteBytes(const BYTE pBytes[], DWORD dwLen)
{
    return WriteNothing();
}

STDMETHODIMP Orc::TableOutput::OptRowColumn::Writer::WriteBytes(const CBinaryBuffer& Buffer)
{
    return WriteBytes(Buffer.GetData(), (DWORD)Buffer.GetCount());
}
