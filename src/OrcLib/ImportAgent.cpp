//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "ImportAgent.h"

#include "ImportMessage.h"
#include "ImportNotification.h"

#include "ImportDefinition.h"

#include "TemporaryStream.h"
#include "MemoryStream.h"
#include "JournalingStream.h"
#include "DecodeMessageStream.h"
#include "DevNullStream.h"
#include "FileStream.h"

#include "CsvFileReader.h"
#include "CsvToSql.h"

#include "ArchiveExtract.h"

#include "ParameterCheck.h"

#include "ConfigFileReader.h"
#include "ConfigFile_Common.h"
#include "EmbeddedResource.h"

#include "EvtLibrary.h"

#include "LogFileWriter.h"

#include "Temporary.h"

#include "Strings.h"
#include "SystemDetails.h"

#include <filesystem>
#include <sstream>
#include <boost/scope_exit.hpp>

#include <boost/algorithm/string/replace.hpp>

using namespace std;
using namespace Concurrency;

namespace fs = std::filesystem;

using namespace Orc;

ImportMessage::Message ImportAgent::TriageNewItem(const ImportItem& input, ImportItem& newItem)
{
    ImportMessage::Message retval;

    newItem.definitions = input.definitions;

    if (!newItem.IsToIgnore())
    {
        newItem.Stream->SetFilePointer(0LL, FILE_BEGIN, NULL);

        if (newItem.GetFileFormat() == ImportItem::ImportItemFormat::Archive)
        {
            newItem.DefinitionItemLookup(ImportDefinition::Expand);
        }

        newItem.ullBytesExtracted += input.Stream->GetSize();

        newItem.computerName = input.computerName;
        newItem.systemType = input.systemType;
        newItem.timeStamp = input.timeStamp;
        newItem.inputFile = input.inputFile;

        auto pFinalTempStream = std::dynamic_pointer_cast<TemporaryStream>(newItem.Stream);

        if (pFinalTempStream)
        {
            if (pFinalTempStream->IsFileStream() == S_OK)
                newItem.ullFileBytesCharged = pFinalTempStream->GetSize();
            if (pFinalTempStream->IsMemoryStream() == S_OK)
                newItem.ullMemBytesCharged = pFinalTempStream->GetSize();
        }
        else
            newItem.ullFileBytesCharged = newItem.Stream->GetSize();

        if (Archive::GetArchiveFormat(newItem.name) != ArchiveFormat::Unknown)
        {
            newItem.isToExtract = true;
            newItem.isToImport = false;
            retval = ImportMessage::MakeExtractRequest(std::move(newItem));
            log::Verbose(_L_, L"\tArchive %s has been extracted\r\n", newItem.name.c_str());
        }
        else
        {
            if (newItem.IsToImport())
            {
                retval = ImportMessage::MakeImportRequest(std::move(newItem));
                log::Verbose(_L_, L"\tArchive %s has been extracted\r\n", newItem.name.c_str());
            }
            else if (newItem.IsToExtract())
            {
                retval = ImportMessage::MakeExtractRequest(std::move(newItem));
                log::Verbose(_L_, L"\tArchive %s has been extracted\r\n", newItem.name.c_str());
            }
        }
    }
    return retval;
}

HRESULT ImportAgent::UnWrapMessage(
    const std::shared_ptr<ByteStream>& pMessageStream,
    const std::shared_ptr<ByteStream>& pOutputStream)
{
    HRESULT hr = E_FAIL;

    auto pDecodeStream = std::make_shared<DecodeMessageStream>(_L_);

    if (FAILED(hr = pDecodeStream->Initialize(pOutputStream)))
    {
        log::Error(_L_, hr, L"Failed to initialise decoding stream to unwrap message\r\n");
        return hr;
    }

    ULONGLONG cbBytesWritten = 0LL;

    if (FAILED(hr = pMessageStream->CopyTo(pDecodeStream, &cbBytesWritten)))
    {
        log::Error(_L_, hr, L"Failed to unwrap envelopped message\r\n");
        return hr;
    }
    else
    {
        PCCERT_CONTEXT pDecryptor = pDecodeStream->GetDecryptor();
        if (pDecryptor)
        {
            std::wstring strSubjectName;
            MessageStream::CertNameToString(&pDecryptor->pCertInfo->Subject, CERT_SIMPLE_NAME_STR, strSubjectName);
            log::Verbose(_L_, L"Successfully unwrapped message with \"%s\"'s certificate\r\n", strSubjectName.c_str());
        }
    }
    return S_OK;
}

HRESULT ImportAgent::EnveloppedItem(ImportItem& input)
{
    HRESULT hr = E_FAIL;

    if (input.GetFileFormat() != ImportItem::Envelopped)
        return E_INVALIDARG;

    GetSystemTime(&input.importStart);

    BOOST_SCOPE_EXIT(&input) { GetSystemTime(&input.importEnd); }
    BOOST_SCOPE_EXIT_END;

    auto pTempStream = std::make_shared<TemporaryStream>(_L_);
    if (FAILED(hr = pTempStream->Open(m_tempOutput.Path.c_str(), input.name, 100 * 1024 * 1024, false)))
    {
        log::Error(_L_, hr, L"Failed to open temporary stream\r\n");
        return hr;
    }

    auto pEnveloppedStream = input.GetInputStream(_L_);
    if (!pEnveloppedStream)
    {
        log::Error(_L_, hr, L"Failed to open envelopped stream\r\n");
        return hr;
    }

    if (FAILED(hr = UnWrapMessage(pEnveloppedStream, pTempStream)))
    {
        log::Error(_L_, hr, L"Failed to unwrap message\r\n");
        return hr;
    }

    if (FAILED(hr = pTempStream->SetFilePointer(0LL, FILE_BEGIN, NULL)))
    {
        log::Error(_L_, hr, L"Failed to rewind unwrapped stream\r\n");
        return hr;
    }

    // Functions GetFileFormat() and Archive::GetArchiveFormat() expect a '7z' extension
    const wchar_t ext7z[] = L".7z";
    auto name = input.GetBaseName(_L_);
    if (!EndsWith(name, ext7z))
    {
        name.append(ext7z);
    }

    ImportItem output;
    output.inputFile = input.inputFile;
    output.definitionItem = nullptr;
    output.definitions = input.definitions;
    output.name = name;
    output.fullName = output.name;
    output.format = output.GetFileFormat();

    output.computerName = input.computerName;
    output.systemType = input.systemType;
    output.timeStamp = input.timeStamp;
    output.inputFile = input.inputFile;

    if (JournalingStream::IsStreamJournalized(_L_, pTempStream) == S_OK)
    {
        auto pOutputStream = std::make_shared<TemporaryStream>(_L_);
        if (FAILED(hr = pOutputStream->Open(m_tempOutput.Path.c_str(), output.name, 100 * 1024 * 1024, false)))
        {
            log::Error(_L_, hr, L"Failed to open temporary archive\r\n");
            return hr;
        }

        if (FAILED(hr = JournalingStream::ReplayJournalStream(_L_, pTempStream, pOutputStream)))
        {
            log::Error(_L_, hr, L"Failed to replay journaled archive\r\n");
            return hr;
        }
        pTempStream->Close();

        if (FAILED(hr = pOutputStream->SetFilePointer(0LL, FILE_BEGIN, NULL)))
        {
            log::Error(_L_, hr, L"Failed to rewind output stream\r\n");
            return hr;
        }
        output.Stream = pOutputStream;

        input.ullBytesExtracted = pOutputStream->GetSize();

        pTempStream->Close();
        pTempStream.reset();
    }
    else
    {
        input.ullBytesExtracted = pTempStream->GetSize();
        output.Stream = pTempStream;
    }

    auto pFinalTempStream = std::dynamic_pointer_cast<TemporaryStream>(output.Stream);

    if (pFinalTempStream)
    {
        if (pFinalTempStream->IsFileStream())
            output.ullFileBytesCharged = pFinalTempStream->GetSize();
        if (pFinalTempStream->IsMemoryStream())
            output.ullMemBytesCharged = pFinalTempStream->GetSize();
    }
    else
        output.ullFileBytesCharged = output.Stream->GetSize();

    if (output.IsToExpand())
        SendRequest(ImportMessage::MakeExpandRequest(output));
    else if (output.IsToExtract())
        SendRequest(ImportMessage::MakeExtractRequest(output));
    return S_OK;
}

HRESULT ImportAgent::ExpandItem(ImportItem& input)
{
    HRESULT hr = E_FAIL;

    if (input.GetFileFormat() != ImportItem::Archive)
        return E_INVALIDARG;

    GetSystemTime(&input.importStart);

    BOOST_SCOPE_EXIT(&input) { GetSystemTime(&input.importEnd); }
    BOOST_SCOPE_EXIT_END;

    auto archiveFormat = Archive::GetArchiveFormat(input.name);

    if (archiveFormat == ArchiveFormat::Unknown)
    {
        log::Error(_L_, hr, L"Unrecognised archive format for %s\r\n", input.name.c_str());
        return hr;
    }

    auto extractor = ArchiveExtract::MakeExtractor(archiveFormat, _L_);

    log::Verbose(_L_, L"Extracting archive\r\n");

    if (input.definitionItem)
    {
        if (!input.definitionItem->Password.empty())
        {
            extractor->SetPassword(input.definitionItem->Password);
        }
    }

    std::vector<ImportItem> tempItems;

    extractor->SetCallback([this, &input, &tempItems](const Archive::ArchiveItem& item) {
        wstring strItemName;
        fs::path path(input.name);
        auto strBaseName = path.stem().wstring();

        if (input.bPrefixSubItem)
        {
            strItemName = strBaseName + L"\\" + item.NameInArchive;
        }
        else
        {
            strItemName = item.NameInArchive;
        }

        auto found = std::find_if(begin(tempItems), end(tempItems), [&strItemName](const ImportItem& tempItem) -> bool {
            return tempItem.name == strItemName;
        });

        if (found == end(tempItems))
        {
            log::Error(
                _L_, HRESULT_FROM_WIN32(ERROR_NO_MATCH), L"Could not find a temp item for %s\r\n", strItemName.c_str());
        }
        else
        {
            if (input.bPrefixSubItem)
            {
                fs::path input_path(input.fullName);
                found->fullName = input_path.parent_path().wstring() + L"\\" + input_path.stem().wstring() + L"\\"
                    + item.NameInArchive;
            }
            else
                found->fullName = strBaseName + L"\\" + item.NameInArchive;

            found->name = strItemName;

            if (!found->IsToIgnore())
            {
                found->Stream->SetFilePointer(0LL, FILE_BEGIN, NULL);

                if (found->GetFileFormat() == ImportItem::ImportItemFormat::Archive)
                {
                    found->DefinitionItemLookup(ImportDefinition::Expand);

                    if (found->definitionItem == nullptr)
                    {
                        found->isToExpand = true;
                    }
                }

                input.ullBytesExtracted += found->Stream->GetSize();

                found->computerName = input.computerName;
                found->systemType = input.systemType;
                found->timeStamp = input.timeStamp;
                found->inputFile = input.inputFile;

                auto pFinalTempStream = std::dynamic_pointer_cast<TemporaryStream>(found->Stream);

                if (pFinalTempStream)
                {
                    if (pFinalTempStream->IsFileStream() == S_OK)
                        found->ullFileBytesCharged = pFinalTempStream->GetSize();
                    if (pFinalTempStream->IsMemoryStream() == S_OK)
                        found->ullMemBytesCharged = pFinalTempStream->GetSize();
                }
                else
                    found->ullFileBytesCharged = found->Stream->GetSize();

                if (Archive::GetArchiveFormat(item.NameInArchive) != ArchiveFormat::Unknown)
                {
                    SendRequest(ImportMessage::MakeExpandRequest(std::move(*found)));
                    log::Verbose(_L_, L"\tArchive %s has been extracted\r\n", strItemName.c_str());
                }
                else
                {
                    if (found->IsToImport())
                    {
                        SendRequest(std::move(ImportMessage::MakeImportRequest(*found)));
                        log::Verbose(_L_, L"\tArchive %s has been extracted\r\n", strItemName.c_str());
                    }
                    else if (found->IsToExtract())
                    {
                        SendRequest(ImportMessage::MakeExtractRequest(std::move(*found)));
                        log::Verbose(_L_, L"\tArchive %s has been extracted\r\n", strItemName.c_str());
                    }
                }
            }
        }
    });

    auto MakeArchiveStream = [this, &input](std::shared_ptr<ByteStream>& stream) -> HRESULT {
        stream = input.GetInputStream(_L_);

        if (stream == nullptr)
        {
            return E_FAIL;
        }
        return S_OK;
    };

    auto MakeWriteStream = [this, &input, &tempItems](const Archive::ArchiveItem& item) -> std::shared_ptr<ByteStream> {
        HRESULT hr = E_FAIL;

        ImportItem output_item;

        output_item.inputFile = input.inputFile;
        output_item.fullName = item.Path;
        output_item.bPrefixSubItem = true;

        if (input.bPrefixSubItem)
        {
            fs::path path(input.name);
            auto strBaseName = path.stem().wstring();
            output_item.name = strBaseName + L"\\" + item.NameInArchive;
        }
        else
        {
            output_item.name = item.NameInArchive;
        }

        output_item.definitions = input.definitions;

        auto pStream = std::make_shared<TemporaryStream>(_L_);

        if (FAILED(hr = pStream->Open(m_tempOutput.Path.c_str(), item.NameInArchive, 800 * 1024 * 1024, false)))
        {
            log::Error(_L_, hr, L"Failed to open temporary archive\r\n");
            return nullptr;
        }
        output_item.Stream = pStream;
        tempItems.push_back(output_item);

        return output_item.Stream;
    };

    auto ShouldItemBeExtracted = [this, &input](const std::wstring& strNameInArchive) {
        wstring strItemName;
        fs::path path(input.name);
        auto strBaseName = path.stem().wstring();

        if (input.bPrefixSubItem)
        {
            strItemName = strBaseName + L"\\" + strNameInArchive;
        }
        else
        {
            strItemName = strNameInArchive;
        }

        if (!ImportItem::IsToIgnore(input.definitions, strItemName))
        {
            if (Archive::GetArchiveFormat(strItemName) != ArchiveFormat::Unknown)
                return true;

            if (ImportItem::IsToImport(input.definitions, strItemName))
                return true;

            if (ImportItem::IsToExtract(input.definitions, strItemName))
                return true;

            if (ImportItem::IsToExpand(input.definitions, strItemName))
                return true;
        }

        log::Verbose(_L_, L"\t%s is not imported\r\n", strItemName.c_str());
        return false;
    };

    if (FAILED(hr = extractor->Extract(MakeArchiveStream, ShouldItemBeExtracted, MakeWriteStream)))
    {
        log::Error(_L_, hr, L"Failed to extract archive %s\r\n", input.name.c_str());
        return hr;
    }
    if (input.Stream)
    {
        input.Stream->Close();
        input.Stream.reset();
    }
    return S_OK;
}

HRESULT ImportAgent::ExtractItem(ImportItem& input)
{
    HRESULT hr = E_FAIL;

    GetSystemTime(&input.importStart);
    BOOST_SCOPE_EXIT(&input) { GetSystemTime(&input.importEnd); }
    BOOST_SCOPE_EXIT_END;

    if (input.Stream)
    {
        wstring strOutputDir;
        if (FAILED(hr = GetOutputDir(m_extractOutput.Path.c_str(), strOutputDir, true)))
        {
            log::Error(_L_, hr, L"Failed to create output file\r\n");
            return hr;
        }

        fs::path output_path(strOutputDir);

        output_path = canonical(output_path);

        auto offset = output_path.wstring().size();

        output_path /= input.fullName;

        error_code err;
        create_directories(output_path.parent_path(), err);

        if (err)
        {
            log::Error(_L_, hr = HRESULT_FROM_WIN32(err.value()), L"Failed to create output file\r\n");
            return hr;
        }

        auto strOutput = output_path.wstring();

        auto tempStream = std::dynamic_pointer_cast<TemporaryStream>(input.Stream);

        if (tempStream)
        {
            input.ullBytesExtracted = tempStream->GetSize();
            input.outputFile = std::make_shared<wstring>(strOutput.substr(offset + 1));

            if (FAILED(hr = tempStream->MoveTo(strOutput.c_str())))
            {
                log::Error(_L_, hr, L"Failed to extract %s to file %s\r\n", input.fullName.c_str(), strOutput.c_str());
                return hr;
            }
            log::Verbose(_L_, L"\t%s is extracted to %s\r\n", input.fullName.c_str(), strOutput.c_str());
        }
        else
        {
            FileStream fileStream(_L_);

            input.outputFile = std::make_shared<wstring>(strOutput.substr(offset));

            if (FAILED(hr = fileStream.WriteTo(strOutput.c_str())))
            {
                return hr;
            }

            ULONGLONG ullWritten = 0LL;
            if (FAILED(hr = input.Stream->CopyTo(fileStream, &ullWritten)))
            {
                log::Error(_L_, hr, L"Failed to extract %s to file %s\r\n", input.fullName.c_str(), strOutput.c_str());
                return hr;
            }
            log::Verbose(_L_, L"\t%s is extracted to %s\r\n", input.fullName.c_str(), strOutput.c_str());
            input.ullBytesExtracted = ullWritten;
        }

        input.Stream->Close();
        input.Stream = nullptr;
    }
    return S_OK;
}

bool ImportAgent::SendResult(const ImportNotification::Notification& notification)
{
    if (notification->Item().ullFileBytesCharged > 0LL)
    {
        m_fileSemaphore.release(notification->Item().ullFileBytesCharged);
    }

    if (notification->Item().ullMemBytesCharged > 0LL)
    {
        m_memSemaphore.release(notification->Item().ullMemBytesCharged);
    }

    m_ulItemProcessed++;

    static_cast<void>(InterlockedDecrement(&m_lInProgressItems));

    LogNotification(notification);

    return Concurrency::send(m_target, notification);
}

HRESULT ImportAgent::InitializeOutputs(
    const OutputSpec& extractOutput,
    const OutputSpec& importOutput,
    const OutputSpec& tempOutput)
{
    m_extractOutput = extractOutput;
    m_databaseOutput = importOutput;
    m_tempOutput = tempOutput;

    m_Complete.reset();

    return S_OK;
}

HRESULT ImportAgent::InitializeTables(std::vector<TableDescription>& tables)
{
    HRESULT hr = E_FAIL;

    m_SqlDataFlow.clear();

    DWORD dwAgentCount = 0L;
    for (const auto& table : tables)
    {
        dwAgentCount += table.dwConcurrency;
    }

    m_SqlDataFlow.reserve(tables.size());
    m_pAgents.reserve(dwAgentCount);

    for (auto& table : tables)
    {
        auto flow = std::make_unique<SqlMessageDataFlow>();

        flow->filter.strTableName = table.name;

        m_SqlMessageBuffer.link_target(&flow->block);

        for (unsigned int i = 1; i <= table.dwConcurrency; i++)
        {
            flow->pAgents.emplace_back(std::make_unique<SqlImportAgent>(
                _L_, flow->block, m_target, m_memSemaphore, m_fileSemaphore, &m_lInProgressItems));

            const auto& pAgent = flow->pAgents.back();
            if (pAgent)
            {
                if (FAILED(hr = pAgent->Initialize(m_databaseOutput, m_tempOutput, table)))
                {
                    log::Error(_L_, hr, L"Failed to initialize SQL import agent for table %s\r\n", table.name.c_str());
                    return hr;
                }

                if (!pAgent->start())
                {
                    log::Error(_L_, hr, L"Failed to start SQL import agent for table %s\r\n", table.name.c_str());
                    return hr;
                }

                m_pAgents.emplace_back(pAgent.get());
            }
        }

        m_SqlDataFlow.emplace_back(std::move(flow));
    }

    auto flow = std::make_unique<SqlMessageDataFlow>();

    m_SqlMessageBuffer.link_target(&flow->block);

    auto pAgent = std::make_unique<SqlImportAgent>(
        _L_, flow->block, m_target, m_memSemaphore, m_fileSemaphore, &m_lInProgressItems);
    if (!pAgent->start())
    {
        log::Error(_L_, hr, L"Failed to start SQL import agent for dummy table\r\n");
        return hr;
    }
    m_pAgents.emplace_back(std::move(pAgent));
    flow->pAgents.emplace_back(std::move(pAgent));
    m_SqlDataFlow.emplace_back(std::move(flow));

    return S_OK;
}

HRESULT ImportAgent::FinalizeTables()
{
    HRESULT hr = E_FAIL;
    for (auto& agent : m_pAgents)
    {
        if (FAILED(hr = agent->Finalize()))
        {
            log::Error(_L_, hr, L"Failed to finalize agent\r\n");
        }
    }
    return S_OK;
}

HRESULT ImportAgent::ImportOneItem(ImportMessage::Message request)
{
    HRESULT hr = E_FAIL;

    ImportMessage::RequestType type = request->m_Request;

    switch (request->Item().format)
    {
        case ImportItem::Envelopped:
        {
            const auto it = m_Tasks.push_back(make_task<std::function<void()>>([this, request]() {
                HRESULT hr = E_FAIL;
                if (FAILED(hr = EnveloppedItem(request->m_item)))
                    SendResult(ImportNotification::MakeFailureNotification(hr, request->m_item));
                else
                    SendResult(ImportNotification::MakeExtractNotification(request->m_item));
            }));
            m_TaskGroup.run(*it);
        }
        break;
        case ImportItem::Archive:
        {
            switch (type)
            {
                case ImportMessage::Extract:
                {
                    const auto it = m_Tasks.push_back(make_task<std::function<void()>>([this, request]() {
                        HRESULT hr = E_FAIL;
                        if (FAILED(hr = ExtractItem(request->m_item)))
                            SendResult(ImportNotification::MakeFailureNotification(hr, request->m_item));
                        else
                            SendResult(ImportNotification::MakeExtractNotification(request->m_item));
                    }));
                    m_TaskGroup.run(*it);
                }
                break;
                case ImportMessage::Expand:
                {
                    const auto it = m_Tasks.push_back(make_task<std::function<void()>>([this, request]() {
                        HRESULT hr = E_FAIL;
                        if (FAILED(hr = ExpandItem(request->m_item)))
                            SendResult(ImportNotification::MakeFailureNotification(hr, request->m_item));
                        else
                            SendResult(ImportNotification::MakeExtractNotification(request->m_item));
                    }));
                    m_TaskGroup.run(*it);
                }
            }
        }
        break;
        case ImportItem::CSV:
            switch (type)
            {
                case ImportMessage::Extract:
                {
                    const auto it = m_Tasks.push_back(make_task<std::function<void()>>([this, request]() {
                        HRESULT hr = E_FAIL;
                        if (FAILED(hr = ExtractItem(request->m_item)))
                            SendResult(ImportNotification::MakeFailureNotification(hr, request->m_item));
                        else
                            SendResult(ImportNotification::MakeExtractNotification(request->m_item));
                    }));

                    m_TaskGroup.run(*it);
                }
                break;
                default:
                    break;
            }
            break;
        case ImportItem::EventLog:
            switch (type)
            {
                case ImportMessage::Extract:
                {
                    const auto it = m_Tasks.push_back(make_task<std::function<void()>>([this, request]() {
                        HRESULT hr = E_FAIL;
                        if (FAILED(hr = ExtractItem(request->m_item)))
                            SendResult(ImportNotification::MakeFailureNotification(hr, request->m_item));
                        else
                            SendResult(ImportNotification::MakeExtractNotification(request->m_item));
                    }));
                    m_TaskGroup.run(*it);
                }
                break;
                default:
                    break;
            }
            break;
        case ImportItem::RegistryHive:
            switch (type)
            {
                case ImportMessage::Extract:
                {
                    const auto it = m_Tasks.push_back(make_task<std::function<void()>>([this, request]() {
                        HRESULT hr = E_FAIL;
                        if (FAILED(hr = ExtractItem(request->m_item)))
                            SendResult(ImportNotification::MakeFailureNotification(hr, request->m_item));
                        else
                            SendResult(ImportNotification::MakeExtractNotification(request->m_item));
                    }));
                    m_TaskGroup.run(*it);
                }
                break;
                default:
                    break;
            }
            break;
        case ImportItem::XML:
        case ImportItem::Data:
        case ImportItem::Text:
            switch (type)
            {
                case ImportMessage::Extract:
                {
                    const auto it = m_Tasks.push_back(make_task<std::function<void()>>([this, request]() {
                        HRESULT hr = E_FAIL;
                        if (FAILED(hr = ExtractItem(request->m_item)))
                            SendResult(ImportNotification::MakeFailureNotification(hr, request->m_item));
                        else
                            SendResult(ImportNotification::MakeExtractNotification(request->m_item));
                    }));
                    m_TaskGroup.run(*it);
                }
                break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    return S_OK;
}

void ImportAgent::run()
{
    HRESULT hr = E_FAIL;

    bool bStopping = false;

    call<LONG*> check_if_done([this](LONG* value) {
        send(m_QueuedItems, *value);
        if (*value > 0)
        {
            return;
        }
        log::Info(_L_, L"Queue is empty, completing agents...\r\n", *value);
        auto complete_request = ImportMessage::MakeCompleteRequest();
        SendRequest(complete_request);
    });

    timer<LONG*> check_timer(500u, &m_lInProgressItems, &check_if_done, true);

    check_timer.start();

    ImportMessage::Message request = GetRequest();

    while (request)
    {
        if (request->m_Request == ImportMessage::Complete)
        {
            log::Verbose(_L_, L"ImportAgent received a complete message\r\n");
        }
        else
        {
            ImportOneItem(request);
        }

        if (request->m_Request == ImportMessage::Complete)
        {
            bStopping = true;
        }

        request = nullptr;

        if (bStopping)
        {
            m_TaskGroup.wait();

            for (auto& flow : m_SqlDataFlow)
            {
                for (auto& agent : flow->pAgents)
                {
                    Concurrency::send(flow->block, ImportMessage::MakeCompleteRequest());
                }
            }
            check_timer.stop();

            if (!m_pAgents.empty())
                concurrency::agent::wait_for_all(m_pAgents.size(), (concurrency::agent**)m_pAgents.data());

            if (!concurrency::try_receive<ImportMessage::Message>(m_source, request))
            {
                request = nullptr;
                done();
            }
        }
        else
            request = GetRequest();
    }

    return;
}

HRESULT ImportAgent::LogStatistics(const logger& pLog)
{
    log::Info(pLog, L"\tMain extraction agent: %d items\r\n\r\n", m_ulItemProcessed);

    for (const auto& flow : m_SqlDataFlow)
    {
        if (!flow->filter.strTableName.empty())
        {
            log::Info(pLog, L"\t%s :", flow->filter.strTableName.c_str());
            bool bFirst = true;
            for (const auto& agent : flow->pAgents)
            {
                log::Info(pLog, L"%s %d", bFirst ? L"" : L" +", agent->ItemsProcess());
                bFirst = false;
            }
            log::Info(pLog, L" items\r\n");
        }
    }
    return S_OK;
}

void ImportAgent::LogNotification(const ImportNotification::Notification& notification)
{
    const auto& item = notification->Item();

    if (FAILED(notification->GetHR()))
    {
        log::Error(
            _L_,
            notification->GetHR(),
            L"\t[%04d] %s failed\r\n",
            QueuedItemsCount(),
            notification->Item().name.c_str());
        return;
    }

    switch (item.format)
    {
        case ImportItem::Envelopped:
            log::Info(
                _L_,
                L"\t[%04d] %s (envelopped message) decrypted (%I64d bytes)\r\n",
                QueuedItemsCount(),
                item.name.c_str(),
                item.ullBytesExtracted);
            break;
        case ImportItem::Archive:
            log::Info(
                _L_,
                L"\t[%04d] %s (archive) extracted (%I64d bytes)\r\n",
                QueuedItemsCount(),
                item.name.c_str(),
                item.ullBytesExtracted);
            break;
        case ImportItem::CSV:
            log::Info(
                _L_,
                L"\t[%04d] %s (csv) imported into %s (%I64d lines)\r\n",
                QueuedItemsCount(),
                item.name.c_str(),
                item.definitionItem->tableName.c_str(),
                item.ullLinesImported);
            break;
        case ImportItem::RegistryHive:
            log::Info(
                _L_,
                L"\t[%04d] %s (registry hive) imported into %s (%I64d lines)\r\n",
                QueuedItemsCount(),
                item.name.c_str(),
                item.definitionItem->tableName.c_str(),
                item.ullLinesImported);
            break;
        case ImportItem::EventLog:
            log::Info(
                _L_,
                L"\t[%04d] %s (event log) imported into %s (%I64d lines)\r\n",
                QueuedItemsCount(),
                item.name.c_str(),
                item.definitionItem->tableName.c_str(),
                item.ullLinesImported);
            break;
        case ImportItem::XML:
            log::Info(
                _L_,
                L"\t[%04d] %s (xml) imported (%I64d bytes)\r\n",
                QueuedItemsCount(),
                item.name.c_str(),
                item.ullBytesExtracted);
            break;
        case ImportItem::Data:
            log::Info(
                _L_,
                L"\t[%04d] %s (data) imported (%I64d bytes)\r\n",
                QueuedItemsCount(),
                item.name.c_str(),
                item.ullBytesExtracted);
            break;
        case ImportItem::Text:
            log::Info(
                _L_,
                L"\t[%04d] %s (text) imported (%I64d bytes)\r\n",
                QueuedItemsCount(),
                item.name.c_str(),
                item.ullBytesExtracted);
            break;
        default:
            log::Warning(
                _L_,
                L"\t[%04d] unexpected notification: %s (text) imported (%I64d bytes)\r\n",
                QueuedItemsCount(),
                item.name.c_str(),
                item.ullBytesExtracted);
            break;
    }
}
