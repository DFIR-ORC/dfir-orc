//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "agents.h"

#include "ImportMessage.h"
#include "ImportNotification.h"
#include "ByteStream.h"

#include "EvtLibrary.h"

#include "RegistryWalker.h"
#include "RegFind.h"

#include "OutputSpec.h"

#include "ImportBytesSemaphore.h"

#include "EvtLibrary.h"

#pragma managed(push, off)

namespace Orc {

class ImportAgent;
class Connection;

class SqlImportAgent : public Concurrency::agent
{
    friend class ImportAgent;

public:
    SqlImportAgent(
        logger pLog,
        ImportMessage::ISource& source,
        ImportNotification::ITarget& target,
        ImportBytesSemaphore& memSem,
        ImportBytesSemaphore& fileSem,
        LONG* plInProgressItems)
        : _L_(std::move(pLog))
        , m_source(source)
        , m_target(target)
        , m_fileSemaphore(fileSem)
        , m_memSemaphore(memSem)
        , m_plInProgressItems(plInProgressItems) {};

    HRESULT Initialize(const OutputSpec& importOutput, const OutputSpec& tempOutput, TableDescription& table);

    virtual void run();

    HRESULT Finalize();

    ULONG ItemsProcess() const { return m_ulItemProcessed; }

    virtual ~SqlImportAgent();

private:
    logger _L_;

    ImportNotification::ITarget& m_target;
    ImportMessage::ISource& m_source;

    ImportBytesSemaphore& m_memSemaphore;
    ImportBytesSemaphore& m_fileSemaphore;

    OutputSpec m_importOutput;
    OutputSpec m_tempOutput;

    LONG* m_plInProgressItems = nullptr;
    ULONG m_ulItemProcessed = 0L;

    std::shared_ptr<TableOutput::IConnection> m_pSqlConnection;

    std::shared_ptr<EvtLibrary> m_wevtapi;

    TableDefinition m_TableDefinition;

    const TableOutput::Schema& GetTableColumns() const { return m_TableDefinition.second; }

    std::shared_ptr<TableOutput::IWriter> GetOutputWriter(ImportItem& input);

    HRESULT ImportSingleEvent(
        ITableOutput& output,
        const ImportItem& input,
        const EVT_HANDLE& hSystemContext,
        const EVT_HANDLE& hEvent);
    HRESULT ImportEvtxData(ImportItem& input);
    HRESULT ImportHiveData(ImportItem& input);

    HRESULT ImportCSVData(ImportItem& input);

    HRESULT ImportTask(ImportMessage::Message request);

    bool SendResult(const ImportNotification::Notification& notification)
    {
        if (notification->Item().ullFileBytesCharged > 0LL)
        {
            m_fileSemaphore.release(notification->Item().ullFileBytesCharged);
        }
        if (notification->Item().ullMemBytesCharged > 0LL)
        {
            m_memSemaphore.release(notification->Item().ullMemBytesCharged);
        }
        static_cast<void>(InterlockedDecrement(m_plInProgressItems));
        m_ulItemProcessed++;
        return Concurrency::send(m_target, notification);
    }
    ImportMessage::Message GetRequest() { return Concurrency::receive<ImportMessage::Message>(m_source); }

    HRESULT InitializeTableColumns(TableDescription& table);
    HRESULT InitializeTable(TableDescription& table);
};

}  // namespace Orc
#pragma managed(pop)
