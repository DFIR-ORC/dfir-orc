//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "LogFileWriter.h"
#include "Robustness.h"
#include "WideAnsi.h"

#include "ParameterCheck.h"

#include "NtfsDataStructures.h"

#include "CriticalSection.h"

using namespace Orc;

namespace Orc {

class LogFileWriterTermination : public TerminationHandler
{
public:
    LogFileWriterTermination(const std::wstring& strDescr, const logger& pLog)
        : TerminationHandler(strDescr, ROBUSTNESS_LOG)
        , _weak_L_(pLog) {};

    HRESULT operator()();

private:
    CriticalSection m_cs;

    std::weak_ptr<LogFileWriter> _weak_L_;
};
}  // namespace Orc

HRESULT LogFileWriterTermination::operator()()
{
    ScopedLock s(m_cs);

    auto pLog = _weak_L_.lock();

    if (pLog != nullptr)
    {
        pLog->Close();
        pLog.reset();
    }
    return S_OK;
}

LogFileWriter::LogFileWriter(DWORD dwBufferSize, bool bANSIConsole)
{
    ZeroMemory(m_szLogFileName, MAX_PATH * sizeof(WCHAR));

    m_BufferSize = dwBufferSize;
    m_bANSIConsole = bANSIConsole;

    DWORD dwConsoleMode = 0;
    if (!GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &dwConsoleMode))
    {
        m_bRedirected = true;
        if (!m_bANSIConsole)
        {
            DWORD bytesWritten;
            BYTE bom[2] = {0xFF, 0xFE};
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bom, 2 * sizeof(BYTE), &bytesWritten, NULL);
        }
    }

    InitializeStorage();
}

void LogFileWriter::Initialize(const logger& pLog)
{
    pLog->SetConsoleLog(true);

    std::wstring strDescr = L"LogFile Termination";
    pLog->m_pTermination = std::make_shared<LogFileWriterTermination>(strDescr, pLog);
    Robustness::AddTerminationHandler(pLog->m_pTermination);

    CONSOLE_SCREEN_BUFFER_INFO ConsoleInfo;
    ZeroMemory(&ConsoleInfo, sizeof(CONSOLE_SCREEN_BUFFER_INFO));
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ConsoleInfo);

    pLog->m_wConsoleDefaultAttributes = ConsoleInfo.wAttributes;

    if (ConsoleInfo.wAttributes & BACKGROUND_BLUE)
    {
        pLog->m_wConsoleErrorAttributes |= BACKGROUND_BLUE;
        pLog->m_wConsoleWarningAttributes |= BACKGROUND_BLUE;
    }
    if (ConsoleInfo.wAttributes & BACKGROUND_RED)
    {
        pLog->m_wConsoleErrorAttributes |= BACKGROUND_RED;
        pLog->m_wConsoleWarningAttributes |= BACKGROUND_RED;
    }
    if (ConsoleInfo.wAttributes & BACKGROUND_GREEN)
    {
        pLog->m_wConsoleErrorAttributes |= BACKGROUND_GREEN;
        pLog->m_wConsoleWarningAttributes |= BACKGROUND_GREEN;
    }
    if (ConsoleInfo.wAttributes & BACKGROUND_INTENSITY)
    {
        pLog->m_wConsoleErrorAttributes |= BACKGROUND_INTENSITY;
        pLog->m_wConsoleWarningAttributes |= BACKGROUND_INTENSITY;
    }

    try
    {
        pLog->FlushBuffer(false);
    }
    catch (...)
    {
        wprintf(L"ERROR: logging failed to initialize, exiting\n");
        pLog->Close();
        exit(-1);
    }
    return;
}

HRESULT LogFileWriter::ConfigureLoggingOptions(int argc, const WCHAR* argv[], const logger& pLog)
{
    HRESULT hr = E_FAIL;
    for (int i = 0; i < argc; i++)
    {
        switch (argv[i][0])
        {
            case L'/':
            case L'-':
                if (!_wcsnicmp(argv[i] + 1, L"Verbose", wcslen(L"Verbose")))
                {
                    pLog->SetVerboseLog(true);
                }
                if (!_wcsnicmp(argv[i] + 1, L"Debug", wcslen(L"Debug")))
                {
                    pLog->SetDebugLog(true);
                }
                else if (!_wcsnicmp(argv[i] + 1, L"NoConsole", wcslen(L"NoConsole")))
                {
                    pLog->SetConsoleLog(false);
                }
                else if (!_wcsnicmp(argv[i] + 1, L"LogFile", wcslen(L"LogFile")))
                {
                    LPCWSTR pEquals = wcschr(argv[i], L'=');
                    WCHAR szLogFile[MAX_PATH] = {};
                    if (!pEquals)
                    {
                        log::Error(pLog, E_FAIL, L"Option /LogFile should be like: /LogFile=c:\\temp\\logfile.log\r\n");
                        return E_FAIL;
                    }
                    else
                    {
                        if (FAILED(hr = GetOutputFile(pEquals + 1, szLogFile, MAX_PATH)))
                        {
                            log::Error(pLog, hr, L"Invalid logging file specified: %s\r\n", pEquals + 1);
                            return hr;
                        }
                        pLog->CloseLogFile();
                        pLog->LogToFile(szLogFile);
                    }
                }
        }
    }
    return S_OK;
}

HRESULT LogFileWriter::InitializeStorage()
{

    size_t dwPagesToAlloc = ((m_BufferSize - 1) / PageSize()) + 1;

    if (dwPagesToAlloc < 2)
        dwPagesToAlloc++;

    size_t dwBytesToAlloc = dwPagesToAlloc * PageSize();
    m_pBuffer = (WCHAR*)VirtualAlloc(NULL, dwBytesToAlloc, MEM_COMMIT, PAGE_READWRITE);
    if (m_pBuffer == NULL)
        return HRESULT_FROM_WIN32(GetLastError());

    DWORD dwOldProtect = 0L;
    m_pGuard = ((BYTE*)m_pBuffer) + (dwPagesToAlloc - 1) * PageSize();
    VirtualProtect(m_pGuard, PageSize(), PAGE_READWRITE | PAGE_GUARD, &dwOldProtect);
    m_bGuardReached = false;

    m_pCurrent = m_pBuffer;
    m_Count = 0;
    m_BufferSize = dwBytesToAlloc;
    return S_OK;
}

HRESULT LogFileWriter::LogToFile(const WCHAR* szFileName)
{
    HRESULT hr = E_FAIL;

    if (szFileName == NULL)
        return E_POINTER;

    if (wcslen(m_szLogFileName) || m_hFile != INVALID_HANDLE_VALUE)
    {
        if (FAILED(hr = CloseLogFile()))
            return hr;
    }

    wcscpy_s(m_szLogFileName, szFileName);

    // ... create it
    m_hFile = CreateFile(
        szFileName,  // file	to create
        GENERIC_WRITE,  // open	for	writing
        FILE_SHARE_WRITE | FILE_SHARE_READ,  // share read&write but not deletion
        NULL,  // default security
        CREATE_ALWAYS,  // overwrite existing
        FILE_ATTRIBUTE_NORMAL,  // normal file
        NULL);  // no attr.	template

    if (INVALID_HANDLE_VALUE == m_hFile)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    {
        // Writing BOM
        DWORD bytesWritten;
        BYTE bom[2] = {0xFF, 0xFE};
        if (!WriteFile(m_hFile, bom, 2 * sizeof(BYTE), &bytesWritten, NULL))
            return HRESULT_FROM_WIN32(GetLastError());
    }

    if (m_pTermination)
    {
        m_pTermination->Description = L"Termination for file ";
        m_pTermination->Description += m_szLogFileName;
    }
    return S_OK;
}

HRESULT LogFileWriter::LogToHandle(HANDLE hFile)
{
    // Use it
    if (hFile == INVALID_HANDLE_VALUE)
        return E_INVALIDARG;

    m_hFile = hFile;

    if (m_pTermination)
    {
        m_pTermination->Description.assign(L"Termination for handle");
    }
    return S_OK;
}

bool LogFileWriter::IsLoggingToFile() const
{
    return wcslen(m_szLogFileName) && !(m_hFile == INVALID_HANDLE_VALUE);
}

bool Orc::LogFileWriter::IsLoggingToStream() const
{
    return m_pByteStream != nullptr;
}

HRESULT LogFileWriter::CloseLogFile()
{
    FlushBuffer(false);

    {
        Concurrency::critical_section::scoped_lock s(m_output_cs);

        HANDLE hFile = m_hFile;
        m_hFile = INVALID_HANDLE_VALUE;

        if (hFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hFile);
        }
        ZeroMemory(m_szLogFileName, MAX_PATH * sizeof(WCHAR));
    }
    return S_OK;
}

HRESULT LogFileWriter::LogToStream(const std::shared_ptr<ByteStream>& pStream)
{
    Concurrency::critical_section::scoped_lock s(m_output_cs);
    m_pByteStream = pStream;

    if (m_pTermination)
    {
        Robustness::RemoveTerminationHandler(m_pTermination);
    }
    m_pTermination = nullptr;

    if (m_pTermination)
    {
        m_pTermination->Description = L"Termination for agent";
    }
    return S_OK;
}

HRESULT LogFileWriter::CloseLogToStream(bool bCloseStream)
{
    if (m_pByteStream == nullptr)
        return S_OK;

    FlushBuffer(false);

    std::shared_ptr<ByteStream> pStream;
    {
        Concurrency::critical_section::scoped_lock s(m_output_cs);
        pStream = m_pByteStream;
        m_pByteStream = nullptr;
    }

    if (bCloseStream)
        pStream->Close();

    {
        Concurrency::critical_section::scoped_lock s(m_output_cs);
        if (m_pTermination)
        {
            Robustness::RemoveTerminationHandler(m_pTermination);
        }
        m_pTermination = nullptr;
    }
    return S_OK;
}

HRESULT LogFileWriter::FlushBuffer(bool bOnlyIfGuardReached)
{
    HRESULT hr = E_FAIL;

    if (bOnlyIfGuardReached && !m_bGuardReached)
        return S_OK;

    {
        Concurrency::critical_section::scoped_lock s(m_output_cs);

        if (m_hFile != INVALID_HANDLE_VALUE)
        {
            DWORD dwTotalBytesWritten = 0L;

            while (dwTotalBytesWritten < m_Count)
            {
                DWORD dwWritten = 0L;

                if (!WriteFile(
                        m_hFile,
                        (BYTE*)m_pBuffer + dwTotalBytesWritten,
                        (DWORD)m_Count - dwTotalBytesWritten,
                        &dwWritten,
                        NULL))
                {
                    return HRESULT_FROM_WIN32(GetLastError());
                }

                dwTotalBytesWritten += dwWritten;
            }
        }

        if (m_pByteStream != nullptr)
        {
            ULONGLONG ullTotalBytesWritten = 0L;

            while (ullTotalBytesWritten < m_Count)
            {
                ULONGLONG ullWritten = 0L;

                if (FAILED(
                        hr = m_pByteStream->Write(
                            (BYTE*)m_pBuffer + ullTotalBytesWritten, m_Count - ullTotalBytesWritten, &ullWritten)))
                {
                    return hr;
                }

                ullTotalBytesWritten += ullWritten;
            }
        }
    }

    {
        Concurrency::critical_section::scoped_lock s(m_buffer_cs);
        m_pCurrent = m_pBuffer;
        m_Count = 0L;
        DWORD dwOldProtect = 0L;
        VirtualProtect(m_pGuard, PageSize(), PAGE_READWRITE | PAGE_GUARD, &dwOldProtect);
        m_bGuardReached = false;
    }
    return S_OK;
}

HRESULT LogFileWriter::StringToConsole(const WCHAR* szMsg, DWORD dwSizeInChars, DWORD& dwWritten)
{
    HRESULT hr = E_FAIL;

    if (!m_bConsoleLog)
        return S_OK;

    if (m_bANSIConsole && (dwSizeInChars > m_ANSIBufferSize))
    {
        PCHAR pNewBuf = nullptr;
        if (m_pANSIBuffer)
            pNewBuf = (PCHAR)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, m_pANSIBuffer, dwSizeInChars);
        else
            pNewBuf = (PCHAR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwSizeInChars);

        if (pNewBuf == NULL)
            return E_OUTOFMEMORY;

        m_pANSIBuffer = pNewBuf;
        m_ANSIBufferSize = dwSizeInChars;
    }

    if (!m_bRedirected)
    {
        if (!m_bANSIConsole)
        {
            if (!WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), szMsg, dwSizeInChars, &dwWritten, NULL))
                return HRESULT_FROM_WIN32(GetLastError());
        }
        else
        {
            if (m_pANSIBuffer != nullptr)
            {
                if (FAILED(hr = WideToAnsi(nullptr, szMsg, dwSizeInChars, m_pANSIBuffer, (DWORD)m_ANSIBufferSize)))
                    return hr;

                if (!WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), m_pANSIBuffer, dwSizeInChars, &dwWritten, NULL))
                    return HRESULT_FROM_WIN32(GetLastError());
            }
        }
    }
    else
    {

        if (!m_bANSIConsole)
        {
            if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), szMsg, dwSizeInChars * sizeof(WCHAR), &dwWritten, NULL))
                return HRESULT_FROM_WIN32(GetLastError());
        }
        else
        {
            if (FAILED(hr = WideToAnsi(nullptr, szMsg, dwSizeInChars, m_pANSIBuffer, (DWORD)m_ANSIBufferSize)))
                return hr;
            if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), m_pANSIBuffer, dwSizeInChars, &dwWritten, NULL))
                return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    return S_OK;
}

HRESULT LogFileWriter::CopyBytesToBuffer(BYTE* pByte, DWORD dwLen)
{
    __try
    {
        CopyMemory(m_pCurrent, pByte, dwLen);

        m_pCurrent[dwLen / sizeof(WCHAR)] = L'\0';
        m_Count += dwLen * sizeof(WCHAR);
    }
    __except (EvalGuardException(GetExceptionCode(), GetExceptionInformation()))
    {
    }
    return E_NOTIMPL;
}

HRESULT LogFileWriter::MultiByteToBuffer(LPCSTR pByte, DWORD dwLen, DWORD& dwNbChars)
{
    __try
    {

        dwNbChars = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, (LPCSTR)pByte, dwLen, m_pCurrent, 0);

        if (dwNbChars == 0)
            return HRESULT_FROM_WIN32(GetLastError());

        m_pCurrent[dwNbChars] = L'\0';
        MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, (LPCSTR)pByte, dwLen, m_pCurrent, dwNbChars);
    }
    __except (EvalGuardException(GetExceptionCode(), GetExceptionInformation()))
    {
    }
    return S_OK;
}

HRESULT LogFileWriter::WriteBuffer(BYTE* pByte, DWORD dwLen, BufferEncoding encoding)
{
    HRESULT hr = E_FAIL;

    {
        Concurrency::critical_section::scoped_lock s(m_buffer_cs);

        switch (encoding)
        {
            case AutoDetect:
                if (IsTextUnicode(pByte, dwLen, NULL))
                {
                    hr = WriteBuffer(pByte, dwLen, LogFileWriter::TreatAsUnicode);
                }
                else
                {
                    hr = WriteBuffer(pByte, dwLen, LogFileWriter::TreatAsAnsi);
                }
                break;
            case TreatAsUnicode:
            {
                // Simple case, we simply write to the buffer
                CopyBytesToBuffer(pByte, dwLen);

                if (m_bDebugLog)
                    OutputDebugString(m_pCurrent);

                if (m_bConsoleLog && !m_bRedirected)
                {
                    DWORD dwNbWritten = 0;
                    WriteConsole(
                        GetStdHandle(STD_OUTPUT_HANDLE), m_pCurrent, dwLen / sizeof(WCHAR), &dwNbWritten, NULL);
                }
                if (m_bConsoleLog && m_bRedirected)
                {
                    DWORD dwNbWritten = 0;
                    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), m_pCurrent, dwLen / sizeof(WCHAR), &dwNbWritten, NULL);
                }

                m_pCurrent += dwLen / sizeof(WCHAR);
            }
            break;
            case TreatAsAnsi:
            {
                DWORD dwNbChars = 0L;
                if (FAILED(hr = MultiByteToBuffer((LPCSTR)pByte, dwLen, dwNbChars)))
                    return hr;

                if (m_bDebugLog)
                    OutputDebugString(m_pCurrent);

                if (m_bConsoleLog && !m_bRedirected)
                {
                    DWORD dwNbWritten = 0;
                    WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), m_pCurrent, dwNbChars, &dwNbWritten, NULL);
                }
                if (m_bConsoleLog && m_bRedirected)
                {
                    DWORD dwNbWritten = 0;
                    WriteFile(
                        GetStdHandle(STD_OUTPUT_HANDLE), m_pCurrent, dwNbChars * sizeof(WCHAR), &dwNbWritten, NULL);
                }
                m_pCurrent += dwNbChars;
            }
            break;
        }
    }

    FlushBuffer();
    return S_OK;
}

HRESULT LogFileWriter::WriteString(const WCHAR* szString)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = PrintToBuffer(L"%s", szString)))
        return hr;
    return S_OK;
}

HRESULT LogFileWriter::WriteString(const CHAR* szString)
{
    HRESULT hr = E_FAIL;

    CBinaryBuffer buffer;

    if (FAILED(hr = AnsiToWide(nullptr, szString, buffer)))
        return hr;

    return WriteString(buffer.GetP<WCHAR>());
}

HRESULT LogFileWriter::WriteString(const std::wstring& strString)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = PrintToBuffer(L"%.*s", strString.size(), strString.data())))
        return hr;
    return S_OK;
}

HRESULT LogFileWriter::WriteString(const std::string& strString)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = PrintToBuffer(L"%.*S", strString.size(), strString.data())))
        return hr;
    return S_OK;
}

HRESULT LogFileWriter::WriteString(const std::wstring_view& strString)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = PrintToBuffer(L"%.*s", strString.size(), strString.data())))
        return hr;
    return S_OK;
}

HRESULT LogFileWriter::WriteString(const std::string_view& strString)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = PrintToBuffer(L"%.*S", strString.size(), strString.data())))
        return hr;
    return S_OK;
}

HRESULT LogFileWriter::WriteFileTime(ULONGLONG fileTime)
{
    return WriteFileTime(*(PFILETIME)&fileTime);
}

HRESULT LogFileWriter::WriteFileTime(FILETIME fileTime)
{
    HRESULT hr = E_FAIL;
    // Convert the Create time to System time.
    SYSTEMTIME stUTC;
    FileTimeToSystemTime(&fileTime, &stUTC);
    return WriteSystemTime(stUTC);
}

HRESULT LogFileWriter::WriteSystemTime(SYSTEMTIME stUTC)
{
    HRESULT hr = E_FAIL;
    if (FAILED(
            hr = PrintToBuffer(
                L"%u-%02u-%02u %02u:%02u:%02u.%03u",
                stUTC.wYear,
                stUTC.wMonth,
                stUTC.wDay,
                stUTC.wHour,
                stUTC.wMinute,
                stUTC.wSecond,
                stUTC.wMilliseconds)))
        return hr;
    return S_OK;
}

HRESULT LogFileWriter::WriteFileSize(LARGE_INTEGER fileSize)
{
    HRESULT hr = E_FAIL;

    // File	size calculation only required for Very	big	files.
    if (FAILED(
            hr = PrintToBuffer(
                L"%I64d",
                fileSize.HighPart ? ((fileSize.HighPart * 0x100000000) + fileSize.LowPart) : fileSize.LowPart)))
        return hr;
    return S_OK;
}

HRESULT LogFileWriter::WriteFileSize(ULARGE_INTEGER fileSize)
{
    HRESULT hr = E_FAIL;

    // File	size calculation only required for Very	big	files.
    if (FAILED(
            hr = PrintToBuffer(
                L"%I64u",
                fileSize.HighPart ? ((fileSize.HighPart * 0x100000000) + fileSize.LowPart) : fileSize.LowPart)))
        return hr;
    return S_OK;
}

HRESULT LogFileWriter::WriteFileSize(DWORD nFileSizeHigh, DWORD nFileSizeLow)
{
    HRESULT hr = E_FAIL;

    // File	size calculation only required for Very	big	files.
    if (FAILED(
            hr =
                PrintToBuffer(L"%I64u", nFileSizeHigh ? ((nFileSizeHigh * 0x100000000) + nFileSizeLow) : nFileSizeLow)))
        return hr;
    return S_OK;
}

HRESULT LogFileWriter::WriteEndOfLine()
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = PrintToBuffer(L"\r\n")))
        return hr;
    return S_OK;
}

HRESULT LogFileWriter::WriteBool(bool bBoolean)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = PrintToBuffer(L"%s", bBoolean ? L"True" : L"False")))
        return hr;
    return S_OK;
}

HRESULT Orc::LogFileWriter::WriteInteger(BYTE Byte)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = PrintToBuffer(L"%hhd", Byte)))
        return hr;
    return S_OK;
}

HRESULT Orc::LogFileWriter::WriteInteger(SHORT Bytes)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = PrintToBuffer(L"%hd", Bytes)))
        return hr;
    return S_OK;
}

HRESULT Orc::LogFileWriter::WriteInteger(DWORD Bytes)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = PrintToBuffer(L"%ld", Bytes)))
        return hr;
    return S_OK;
}

HRESULT Orc::LogFileWriter::WriteInteger(ULONGLONG Bytes)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = PrintToBuffer(L"%llu", Bytes)))
        return hr;
    return S_OK;
}

HRESULT LogFileWriter::WriteAttributes(DWORD dwFileAttributes)
{
    HRESULT hr = E_FAIL;

    if (FAILED(
            hr = PrintToBuffer(
                L"%c%c%c%c%c%c%c%c%c%c%c%c%c",
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
                dwFileAttributes & FILE_ATTRIBUTE_VIRTUAL ? L'V' : L'.')))
    {
        return hr;
    }
    return S_OK;
}

HRESULT LogFileWriter::WriteEnum(DWORD dwEnum, const WCHAR* EnumValues[])
{
    for (int i = 0; EnumValues[i] != NULL; ++i)
    {
        if (i == dwEnum)
        {
            return PrintToBuffer(L"%s", EnumValues[dwEnum]);
        }
    }

    return E_INVALIDARG;
}

HRESULT LogFileWriter::FormatHRToBuffer(HRESULT hr, size_t& cchWritten)
{
    __try
    {
        cchWritten = FormatMessage(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            HRESULT_CODE(hr),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
            m_pCurrent,
            (DWORD)(m_BufferSize - m_Count / sizeof(WCHAR)),
            NULL);

        while (cchWritten > 0
               && (m_pCurrent[cchWritten - 1] == L'\r' || m_pCurrent[cchWritten - 1] == L'\n'
                   || m_pCurrent[cchWritten - 1] == L'.'))
        {
            m_pCurrent[cchWritten - 1] = 0;
            cchWritten--;
        }
    }
    __except (EvalGuardException(GetExceptionCode(), GetExceptionInformation()))
    {
    }
    return S_OK;
}

HRESULT LogFileWriter::WriteHRESULT(LPCWSTR szPrefix, HRESULT theHR, LPCWSTR szSuffix)
{
    HRESULT hr = E_FAIL;

    if (HRESULT_FACILITY(theHR) == FACILITY_WIN32)
    {
        WriteFormatedString(L"%s (", szPrefix);

        if (m_pCurrent)
        {
            bool bFormatSucceeded = false;
            {
                Concurrency::critical_section::scoped_lock s(m_buffer_cs);

                WCHAR* pNewCurrent = NULL;
                size_t cchWritten = 0;

                hr = FormatHRToBuffer(theHR, cchWritten);
                pNewCurrent = m_pCurrent + cchWritten;

                if (cchWritten > 0)
                {
                    size_t dwRemaining = (m_BufferSize - m_Count) - (cchWritten * sizeof(WCHAR));
                    pNewCurrent = m_pCurrent + cchWritten;

                    if (m_bDebugLog)
                        OutputDebugString(m_pCurrent);

                    if (m_bConsoleLog)
                    {
                        DWORD dwWritten = 0L;
                        if (FAILED(hr = StringToConsole(m_pCurrent, (DWORD)cchWritten, dwWritten)))
                        {
                            return hr;
                        }
                    }
                    if (m_pLogCallback != nullptr)
                    {
                        DWORD dwWritten = 0L;
                        if (FAILED(hr = m_pLogCallback(m_pCurrent, (DWORD)cchWritten, dwWritten)))
                        {
                            return hr;
                        }
                    }
                    m_pCurrent = pNewCurrent;
                    m_Count = (DWORD)(m_BufferSize - dwRemaining);
                    bFormatSucceeded = true;
                }
            }
            if (!bFormatSucceeded)
            {
                WriteFormatedString(L"hr=0x%lx)%s", theHR, szSuffix);
                return HRESULT_FROM_WIN32(GetLastError());
            }
            FlushBuffer();
        }
        else if (m_bConsoleLog || m_bDebugLog || m_pLogCallback != nullptr)
        {
            WCHAR szBuffer[1024];
            size_t dwRemaining = 0;

            DWORD cchWritten = FormatMessage(
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                HRESULT_CODE(theHR),
                MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                szBuffer,
                1024,
                NULL);
            while (cchWritten > 0
                   && (szBuffer[cchWritten - 1] == L'\r' || szBuffer[cchWritten - 1] == L'\n'
                       || szBuffer[cchWritten - 1] == L'.'))
            {
                szBuffer[cchWritten - 1] = 0;
                cchWritten--;
            }

            if (cchWritten > 0)
            {
                dwRemaining = (1024 - cchWritten) * sizeof(WCHAR);
                if (m_bDebugLog)
                    OutputDebugString(szBuffer);

                if (m_bConsoleLog)
                {
                    DWORD dwWritten = 0L;
                    if (FAILED(hr = StringToConsole(szBuffer, cchWritten, dwWritten)))
                    {
                        return hr;
                    }
                }
                if (m_pLogCallback != nullptr)
                {
                    DWORD dwWritten = 0L;
                    if (FAILED(hr = m_pLogCallback(szBuffer, cchWritten, dwWritten)))
                    {
                        return hr;
                    }
                }
            }
            else
            {
                WriteFormatedString(L"hr=0x%lx)%s", theHR, szSuffix);
                return HRESULT_FROM_WIN32(GetLastError());
            }
        }
        WriteFormatedString(L", hr=0x%lx)%s", theHR, szSuffix);
    }
    else
    {
        switch (theHR)
        {
            case E_UNEXPECTED:
                WriteFormatedString(L"%s (Catastrophic failure, hr=E_UNEXPECTED 0x%lx)%s", szPrefix, theHR, szSuffix);
                break;
            case E_NOTIMPL:
                WriteFormatedString(L"%s (Not implemented, hr=E_NOTIMPL 0x%lx)%s", szPrefix, theHR, szSuffix);
                break;
            case E_OUTOFMEMORY:
                WriteFormatedString(L"%s (Ran out of memory, hr=E_OUTOFMEMORY 0x%lx)%s", szPrefix, theHR, szSuffix);
                break;
            case E_INVALIDARG:
                WriteFormatedString(
                    L"%s (One or more arguments are invalid, hr=E_INVALIDARG 0x%lx)%s", szPrefix, theHR, szSuffix);
                break;
            case E_NOINTERFACE:
                WriteFormatedString(
                    L"%s (No such interface supported, hr=E_NOINTERFACE 0x%lx)%s", szPrefix, theHR, szSuffix);
                break;
            case E_POINTER:
                WriteFormatedString(L"%s (Invalid pointer, hr=E_POINTER 0x%lx)%s", szPrefix, theHR, szSuffix);
                break;
            case E_HANDLE:
                WriteFormatedString(L"%s (Invalid handle, hr=E_HANDLE 0x%lx)%s", szPrefix, theHR, szSuffix);
                break;
            case E_ABORT:
                WriteFormatedString(L"%s (Operation aborted, hr=E_ABORT 0x%lx)%s", szPrefix, theHR, szSuffix);
                break;
            case E_FAIL:
                WriteFormatedString(L"%s (Unspecified error, hr=E_FAIL 0x%lx)%s", szPrefix, theHR, szSuffix);
                break;
            case E_ACCESSDENIED:
                WriteFormatedString(
                    L"%s (General access denied error, hr=E_ACCESSDENIED 0x%lx)%s", szPrefix, theHR, szSuffix);
                break;
                // case : WriteFormatedString(L"%s (, hr=0x%lx)%s", szPrefix, theHR, szSuffix); break;
            default:
                WriteFormatedString(L"%s (hr=0x%lx)%s", szPrefix, theHR, szSuffix);
                break;
        }
    }
    return S_OK;
}

HRESULT LogFileWriter::WriteBytesInHex(const BYTE pBytes[], DWORD dwLen, bool bOxPrefix)
{
    HRESULT hr = E_FAIL;
    if (bOxPrefix)
        if (FAILED(hr = PrintToBuffer(L"0x")))
            return hr;

    for (DWORD i = 0; i < dwLen; i++)
    {
        if (FAILED(hr = PrintToBuffer(L"%2.2X", pBytes[i])))
            return hr;
    }
    return S_OK;
}

HRESULT Orc::LogFileWriter::WriteBytesInHex(BYTE Byte, bool b0xPrefix)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = PrintToBuffer(b0xPrefix ? L"0x%*.*X" : L"%*.*X", sizeof(Byte) * 2, sizeof(Byte) * 2, Byte)))
        return hr;
    return S_OK;
}

HRESULT Orc::LogFileWriter::WriteBytesInHex(SHORT Bytes, bool b0xPrefix)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = PrintToBuffer(b0xPrefix ? L"0x%*.*X" : L"%*.*X", sizeof(Bytes) * 2, sizeof(Bytes) * 2, Bytes)))
        return hr;
    return S_OK;
}

HRESULT LogFileWriter::WriteBytesInHex(DWORD Bytes, bool bOxPrefix)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = PrintToBuffer(bOxPrefix ? L"0x%*.*X" : L"%*.*X", sizeof(Bytes) * 2, sizeof(Bytes) * 2, Bytes)))
        return hr;
    return S_OK;
}

HRESULT LogFileWriter::WriteBytesInHex(ULONGLONG Bytes, bool bOxPrefix)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = PrintToBuffer(bOxPrefix ? L"0x%*.*X" : L"%*.*X", sizeof(Bytes) * 2, sizeof(Bytes) * 2, Bytes)))
        return hr;
    return S_OK;
}

HRESULT LogFileWriter::WriteSizeInUnits(ULONGLONG ullBytes)
{
    DWORD dwBytes = ullBytes % 1024;
    DWORD dwKBytes = (ullBytes / 1024) % 1024;
    DWORD dwMBytes = (ullBytes / (1024 * 1024)) % 1024;
    DWORD dwGBytes = (ullBytes / (1024 * 1024 * 1024)) % 1024;
    ULONGLONG ullTBytes = ullBytes / ((ULONGLONG)1024 * 1024 * 1024 * 1024);

    if (ullTBytes)
        PrintToBuffer(L"%u TB, ", ullTBytes);
    if (dwGBytes)
        PrintToBuffer(L"%u GB, ", dwGBytes);
    if (dwMBytes)
        PrintToBuffer(L"%u MB, ", dwMBytes);
    if (dwKBytes)
        PrintToBuffer(L"%u KB, ", dwKBytes);
    if (dwBytes)
        PrintToBuffer(L"%u B ", dwBytes);
    return PrintToBuffer(L"(%I64d bytes)", ullBytes);
}

HRESULT LogFileWriter::WriteFRN(const MFT_SEGMENT_REFERENCE& frn)
{
    return PrintToBuffer(L"0x%.16I64X", NtfsFullSegmentNumber(&frn));
}

HRESULT LogFileWriter::WriteFlags(DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator)
{
    HRESULT hr = E_FAIL;
    bool bFirst = true;
    int idx = 0;

    while (FlagValues[idx].dwFlag != 0xFFFFFFFF)
    {
        if (dwFlags & FlagValues[idx].dwFlag)
        {
            if (bFirst)
            {
                bFirst = false;
                if (FAILED(hr = PrintToBuffer(L"%s", FlagValues[idx].szShortDescr)))
                {
                    return hr;
                }
            }
            else
            {
                if (FAILED(hr = PrintToBuffer(L"%c%s", cSeparator, FlagValues[idx].szShortDescr)))
                {
                    return hr;
                }
            }
        }
        idx++;
    }

    return S_OK;
}

HRESULT LogFileWriter::WriteExactFlags(DWORD dwFlags, const FlagsDefinition FlagValues[])
{
    HRESULT hr = E_FAIL;
    int idx = 0;
    bool found = false;

    while (FlagValues[idx].dwFlag != 0xFFFFFFFF)
    {
        if (dwFlags == FlagValues[idx].dwFlag)
        {
            if (FAILED(hr = PrintToBuffer(L"%s", FlagValues[idx].szShortDescr)))
            {
                return hr;
            }
            found = true;
            break;
        }
        idx++;
    }

    return S_OK;
}

int LogFileWriter::EvalGuardException(int n_except, LPEXCEPTION_POINTERS pExcepts)
{
    DBG_UNREFERENCED_PARAMETER(pExcepts);
    if (n_except != EXCEPTION_GUARD_PAGE)  // Pass on most
        return EXCEPTION_CONTINUE_SEARCH;  //  exceptions

        // Execute some code to clean up problem
#ifdef DEBUG
    OutputDebugString(L"LogFileWriter buffer filled, Flush!!!\n");
#endif
    m_bGuardReached = true;
    return EXCEPTION_CONTINUE_EXECUTION;
}

HRESULT LogFileWriter::Close()
{
    if (m_pCurrent)
        FlushBuffer(false);

    if (IsLoggingToFile())
        CloseLogFile();
    if (m_pByteStream != nullptr)
        CloseLogToStream();

    if (m_pBuffer)
    {
        VirtualFree(m_pBuffer, 0L, MEM_RELEASE);
        m_pBuffer = NULL;
    }
    m_pCurrent = NULL;

    if (m_pANSIBuffer != nullptr)
    {
        HeapFree(GetProcessHeap(), 0L, m_pANSIBuffer);
        m_pANSIBuffer = nullptr;
        m_ANSIBufferSize = 0L;
    }
    if (m_pTermination)
    {
        Robustness::RemoveTerminationHandler(m_pTermination);
        m_pTermination = nullptr;
    }
    return S_OK;
}

LogFileWriter::~LogFileWriter(void)
{

    Close();
}
