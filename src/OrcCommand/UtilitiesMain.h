//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "Configuration/ConfigFile.h"
#include "Archive.h"
#include "ParameterCheck.h"
#include "Robustness.h"
#include "Configuration/ConfigFile_Common.h"
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
#include "Console.h"
#include "ResurrectRecordsMode.h"
#include "Log/Log.h"
#include "Text/Print.h"
#include "Text/Fmt/Offset.h"
#include "Text/Fmt/ByteQuantity.h"
#include "Text/Fmt/FILE_NAME.h"
#include "Text/Fmt/FILETIME.h"
#include "Text/Fmt/std_optional.h"
#include "Text/Fmt/SYSTEMTIME.h"
#include "Text/Fmt/TimeUtc.h"
#include "Utils/Guard.h"
#include "Log/UtilitiesLogger.h"
#include "Log/UtilitiesLoggerConfiguration.h"
#include "Log/LogTerminationHandler.h"
#include "Utils/StdStream/StandardOutput.h"
#include "Text/Guid.h"
#include "Limit.h"
#include "VolumeReader.h"

#include "Utils/EnumFlags.h"

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

#pragma managed(push, off)

namespace Orc {

namespace Command {

class UtilitiesMain
{
public:
    class Configuration
    {
    public:
        std::wstring strConfigFile;
    };

    // Common configuration
    struct UtilitiesConfiguration
    {
        std::wstring strComputerName;
        UtilitiesLoggerConfiguration log;
    };

    class OutputInfo
    {
    public:
        enum class DataType
        {
            kFileInfo = 1,
            kAttrInfo,
            ki30Info,
            kNtfsTimeline,
            kSecDescrInfo,
            kFatInfo,
            kObjInfo,
            kUsnInfo
        };

        OutputInfo(std::shared_ptr<Orc::TableOutput::IWriter> writer)
            : m_writer(std::move(writer))
            , m_path()
            , m_type()
        {
        }

        OutputInfo(std::shared_ptr<Orc::TableOutput::IWriter> writer, const std::wstring& path, DataType type)
            : m_writer(std::move(writer))
            , m_path(path)
            , m_type(type)
        {
        }

        std::shared_ptr<Orc::TableOutput::IWriter>& Writer() { return m_writer; }
        const std::shared_ptr<Orc::TableOutput::IWriter>& Writer() const { return m_writer; }

        void SetPath(const std::wstring& path) { m_path = path; }
        const std::optional<std::wstring>& Path() const { return m_path; }

        void SetType(DataType type) { m_type = type; }
        std::optional<DataType> Type() const { return m_type; }

    public:
        std::shared_ptr<Orc::TableOutput::IWriter> m_writer;
        std::optional<std::wstring> m_path;
        DataType m_type;
    };

    template <class T>
    class MultipleOutput
    {
    public:
        using OutputPair = std::pair<T, OutputInfo>;

    private:
        std::vector<OutputPair> m_outputs;

        ArchiveMessage::UnboundedMessageBuffer m_messageBuf;
        std::unique_ptr<Concurrency::call<ArchiveNotification::Notification>> m_notificationBuf;
        std::shared_ptr<ArchiveAgent> m_pArchiveAgent;

    public:
        std::vector<OutputPair>& Outputs() { return m_outputs; };
        const std::vector<OutputPair>& Outputs() const { return m_outputs; };

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
                        volStatOutput.WriteString(fmt::format(L"{}", fmt::join(loc->GetPaths(), L";")));
                        volStatOutput.WriteString(loc->GetShadow() ? ToStringW(loc->GetShadow()->guid).c_str() : L"");

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

        HRESULT GetWriters(const OutputSpec& output, LPCWSTR szPrefix, OutputInfo::DataType dataType)
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
                case OutputSpec::Kind::ORC | OutputSpec::Kind::TableFile: {
                    if (nullptr == (pWriter = ::Orc::TableOutput::GetWriter(output)))
                    {
                        Log::Error("Failed to create ouput writer");
                        return E_FAIL;
                    }
                    for (auto& out : m_outputs)
                    {
                        out.second = {pWriter, output.Path, dataType};
                    }
                }
                break;
                case OutputSpec::Kind::Directory: {
                    for (auto& out : m_outputs)
                    {
                        std::shared_ptr<::Orc::TableOutput::IWriter> pW;

                        WCHAR szOutputFile[ORC_MAX_PATH];
                        StringCchPrintf(
                            szOutputFile, ORC_MAX_PATH, L"%s_%s.csv", szPrefix, out.first.GetIdentifier().c_str());
                        if (nullptr == (pW = ::Orc::TableOutput::GetWriter(szOutputFile, output)))
                        {
                            Log::Error("Failed to create output file information");
                            return E_FAIL;
                        }

                        out.second = {pW, szOutputFile, dataType};
                    }
                }
                break;
                case OutputSpec::Kind::Archive: {
                    DWORD idx = 0L;

                    for (auto& out : m_outputs)
                    {
                        std::shared_ptr<::Orc::TableOutput::IWriter> pW;

                        WCHAR szOutputFile[ORC_MAX_PATH];
                        StringCchPrintf(
                            szOutputFile,
                            ORC_MAX_PATH,
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

                        out.second = {pW, szOutputFile, dataType};
                    }
                    Concurrency::send(m_messageBuf, ArchiveMessage::MakeFlushQueueRequest());
                }
                default:
                    break;
            }
            return S_OK;
        };

        template <class InputContainer>
        HRESULT GetWriters(
            const OutputSpec& output,
            LPCWSTR szPrefix,
            const InputContainer& container,
            OutputInfo::DataType dataType)
        {
            m_outputs.reserve(container.size());

            for (const auto& item : container)
            {
                m_outputs.emplace_back(item, nullptr);
            }

            return GetWriters(output, szPrefix, dataType);
        };

        HRESULT ForEachOutput(const OutputSpec& output, std::function<HRESULT(const OutputPair& out)> aCallback)
        {
            Guard::Scope sg([&output, this] { CloseAll(output); });

            for (auto& item : m_outputs)
            {
                Guard::Scope forGuard([&output, &item, this]() { CloseOne(output, item); });

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
                    item.second.Writer()->Close();
                    item.second.Writer().reset();
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
                    if (!m_outputs.empty() && m_outputs.front().second.Writer() != nullptr)
                    {
                        m_outputs.front().second.Writer()->Close();
                        m_outputs.front().second.Writer().reset();
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
                    Log::Critical("Complete archive operation has timed out");
                    return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                }
            }
            return S_OK;
        }
    };

protected:
    UtilitiesLogger m_logging;

    StandardOutput m_standardOutput;
    mutable Console m_console;

    Traits::TimeUtc<SYSTEMTIME> theStartTime;
    Traits::TimeUtc<SYSTEMTIME> theFinishTime;
    DWORD theStartTickCount;
    DWORD theFinishTickCount;
    UtilitiesConfiguration m_utilitiesConfig;
    HANDLE m_hMothership;

    std::vector<std::shared_ptr<ExtensionLibrary>> m_extensions;
    HRESULT LoadCommonExtensions();
    HRESULT LoadWinTrust();
    HRESULT LoadEvtLibrary();
    HRESULT LoadPSAPI();

    virtual void Configure(int argc, const wchar_t* argv[]);

    void PrintCommonParameters(Orc::Text::Tree& root);
    void PrintCommonFooter(Orc::Text::Tree& root);

    //
    // Option handling
    //
    static bool OutputOption(LPCWSTR szArg, LPCWSTR szOption, OutputSpec::Kind supportedTypes, OutputSpec& anOutput);

    template <typename OptionType>
    static bool
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

    static bool OutputOption(LPCWSTR szArg, LPCWSTR szOption, OutputSpec& anOutput)
    {
        return OutputOption(szArg, szOption, anOutput.supportedTypes, anOutput);
    };

    template <typename OptionType>
    static bool OutputOption(LPCWSTR szArg, LPCWSTR szOption, std::optional<OptionType> parameter)
    {
        OptionType result;
        if (OutputOption(szArg, szOption, result))
        {
            parameter.emplace(std::move(result));
            return true;
        }
        return false;
    }

    static bool OutputFileOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strOutputFile);

    template <typename OptionType>
    static bool OutputFileOption(LPCWSTR szArg, LPCWSTR szOption, std::optional<OptionType> parameter)
    {
        OptionType result;
        if (OutputFileOption(szArg, szOption, result))
        {
            parameter.emplace(std::move(result));
            return true;
        }
        return false;
    }

    static bool OutputDirOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strOutputFile);

    template <typename OptionType>
    static bool OutputDirOption(LPCWSTR szArg, LPCWSTR szOption, std::optional<OptionType>& parameter)
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
    static bool InputFileOption(LPCWSTR szArg, LPCWSTR szOption, std::optional<OptionType>& parameter)
    {
        OptionType result;
        if (InputFileOption(szArg, szOption, result))
        {
            parameter.emplace(std::move(result));
            return true;
        }
        return false;
    }

    static bool InputDirOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strInputFile);

    template <typename OptionType>
    static bool InputDirOption(LPCWSTR szArg, LPCWSTR szOption, std::optional<OptionType>& parameter)
    {
        OptionType result;
        if (InputDirOption(szArg, szOption, result))
        {
            parameter.emplace(std::move(result));
            return true;
        }
        return false;
    }

    static bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::wstring& strParameter);
    static bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, ULONGLONG& ullParameter);
    static bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, DWORD& dwParameter);
    static bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::chrono::minutes& dwParameter);
    static bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::chrono::seconds& dwParameter);
    static bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::chrono::milliseconds& dwParameter);
    static bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, boost::logic::tribool& bParameter);

    template <typename OptionType>
    static bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::optional<OptionType>& parameter)
    {
        OptionType result;
        if (ParameterOption(szArg, szOption, result))
        {
            parameter.emplace(std::move(result));
            return true;
        }
        return false;
    }

    template <typename OptionType>
    static bool ParameterOption(LPCWSTR szArg, LPCWSTR szOption, std::optional<Limit<OptionType>>& parameter)
    {
        OptionType result;
        if (ParameterOption(szArg, szOption, result))
        {
            parameter.emplace(std::move(result));
            return true;
        }
        return false;
    }

    static bool OptionalParameterOption(
        LPCWSTR szArg,
        LPCWSTR szOption,
        std::optional<std::wstring>& strParameter,
        const std::optional<std::wstring> defaultValue = std::nullopt);

    static bool OptionalParameterOption(
        LPCWSTR szArg,
        LPCWSTR szOption,
        std::optional<ULONGLONG>& ullParameter,
        const std::optional<ULONGLONG> defaultValue = std::nullopt);
    static bool OptionalParameterOption(
        LPCWSTR szArg,
        LPCWSTR szOption,
        std::optional<DWORD>& dwParameter,
        const std::optional<DWORD> defaultValue = std::nullopt);

    template <class ListContainerT>
    static void
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
    static bool ParameterListOption(
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
                Log::Critical(L"Option /{} should be like: /{}=<Value>", szOption, szOption);
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

    template <typename OptionType>
    static bool FileSizeOption(LPCWSTR szArg, LPCWSTR szOption, std::optional<OptionType>& parameter)
    {
        OptionType result(0);
        if (FileSizeOption(szArg, szOption, result))
        {
            parameter.emplace(std::move(result));
            return true;
        }
        return false;
    }

    static bool FileSizeOption(LPCWSTR szArg, LPCWSTR szOption, DWORDLONG& dwlFileSize);

    static bool AltitudeOption(LPCWSTR szArg, LPCWSTR szOption, LocationSet::Altitude& altitude);

    static bool BooleanOption(LPCWSTR szArg, LPCWSTR szOption, bool& bOption);
    static bool BooleanOption(LPCWSTR szArg, LPCWSTR szOption, boost::logic::tribool& bOption);
    static bool BooleanExactOption(LPCWSTR szArg, LPCWSTR szOption, boost::logic::tribool& bPresent);

    static bool ToggleBooleanOption(LPCWSTR szArg, LPCWSTR szOption, bool& bOption);

    template <typename _EnumT>
    static bool EnumOption(LPCWSTR szArg, LPCWSTR szOption, _EnumT& eOption, _EnumT eValue)
    {
        if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
            return false;
        eOption = eValue;
        return true;
    }

    template <typename _FlagT, typename = std::enable_if_t<EnumFlagsOperator<_FlagT>::value>>
    static bool FlagOption(LPCWSTR szArg, LPCWSTR szOption, _FlagT& eOption, _FlagT eValue)
    {
        if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
            return false;
        eOption |= eValue;
        return true;
    }
    template <typename _FlagT, typename = std::enable_if_t<EnumFlagsOperator<_FlagT>::value>>
    static bool FlagOption(LPCWSTR szArg, LPCWSTR szOption, std::optional<_FlagT>& fOption, _FlagT eValue)
    {
        if (_wcsnicmp(szArg, szOption, wcslen(szOption)))
            return false;
        if (!fOption)
            fOption.emplace();
        fOption.value() |= eValue;
        return true;
    }

    static bool ResurrectRecordsOption(LPCWSTR szArg, LPCWSTR szOption, ResurrectRecordsMode& mode);

    static bool ShadowsOption(
        LPCWSTR szArg,
        LPCWSTR szOption,
        boost::logic::tribool& bAddShadows,
        std::optional<LocationSet::ShadowFilters>& filters);

    static bool
    LocationExcludeOption(LPCWSTR szArg, LPCWSTR szOption, std::optional<LocationSet::PathExcludes>& excludes);

    static bool CryptoHashAlgorithmOption(LPCWSTR szArg, LPCWSTR szOption, CryptoHashStream::Algorithm& algo);
    static bool FuzzyHashAlgorithmOption(LPCWSTR szArg, LPCWSTR szOption, FuzzyHashStream::Algorithm& algo);

    static bool ProcessPriorityOption(LPCWSTR szArg, LPCWSTR szOption = L"Low");
    static bool EncodingOption(LPCWSTR szArg, OutputSpec::Encoding& anEncoding);

    bool UsageOption(LPCWSTR szArg);

    bool WaitForDebugger(int argc, const WCHAR* argv[]);
    bool IgnoreWaitForDebuggerOption(LPCWSTR szArg);
    bool WaitForDebuggerOption(LPCWSTR szArg);

    bool IgnoreLoggingOptions(LPCWSTR szArg);
    bool IgnoreConfigOptions(LPCWSTR szArg);
    bool IgnoreCommonOptions(LPCWSTR szArg);
    bool IgnoreEarlyOptions(LPCWSTR szArg);

    static void ParseShadowsOption(
        const std::wstring& shadows,
        boost::logic::tribool& bAddShadows,
        std::optional<LocationSet::ShadowFilters>& filters);

    static void
    ParseLocationExcludes(const std::wstring& rawExcludes, std::optional<LocationSet::PathExcludes>& excludes);

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
        Robustness::AddTerminationHandler(std::make_shared<LogTerminationHandler>());

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

        // TODO: FIXME

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
            return E_UNEXPECTED;
        }
        catch (...)
        {
            Log::Critical("Exception during configuration evaluation");
            return E_UNEXPECTED;
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
            std::cerr << boost::stacktrace::stacktrace();
#endif
            return E_UNEXPECTED;
        }
        catch (...)
        {
            std::cerr << "Exception during during command execution" << std::endl;
            Log::Critical("Exception during during command execution.");

#ifdef ORC_BUILD_BOOST_STACKTRACE
            std::cerr << boost::stacktrace::stacktrace();
#endif
            return E_UNEXPECTED;
        }

        GetSystemTime(&Cmd.theFinishTime.value);
        Cmd.theFinishTickCount = GetTickCount();
        Cmd.PrintFooter();

        if (WSACleanup())
        {
            Log::Error(L"Failed to cleanup WinSock 2.2 [{}]", Win32Error(WSAGetLastError()));
        }

        Robustness::UnInitialize(INFINITE);
        return static_cast<int>(Cmd.m_logging.logger().criticalCount());
    }
};  // namespace Command

}  // namespace Command
}  // namespace Orc

#pragma managed(pop)
