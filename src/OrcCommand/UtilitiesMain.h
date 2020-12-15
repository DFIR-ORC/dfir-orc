//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <conio.h>
#include <iostream>
#include <chrono>
#include <filesystem>

#include <concrt.h>

#include <boost/logic/tribool.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#ifdef ORC_BUILD_BOOST_STACKTRACE
#    include <boost/stacktrace.hpp>
#endif

#include "ConfigFile.h"
#include "Archive.h"
#include "ParameterCheck.h"
#include "Robustness.h"
#include "ConfigFile_Common.h"
#include "ArchiveMessage.h"
#include "ArchiveNotification.h"
#include "ArchiveAgent.h"
#include "CryptoHashStream.h"
#include "FuzzyHashStream.h"
#include "LocationSet.h"
#include "ToolVersion.h"
#include "SystemDetails.h"
#include "TableOutputWriter.h"
#include "ExtensionLibrary.h"
#include "Log/Log.h"
#include "Output/Console/Console.h"
#include "Output/Text/Print.h"
#include "Output/Text/Fmt/FILE_NAME.h"
#include "Output/Text/Fmt/FILETIME.h"
#include "Output/Text/Fmt/SYSTEMTIME.h"
#include "Output/Text/Fmt/TimeUtc.h"
#include "Utils/Guard.h"
#include "UtilitiesLogger.h"

#pragma managed(push, off)

namespace Orc {

namespace Command {

class ORCLIB_API UtilitiesMain
{
public:
    class Configuration
    {
    public:
        std::wstring strConfigFile;
    };

    // Common configuration
    class UtilitiesConfiguration
    {
    public:
        enum class LogLevel
        {
            kDefault = 0,
            kCritical,
            kError,
            kWarn,
            kInfo,
            kDebug,
            kTrace
        };

        std::wstring strComputerName;
        std::filesystem::path logFile;
        LogLevel logLevel;
        bool logToConsole;
    };

    template <class T>
    class MultipleOutput
    {
    public:
        using OutputPair = std::pair<T, std::shared_ptr<::Orc::TableOutput::IWriter>>;

    private:
        std::vector<OutputPair> m_outputs;

        ArchiveMessage::UnboundedMessageBuffer m_messageBuf;
        std::unique_ptr<Concurrency::call<ArchiveNotification::Notification>> m_notificationBuf;
        std::shared_ptr<ArchiveAgent> m_pArchiveAgent;

    public:
        std::vector<OutputPair>& Outputs() { return m_outputs; };

        HRESULT WriteVolStats(const OutputSpec& volStatsSpec, const std::vector<std::shared_ptr<Location>>& locations)
        {

            std::shared_ptr<TableOutput::IWriter> volStatWriter;

            if (volStatsSpec.Type == OutputSpec::Kind::Archive || volStatsSpec.Type == OutputSpec::Kind::Directory)
                volStatWriter = Orc::TableOutput::GetWriter(L"volstats.csv", volStatsSpec);
            else
                return S_OK;

            if (volStatWriter != nullptr)
            {
                auto& volStatOutput = *volStatWriter;
                for (auto& loc : locations)
                {
                    auto reader = loc->GetReader();

                    if (reader != nullptr)
                    {
                        SystemDetails::WriteComputerName(volStatOutput);
                        volStatOutput.WriteInteger(reader->VolumeSerialNumber());
                        volStatOutput.WriteString(reader->GetLocation());
                        volStatOutput.WriteString(FSVBR::GetFSName(reader->GetFSType()).c_str());
                        volStatOutput.WriteBool(loc->GetParse());
                        volStatOutput.WriteEndOfLine();
                    }
                    else
                    {
                        return E_FAIL;
                    }
                }

                volStatWriter->Close();
                auto pStreamWriter = std::dynamic_pointer_cast<TableOutput::IStreamWriter>(volStatWriter);
                if (volStatsSpec.Type == OutputSpec::Kind::Archive && pStreamWriter && pStreamWriter->GetStream())
                    AddStream(volStatsSpec, L"volstats.csv", pStreamWriter->GetStream(), false, true);
            }
            else
            {
                return E_FAIL;
            }

            return S_OK;
        }

        HRESULT Prepare(const OutputSpec& output)
        {

            if (output.Type != OutputSpec::Kind::Archive)
                return S_OK;

            m_notificationBuf = std::make_unique<Concurrency::call<ArchiveNotification::Notification>>(
                [this](const ArchiveNotification::Notification& item) {
                    if (SUCCEEDED(item->GetHResult()))
                    {
                        switch (item->GetType())
                        {
                            case ArchiveNotification::ArchiveStarted:
                                Log::Info(L"Archive: '{}' started", item->Keyword());
                                break;
                            case ArchiveNotification::FileAddition:
                                Log::Info(L"Archive: File '{}' added ({})", item->Keyword(), item->FileSize());
                                break;
                            case ArchiveNotification::DirectoryAddition:
                                Log::Info(L"Archive: Directory '{}' added", item->Keyword());
                                break;
                            case ArchiveNotification::StreamAddition:
                                Log::Info(L"Archive: Output '{}' added", item->Keyword());
                                break;
                            case ArchiveNotification::ArchiveComplete:
                                Log::Info(L"Archive: '{}' is complete ({})", item->Keyword(), item->FileSize());
                                break;
                        }
                    }
                    else
                    {
                        Log::Error(
                            L"ArchiveOperation: Operation for '{}' failed '{}' [{}]",
                            item->Keyword(),
                            item->Description(),
                            SystemError(item->GetHResult()));
                    }

                    return;
                });

            m_pArchiveAgent = std::make_unique<ArchiveAgent>(m_messageBuf, m_messageBuf, *m_notificationBuf);

            if (!m_pArchiveAgent->start())
            {
                Log::Error("Archive agent as failed to start");
                return E_FAIL;
            }

            Concurrency::send(m_messageBuf, ArchiveMessage::MakeOpenRequest(output));
            return S_OK;
        }

        HRESULT GetWriters(const OutputSpec& output, LPCWSTR szPrefix)
        {
            HRESULT hr = E_FAIL;

            std::shared_ptr<TableOutput::IWriter> pWriter;

            switch (output.Type)
            {
                case OutputSpec::Kind::None:
                    break;
                case OutputSpec::Kind::TableFile:
                case OutputSpec::Kind::CSV:
                case OutputSpec::Kind::CSV | OutputSpec::Kind::TableFile:
                case OutputSpec::Kind::TSV:
                case OutputSpec::Kind::TSV | OutputSpec::Kind::TableFile:
                case OutputSpec::Kind::Parquet:
                case OutputSpec::Kind::Parquet | OutputSpec::Kind::TableFile:
                case OutputSpec::Kind::ORC:
                case OutputSpec::Kind::ORC | OutputSpec::Kind::TableFile:
                case OutputSpec::Kind::SQL: {
                    if (nullptr == (pWriter = ::Orc::TableOutput::GetWriter(output)))
                    {
                        Log::Error("Failed to create ouput writer");
                        return E_FAIL;
                    }
                    for (auto& out : m_outputs)
                    {
                        out.second = pWriter;
                    }
                }
                break;
                case OutputSpec::Kind::Directory: {
                    for (auto& out : m_outputs)
                    {
                        std::shared_ptr<::Orc::TableOutput::IWriter> pW;

                        WCHAR szOutputFile[MAX_PATH];
                        StringCchPrintf(
                            szOutputFile, MAX_PATH, L"%s_%s.csv", szPrefix, out.first.GetIdentifier().c_str());
                        if (nullptr == (pW = ::Orc::TableOutput::GetWriter(szOutputFile, output)))
                        {
                            Log::Error("Failed to create output file information");
                            return E_FAIL;
                        }
                        out.second = pW;
                    }
                }
                break;
                case OutputSpec::Kind::Archive: {
                    DWORD idx = 0L;

                    for (auto& out : m_outputs)
                    {

                        std::shared_ptr<::Orc::TableOutput::IWriter> pW;

                        WCHAR szOutputFile[MAX_PATH];
                        StringCchPrintf(
                            szOutputFile,
                            MAX_PATH,
                            L"%s_%.8d_%s_.csv",
                            szPrefix,
                            idx++,
                            out.first.GetIdentifier().c_str());
                        if (nullptr == (pW = ::Orc::TableOutput::GetWriter(szOutputFile, output)))
                        {
                            Log::Error("Failed to create output file information file");
                            return E_FAIL;
                        }

                        ;
                        if (auto pStreamWriter = std::dynamic_pointer_cast<TableOutput::IStreamWriter>(pW);
                            pStreamWriter && pStreamWriter->GetStream())
                        {
                            Concurrency::send(
                                m_messageBuf,
                                ArchiveMessage::MakeAddStreamRequest(szOutputFile, pStreamWriter->GetStream(), true));
                        }
                        out.second = pW;
                    }
                    Concurrency::send(m_messageBuf, ArchiveMessage::MakeFlushQueueRequest());
                }
                default:
                    break;
            }
            return S_OK;
        };

        template <class InputContainer>
        HRESULT GetWriters(const OutputSpec& output, LPCWSTR szPrefix, const InputContainer& container)
        {

            m_outputs.reserve(container.size());
            for (const auto& item : container)
            {
                m_outputs.emplace_back(item, nullptr);
            }
            return GetWriters(output, szPrefix);
        };

        HRESULT ForEachOutput(const OutputSpec& output, std::function<HRESULT(const OutputPair& out)> aCallback)
        {
            Guard::ScopeGuard sg([&output, this] { CloseAll(output); });

            for (auto& item : m_outputs)
            {
                Guard::ScopeGuard forGuard([&output, &item, this]() { CloseOne(output, item); });

                HRESULT hr = E_FAIL;
                // Actually enumerate objects here

                if (FAILED(hr = aCallback(item)))
                {
                    Log::Error(L"Failed during callback on output item '{}'", item.first.GetIdentifier());
                }
            }
            return S_OK;
        }

        HRESULT AddFile(
            const OutputSpec& output,
            const std::wstring& szNameInArchive,
            const std::wstring& szFileName,
            bool bHashData,
            bool bDeleteWhenDone)
        {
            switch (output.Type)
            {
                case OutputSpec::Kind::Directory:
                case OutputSpec::Kind::Archive:

                    if (VerifyFileExists(szFileName.c_str()) == S_OK)
                    {
                        concurrency::send(
                            m_messageBuf,
                            ArchiveMessage::MakeAddFileRequest(
                                szNameInArchive, szFileName, bHashData, bDeleteWhenDone));
                    }
                    break;
                default:
                    break;
            }
            return S_OK;
        }

        HRESULT AddStream(
            const OutputSpec& output,
            const std::wstring& szNameInArchive,
            const std::shared_ptr<ByteStream>& pStream,
            bool bHashData,
            bool bDeleteWhenDone)
        {
            switch (output.Type)
            {
                case OutputSpec::Kind::Directory:
                case OutputSpec::Kind::Archive:

                    if (pStream)
                    {
                        concurrency::send(m_messageBuf, ArchiveMessage::MakeAddStreamRequest(szNameInArchive, pStream));
                    }
                    break;
                default:
                    break;
            }
            return S_OK;
        }

        HRESULT CloseOne(const OutputSpec& output, OutputPair& item) const
        {
            switch (output.Type)
            {
                case OutputSpec::Kind::Directory:
                case OutputSpec::Kind::Archive:
                    item.second->Close();
                    item.second.reset();
                    break;
                default:
                    break;
            }
            return S_OK;
        }

        HRESULT CloseAll(const OutputSpec& output)
        {
            switch (output.Type)
            {
                case OutputSpec::Kind::TableFile:
                case OutputSpec::Kind::CSV:
                case OutputSpec::Kind::TableFile | OutputSpec::Kind::CSV:
                case OutputSpec::Kind::TSV:
                case OutputSpec::Kind::TableFile | OutputSpec::Kind::TSV:
                case OutputSpec::Kind::Parquet:
                case OutputSpec::Kind::TableFile | OutputSpec::Kind::Parquet:
                case OutputSpec::Kind::ORC:
                case OutputSpec::Kind::TableFile | OutputSpec::Kind::ORC:
                case OutputSpec::Kind::SQL:
                    if (!m_outputs.empty() && m_outputs.front().second != nullptr)
                    {
                        m_outputs.front().second->Close();
                        m_outputs.front().second.reset();
                    }
                    break;
                default:
                    break;
            }
            if (m_pArchiveAgent)
            {
                concurrency::send(m_messageBuf, ArchiveMessage::MakeCompleteRequest());

                try
                {
                    concurrency::agent::wait(
                        m_pArchiveAgent.get(), 120000);  // Wait 2 minutes for archive agent to complete
                }
                catch (concurrency::operation_timed_out&)
                {
                    Log::Error("Complete archive operation has timed out");
                    return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                }
            }
            return S_OK;
        }
    };

protected:
    UtilitiesLogger m_logging;

    mutable Output::Console m_console;

    Traits::TimeUtc<SYSTEMTIME> theStartTime;
    Traits::TimeUtc<SYSTEMTIME> theFinishTime;
    DWORD theStartTickCount;
    DWORD theFinishTickCount;
    UtilitiesConfiguration m_utilitiesConfig;

    std::vector<std::shared_ptr<ExtensionLibrary>> m_extensions;
    HRESULT LoadCommonExtensions();
    HRESULT LoadWinTrust();
    HRESULT LoadEvtLibrary();
    HRESULT LoadPSAPI();

    virtual void Configure(int argc, const wchar_t* argv[])
    {
        m_logging.Configure(argc, argv);

        // FIX: Some arguments must be processed very early as others depends
        // on their value. This is not a clean fix but a more global refactor is
        // required on options handling...
        std::wstring computerName;
        std::wstring fullComputerName;
        std::wstring systemType;

        for (int i = 0; i < argc; i++)
        {

            switch (argv[i][0])
            {
                case L'/':
                case L'-':
                    if (ParameterOption(argv[i] + 1, L"Computer", computerName))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"FullComputer", fullComputerName))
                        ;
                    else if (ParameterOption(argv[i] + 1, L"SystemType", systemType))
                        ;
                    break;
                default:
                    break;
            }
        }

        if (computerName.empty() && !fullComputerName.empty())
        {
            computerName = fullComputerName;
        }

        if (fullComputerName.empty() && !computerName.empty())
        {
            fullComputerName = computerName;
        }

        if (!computerName.empty())
        {
            SystemDetails::SetOrcComputerName(computerName);
        }

        if (!fullComputerName.empty())
        {
            SystemDetails::SetOrcFullComputerName(fullComputerName);
        }

        if (!systemType.empty())
        {
            SystemDetails::SetSystemType(systemType);
        }
    }

    template <typename T>
    void PrintCommonParameters(Orc::Text::Tree<T>& root)
    {
        PrintValue(root, "Start time", theStartTime);

        std::wstring computerName;
        SystemDetails::GetComputerName_(computerName);
        PrintValue(root, "Computer name", computerName);

        std::wstring fullComputerName;
        SystemDetails::GetFullComputerName(fullComputerName);
        if (fullComputerName != computerName)
        {
            PrintValue(root, "Full computer name", fullComputerName);
        }

        std::wstring orcComputerName;
        SystemDetails::GetOrcComputerName(orcComputerName);
        if (computerName != orcComputerName)
        {
            PrintValue(root, "DFIR-Orc computer name", orcComputerName);
        }

        std::wstring orcFullComputerName;
        SystemDetails::GetOrcFullComputerName(orcFullComputerName);
        if (orcFullComputerName != fullComputerName && orcFullComputerName != orcComputerName)
        {
            PrintValue(root, "DFIR-Orc computer", orcFullComputerName);
        }

        std::wstring description;
        SystemDetails::GetDescriptionString(description);
        PrintValue(root, "Operating system", description);

        std::wstring userName;
        SystemDetails::WhoAmI(userName);
        bool bIsElevated = false;
        SystemDetails::AmIElevated(bIsElevated);
        PrintValue(root, L"User", fmt::format(L"{}{}", userName, bIsElevated ? L" (elevated)" : L""));

        std::wstring systemType;
        SystemDetails::GetSystemType(systemType);
        PrintValue(root, L"System type", systemType);

        PrintValue(root, L"System tags", boost::join(SystemDetails::GetSystemTags(), ", "));
    }

    template <typename T>
    void PrintCommonFooter(Orc::Text::Tree<T>& root)
    {
        PrintValue(root, "Warning(s)", m_logging.logger().warningCount());
        PrintValue(root, "Error(s)", m_logging.logger().errorCount());
        PrintValue(root, "Critical error(s)", m_logging.logger().criticalCount());

        PrintValue(root, "Finish time", theFinishTime);

        // TODO: std::chrono
        DWORD dwElapsed;
        if (theFinishTickCount < theStartTickCount)
        {
            dwElapsed = (MAXDWORD - theStartTickCount) + theFinishTickCount;
        }
        else
        {
            dwElapsed = theFinishTickCount - theStartTickCount;
        }

        DWORD dwMillisec = dwElapsed % 1000;
        DWORD dwSec = (dwElapsed / 1000) % 60;
        DWORD dwMin = (dwElapsed / (1000 * 60)) % 60;
        DWORD dwHour = (dwElapsed / (1000 * 60 * 60)) % 24;
        DWORD dwDay = (dwElapsed / (1000 * 60 * 60 * 24));

        std::vector<std::wstring> durations;
        if (dwDay)
        {
            durations.push_back(fmt::format(L"{} day(s)", dwDay));
        }

        if (dwHour)
        {
            durations.push_back(fmt::format(L"{} hour(s)", dwHour));
        }

        if (dwMin)
        {
            durations.push_back(fmt::format(L"{} min(s)", dwMin));
        }

        if (dwSec)
        {
            durations.push_back(fmt::format(L"{} sec(s)", dwSec));
        }

        durations.push_back(fmt::format(L"{} msecs", dwMillisec));

        PrintValue(root, "Elapsed time", boost::join(durations, L", "));
    }

    //
    // Option handling
    //
    bool OutputOption(LPCWSTR szArg, LPCWSTR szOption, OutputSpec::Kind supportedTypes, OutputSpec& anOutput);

    template <typename OptionType>
    bool
    OutputOption(LPCWSTR szArg, LPCWSTR szOption, OutputSpec::Kind supportedTypes, std::optional<OptionType>& parameter)
    {
        OptionType result;
        if (OutputOption(szArg, szOption, supportedTypes, result))
        {
            parameter.emplace(std::move(result));
            return true;
        }
        return false;
    }

    bool OutputOption(LPCWSTR szArg, LPCWSTR szOption, OutputSpec& anOutput)
    {
        return OutputOption(szArg, szOption, anOutput.supportedTypes, anOutput);
    };

    template <typename OptionType>
    bool OutputOption(LPCWSTR szArg, LPCWSTR szOption, std::optional<OptionType> parameter)
    {
        OptionType result;
        if (OutputOption(szArg, szOption, result))
        {
            parameter.emplace(std::move(result));
            return true;
        }
        return false;
    }

    bool OutputFileOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strOutputFile);
    template <typename OptionType>
    bool OutputFileOption(LPCWSTR szArg, LPCWSTR szOption, std::optional<OptionType> parameter)
    {
        OptionType result;
        if (OutputFileOption(szArg, szOption, result))
        {
            parameter.emplace(std::move(result));
            return true;
        }
        return false;
    }

    bool OutputDirOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strOutputFile);
    template <typename OptionType>
    bool OutputDirOption(LPCWSTR szArg, LPCWSTR szOption, std::optional<OptionType>& parameter)
    {
        OptionType result;
        if (OutputDirOption(szArg, szOption, result))
        {
            parameter.emplace(std::move(result));
            return true;
        }
        return false;
    }

    bool InputFileOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strInputFile);

    template <typename OptionType>
    bool InputFileOption(LPCWSTR szArg, LPCWSTR szOption, std::optional<OptionType>& parameter)
    {
        OptionType result;
        if (InputFileOption(szArg, szOption, result))
        {
            parameter.emplace(std::move(result));
            return true;
        }
        return false;
    }

    bool InputDirOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strInputFile);

    template <typename OptionType>
    bool InputDirOption(LPCWSTR szArg, LPCWSTR szOption, std::optional<OptionType>& parameter)
    {
        OptionType result;
        if (InputDirOption(szArg, szOption, result))
        {
            parameter.emplace(std::move(result));
            return true;
        }
        return false;
    }

    bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strParameter);
    bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, ULONGLONG& ullParameter);
    bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, DWORD& dwParameter);
    bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::chrono::minutes& dwParameter);
    bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::chrono::seconds& dwParameter);
    bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::chrono::milliseconds& dwParameter);
    bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, boost::logic::tribool& bParameter);

    template <typename OptionType>
    bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::optional<OptionType>& parameter)
    {
        OptionType result;
        if (ParameterOption(szArg, szOption, result))
        {
            parameter.emplace(std::move(result));
            return true;
        }
        return false;
    }

    bool OptionalParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strParameter);
    bool OptionalParameterOption(LPCWSTR szArg, LPCWSTR szOption, ULONGLONG& ullParameter);
    bool OptionalParameterOption(LPCWSTR szArg, LPCWSTR szOption, DWORD& dwParameter);

    template <class ListContainerT>
    void
    CSVListToContainer(const std::wstring& strKeywords, ListContainerT& parameterList, const LPCWSTR szSeparator = L",")
    {
        ListContainerT keys;

        if (szSeparator != NULL)
        {
            boost::split(keys, strKeywords, boost::is_any_of(szSeparator));
            std::merge(
                begin(parameterList),
                end(parameterList),
                begin(keys),
                end(keys),
                std::insert_iterator<ListContainerT>(parameterList, end(parameterList)));
        }
        else
        {
            auto back_inserter = std::insert_iterator<ListContainerT>(parameterList, end(parameterList));
            *back_inserter = strKeywords;
            back_inserter++;
        }
        return;
    };

    template <class ListContainerT>
    bool ParameterListOption(
        LPCWSTR szArg,
        LPCWSTR szOption,
        ListContainerT& parameterList,
        const LPCWSTR szSeparator = L",")
    {
        if (!_wcsnicmp(szArg, szOption, wcslen(szOption)))
        {
            LPCWSTR pEquals = wcschr(szArg, L'=');
            if (!pEquals)
            {
                Log::Error(L"Option /{} should be like: /{}=<Value>", szOption, szOption);
                return false;
            }
            else
            {
                std::wstring strKeywords = pEquals + 1;
                CSVListToContainer(strKeywords, parameterList, szSeparator);
            }
            return true;
        }
        return false;
    }

    bool FileSizeOption(LPCWSTR szArg, LPCWSTR szOption, DWORDLONG& dwlFileSize);

    bool AltitudeOption(LPCWSTR szArg, LPCWSTR szOption, LocationSet::Altitude& altitude);

    bool BooleanOption(LPCWSTR szArg, LPCWSTR szOption, bool& bOption);
    bool BooleanOption(LPCWSTR szArg, LPCWSTR szOption, boost::logic::tribool& bOption);

    bool ToggleBooleanOption(LPCWSTR szArg, LPCWSTR szOption, bool& bOption);

    bool CryptoHashAlgorithmOption(LPCWSTR szArg, LPCWSTR szOption, CryptoHashStream::Algorithm& algo);
    bool FuzzyHashAlgorithmOption(LPCWSTR szArg, LPCWSTR szOption, FuzzyHashStream::Algorithm& algo);

    bool ProcessPriorityOption(LPCWSTR szArg, LPCWSTR szOption = L"Low");
    bool EncodingOption(LPCWSTR szArg, OutputSpec::Encoding& anEncoding);
    bool UsageOption(LPCWSTR szArg);

    bool WaitForDebugger(int argc, const WCHAR* argv[]);
    bool IgnoreWaitForDebuggerOption(LPCWSTR szArg);
    bool WaitForDebuggerOption(LPCWSTR szArg);

    bool IgnoreLoggingOptions(LPCWSTR szArg);
    bool IgnoreConfigOptions(LPCWSTR szArg);
    bool IgnoreCommonOptions(LPCWSTR szArg);
    bool IgnoreEarlyOptions(LPCWSTR szArg);

public:
    UtilitiesMain();
    virtual ~UtilitiesMain() {}

    HRESULT ReadConfiguration(
        int argc,
        LPCWSTR argv[],
        LPCWSTR szCmdLineOption,
        LPCWSTR szResourceID,
        LPCWSTR szRefResourceID,
        LPCWSTR szConfigExt,
        ConfigItem& configitem,
        ConfigItem::InitFunction init);

    static bool IsProcessParent(LPCWSTR szImageName);

    virtual HRESULT GetConfigurationFromConfig(const ConfigItem& configitem) = 0;
    virtual HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem) = 0;
    virtual HRESULT GetSchemaFromConfig(const ConfigItem&) { return S_OK; };
    virtual HRESULT GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[]) = 0;
    virtual HRESULT CheckConfiguration() = 0;

    //
    // Output handling
    //
    virtual void PrintUsage() = 0;

    virtual void PrintHeader(LPCWSTR szToolName, LPCWSTR lpszToolDescription, LPCWSTR szVersion);
    virtual void PrintParameters() = 0;
    virtual void PrintFooter() = 0;

    template <class UtilityT>
    static int WMain(int argc, const WCHAR* argv[])
    {
        Robustness::Initialize(UtilityT::ToolName());

        UtilityT Cmd;
        Cmd.Configure(argc, argv);

        HRESULT hr = E_FAIL;

        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data))
        {
            Log::Error(L"Failed to initialize WinSock 2.2 [{}]", Win32Error(WSAGetLastError()));
        }

        if (FAILED(hr = CoInitializeEx(0, COINIT_MULTITHREADED)))
        {
            Log::Critical(L"Failed to initialize COM library [{}]", SystemError(hr));
            return hr;
        }

        if (FAILED(
                hr = CoInitializeSecurity(
                    NULL,
                    -1,  // COM negotiates service
                    NULL,  // Authentication services
                    NULL,  // Reserved
                    RPC_C_AUTHN_LEVEL_DEFAULT,  // Default authentication
                    RPC_C_IMP_LEVEL_IMPERSONATE,  // Default Impersonation
                    NULL,  // Authentication info
                    EOAC_NONE,  // Additional capabilities
                    NULL  // Reserved
                    )))
        {
            Log::Warn("Failed to initialize COM security");
        }

        Cmd.WaitForDebugger(argc, argv);
        Cmd.LoadCommonExtensions();
        Cmd.PrintHeader(UtilityT::ToolName(), UtilityT::ToolDescription(), kOrcFileVerStringW);

        try
        {
            if (UtilityT::DefaultSchema() != nullptr)
            {
                ConfigItem schemaitem;
                if (FAILED(
                        hr = Cmd.ReadConfiguration(
                            argc,
                            argv,
                            L"SqlSchema",
                            UtilityT::DefaultSchema(),
                            nullptr,
                            nullptr,
                            schemaitem,
                            Orc::Config::Common::sqlschema)))
                {
                    Log::Critical(L"Failed to parse schema configuration item [{}]", SystemError(hr));
                    return hr;
                }

                if (FAILED(hr = Cmd.GetSchemaFromConfig(schemaitem)))
                {
                    Log::Critical(L"Failed to process configuration schema [{}]", SystemError(hr));
                    return hr;
                }
            }

            if (UtilityT::DefaultConfiguration() != nullptr || UtilityT::ConfigurationExtension() != nullptr)
            {
                ConfigItem configitem;
                if (FAILED(
                        hr = Cmd.ReadConfiguration(
                            argc,
                            argv,
                            L"Config",
                            UtilityT::DefaultConfiguration(),
                            UtilityT::DefaultConfiguration(),
                            UtilityT::ConfigurationExtension(),
                            configitem,
                            UtilityT::GetXmlConfigBuilder())))
                {
                    Log::Critical(L"Failed to parse default configuration [{}]", SystemError(hr));
                    return hr;
                }

                if (FAILED(hr = Cmd.GetConfigurationFromConfig(configitem)))
                {
                    Log::Critical(L"Failed to parse xml configuration [{}]", SystemError(hr));
                    return hr;
                }
            }

            if (UtilityT::LocalConfiguration() != nullptr || UtilityT::LocalConfigurationExtension() != nullptr)
            {
                ConfigItem configitem;
                if (FAILED(
                        hr = Cmd.ReadConfiguration(
                            argc,
                            argv,
                            L"Local",
                            UtilityT::LocalConfiguration(),
                            UtilityT::DefaultConfiguration(),
                            UtilityT::LocalConfigurationExtension(),
                            configitem,
                            UtilityT::GetXmlLocalConfigBuilder())))
                    return hr;
                if (FAILED(hr = Cmd.GetLocalConfigurationFromConfig(configitem)))
                {
                    Log::Critical(L"Failed to parse local xml configuration [{}]", SystemError(hr));
                    return hr;
                }
            }

            if (FAILED(hr = Cmd.GetConfigurationFromArgcArgv(argc, argv)))
            {
                Log::Critical(L"Failed to parse command line arguments [{}]", SystemError(hr));
                return hr;
            }

            if (FAILED(hr = Cmd.CheckConfiguration()))
            {
                Log::Critical(L"Failed while checking configuration [{}]", SystemError(hr));
                return hr;
            }
        }
        catch (std::exception& e)
        {
            Log::Critical(
                "Exception during configuration evaluation. Type: {}, Reason: {}", typeid(e).name(), e.what());
            return E_ABORT;
        }
        catch (...)
        {
            Log::Critical("Exception during configuration evaluation");
            return E_ABORT;
        }

        // save the start time
        GetSystemTime(&Cmd.theStartTime.value);
        Cmd.theStartTickCount = GetTickCount();

        if (!Cmd.m_utilitiesConfig.strComputerName.empty())
        {
            SystemDetails::SetOrcComputerName(Cmd.m_utilitiesConfig.strComputerName);
        }

        // Parameters are displayed when the configuration is complete and checked
        Cmd.PrintParameters();

        try
        {
            hr = Cmd.Run();
            if (FAILED(hr))
            {
                Log::Critical(L"Command failed with {}", SystemError(hr));
                return hr;
            }
        }
        catch (std::exception& e)
        {
            Log::Critical("Exception during execution. Type: {}, Reason: {}", typeid(e).name(), e.what());

#ifdef ORC_BUILD_BOOST_STACKTRACE
            boost::stacktrace::stacktrace();
#endif
            return E_ABORT;
        }
        catch (...)
        {
            Log::Critical("Exception during configuration evaluation.");

#ifdef ORC_BUILD_BOOST_STACKTRACE
            boost::stacktrace::stacktrace();
#endif
            return E_ABORT;
        }

        GetSystemTime(&Cmd.theFinishTime.value);
        Cmd.theFinishTickCount = GetTickCount();
        Cmd.PrintFooter();

        if (WSACleanup())
        {
            Log::Error(L"Failed to cleanup WinSock 2.2 [{}]", Win32Error(WSAGetLastError()));
        }

        Robustness::UnInitialize(INFINITE);

        // FIXME: Once log level are fine we should be able to return only m_logger->criticalCount()
        //        Until then, always dump the backtrace with a Log::Critical
        if (Cmd.m_logging.logger().errorCount())
        {
            Log::Critical(
                L"Dump log backtrace due to some previously encoutered error(s). "
                L"This could probably be ignored, you may NOT have encoutered any critical error. Error levels are "
                L"being reevaluated and this backtrace could help in case of mistakes.");
        }

        return static_cast<int>(Cmd.m_logging.logger().errorCount() + Cmd.m_logging.logger().criticalCount());
    }
};

}  // namespace Command
}  // namespace Orc

#pragma managed(pop)
