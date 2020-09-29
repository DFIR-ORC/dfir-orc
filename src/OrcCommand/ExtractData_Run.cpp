//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            fabienfl
//

#include "stdafx.h"

#include <filesystem>

#include "ExtractData.h"

#include "ConfigFileReader.h"
#include "ConfigFile_Common.h"
#include "ImportAgent.h"
#include "ImportMessage.h"
#include "ImportNotification.h"
#include "CommandAgent.h"
#include "SystemDetails.h"

using namespace concurrency;
namespace fs = std::filesystem;

using namespace Orc;
using namespace Orc::Command::ExtractData;

namespace {

void WriteSuccessfulReport(TableOutput::IWriter& writer, const ImportNotification::Notification& notification)
{
    const auto& item = notification->Item();
    auto& reportTable = writer;

    SystemDetails::WriteComputerName(reportTable);

    if (item.inputFile != nullptr)
        reportTable.WriteString(*item.inputFile);
    else
        reportTable.WriteNothing();

    reportTable.WriteString(item.name);
    reportTable.WriteString(item.fullName);

    switch (notification->Action())
    {
        case ImportNotification::Extract:
            reportTable.WriteString(L"Extract");
            break;
        default:
            reportTable.WriteString(L"Unknown");
            break;
    }

    reportTable.WriteString(item.systemType);
    reportTable.WriteString(item.computerName);
    reportTable.WriteString(item.timeStamp);

    FILETIME start, end;
    SystemTimeToFileTime(&item.importStart, &start);
    reportTable.WriteFileTime(start);

    SystemTimeToFileTime(&item.importEnd, &end);
    reportTable.WriteFileTime(end);

    switch (notification->Action())
    {
        case ImportNotification::Extract:
            reportTable.WriteNothing();
            break;
        default:
            reportTable.WriteNothing();
            break;
    }

    switch (notification->Action())
    {
        case ImportNotification::Extract:
            if (item.outputFile)
            {
                reportTable.WriteString(*item.outputFile);
            }
            else
                reportTable.WriteNothing();
            break;
        default:
            reportTable.WriteNothing();
            break;
    }

    reportTable.WriteInteger(item.ullBytesExtracted);
    reportTable.WriteInteger(static_cast<DWORD>(notification->GetHR()));
    reportTable.WriteEndOfLine();
}

void WriteFailureReport(TableOutput::IWriter& writer, const ImportNotification::Notification& notification)
{
    const auto& item = notification->Item();
    TableOutput::IOutput& reportTable = writer;

    SystemDetails::WriteComputerName(reportTable);

    if (item.inputFile != nullptr)
        reportTable.WriteString(*item.inputFile);
    else
        reportTable.WriteNothing();

    reportTable.WriteString(item.name);
    reportTable.WriteString(item.fullName);

    reportTable.WriteString(L"Extract");

    reportTable.WriteNothing();
    reportTable.WriteNothing();
    reportTable.WriteNothing();

    reportTable.WriteNothing();
    reportTable.WriteNothing();

    reportTable.WriteNothing();

    reportTable.WriteNothing();

    reportTable.WriteNothing();
    reportTable.WriteNothing();

    reportTable.WriteInteger((DWORD)notification->GetHR());

    reportTable.WriteEndOfLine();
}

void WriteReport(TableOutput::IWriter& writer, const ImportNotification::Notification& notification)
{
    if (FAILED(notification->GetHR()))
    {
        WriteFailureReport(writer, notification);
        return;
    }

    WriteSuccessfulReport(writer, notification);
}

HRESULT ExecuteAgent(ImportAgent& agent)
{
    HRESULT hr = E_FAIL;

    spdlog::info(L"Extracting data...");

    if (!agent.start())
    {
        spdlog::critical(L"Failed to start import Agent failed");
        return E_FAIL;
    }

    try
    {
        concurrency::agent::wait(&agent);
    }
    catch (concurrency::operation_timed_out timeout)
    {
        spdlog::critical("Timed out while waiting for agent completion");
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }

    return S_OK;
}

}  // namespace

HRESULT
Main::AddDirectoryForInput(const fs::path& path, const InputItem& input, std::vector<ImportItem>& inputPaths)
{
    HRESULT hr = E_FAIL;
    std::error_code ec;

    fs::path root(path);
    root = canonical(root, ec);
    if (ec)
    {
        return HRESULT_FROM_WIN32(hr);
    }

    auto dirIt = fs::recursive_directory_iterator(root, ec);
    if (ec)
    {
        return HRESULT_FROM_WIN32(hr);
    }

    if (!config.bRecursive)
    {
        dirIt.disable_recursion_pending();
    }

    fs::recursive_directory_iterator end_itr;
    for (; dirIt != end_itr; dirIt++)
    {
        hr = AddFileForInput(*dirIt, input, inputPaths);
        if (FAILED(hr))
        {
            spdlog::warn("Failed to process '{}'", dirIt->path());
        }
    }

    return S_OK;
}

HRESULT Main::AddFileForInput(const fs::path& path, const InputItem& input, std::vector<ImportItem>& inputPaths)
{
    std::error_code ec;

    const auto isRegularFile = is_regular_file(path, ec);
    if (ec)
    {
        return HRESULT_FROM_WIN32(ec.value());
    }

    if (!isRegularFile)
    {
        return S_OK;
    }

    fs::path root(fs::canonical(path));
    const auto pathFromRoot = fs::relative(path, root, ec);
    if (ec)
    {
        return HRESULT_FROM_WIN32(ec.value());
    }

    ImportItem item;

    item.inputFile = std::make_shared<std::wstring>(path);
    item.fullName = pathFromRoot;
    item.name = path.filename();
    item.definitions = &input.importDefinitions;
    item.definitionItem = item.DefinitionItemLookup(ImportDefinition::Extract);
    //item.definitionItem = ImportDefinition::Extract;
    item.format = item.GetFileFormat();
    item.isToExtract = true;
    item.isToIgnore = false;
    item.isToExpand = false;

    if (input.matchRegex.empty())
    {
        spdlog::debug(L"Input file added: '{}'", item.name);
        inputPaths.push_back(item);
        return S_OK;
    }

    HRESULT hr = CommandAgent::ReversePattern(
        input.matchRegex, item.name, item.systemType, item.fullComputerName, item.computerName, item.timeStamp);

    if (SUCCEEDED(hr))
    {
        spdlog::debug(L"Input file added: '{}'", item.name);
        inputPaths.push_back(std::move(item));
        return S_OK;
    }

    spdlog::debug(L"Input file '{}' does not match '{}'", item.name, input.matchRegex);
    return S_OK;
}

std::vector<ImportItem> Main::GetInputFiles(const Main::InputItem& input)
{
    if (input.path.empty())
    {
        return {};
    }

    std::error_code ec;
    HRESULT hr = E_FAIL;

    const auto isDirectory = fs::is_directory(input.path, ec);
    if (ec)
    {
        spdlog::warn(L"Invalid input '{}' specified (code: {:#x})", input.path, ec.value());
        return {};
    }

    std::vector<ImportItem> items;
    if (isDirectory)
    {
        hr = AddDirectoryForInput(input.path, input, items);
    }
    else
    {
        hr = AddFileForInput(input.path, input, items);
    }

    if (FAILED(hr))
    {
        spdlog::error(L"Failed to add input files for '{}' (code: {:#x})", input.path, hr);
        return {};
    }

    return items;
}

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    hr = LoadWinTrust();
    if (FAILED(hr))
    {
        return hr;
    }

    auto reportWriter = TableOutput::GetWriter(config.reportOutput);
    if (reportWriter == nullptr)
    {
        spdlog::debug("No report writer, extraction report will be disabled");
    }

    m_notificationCb = std::make_unique<call<ImportNotification::Notification>>(
        [this, &reportWriter](const ImportNotification::Notification& notification) {
            m_ullProcessedBytes += notification->Item().ullBytesExtracted;

            if (reportWriter)
            {
                ::WriteReport(*reportWriter, notification);
            }
        });

    auto importAgent = std::make_unique<ImportAgent>(m_importRequestBuffer, m_importRequestBuffer, *m_notificationCb);

    hr = importAgent->InitializeOutputs(config.output, OutputSpec(), config.tempOutput);
    if (FAILED(hr))
    {
        spdlog::error("Failed to initialize import agent output (code: {:#x}", hr);
        return hr;
    }

    //::QueueExtractions(*importAgent, _L_);
    m_console.Print(L"Enumerate input files...");

    ULONG ulInputFiles = 0L;
    for (const auto& inputItem : config.inputItems)
    {
        auto files = GetInputFiles(inputItem);
        for (auto& file : files)
        {
            ++ulInputFiles;
            auto expand = ImportMessage::MakeExpandRequest(std::move(file));
            importAgent->SendRequest(expand);
        }
    }

    m_console.Print(L"Queued {} input files for extraction", ulInputFiles);

    hr = ::ExecuteAgent(*importAgent);
    if (FAILED(hr))
    {
        spdlog::error("Failed while extracting");
        return hr;
    }

    importAgent->LogStatistics();

    if (reportWriter)
    {
        reportWriter->Close();
    }

    return S_OK;
}
