//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "ImportData.h"

#include "ConfigFileReader.h"
#include "ConfigFile_Common.h"

#include "ImportAgent.h"
#include "ImportMessage.h"
#include "ImportNotification.h"
#include "LogFileWriter.h"

#include "CommandAgent.h"

#include "SystemDetails.h"

#include <filesystem>

using namespace std;
using namespace concurrency;
namespace fs = std::filesystem;

using namespace Orc;
using namespace Orc::Command::ImportData;

namespace {

void WriteSuccessfulReport(TableOutput::IWriter& writer, const ImportNotification::Notification& notification)
{
    auto& output = writer.GetTableOutput();

    SystemDetails::WriteComputerName(output);

    if (notification->Item().inputFile != nullptr)
        output.WriteString(notification->Item().inputFile->c_str());
    else
        output.WriteNothing();

    output.WriteString(notification->Item().name.c_str());
    output.WriteString(notification->Item().fullName.c_str());

    switch (notification->Action())
    {
        case ImportNotification::Import:
            output.WriteString(L"Import");
            break;
        case ImportNotification::Extract:
            output.WriteString(L"Extract");
            break;
        default:
            output.WriteString(L"Unknown");
            break;
    }

    output.WriteString(notification->Item().systemType.c_str());
    output.WriteString(notification->Item().computerName.c_str());
    output.WriteString(notification->Item().timeStamp.c_str());

    FILETIME start, end;
    SystemTimeToFileTime(&notification->Item().importStart, &start);
    output.WriteFileTime(start);

    SystemTimeToFileTime(&notification->Item().importEnd, &end);
    output.WriteFileTime(end);

    switch (notification->Action())
    {
        case ImportNotification::Import:
            if (notification->Item().definitionItem)
                output.WriteString(notification->Item().definitionItem->tableName.c_str());
            else
                output.WriteNothing();
            break;
        case ImportNotification::Extract:
            output.WriteNothing();
            break;
        default:
            output.WriteNothing();
            break;
    }

    switch (notification->Action())
    {
        case ImportNotification::Import:
            output.WriteNothing();
            break;
        case ImportNotification::Extract:
            if (notification->Item().outputFile)
            {
                output.WriteString(notification->Item().outputFile->c_str());
            }
            else
                output.WriteNothing();
            break;
        default:
            output.WriteNothing();
            break;
    }

    output.WriteInteger(notification->Item().ullLinesImported);
    output.WriteInteger(notification->Item().ullBytesExtracted);

    output.WriteInteger((DWORD)notification->GetHR());

    output.WriteEndOfLine();
}

void WriteFailureReport(TableOutput::IWriter& writer, const ImportNotification::Notification& notification)
{
    auto& output = writer.GetTableOutput();

    SystemDetails::WriteComputerName(output);

    if (notification->Item().inputFile != nullptr)
        output.WriteString(notification->Item().inputFile->c_str());
    else
        output.WriteNothing();

    output.WriteString(notification->Item().name.c_str());
    output.WriteString(notification->Item().fullName.c_str());

    output.WriteString(L"Import");

    output.WriteNothing();
    output.WriteNothing();
    output.WriteNothing();

    output.WriteNothing();
    output.WriteNothing();

    output.WriteNothing();

    output.WriteNothing();

    output.WriteNothing();
    output.WriteNothing();

    output.WriteInteger((DWORD)notification->GetHR());

    output.WriteEndOfLine();
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

}  // namespace

HRESULT
Main::AddDirectoryForInput(const std::wstring& dir, const InputItem& input, std::vector<ImportItem>& input_paths)
{
    fs::recursive_directory_iterator end_itr;

    fs::path root(dir);
    root = canonical(root);

    auto dir_iter = fs::recursive_directory_iterator(root);

    auto offset = root.wstring().size() + 1;

    if (!config.bRecursive)
        dir_iter.disable_recursion_pending();

    for (; dir_iter != end_itr; dir_iter++)
    {
        auto& entry = *dir_iter;

        if (is_regular_file(entry.status()))
        {
            ImportItem item;

            item.inputFile = make_shared<wstring>(entry.path().wstring());
            item.fullName = item.inputFile->substr(offset);
            item.name = entry.path().filename().wstring();
            item.definitions = &input.ImportDefinitions;
            item.definitionItem = item.DefinitionItemLookup(ImportDefinition::Expand);
            item.format = item.GetFileFormat();
            item.isToExtract = false;
            item.isToIgnore = false;
            item.isToImport = false;
            item.isToExpand = true;

            if (!input.NameMatch.empty())
            {
                if (SUCCEEDED(CommandAgent::ReversePattern(
                        input.NameMatch,
                        item.name,
                        item.systemType,
                        item.fullComputerName,
                        item.computerName,
                        item.timeStamp)))
                {
                    log::Verbose(_L_, L"Input file added: %s\r\n", item.name.c_str());
                    input_paths.push_back(std::move(item));
                }
                else
                {
                    log::Verbose(
                        _L_, L"Input file %s does not match %s\r\n", item.name.c_str(), input.NameMatch.c_str());
                }
            }
            else
            {
                log::Verbose(_L_, L"Input file added: %s\r\n", item.name.c_str());
                input_paths.push_back(item);
            }
        }
    }
    return S_OK;
}

HRESULT Main::AddFileForInput(const std::wstring& file, const InputItem& input, std::vector<ImportItem>& input_paths)
{
    fs::path entry(file);

    if (is_regular_file(entry))
    {
        ImportItem item;

        item.inputFile = make_shared<wstring>(entry.wstring());

        item.fullName = entry.relative_path().wstring();
        item.name = entry.filename().wstring();
        item.definitions = &input.ImportDefinitions;
        item.format = item.GetFileFormat();
        item.isToExtract = true;
        item.isToIgnore = false;
        item.isToImport = false;
        item.isToExpand = false;

        if (!input.NameMatch.empty())
        {
            auto filename = entry.filename().wstring();

            if (SUCCEEDED(CommandAgent::ReversePattern(
                    input.NameMatch,
                    filename,
                    item.systemType,
                    item.fullComputerName,
                    item.computerName,
                    item.timeStamp)))
            {
                log::Verbose(_L_, L"Input file added: %s\r\n", filename.c_str());
                input_paths.push_back(std::move(item));
            }
            else
            {
                log::Verbose(_L_, L"Input file %s does not match %s\r\n", filename.c_str(), input.NameMatch.c_str());
            }
        }
        else
        {
            log::Verbose(_L_, L"Input file added: %s\r\n", item.name.c_str());
            input_paths.push_back(std::move(item));
        }
    }
    return S_OK;
}

std::vector<ImportItem> Main::GetInputFiles(const Main::InputItem& input)
{
    HRESULT hr = E_FAIL;

    std::vector<ImportItem> items;

    if (!input.InputDirectory.empty())
    {
        if (FAILED(hr = AddDirectoryForInput(input.InputDirectory, input, items)))
        {
            log::Error(_L_, hr, L"Failed to add input files for %s\r\n", input.InputDirectory.c_str());
        }
    }

    if (!config.strInputDirs.empty())
    {
        for (auto& dir : config.strInputDirs)
        {
            if (FAILED(hr = AddDirectoryForInput(dir, input, items)))
            {
                log::Error(_L_, hr, L"Failed to add input files for %s\r\n", dir.c_str());
            }
        }
    }

    if (!config.strInputFiles.empty())
    {
        for (auto& file : config.strInputFiles)
        {
            if (FAILED(hr = AddFileForInput(file, input, items)))
            {
                log::Error(_L_, hr, L"Failed to add input file %s\r\n", file.c_str());
            }
        }
    }
    return items;
}

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = LoadWinTrust()))
        return hr;

    auto reportWriter = TableOutput::GetWriter(_L_, config.reportOutput);
    if (reportWriter == nullptr)
    {
        log::Warning(_L_, E_FAIL, L"No writer for ImportData output: no data about imports will be generated\r\n");
    }

    m_notificationCb = std::make_unique<call<ImportNotification::Notification>>(
        [this, &reportWriter](const ImportNotification::Notification& notification) mutable {
            m_ullImportedLines += notification->Item().ullLinesImported;
            m_ullProcessedBytes += notification->Item().ullBytesExtracted;

            if (reportWriter)
            {
                ::WriteReport(*reportWriter, notification);
            }
        });

    if (!config.m_Tables.empty())
    {
        log::Info(_L_, L"\r\nTables configuration :\r\n");
        for (const auto& table : config.m_Tables)
        {
            LPWSTR szDisp = nullptr;
            switch (table.Disposition)
            {
                case AsIs:
                    szDisp = L"as is";
                    break;
                case Truncate:
                    szDisp = L"truncate";
                    break;
                case CreateNew:
                    szDisp = L"create new";
                    break;
                default:
                    break;
            }

            log::Info(
                _L_,
                L"\t%s : %s, %s, %s with %d concurrent agents\r\n",
                table.name.c_str(),
                szDisp,
                table.bCompress ? L"compression" : L"no compression",
                table.bTABLOCK ? L"table lock" : L"rows lock",
                table.dwConcurrency);
        }
        log::Info(_L_, L"\r\n\r\n");
    }

    m_importAgent = std::make_unique<ImportAgent>(_L_, m_importRequestBuffer, m_importRequestBuffer, *m_notificationCb);

    if (FAILED(
            hr = m_importAgent->InitializeOutputs(
                config.extractOutput, config.importOutput, config.tempOutput)))
    {
        log::Error(_L_, hr, L"Failed to initialize import agent output\r\n");
        return hr;
    }

    if (!config.m_Tables.empty())
    {
        log::Info(_L_, L"\r\nInitialize tables...");
        if (FAILED(hr = m_importAgent->InitializeTables(config.m_Tables)))
        {
            log::Error(_L_, hr, L"\r\nFailed to initialize import agent table definitions\r\n");
            return hr;
        }
        log::Info(_L_, L" Done\r\n");
    }

    log::Info(_L_, L"\r\nEnumerate input files tables...");
    ULONG ulInputFiles = 0L;

    vector<vector<ImportItem>> inputs;
    inputs.reserve(config.Inputs.size());

    for (auto& input : config.Inputs)
    {
        inputs.push_back(GetInputFiles(input));
    }

    auto it = std::max_element(
        begin(inputs), end(inputs), [](const vector<ImportItem>& one, const vector<ImportItem>& two) -> bool {
            return one.size() < two.size();
        });

    auto dwMax = it->size();

    for (unsigned int i = 0; i < dwMax; i++)
    {
        for (auto& input : inputs)
        {
            if (input.size() > i)
            {
                m_importAgent->SendRequest(ImportMessage::MakeExpandRequest(move(input[i])));
                ulInputFiles++;
            }
        }
    }

    log::Info(_L_, L" Done (%d input files)\r\n\r\n", ulInputFiles);

    log::Info(_L_, L"\r\nImporting data...\r\n");

    if (!m_importAgent->start())
    {
        log::Error(_L_, E_FAIL, L"Start for import Agent failed\r\n");
        return E_FAIL;
    }

    try
    {
        concurrency::agent::wait(m_importAgent.get());
    }
    catch (concurrency::operation_timed_out timeout)
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT), L"Timed out while waiting for agent completion");
        return hr;
    }

    if (!config.m_Tables.empty())
    {
        log::Info(_L_, L"\r\nAfter statements\r\n");
        if (FAILED(hr = m_importAgent->FinalizeTables()))
        {
            log::Error(_L_, hr, L"Failed to finalize import tables\r\n");
        }
    }

    log::Info(_L_, L"\r\nSome statistics\r\n)");
    m_importAgent->Statistics(_L_);

    m_importAgent.release();

    if (reportWriter)
    {
        reportWriter->Close();
    }

    return S_OK;
}
