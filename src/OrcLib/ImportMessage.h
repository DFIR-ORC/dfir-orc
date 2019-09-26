//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "ImportItem.h"
#include "PriorityBuffer.h"

#include <agents.h>

#pragma managed(push, off)

namespace Orc {
class ImportAgent;

class ORCLIB_API ImportMessage
{
    friend class ImportAgent;
    friend class SqlImportAgent;

public:
    using UnboundedMessageBuffer = Concurrency::unbounded_buffer<std::shared_ptr<ImportMessage>>;
    using PriorityMessageBuffer = PriorityBuffer<std::shared_ptr<ImportMessage>>;
    using Message = std::shared_ptr<ImportMessage>;
    using ITarget = Concurrency::ITarget<Message>;
    using ITargetRef = Concurrency::ITarget<Message>&;
    using ISource = Concurrency::ISource<Message>;
    using ISourceRef = Concurrency::ISource<Message>&;
    using MessagePipeline = std::pair<ISource, ITarget>;
    using MessagePipelineRef = std::pair<ISourceRef, ITargetRef>;
    using ImportStatement = std::wstring;

    enum RequestType
    {
        Ignore,
        BeforeImport,
        PipeExtract,
        Extract,
        Expand,
        PipeImport,
        Import,
        AfterImport,
        Complete
    };

protected:

	ImportMessage(void) {};


private:
    ImportItem m_item;
    ImportStatement m_Statement;
    RequestType m_Request;

public:
    static Message MakeBeforeStatementRequest(const ImportStatement& statement);
    static Message MakeBeforeStatementRequest(ImportStatement&& statement);
    static Message MakeImportRequest(const ImportItem& item);
    static Message MakeImportRequest(ImportItem&& item);
    static Message MakeExpandRequest(const ImportItem& item);
    static Message MakeExpandRequest(ImportItem&& item);
    static Message MakeExtractRequest(const ImportItem& item);
    static Message MakeExtractRequest(ImportItem&& item);
    static Message MakePipeImportRequest(const ImportItem& item);
    static Message MakePipeImportRequest(ImportItem&& item);
    static Message MakePipeExtractRequest(const ImportItem& item);
    static Message MakePipeExtractRequest(ImportItem&& item);
    static Message MakeAfterStatementRequest(const ImportStatement& statement);
    static Message MakeAfterStatementRequest(ImportStatement&& statement);

    static Message MakeCompleteRequest();

    const ImportItem& Item() const { return m_item; };

    bool operator<(const ImportMessage& message)
    {
        if (m_Request > message.m_Request)
            return true;

        return m_item.Format > message.m_item.Format;
    }

    ~ImportMessage(void);
};

}  // namespace Orc

#pragma managed(pop)
