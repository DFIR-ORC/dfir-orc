//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

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
#include "LogFileWriter.h"

#include "LocationSet.h"

#include "ToolVersion.h"
#include "SystemDetails.h"

#include "TableOutputWriter.h"

#include "ExtensionLibrary.h"

#include <concrt.h>

#include <boost/logic/tribool.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/scope_exit.hpp>

#include <conio.h>
#include <iostream>
#include <chrono>

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;

namespace Command {

class ORCLIB_API UtilitiesMain
{
public:
    class Configuration
    {
    public:
        std::wstring strConfigFile;
    };

    template <class T>
    class MultipleOutput
    {
    public:
        using OutputPair = std::pair<T, std::shared_ptr<::Orc::TableOutput::IWriter>>;

    private:
        std::vector<OutputPair> m_outputs;
        logger _L_;

        ArchiveMessage::UnboundedMessageBuffer m_messageBuf;
        std::unique_ptr<Concurrency::call<ArchiveNotification::Notification>> m_notificationBuf;
        std::shared_ptr<ArchiveAgent> m_pArchiveAgent;

    public:
        MultipleOutput(logger pLog)
            : _L_(std::move(pLog)) {};

        std::vector<OutputPair>& Outputs() { return m_outputs; };

        HRESULT WriteVolStats(const OutputSpec& volStatsSpec, const std::vector<std::shared_ptr<Location>>& locations)
        {

            std::shared_ptr<TableOutput::IWriter> volStatWriter;

            if (volStatsSpec.Type == OutputSpec::Kind::Archive || volStatsSpec.Type == OutputSpec::Kind::Directory)
                volStatWriter = Orc::TableOutput::GetWriter(_L_, L"volstats.csv", volStatsSpec);
            else
                return S_OK;

            if (volStatWriter != nullptr)
            {
                auto& volStatOutput = volStatWriter->GetTableOutput();
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
                    AddStream(volStatsSpec, L"volstats.csv", pStreamWriter->GetStream(), false, 0L, true);
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
                                log::Info(_L_, L"Archive: %s started\r\n", item->Keyword().c_str());
                                break;
                            case ArchiveNotification::FileAddition:
                                log::Info(_L_, L"Archive: File %s added\r\n", item->Keyword().c_str());
                                break;
                            case ArchiveNotification::DirectoryAddition:
                                log::Info(_L_, L"Archive: Directory %s added\r\n", item->Keyword().c_str());
                                break;
                            case ArchiveNotification::StreamAddition:
                                log::Info(_L_, L"Archive: Output %s added\r\n", item->Keyword().c_str());
                                break;
                            case ArchiveNotification::ArchiveComplete:
                                log::Info(_L_, L"Archive: %s is complete\r\n", item->Keyword().c_str());
                                break;
                        }
                    }
                    else
                    {
                        log::Error(
                            _L_,
                            item->GetHResult(),
                            L"ArchiveOperation: Operation for %s failed \"%s\"\r\n",
                            item->Keyword().c_str(),
                            item->Description().c_str());
                    }
                    return;
                });

            m_pArchiveAgent = std::make_unique<ArchiveAgent>(_L_, m_messageBuf, m_messageBuf, *m_notificationBuf);

            if (!m_pArchiveAgent->start())
            {
                log::Error(_L_, E_FAIL, L"Archive agent failed to start\r\n");
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
                case OutputSpec::Kind::SQL:
                {
                    if (nullptr == (pWriter = ::Orc::TableOutput::GetWriter(_L_, output)))
                    {
                        log::Error(_L_, E_FAIL, L"Failed to create ouput writer\r\n");
                        return E_FAIL;
                    }
                    for (auto& out : m_outputs)
                    {
                        out.second = pWriter;
                    }
                }
                break;
                case OutputSpec::Kind::Directory:
                {
                    for (auto& out : m_outputs)
                    {
                        std::shared_ptr<::Orc::TableOutput::IWriter> pW;

                        WCHAR szOutputFile[MAX_PATH];
                        StringCchPrintf(
                            szOutputFile, MAX_PATH, L"%s_%s.csv", szPrefix, out.first.GetIdentifier().c_str());
                        if (nullptr == (pW = ::Orc::TableOutput::GetWriter(_L_, szOutputFile, output)))
                        {
                            log::Error(_L_, E_FAIL, L"Failed to create output file information file\r\n");
                            return E_FAIL;
                        }
                        out.second = pW;
                    }
                }
                break;
                case OutputSpec::Kind::Archive:
                {
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
                        if (nullptr == (pW = ::Orc::TableOutput::GetWriter(_L_, szOutputFile, output)))
                        {
                            log::Error(_L_, E_FAIL, L"Failed to create output file information file\r\n");
                            return E_FAIL;
                        }

                        ;
                        if (auto pStreamWriter = std::dynamic_pointer_cast<TableOutput::IStreamWriter>(pW);
                            pStreamWriter && pStreamWriter->GetStream())
                        {
                            Concurrency::send(
                                m_messageBuf,
                                ArchiveMessage::MakeAddStreamRequest(
                                    szOutputFile, pStreamWriter->GetStream(), true, 0L));
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

            BOOST_SCOPE_EXIT(&output, this_) { this_->CloseAll(output); }
            BOOST_SCOPE_EXIT_END;

            for (auto& item : m_outputs)
            {
                BOOST_SCOPE_EXIT(&output, &item, this_) { this_->CloseOne(output, item); }
                BOOST_SCOPE_EXIT_END;

                HRESULT hr = E_FAIL;
                // Actually enumerate objects here

                if (FAILED(hr = aCallback(item)))
                {
                    log::Error(
                        _L_, hr, L"Failed during callback on output item %s\r\n", item.first.GetIdentifier().c_str());
                }
            }
            return S_OK;
        }

        HRESULT AddFile(
            const OutputSpec& output,
            const std::wstring& szNameInArchive,
            const std::wstring& szFileName,
            bool bHashData,
            DWORD dwXORPattern,
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
                                szNameInArchive, szFileName, bHashData, dwXORPattern, bDeleteWhenDone));
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
            DWORD dwXORPattern,
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
                    log::Error(_L_, E_FAIL, L"Complete archive operation has timed out\r\n");
                    return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                }
            }
            return S_OK;
        }
    };

private:
    SYSTEMTIME theStartTime;
    SYSTEMTIME theFinishTime;
    DWORD theStartTickCount;
    DWORD theFinishTickCount;

protected:
    logger _L_;

    std::vector<std::shared_ptr<ExtensionLibrary>> m_extensions;
    HRESULT LoadCommonExtensions();
    HRESULT LoadWinTrust();
    HRESULT LoadEvtLibrary();
    HRESULT LoadPSAPI();

    HRESULT SaveAndPrintStartTime();
    HRESULT PrintSystemType();
    HRESULT PrintSystemTags();
    HRESULT PrintComputerName();
    HRESULT PrintWhoAmI();
    HRESULT PrintOperatingSystem();
    HRESULT PrintExecutionTime();
    LPCWSTR GetEncoding(OutputSpec::Encoding anEncoding);
    LPCWSTR GetXOR(DWORD dwXOR);
    LPCWSTR GetCompression(const std::wstring& strCompression);
    HRESULT PrintOutputOption(const OutputSpec& anOutput) { return PrintOutputOption(L"Output", anOutput); }
    HRESULT PrintOutputOption(LPCWSTR szOutputName, const OutputSpec& anOutput);
    HRESULT PrintBooleanOption(LPCWSTR szOptionName, bool bValue);
    HRESULT PrintBooleanOption(LPCWSTR szOptionName, const boost::logic::tribool& bValue);
    HRESULT PrintIntegerOption(LPCWSTR szOptionName, DWORD dwOption);
    HRESULT PrintIntegerOption(LPCWSTR szOptionName, LONGLONG llOption);
    HRESULT PrintIntegerOption(LPCWSTR szOptionName, ULONGLONG ullOption);
    HRESULT PrintHashAlgorithmOption(LPCWSTR szOptionName, CryptoHashStream::Algorithm algs);
    HRESULT PrintHashAlgorithmOption(LPCWSTR szOptionName, FuzzyHashStream::Algorithm algs);

    HRESULT PrintStringOption(LPCWSTR szOptionName, LPCWSTR szOption);
    HRESULT PrintFormatedStringOption(LPCWSTR szOptionName, LPCWSTR szFormat, va_list argList);
    HRESULT PrintFormatedStringOption(LPCWSTR szOptionName, LPCWSTR szFormat, ...);

    //
    // Option handling
    //
    bool OutputOption(LPCWSTR szArg, LPCWSTR szOption, OutputSpec::Kind supportedTypes, OutputSpec& anOutput);
    bool OutputOption(LPCWSTR szArg, LPCWSTR szOption, OutputSpec& anOutput)
    {
        return OutputOption(szArg, szOption, anOutput.supportedTypes, anOutput);
    };

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
                log::Error(_L_, E_INVALIDARG, L"Option /%s should be like: /%s=<Value>\r\n", szOption, szOption);
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

public:
    UtilitiesMain(logger pLog)
        : _L_(std::move(pLog))
    {
        ZeroMemory(&theStartTime, sizeof(SYSTEMTIME));
        ZeroMemory(&theFinishTime, sizeof(SYSTEMTIME));
        theStartTickCount = 0L;
        theFinishTickCount = 0L;
        m_extensions.reserve(10);
    };

    HRESULT ReadConfiguration(
        int argc,
        LPCWSTR argv[],
        LPCWSTR szCmdLineOption,
        LPCWSTR szResourceID,
        LPCWSTR szRefResourceID,
        LPCWSTR szConfigExt,
        ConfigItem& configitem,
        ConfigItem::InitFunction init);

    bool IsProcessParent(LPCWSTR szImageName);

    //
    //
    //
    virtual HRESULT GetConfigurationFromConfig(const ConfigItem& configitem) = 0;
    virtual HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem) = 0;
    virtual HRESULT GetSchemaFromConfig(const ConfigItem&) { return S_OK; };
    virtual HRESULT GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[]) = 0;
    virtual HRESULT CheckConfiguration() = 0;

    //
    // Output handling
    //
    virtual void PrintUsage() = 0;
    virtual void PrintLoggingUsage();
    virtual void PrintPriorityUsage();
    virtual void PrintCommonUsage() {
        PrintLoggingUsage();
        PrintPriorityUsage();
    }
    virtual void PrintHeader(LPCWSTR szToolName, LPCWSTR szVersion);
    virtual void PrintParameters() = 0;
    virtual void PrintFooter() = 0;

    template <class UtilityT>
    static int WMain(int argc, const WCHAR* argv[])
    {
        Robustness::Initialize(UtilityT::ToolName());

        HRESULT hr = E_FAIL;
        logger _L_;

        try
        {
            _L_ = std::make_shared<LogFileWriter>();
            LogFileWriter::Initialize(_L_);
            LogFileWriter::ConfigureLoggingOptions(argc, argv, _L_);
        }
        catch (std::exception& e)
        {
            std::cerr << "std::exception during LogFileWrite initialisation" << std::endl;
            std::cerr << "Caught " << e.what() << std::endl;
            std::cerr << "Type " << typeid(e).name() << std::endl;
            return E_ABORT;
        }
        catch (...)
        {
            std::cerr << "Exception during LogFileWrite initialisation" << std::endl;
            return E_ABORT;
        }

        if (FAILED(hr = CoInitializeEx(0, COINIT_MULTITHREADED)))
        {
            log::Error(_L_, hr, L"Failed to initialize COM library\r\n");
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
            log::Warning(_L_, hr, L"Failed to initialize COM security\r\n");
        }

        UtilityT Cmd(_L_);

        Cmd.WaitForDebugger(argc, argv);

        Cmd.LoadCommonExtensions();

        Cmd.PrintHeader(UtilityT::ToolDescription(), ORC_FILEVER_STRINGW);

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
                    return hr;
                if (FAILED(hr = Cmd.GetSchemaFromConfig(schemaitem)))
                    return hr;
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
                    return hr;
                if (FAILED(hr = Cmd.GetConfigurationFromConfig(configitem)))
                    return hr;
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
                    return hr;
            }

            if (FAILED(hr = Cmd.GetConfigurationFromArgcArgv(argc, argv)))
                return hr;

            if (FAILED(hr = Cmd.CheckConfiguration()))
                return hr;
        }
        catch (std::exception& e)
        {
            std::cerr << "std::exception during configuration evaluation" << std::endl;
            std::cerr << "Caught " << e.what() << std::endl;
            std::cerr << "Type " << typeid(e).name() << std::endl;
            return E_ABORT;
        }
        catch (...)
        {
            std::cerr << "Exception during configuration evaluation" << std::endl;
            return E_ABORT;
        }

        Cmd.PrintParameters();

        try
        {
            if (FAILED(hr = Cmd.Run()))
            {
                return hr;
            }
        }
        catch (std::exception& e)
        {
            std::cerr << "std::exception during execution" << std::endl;
            std::cerr << "Caught " << e.what() << std::endl;
            std::cerr << "Type " << typeid(e).name() << std::endl;
            return E_ABORT;
        }
        catch (...)
        {
            std::cerr << "Exception during during execution" << std::endl;
            return E_ABORT;
        }

        Cmd.PrintFooter();

        DWORD dwErrorCount = _L_->GetErrorCount();

        if (dwErrorCount > 0)
        {
            log::Info(_L_, L"\r\nInformation           : %d errors occurred during program execution\r\n", dwErrorCount);
        }

        {
            // if parent is 'explorer' or debugger attached press any key to continue
            if (IsDebuggerPresent())
            {
                DebugBreak();
            }
            else if (Cmd.IsProcessParent(L"explorer.exe"))
            {
                log::Info(_L_, L"Press any key to continue . . . ");
                _getch();
            }
        }

        _L_->Close();
        Robustness::UnInitialize(INFINITE);
        return dwErrorCount;
    }

    ~UtilitiesMain(void);
};
}  // namespace Command
}  // namespace Orc

#pragma managed(pop)
