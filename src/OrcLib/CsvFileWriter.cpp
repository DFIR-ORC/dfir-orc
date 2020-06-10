//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include <string>

#include "CSVFileWriter.h"

#include "BinaryBuffer.h"
#include "Robustness.h"

#include "WideAnsi.h"

#include "ByteStream.h"
#include "FileStream.h"

#include "OrcException.h"

#include <boost/algorithm/string/replace.hpp>

#include <fmt/ostream.h>

using namespace Orc;
namespace fs = std::filesystem;

class Orc::TableOutput::CSV::WriterTermination : public TerminationHandler
{
public:
    WriterTermination(const std::wstring& strDescr, std::weak_ptr<Writer> pW)
        : TerminationHandler(strDescr, ROBUSTNESS_CSV)
        , m_pWriter(std::move(pW)) {};

    HRESULT operator()();

private:
    std::weak_ptr<Writer> m_pWriter;
};

HRESULT Orc::TableOutput::CSV::WriterTermination::operator()()
{
    if (auto pWriter = m_pWriter.lock(); pWriter)
    {
        pWriter->Flush();
    }
    return S_OK;
}

Orc::TableOutput::CSV::Writer::Writer(logger pLog, std::unique_ptr<Options>&& options)
    : _L_(std::move(pLog))
    , m_Options(std::move(options))
{
}

struct Orc::TableOutput::CSV::Writer::MakeSharedEnabler : public Orc::TableOutput::CSV::Writer
{
    MakeSharedEnabler(logger pLog, std::unique_ptr<Options>&& options)
        : Orc::TableOutput::CSV::Writer(std::move(pLog), std::move(options))
    {
    }
};

std::shared_ptr<Orc::TableOutput::CSV::Writer>
Orc::TableOutput::CSV::Writer::MakeNew(logger pLog, std::unique_ptr<TableOutput::Options>&& options)
{
    auto retval =
        std::make_shared<MakeSharedEnabler>(std::move(pLog), dynamic_unique_ptr_cast<CSV::Options>(std::move(options)));

    if (FAILED(retval->InitializeBuffer(retval->m_Options->dwBufferSize)))
        return nullptr;

    std::wstring strDescr = L"Termination for CSV::Writer";
    retval->m_pTermination = std::make_shared<WriterTermination>(strDescr, retval);
    Robustness::AddTerminationHandler(retval->m_pTermination);
    return retval;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::SetSchema(const Schema& schema)
{
    m_Schema.reserve(schema.size());

    bool bFirst = true;

    std::wstring emptyStr;

    for (const auto& column : schema)
    {
        auto csv_col = std::make_unique<Column>(*column);

        if (csv_col->Type == ColumnType::UTF16Type || csv_col->Type == ColumnType::UTF8Type
            || csv_col->Type == ColumnType::XMLType)
        {
            csv_col->FormatColumn = fmt::format(
                L"{}{}{}{}",
                bFirst ? emptyStr : m_Options->Delimiter,
                m_Options->StringDelimiter,
                csv_col->Format.value_or(L"{}"),
                m_Options->StringDelimiter);
        }
        else if (csv_col->Type == ColumnType::BinaryType || csv_col->Type == ColumnType::FixedBinaryType)
        {
            csv_col->FormatColumn =
                fmt::format(L"{}{}", bFirst ? emptyStr : m_Options->Delimiter, csv_col->Format.value_or(L"{:02X}"));
        }
        else if (csv_col->Type == ColumnType::TimeStampType)
        {
            csv_col->FormatColumn = fmt::format(
                L"{}{}",
                bFirst ? emptyStr : m_Options->Delimiter,
                csv_col->Format.value_or(L"{YYYY:#04}-{MM:#02}-{DD:#02} {hh:#02}:{mm:#02}:{ss:#02}.{mmm:#03}"));
        }
        else
        {
            csv_col->FormatColumn =
                fmt::format(L"{}{}", bFirst ? emptyStr : m_Options->Delimiter, csv_col->Format.value_or(L"{}"));
        }

        m_Schema.AddColumn(std::move(csv_col));
        bFirst = false;
    }

    m_dwColumnNumber = static_cast<DWORD>(m_Schema.size());
    m_dwColumnCounter = 0L;

    try
    {
        if (m_pByteStream)
        {
            auto hr = E_FAIL;
            if (FAILED(hr = WriteHeaders(m_Schema)))
            {
                if (m_szFileName)
                    log::Error(_L_, hr, L"Could not write columns to specified file %s\r\n", m_szFileName);
                else
                    log::Error(_L_, hr, L"Could not write columns to specified stream\r\n", m_szFileName);
                return hr;
            }
        }
    }
    catch (Orc::Exception& e)
    {
        e.PrintMessage(_L_);
        return e.GetHRESULT();
    }
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::InitializeBuffer(DWORD dwBufferSize)
{
    ScopedLock sl(m_cs);

    DWORD dwPagesToAlloc = ((dwBufferSize - 1) / PageSize()) + 1;

    if (dwPagesToAlloc < 2)
        dwPagesToAlloc++;

    DWORD dwBytesToAlloc = dwPagesToAlloc * PageSize();
    m_pBuffer = (WCHAR*)VirtualAlloc(NULL, dwBytesToAlloc, MEM_COMMIT, PAGE_READWRITE);
    if (m_pBuffer == NULL)
        return HRESULT_FROM_WIN32(GetLastError());

    m_pCurrent = m_pBuffer;
    m_dwCount = 0;
    m_dwBufferSize = dwBytesToAlloc;

    switch (m_Options->Encoding)
    {
        case OutputSpec::Encoding::UTF8:
            m_pUTF8Buffer = (LPSTR)VirtualAlloc(NULL, dwBytesToAlloc, MEM_COMMIT, PAGE_READWRITE);
            if (m_pUTF8Buffer == NULL)
                return HRESULT_FROM_WIN32(GetLastError());
            m_dwUTF8BufferSize = dwBytesToAlloc;
            break;
        default:
            break;
    }

    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteBOM()
{
    if (m_pByteStream == nullptr)
        return E_UNEXPECTED;
    if (m_bBOMWritten)
        return S_OK;

    if (m_Options->bBOM)
    {
        ULONGLONG bytesWritten;

        switch (m_Options->Encoding)
        {
            case OutputSpec::Encoding::UTF8:
            {
                BYTE bom[3] = {0xEF, 0xBB, 0xBF};
                if (auto hr = m_pByteStream->Write(bom, 3 * sizeof(BYTE), &bytesWritten); FAILED(hr))
                    return hr;
            }
            break;
            case OutputSpec::Encoding::UTF16:
            {
                BYTE bom[2] = {0xFF, 0xFE};
                if (auto hr = m_pByteStream->Write(bom, 2 * sizeof(BYTE), &bytesWritten); FAILED(hr))
                    return hr;
            }
            break;
            default:
                break;
        }
    }
    m_bBOMWritten = true;
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::Writer::WriteToFile(const fs::path& path)
{
    return WriteToFile(path.c_str());
}

HRESULT Orc::TableOutput::CSV::Writer::WriteToFile(const WCHAR* szFileName)
{
    if (szFileName == NULL)
        return E_POINTER;

    auto pFileStream = std::make_shared<FileStream>(_L_);

    if (pFileStream == nullptr)
        return E_OUTOFMEMORY;

    // ... create it
    if (auto hr = pFileStream->WriteTo(szFileName); FAILED(hr))
        return hr;

    return WriteToStream(pFileStream, true);
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteToStream(const std::shared_ptr<ByteStream>& pStream, bool bCloseStream)
{
    if (m_pByteStream != nullptr && m_bCloseStream)
    {
        m_pByteStream->Close();
    }

    m_bCloseStream = bCloseStream;
    m_pByteStream = pStream;

    if (auto hr = WriteBOM(); FAILED(hr))
        return hr;

    if (m_Schema)
    {
        if (auto hr = WriteHeaders(m_Schema); FAILED(hr))
            return hr;
    }
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::Flush()
{
    ScopedLock sl(m_cs);

    LPBYTE pBuffer = NULL;
    DWORD dwBytesToWrite = 0L;

    switch (m_Options->Encoding)
    {
        case OutputSpec::Encoding::UTF8:
            dwBytesToWrite = WideCharToMultiByte(
                CP_UTF8, 0L, m_pBuffer, (m_dwCount / sizeof(WCHAR)), m_pUTF8Buffer, m_dwUTF8BufferSize, NULL, NULL);
            if (!dwBytesToWrite)
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
            pBuffer = (LPBYTE)m_pUTF8Buffer;
            break;
        case OutputSpec::Encoding::UTF16:
            pBuffer = (LPBYTE)m_pBuffer;
            dwBytesToWrite = m_dwCount;
            break;
        default:
            return E_INVALIDARG;
    }

    if (m_pByteStream)
    {
        ULONGLONG ullBytesWritten;
        if (auto hr = m_pByteStream->Write((LPBYTE)pBuffer, dwBytesToWrite, &ullBytesWritten); FAILED(hr))
            return hr;

        if (ullBytesWritten < dwBytesToWrite)
        {
            return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        }
    }

    DWORD dwOldProtect = 0L;
    if (m_pCurrent != nullptr && m_pBuffer != nullptr)
    {
        m_pCurrent = m_pBuffer;
        m_dwCount = 0;
    }
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::Close()
{
    ScopedLock sl(m_cs);

    if (m_pTermination)
    {
        Robustness::RemoveTerminationHandler(m_pTermination);
        m_pTermination = nullptr;
    }
    if (m_pCurrent)
    {
        Flush();
        m_pCurrent = NULL;
    }
    if (m_pByteStream != nullptr && m_bCloseStream)
    {
        m_pByteStream->Close();
    }
    if (m_pBuffer)
    {
        VirtualFree(m_pBuffer, 0, MEM_RELEASE);
        m_pBuffer = NULL;
    }
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::Writer::AddColumnAndCheckNumbers()
{
    m_dwColumnCounter++;
    if (m_dwColumnCounter > m_dwColumnNumber)
    {
        auto counter = m_dwColumnCounter;
        m_dwColumnCounter = 0L;
        throw Orc::Exception(
            ExceptionSeverity::Fatal,
            L"Too many columns written to CSV (got %d, max is %d)",
            counter,
            m_dwColumnNumber);
    }
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteHeaders(const TableOutput::Schema& columns)
{
    bool bFirst = true;
    m_dwColumnCounter = 0;

    if (!columns)
        return E_INVALIDARG;

    using namespace std::string_view_literals;
    auto format_string = fmt::format(L"{}{{}}", m_Options->Delimiter);

    for (const auto& column : columns)
    {
        if (bFirst)
        {
            if (auto hr = FormatToBuffer(L"{}", column->ColumnName); FAILED(hr))
                AbandonColumn();
            bFirst = false;
        }
        else
        {
            if (auto hr = FormatToBuffer(format_string, column->ColumnName); FAILED(hr))
                AbandonColumn();
        }
        m_dwColumnCounter++;
    }
    WriteEndOfLine();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteNothing()
{
    if (m_dwColumnCounter > 0)  // First column does not need the ",", second column will be prepended with it
    {
        if (auto hr = FormatToBuffer(m_Options->Delimiter); FAILED(hr))
            return hr;
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP
Orc::TableOutput::CSV::Writer::WriteFormated_(const std::wstring_view& szFormat, IOutput::wformat_args args)
{
    using namespace std::string_view_literals;

    Buffer<WCHAR, MAX_PATH> buffer;
    auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);

    std::wstring_view result_string = buffer.size() > 0 ? std::wstring_view((LPCWSTR)buffer, buffer.size()) : L""sv;

    if (auto hr = FormatColumn(result_string); FAILED(hr))
    {
        AbandonColumn();
        return hr;
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteFormated_(const std::string_view& szFormat, IOutput::format_args args)
{
    Buffer<CHAR, MAX_PATH> buffer;
    auto result = fmt::vformat_to(std::back_inserter(buffer), szFormat, args);

    std::string_view result_string((LPCSTR)buffer, buffer.size());

    if (auto [hr, wstr] = AnsiToWide(_L_, result_string); SUCCEEDED(hr))
    {
        if (auto hr = FormatColumn(wstr); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }
        AddColumnAndCheckNumbers();
    }
    else
    {
        AbandonColumn();
        return hr;
    }
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteAttributes(DWORD dwFileAttributes)
{
    if (auto hr = WriteFormated(
            L"{}{}{}{}{}{}{}{}{}{}{}{}{}",
            dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE ? L'A' : L'.',
            dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED ? L'C' : L'.',
            dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? L'D' : L'.',
            dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED ? L'E' : L'.',
            dwFileAttributes & FILE_ATTRIBUTE_HIDDEN ? L'H' : L'.',
            dwFileAttributes & FILE_ATTRIBUTE_NORMAL ? L'N' : L'.',
            dwFileAttributes & FILE_ATTRIBUTE_OFFLINE ? L'O' : L'.',
            dwFileAttributes & FILE_ATTRIBUTE_READONLY ? L'R' : L'.',
            dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ? L'L' : L'.',
            dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE ? L'P' : L'.',
            dwFileAttributes & FILE_ATTRIBUTE_SYSTEM ? L'S' : L'.',
            dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY ? L'T' : L'.',
            dwFileAttributes & FILE_ATTRIBUTE_VIRTUAL ? L'V' : L'.');
        FAILED(hr))
    {
        AbandonColumn();
        return hr;
    }
    return S_OK;
}

HRESULT Orc::TableOutput::CSV::Writer::WriteFileTime(FILETIME fileTime)
{
    // Convert the Create time to System time.
    SYSTEMTIME stUTC;
    FileTimeToSystemTime(&fileTime, &stUTC);

    if (auto hr = FormatColumn(
            fmt::arg(L"YYYY", stUTC.wYear),
            fmt::arg(L"MM", stUTC.wMonth),
            fmt::arg(L"DD", stUTC.wDay),
            fmt::arg(L"hh", stUTC.wHour),
            fmt::arg(L"mm", stUTC.wMinute),
            fmt::arg(L"ss", stUTC.wSecond),
            fmt::arg(L"mmm", stUTC.wMilliseconds));
        FAILED(hr))
    {
        AbandonColumn();
        return hr;
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteFileTime(LONGLONG fileTime)
{
    return WriteFileTime(*((FILETIME*)(&fileTime)));
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteTimeStamp(time_t time)
{
    // @@TimeStamp
    // Convert the timestamp to GMT/UTC
    tm tmStamp;
    errno_t errnum = gmtime_s(&tmStamp, (const time_t*)&time);
    if (errnum)
    {
        return E_INVALIDARG;
    }
    return WriteTimeStamp(tmStamp);
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteTimeStamp(tm tmStamp)
{
    if (auto hr = FormatColumn(
            fmt::arg(L"YYYY", tmStamp.tm_year + 1900),
            fmt::arg(L"MM", tmStamp.tm_mon + 1),
            fmt::arg(L"DD", tmStamp.tm_mday),
            fmt::arg(L"hh", tmStamp.tm_hour),
            fmt::arg(L"mm", tmStamp.tm_min),
            fmt::arg(L"ss", tmStamp.tm_sec),
            fmt::arg(L"mmm", 0));
        FAILED(hr))
    {
        AbandonColumn();
        return hr;
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteFileSize(LARGE_INTEGER fileSize)
{
    // File	size calculation only required for Very	big	files.
    if (auto hr = FormatColumn(fileSize.QuadPart); FAILED(hr))
    {
        AbandonColumn();
        return hr;
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteFileSize(DWORD nFileSizeHigh, DWORD nFileSizeLow)
{
    LARGE_INTEGER FileSize;

    FileSize.HighPart = nFileSizeHigh;
    FileSize.LowPart = nFileSizeLow;

    // File	size calculation only required for Very	big	files.
    return WriteFileSize(FileSize);
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::AbandonRow()
{
    return E_NOTIMPL;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::AbandonColumn()
{
    return WriteNothing();
}

HRESULT Orc::TableOutput::CSV::Writer::WriteEndOfLine()
{
    if (auto hr = FormatToBuffer(m_Options->EndOfLine); FAILED(hr))
        return hr;

    auto counter = m_dwColumnCounter;
    m_dwColumnCounter = 0L;
    if (counter < m_dwColumnNumber)
        throw Orc::Exception(
            ExceptionSeverity::Fatal, L"Too few columns written to CSV (got %d, max is %d)", counter, m_dwColumnNumber);
    if (counter > m_dwColumnNumber)
        throw Orc::Exception(
            ExceptionSeverity::Fatal,
            L"Too many columns written to CSV (got %d, max is %d)",
            counter,
            m_dwColumnNumber);
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteBool(bool bBoolean)
{
    if (auto hr = FormatColumn(bBoolean ? m_Options->BoolChars[0] : m_Options->BoolChars[1]); FAILED(hr))
    {
        AbandonColumn();
        return hr;
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteEnum(DWORD dwEnum)
{
    auto pCol = static_cast<const Column*>(&m_Schema[m_dwColumnCounter]);

    unsigned int i = 0;
    const WCHAR* szValue = NULL;

    if (pCol->EnumValues.has_value())
    {
        auto it = std::find_if(
            std::begin(pCol->EnumValues.value()), end(pCol->EnumValues.value()), [dwEnum](const auto& col) {
                return dwEnum == col.Index;
            });
        if (it != std::end(pCol->EnumValues.value()))
        {
            if (auto hr = FormatColumn(it->strValue); FAILED(hr))
            {
                AbandonColumn();
                return hr;
            }
            AddColumnAndCheckNumbers();
            return S_OK;
        }
    }
    if (auto hr = FormatColumn(dwEnum); FAILED(hr))
    {
        AbandonColumn();
        return hr;
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteEnum(DWORD dwEnum, const WCHAR* EnumValues[])
{
    using namespace std::string_view_literals;

    unsigned int i = 0;
    const WCHAR* szValue = NULL;

    while (((EnumValues[i]) != NULL) && i <= dwEnum)
    {
        if (i == dwEnum)
            szValue = EnumValues[i];
        i++;
    }

    if (szValue == NULL)
    {
        WriteFormated(L"IllegalEnumValue#{}"sv, dwEnum);
    }
    else
    {
        if (auto hr = FormatColumn(szValue); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }
        AddColumnAndCheckNumbers();
    }
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteFlags(DWORD dwFlags)
{
    auto pCol = static_cast<const Column*>(&m_Schema[m_dwColumnCounter]);

    if (!pCol->FlagsValues.has_value())
    {
        if (auto hr = FormatColumn(dwFlags); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }
        AddColumnAndCheckNumbers();
        return S_OK;
    }

    const auto& values = pCol->FlagsValues.value();

    bool bFirst = true;
    Buffer<WCHAR, MAX_PATH> buffer;
    for (const auto& value : values)
    {
        if (dwFlags & value.dwFlag)
        {
            if (bFirst)
            {
                bFirst = false;
                fmt::format_to(std::back_inserter(buffer), L"{}", value.strFlag);
            }
            else
            {
                fmt::format_to(std::back_inserter(buffer), L"|{}", value.strFlag);
            }
        }
    }

    if (buffer.empty())
    {
        if (auto hr = WriteNothing(); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }
        return S_OK;
    }
    else
    {
        if (auto hr = FormatColumn(std::wstring_view(buffer.get(), buffer.size())); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP
Orc::TableOutput::CSV::Writer::WriteFlags(DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator)
{
    bool bFirst = true;
    int idx = 0;

    Buffer<WCHAR, MAX_PATH> buffer;
    while (FlagValues[idx].dwFlag != 0xFFFFFFFF)
    {
        if (dwFlags & FlagValues[idx].dwFlag)
        {
            if (bFirst)
            {
                bFirst = false;
                fmt::format_to(std::back_inserter(buffer), L"{}", FlagValues[idx].szShortDescr);
            }
            else
            {
                fmt::format_to(std::back_inserter(buffer), L"{}{}", cSeparator, FlagValues[idx].szShortDescr);
            }
        }
        idx++;
    }
    if (bFirst)
    {
        if (auto hr = FormatColumn(dwFlags); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }
    }
    else
    {
        if (auto hr = FormatColumn(std::wstring_view(buffer.get(), buffer.size())); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteExactFlags(DWORD dwFlags)
{
    int idx = 0;

    auto pCol = static_cast<const Column*>(&m_Schema[m_dwColumnCounter]);

    const auto& values = pCol->FlagsValues.value();

    bool bFound = false;
    Buffer<WCHAR, MAX_PATH> buffer;
    for (const auto& value : values)
    {
        if (dwFlags == value.dwFlag)
        {
            bFound = true;
            fmt::format_to(std::back_inserter(buffer), L"{}", value.strFlag);
            break;
        }
    }

    if (bFound)
    {
        if (auto hr = FormatColumn(std::wstring_view(buffer.get(), buffer.size())))
        {
            AbandonColumn();
            return hr;
        }
    }
    else
    {
        if (auto hr = FormatColumn(dwFlags); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }
    }

    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteExactFlags(DWORD dwFlags, const FlagsDefinition FlagValues[])
{
    int idx = 0;
    bool found = false;

    Buffer<WCHAR, MAX_PATH> buffer;
    while (FlagValues[idx].dwFlag != 0xFFFFFFFF)
    {
        if (dwFlags == FlagValues[idx].dwFlag)
        {
            fmt::format_to(std::back_inserter(buffer), L"{}", FlagValues[idx].szShortDescr);
            found = true;
            break;
        }
        idx++;
    }

    if (found && !buffer.empty())
    {
        if (auto hr = FormatColumn(std::wstring_view(buffer.get(), buffer.size())); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }
    }
    else
    {
        if (auto hr = FormatColumn(dwFlags); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }
    }

    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteGUID(const GUID& guid)
{
    WCHAR szCLSID[MAX_GUID_STRLEN];
    if (!StringFromGUID2(guid, szCLSID, MAX_GUID_STRLEN))
    {
        AbandonColumn();
        return E_NOT_SUFFICIENT_BUFFER;
    }
    if (auto hr = FormatColumn(szCLSID); FAILED(hr))
    {
        AbandonColumn();
        return hr;
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteXML(const WCHAR* szString)
{
    std::wstring strXML(szString);
    std::replace(begin(strXML), end(strXML), L'\r', L' ');
    std::replace(begin(strXML), end(strXML), L'\n', L' ');

    if (auto hr = FormatColumn(strXML); FAILED(hr))
    {
        AbandonColumn();
        return hr;
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteXML(const WCHAR* szString, DWORD dwCharCount)
{
    std::wstring strXML;
    strXML.assign(szString, dwCharCount);
    std::replace(begin(strXML), end(strXML), L'\r', L' ');
    std::replace(begin(strXML), end(strXML), L'\n', L' ');

    if (auto hr = FormatColumn(strXML); FAILED(hr))
    {
        AbandonColumn();
        return hr;
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteXML(const CHAR* szString)
{
    std::string strXML(szString);
    std::replace(begin(strXML), end(strXML), '\r', ' ');
    std::replace(begin(strXML), end(strXML), '\n', ' ');

    if (auto [hr, wstr] = AnsiToWide(_L_, strXML); SUCCEEDED(hr))
    {
        if (auto hr = FormatColumn(wstr); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }
        AddColumnAndCheckNumbers();
    }
    else
    {
        AbandonColumn();
        return hr;
    }

    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteXML(const CHAR* szString, DWORD dwCharCount)
{
    if (auto [hr, wstr] = AnsiToWide(_L_, std::string_view(szString, dwCharCount)); SUCCEEDED(hr))
    {
        std::replace(begin(wstr), end(wstr), L'\r', L' ');
        std::replace(begin(wstr), end(wstr), L'\n', L' ');

        if (auto hr = FormatColumn(wstr); FAILED(hr))
        {
            AbandonColumn();
            return hr;
        }
        AddColumnAndCheckNumbers();
    }
    else
    {
        AbandonColumn();
        return hr;
    }

    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteBytes(const BYTE pBytes[], DWORD dwLen)
{
    Buffer<BYTE> buffer;

    buffer.view_of((BYTE*)pBytes, dwLen, dwLen);

    if (auto hr = FormatColumn(buffer); FAILED(hr))
    {
        AbandonColumn();
        return hr;
    }
    AddColumnAndCheckNumbers();
    return S_OK;
}

STDMETHODIMP Orc::TableOutput::CSV::Writer::WriteBytes(const CBinaryBuffer& Buffer)
{
    return WriteBytes(Buffer.GetData(), (DWORD)Buffer.GetCount());
}

Orc::TableOutput::CSV::Writer::~Writer(void)
{
    Close();
}
