//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "ImportMessage.h"
#include "ImportNotification.h"
#include "ByteStream.h"

#include "EvtLibrary.h"

#include "RegistryWalker.h"
#include "RegFind.h"

#include "OutputSpec.h"

#include "CaseInsensitive.h"

#include "SqlImportAgent.h"

#include "ImportBytesSemaphore.h"

#include <agents.h>
#include <concurrent_vector.h>
#include <ppl.h>

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API ImportAgent : public Concurrency::agent
{
public:
    class SqlAgentFilter
    {

    public:
        std::wstring strTableName;

        bool operator()(const ImportMessage::Message& msg)
        {

            if (strTableName.empty())
                return true;

            auto pDef = msg->Item().GetDefinitionItem();
            if (pDef)
            {
                if (equalCaseInsensitive(pDef->tableName, strTableName))
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
            return true;
        };
    };

    class SqlMessageDataFlow
    {
    public:
        SqlAgentFilter filter;
        ImportMessage::PriorityMessageBuffer block;
        std::vector<std::unique_ptr<SqlImportAgent>> pAgents;

        SqlMessageDataFlow()
            : block([this](const ImportMessage::Message& msg) -> bool { return filter(msg); }) {};

        SqlMessageDataFlow(const SqlMessageDataFlow&) = default;
    };

public:
    ImportAgent(
        ImportMessage::ISource& source,
        ImportMessage::ITarget& import_target,
        ImportNotification::ITarget& target)
        : m_source(source)
        , m_import_target(import_target)
        , m_target(target)
    {
        m_memSemaphore.SetCapacity(40LL * 1024 * 1024 * 1024);
        m_fileSemaphore.SetCapacity(100LL * 1024 * 1024 * 1024);
    }

    HRESULT InitializeOutputs(
        const OutputSpec& extractOutput,
        const OutputSpec& importOutput,
        const OutputSpec& tempOutput);

    HRESULT InitializeTables(std::vector<TableDescription>& tables);
    HRESULT FinalizeTables();

    bool SendRequest(const ImportMessage::Message& request)
    {
        if (request->Item().ullFileBytesCharged > 0LL)
        {
            m_fileSemaphore.acquire(request->Item().ullFileBytesCharged);
        }
        if (request->Item().ullMemBytesCharged > 0LL)
        {
            m_memSemaphore.acquire(request->Item().ullMemBytesCharged);
        }

        if (request->m_Request == ImportMessage::RequestType::Complete)
            return Concurrency::send(m_import_target, request);

        static_cast<void>(InterlockedIncrement(&m_lInProgressItems));

        if (request->m_item.IsToExtract() || request->m_item.IsToExpand())
            return Concurrency::send(m_import_target, request);
        else
            return Concurrency::send(m_SqlMessageBuffer, request);
    }

    virtual void run();

    ULONG ItemsProcess() const { return m_ulItemProcessed; }

    Concurrency::ISource<LONG>& QueuedItemsCount() { return m_QueuedItems; }

    HRESULT LogStatistics();

    ~ImportAgent(void) = default;

private:

    ImportNotification::ITarget& m_target;
    ImportMessage::ISource& m_source;
    ImportMessage::ITarget& m_import_target;

    ImportBytesSemaphore m_fileSemaphore;
    ImportBytesSemaphore m_memSemaphore;

    OutputSpec m_extractOutput;
    OutputSpec m_databaseOutput;
    OutputSpec m_tempOutput;

    concurrency::event m_Complete;
    // In "queues" item counter
    LONG m_lInProgressItems = 0L;
    concurrency::overwrite_buffer<LONG> m_QueuedItems;

    ULONG m_ulItemProcessed = 0L;

    Concurrency::concurrent_vector<Concurrency::task_handle<std::function<void()>>> m_Tasks;
    Concurrency::task_group m_TaskGroup;

    ImportMessage::PriorityMessageBuffer m_SqlMessageBuffer;

    std::vector<std::unique_ptr<SqlMessageDataFlow>> m_SqlDataFlow;
    std::vector<std::unique_ptr<SqlImportAgent>> m_pAgents;

    typedef std::pair<TableDescription, std::vector<TableOutput::Column>> TableDefinition;
    std::vector<TableDefinition> m_TableDefinitions;

    bool SendResult(const ImportNotification::Notification& notification);

    ImportMessage::Message GetRequest() { return Concurrency::receive<ImportMessage::Message>(m_source); }

    ImportMessage::Message TriageNewItem(const ImportItem& input, ImportItem& newItem);

    HRESULT
    UnWrapMessage(const std::shared_ptr<ByteStream>& pMessageStream, const std::shared_ptr<ByteStream>& pOutputStream);

    HRESULT EnveloppedItem(ImportItem& input);

    HRESULT ExpandItem(ImportItem& input);

    HRESULT ExtractItem(ImportItem& input);

    HRESULT ImportOneItem(ImportMessage::Message request);

    void LogNotification(const ImportNotification::Notification& notification);
};
}  // namespace Orc
#pragma managed(pop)
