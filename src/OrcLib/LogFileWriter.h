//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OutputWriter.h"

#include <concrt.h>
#include <strsafe.h>
#include <functional>
#include <optional>

#ifdef DEBUG

#    ifndef __WFILE__
#        define WIDEN2(x) L##x
#        define WIDEN(x) WIDEN2(x)
#        define __WFILE__ WIDEN(__FILE__)
#    endif  // !

#endif

#pragma managed(push, off)

struct MFT_SEGMENT_REFERENCE;

namespace Orc {

class LogFileWriterTermination;

class ORCLIB_API LogFileWriter
    : public OutputWriter
    , std::enable_shared_from_this<LogFileWriter>
{
public:

    typedef enum _BufferEncoding
    {
        TreatAsUnicode,
        TreatAsAnsi,
        AutoDetect
    } BufferEncoding;

    enum class IntegerFormat
    {
        Decimal,
        HexaDecimal,
        PrefixedHexaDecimal
    };

    enum class BinaryFormat
    {
        BytesArray,
        ShortArray,
        Int32Array,
        Int64Array,
        Hex,
        PrefixedHex,
        Base64
    };

    typedef std::function<HRESULT(const WCHAR* szMsg, DWORD dwSize, DWORD& dwWritten)> LoggingCallback;

public:
    constexpr static auto WRITE_BUFFER = 0x100000LU;

    LogFileWriter(DWORD dwBufferSize = WRITE_BUFFER, bool bANSIConsole = true);
    LogFileWriter(const LogFileWriter&) = delete;

    static void Initialize(const logger& pLog);

    void SetDebugLog(bool bDebugLog) { m_bDebugLog = bDebugLog; };
    bool DebugLog() { return m_bDebugLog; };

    void SetConsoleLog(bool bConsoleLog) { m_bConsoleLog = bConsoleLog; };
    bool ConsoleLog() { return m_bConsoleLog; };

    void SetVerboseLog(bool bVerboseLog) { m_bVerboseLog = bVerboseLog; };
    bool VerboseLog() { return m_bVerboseLog; };

    void SetLogCallback(LoggingCallback pCallback) { m_pLogCallback = pCallback; };
    bool LogCallbackEnabled() { return m_pLogCallback != nullptr; };

    void IncrementErrorCount() { InterlockedIncrement(&m_ErrorCount); };
    DWORD GetErrorCount() { return m_ErrorCount; }

    static HRESULT ConfigureLoggingOptions(int argc, const WCHAR* argv[], const logger& pLog);

    const WCHAR* FileName() const { return m_szLogFileName; };

    HRESULT LogToHandle(HANDLE hFile);
    HRESULT LogToFile(const WCHAR* szFileName);
    HANDLE GetHandle() const { return m_hFile; };
    HRESULT CloseLogFile();

    HRESULT LogToStream(const std::shared_ptr<ByteStream>& pStream);
    const std::shared_ptr<ByteStream>& GetByteStream() const { return m_pByteStream; };
    HRESULT CloseLogToStream(bool bCloseStream = true);

    bool IsLoggingToFile() const;
    bool IsLoggingToStream() const;

    HRESULT FlushBuffer(bool bOnlyIfGuardReached = true);
    HRESULT Close();

    HRESULT DefaultColor() { return SetConsoleAttr(m_wConsoleDefaultAttributes); }
    HRESULT WarnColor() { return SetConsoleAttr(m_wConsoleWarningAttributes); }
    HRESULT ErrorColor() { return SetConsoleAttr(m_wConsoleErrorAttributes); }

    HRESULT WriteBuffer(BYTE* pByte, DWORD dwLen, BufferEncoding encoding = LogFileWriter::AutoDetect);

    HRESULT WriteString(const WCHAR* szString);
    HRESULT WriteString(const CHAR* szString);
    HRESULT WriteString(const std::wstring& strString);
    HRESULT WriteString(const std::string& strString);
    HRESULT WriteString(const std::wstring_view& strString);
    HRESULT WriteString(const std::string_view& strString);

    template <typename... Args>
    HRESULT WriteFormatedString(const WCHAR* szFormat, Args&&... args)
    {
        return PrintToBuffer(szFormat, std::forward<Args>(args)...);
    }

    template <typename... Args>
    HRESULT ConsoleFormatedString(const WCHAR* szFormat, Args&&... args)
    {
        return PrintToConsole(szFormat, std::forward<Args>(args)...);
    }

    HRESULT WriteInteger(BYTE Byte);
    HRESULT WriteInteger(SHORT Bytes);
    HRESULT WriteInteger(DWORD Bytes);
    HRESULT WriteInteger(ULONGLONG Bytes);

    HRESULT WriteAttributes(DWORD dwAttibutes);
    HRESULT WriteFileTime(ULONGLONG fileTime);
    HRESULT WriteFileTime(FILETIME fileTime);
    HRESULT WriteSystemTime(SYSTEMTIME fileTime);
    HRESULT WriteFileSize(LARGE_INTEGER fileSize);
    HRESULT WriteFileSize(ULARGE_INTEGER fileSize);
    HRESULT WriteFileSize(DWORD nFileSizeHigh, DWORD nFileSizeLow);
    HRESULT WriteFRN(const MFT_SEGMENT_REFERENCE& frn);
    HRESULT WriteFlags(DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator);
    HRESULT WriteExactFlags(DWORD dwFlags, const FlagsDefinition FlagValues[]);

    HRESULT WriteBytesInHex(const BYTE pBytes[], DWORD dwLen, bool b0xPrefix);

    HRESULT WriteBytesInHex(BYTE Byte, bool b0xPrefix);
    HRESULT WriteBytesInHex(SHORT Bytes, bool b0xPrefix);
    HRESULT WriteBytesInHex(DWORD Bytes, bool b0xPrefix);
    HRESULT WriteBytesInHex(ULONGLONG Bytes, bool b0xPrefix);

    HRESULT WriteSizeInUnits(ULONGLONG Bytes);

    HRESULT WriteBool(bool bBoolean);
    HRESULT WriteEnum(DWORD dwEnum, const WCHAR* EnumValues[]);

    HRESULT WriteHRESULT(LPCWSTR szPrefix, HRESULT hr, LPCWSTR szSuffix);

    HRESULT WriteEndOfLine();

    ~LogFileWriter(void) { Close(); }

    friend Orc::logger& operator<<(Orc::logger& L, LPCWSTR szString);
    friend Orc::logger& operator<<(Orc::logger& L, LPCSTR szString);
    friend Orc::logger& operator<<(Orc::logger& L, const std::string& strString);
    friend Orc::logger& operator<<(Orc::logger& L, const std::wstring& strString);
    friend Orc::logger& operator<<(Orc::logger& L, bool bBoolean);
    friend Orc::logger& operator<<(Orc::logger& L, SYSTEMTIME sysTime);
    friend Orc::logger& operator<<(Orc::logger& L, FILETIME fileTime);
    friend Orc::logger& operator<<(Orc::logger& L, const CBinaryBuffer& buffer);
    friend Orc::logger& operator<<(Orc::logger& L, const ULONG uLong);
    friend Orc::logger& operator<<(Orc::logger& L, const LONG uLong);
    friend Orc::logger& operator<<(Orc::logger& L, const ULONG64 uLongLong);
    friend Orc::logger& operator<<(Orc::logger& L, const SHORT Bytes);
    friend Orc::logger& operator<<(Orc::logger& L, const BYTE Byte);
    friend Orc::logger& operator<<(Orc::logger& L, LogFileWriter& (*pf)(LogFileWriter&));

    template <typename T>
    friend Orc::logger& operator<<(Orc::logger& L, const std::vector<T>& vect)
    {
        L->WriteString(L"[");
        bool bFirst = true;
        for (const auto& item : vect)
        {
            if (!bFirst)
            {
                L->WriteString(L", ");
            }
            bFirst = false;
            L << item;
        }
        L->WriteString(L"]");
        return L;
    }

    static LogFileWriter& filetime(LogFileWriter& L, ULONGLONG fileTime)
    {
        L.WriteFileTime(fileTime);
        return L;
    }

    static LogFileWriter& endl(LogFileWriter& L)
    {
        L.WriteEndOfLine();
        return L;
    }

    static LogFileWriter& tab(LogFileWriter& L)
    {
        L.WriteString(L"\t");
        return L;
    }

    static LogFileWriter& hex(LogFileWriter& L)
    {
        L.m_IntFormat = IntegerFormat::HexaDecimal;
        return L;
    }
    static LogFileWriter& hex_0x(LogFileWriter& L)
    {
        L.m_IntFormat = IntegerFormat::PrefixedHexaDecimal;
        return L;
    }

    static LogFileWriter& dec(LogFileWriter& L)
    {
        L.m_IntFormat = IntegerFormat::Decimal;
        return L;
    }

    IntegerFormat IntFormat() const { return m_IntFormat; }
    void IntFormat(const IntegerFormat format) { m_IntFormat = format; }

    BinaryFormat BinFormat() const { return m_BinFormat; };
    void BinFormat(const BinaryFormat format) { m_BinFormat = format; }

    static LogFileWriter& default_color(LogFileWriter& L)
    {
        L.DefaultColor();
        return L;
    }
    static LogFileWriter& warn(LogFileWriter& L)
    {
        L.WarnColor();
        return L;
    }
    static LogFileWriter& error(LogFileWriter& L)
    {
        L.ErrorColor();
        return L;
    }

    struct format
    {
        std::optional<LogFileWriter::IntegerFormat> _int_format;
        std::optional<LogFileWriter::BinaryFormat> _bin_format;

        format(LogFileWriter::IntegerFormat format)
            : _int_format(format) {};
        format(LogFileWriter::BinaryFormat format)
            : _bin_format(format) {};
        format(LogFileWriter::IntegerFormat iformat, LogFileWriter::BinaryFormat bformat)
            : _bin_format(bformat)
            , _int_format(iformat) {};
    };

    LogFileWriter& operator<<(const format& _Manip)
    {
        if (_Manip._bin_format.has_value())
            BinFormat(_Manip._bin_format.value());

        if (_Manip._int_format.has_value())
            IntFormat(_Manip._int_format.value());

        return *this;
    }

    friend Orc::logger& operator<<(Orc::logger& L, const format& _Manip)
    {
        if (_Manip._bin_format.has_value())
            L->BinFormat(_Manip._bin_format.value());

        if (_Manip._int_format.has_value())
            L->IntFormat(_Manip._int_format.value());
        return L;
    }

    //
    //// Info messages
    //
    template <typename... Args>
    void Info(LPCWSTR szMessage)
    {
        WriteString(szMessage);
    }
    template <typename... Args>
    void Info(LPCWSTR szFormatString, Args&&... args)
    {
        WriteFormatedString(szFormatString, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Info(const std::shared_ptr<LogFileWriter>& LogFile, LPCWSTR szFormatString, Args&&... args)
    {
        if (LogFile)
            LogFile->Info(szFormatString, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static void Info(const std::unique_ptr<LogFileWriter>& LogFile, LPCWSTR szFormatString, Args&&... args)
    {
        if (LogFile)
            LogFile->Info(szFormatString, std::forward<Args>(args)...);
    }

    //
    //// Error messages
    //
    template <typename... Args>
    void Error(HRESULT hr, LPCWSTR szMessage)
    {
        ErrorColor();
        WriteHRESULT(L"ERROR", hr, L": ");
        WriteString(szMessage);
        IncrementErrorCount();
        DefaultColor();
    }

    template <typename... Args>
    void Error(HRESULT hr, LPCWSTR szFormatString, Args&&... args)
    {
        ErrorColor();
        WriteHRESULT(L"ERROR", hr, L": ");
        WriteFormatedString(szFormatString, std::forward<Args>(args)...);
        IncrementErrorCount();
        DefaultColor();
    }

    template <typename... Args>
    static void Error(const std::shared_ptr<LogFileWriter>& LogFile, HRESULT hr, LPCWSTR szFormatString, Args&&... args)
    {
        if (LogFile)
            LogFile->Error(hr, szFormatString, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static void Error(const std::unique_ptr<LogFileWriter>& LogFile, HRESULT hr, LPCWSTR szFormatString, Args&&... args)
    {
        if (LogFile)
            LogFile->Error(hr, szFormatString, std::forward<Args>(args)...);
    }

    //
    //// Warning messages
    //
    template <typename... Args>
    void Warning(LPCWSTR szFormatString, Args&&... args)
    {
        WarnColor();
        WriteString("WARNING");
        WriteFormatedString(szFormatString, std::forward<Args>(args)...);
        DefaultColor();
    }

    template <typename... Args>
    void Warning(HRESULT hr, LPCWSTR szFormatString, Args&&... args)
    {
        WarnColor();
        WriteHRESULT(L"WARNING", hr, L": ");
        WriteFormatedString(szFormatString, std::forward<Args>(args)...);
        DefaultColor();
    }

    template <typename... Args>
    static void
    Warning(const std::shared_ptr<LogFileWriter>& LogFile, HRESULT hr, LPCWSTR szFormatString, Args&&... args)
    {
        if (LogFile)
            LogFile->Warning(hr, szFormatString, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void
    Warning(const std::unique_ptr<LogFileWriter>& LogFile, HRESULT hr, LPCWSTR szFormatString, Args&&... args)
    {
        if (LogFile)
            LogFile->Warning(hr, szFormatString, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Warning(const std::shared_ptr<LogFileWriter>& LogFile, LPCWSTR szFormatString, Args&&... args)
    {
        if (LogFile)
            LogFile->Warning(szFormatString, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Warning(const std::unique_ptr<LogFileWriter>& LogFile, LPCWSTR szFormatString, Args&&... args)
    {
        if (LogFile)
            LogFile->Warning(szFormatString, std::forward<Args>(args)...);
    }

    //
    //// Progress messages
    //
    template <typename... Args>
    void Progress(LPCWSTR szFormatString, Args&&... args)
    {
        ConsoleFormatedString(szFormatString, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static void Progress(const std::shared_ptr<LogFileWriter>& LogFile, LPCWSTR szFormatString, Args&&... args)
    {
        if (LogFile)
            LogFile->Progress(szFormatString, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static void Progress(const std::unique_ptr<LogFileWriter>& LogFile, LPCWSTR szFormatString, Args&&... args)
    {
        if (LogFile)
            LogFile->Progress(szFormatString, std::forward<Args>(args)...);
    }

    //
    //// Verbose messages
    //
    template <typename... Args>
    void Verbose(LPCWSTR szFormatString, Args&&... args)
    {
        if (VerboseLog())
        {
            WriteFormatedString(szFormatString, std::forward<Args>(args)...);
        }
    }
    template <typename... Args>
    void Verbose(LPCWSTR szMessage)
    {
        if (VerboseLog())
        {
            WriteString(szMessage);
        }
    }
    template <typename... Args>
    static void Verbose(const std::shared_ptr<LogFileWriter>& LogFile, LPCWSTR szFormatString, Args&&... args)
    {
        if (LogFile)
            LogFile->Verbose(szFormatString, std::forward<Args>(args)...);
    }
    template <typename... Args>
    static void Verbose(const std::unique_ptr<LogFileWriter>& LogFile, LPCWSTR szFormatString, Args&&... args)
    {
        if (LogFile)
            LogFile->Verbose(szFormatString, std::forward<Args>(args)...);
    }

    //
    //// Debug messages
    //

    template <typename... Args>
    void Debug(LPCWSTR szFormatString, Args&&... args)
    {
    }
    template <typename... Args>
    static void Debug(const std::shared_ptr<LogFileWriter>& LogFile, LPCWSTR szFormatString, Args&&... args)
    {
    }
    template <typename... Args>
    static void Debug(const std::unique_ptr<LogFileWriter>& LogFile, LPCWSTR szFormatString, Args&&... args)
    {
    }

private:
    IntegerFormat m_IntFormat = IntegerFormat::Decimal;
    BinaryFormat m_BinFormat = BinaryFormat::Hex;

    WCHAR m_szLogFileName[MAX_PATH] = {0};

    size_t m_BufferSize = 0L;  // in BYTEs
    WCHAR* m_pBuffer = nullptr;

    bool m_bANSIConsole = false;
    size_t m_ANSIBufferSize = 0L;
    CHAR* m_pANSIBuffer = nullptr;

    LPVOID m_pGuard = nullptr;
    bool m_bGuardReached = false;

    Concurrency::critical_section m_buffer_cs;
    Concurrency::critical_section m_output_cs;

    bool m_bConsoleLog = false;
    bool m_bDebugLog = false;
    HANDLE m_hFile = INVALID_HANDLE_VALUE;

    std::shared_ptr<LogFileWriterTermination> m_pTermination;

    std::shared_ptr<ByteStream> m_pByteStream;

    bool m_bVerboseLog = false;
    bool m_bRedirected = false;

    LoggingCallback m_pLogCallback = nullptr;

    WCHAR* m_pCurrent = nullptr;
    size_t m_Count = 0;  // in BYTEs

    DWORD m_ErrorCount = 0L;

    WORD m_wConsoleDefaultAttributes = FOREGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN;
    WORD m_wConsoleErrorAttributes = FOREGROUND_RED | FOREGROUND_INTENSITY;
    WORD m_wConsoleWarningAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;

    // Taille d'une page (en octets)
    size_t m_PageSize = 0L;
    size_t PageSize()
    {
        if (!m_PageSize)
        {
            SYSTEM_INFO sysinfo;
            GetSystemInfo(&sysinfo);
            m_PageSize = sysinfo.dwPageSize;
        }
        return m_PageSize;
    }

    HRESULT InitializeStorage();

    int EvalGuardException(int n_except, LPEXCEPTION_POINTERS pExcepts);

    HRESULT StringToConsole(const WCHAR* szMsg, DWORD dwSize, DWORD& dwWritten);
    template <typename... Args>
    HRESULT PrintToConsole(const WCHAR* szFormat, Args&&... args)
    {
        if (!m_bConsoleLog)
            return S_OK;

        WCHAR szBuffer[1024];
        HRESULT hr = E_FAIL;
        size_t dwRemaining = 0L;

        if (SUCCEEDED(
                hr = StringCbPrintfExW(
                    szBuffer,
                    1024 * sizeof(WCHAR),
                    NULL,
                    &dwRemaining,
                    STRSAFE_IGNORE_NULLS,
                    szFormat,
                    std::forward<Args>(args)...)))
        {
            DWORD dwWritten = 0L;
            if (FAILED(
                    hr = StringToConsole(
                        szBuffer, (DWORD)(1024 * sizeof(WCHAR) - dwRemaining) / sizeof(WCHAR), dwWritten)))
            {
                return hr;
            }
        }
        return S_OK;
    }

    template <typename... Args>
    HRESULT
    StringPrintfToBuffer(WCHAR** pNewCurrent, size_t& dwRemaining, DWORD flags, const WCHAR* szFormat, Args&&... args)
    {
        HRESULT hr = E_FAIL;

        __try
        {
            return StringCbPrintfExW(
                m_pCurrent,
                m_BufferSize - m_Count,
                pNewCurrent,
                &dwRemaining,
                flags,
                szFormat,
                std::forward<Args>(args)...);
        }
        __except (EvalGuardException(GetExceptionCode(), GetExceptionInformation()))
        {
        }
        return S_OK;
    }

    HRESULT CopyBytesToBuffer(BYTE* pByte, DWORD dwLen);
    HRESULT MultiByteToBuffer(LPCSTR pByte, DWORD dwLen, DWORD& dwNbChars);
    HRESULT FormatHRToBuffer(HRESULT hr, size_t& cchWritten);

    template <typename... Args>
    HRESULT PrintToBuffer(const WCHAR* szFormat, Args&&... args)
    {
        HRESULT hr = E_FAIL;

        if (m_pCurrent)
        {
            {
                Concurrency::critical_section::scoped_lock s(m_buffer_cs);
                WCHAR* pNewCurrent = NULL;
                size_t dwRemaining = 0L;

                if (SUCCEEDED(
                        hr = StringPrintfToBuffer(
                            &pNewCurrent, dwRemaining, STRSAFE_IGNORE_NULLS, szFormat, std::forward<Args>(args)...)))
                {
                    if (m_bDebugLog)
                        OutputDebugString(m_pCurrent);

                    if (m_bConsoleLog)
                    {
                        DWORD dwWritten = 0L;
                        if (FAILED(hr = StringToConsole(m_pCurrent, (DWORD)(pNewCurrent - m_pCurrent), dwWritten)))
                        {
                            return hr;
                        }
                    }

                    if (m_pLogCallback != nullptr)
                    {
                        DWORD dwWritten = 0L;
                        if (FAILED(hr = m_pLogCallback(m_pCurrent, (DWORD)(pNewCurrent - m_pCurrent), dwWritten)))
                        {
                            return hr;
                        }
                    }

                    m_pCurrent = pNewCurrent;
                    m_Count = (DWORD)(m_BufferSize - dwRemaining);
                }
                else
                {
                    return hr;
                }
            }
            FlushBuffer();
        }
        else if (m_bConsoleLog || m_bDebugLog || m_pLogCallback != nullptr)
        {
            WCHAR szBuffer[1024];
            size_t dwRemaining = 0;
            if (SUCCEEDED(
                    hr = StringCbPrintfExW(
                        szBuffer,
                        1024 * sizeof(WCHAR),
                        NULL,
                        &dwRemaining,
                        STRSAFE_IGNORE_NULLS,
                        szFormat,
                        std::forward<Args>(args)...)))
            {
                if (m_bDebugLog)
                    OutputDebugString(szBuffer);

                if (m_bConsoleLog)
                {
                    DWORD dwWritten = 0L;
                    if (FAILED(
                            hr = StringToConsole(
                                szBuffer, (DWORD)(1024 * sizeof(WCHAR) - dwRemaining) / sizeof(WCHAR), dwWritten)))
                    {
                        return hr;
                    }
                }
                if (m_pLogCallback)
                {
                    DWORD dwWritten = 0L;
                    if (FAILED(
                            hr = m_pLogCallback(
                                szBuffer, (DWORD)(1024 * sizeof(WCHAR) - dwRemaining) / sizeof(WCHAR), dwWritten)))
                    {
                        return hr;
                    }
                }
            }
        }
        return S_OK;
    }

    HRESULT SetConsoleAttr(WORD wAttributes)
    {
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), wAttributes);
        return S_OK;
    };
};

using log = LogFileWriter;
using logger = std::shared_ptr<LogFileWriter>;
}  // namespace Orc

#pragma managed(pop)
