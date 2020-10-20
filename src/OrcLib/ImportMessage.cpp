//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "ImportMessage.h"

using namespace Orc;

struct ImportMessage_make_shared_enabler : public Orc::ImportMessage
{
    inline ImportMessage_make_shared_enabler() = default;
};

ImportMessage::Message ImportMessage::MakeBeforeStatementRequest(const ImportStatement& statement)
{
    auto retval = std::make_shared<ImportMessage_make_shared_enabler>();

    retval->m_Request = BeforeImport;
    retval->m_Statement = statement;
    return retval;
}

ImportMessage::Message ImportMessage::MakeBeforeStatementRequest(ImportStatement&& statement)
{
    auto retval = std::make_shared<ImportMessage_make_shared_enabler>();

    retval->m_Request = BeforeImport;
    std::swap(retval->m_Statement, statement);
    return retval;
}

ImportMessage::Message ImportMessage::MakeImportRequest(const ImportItem& item)
{
    if (item.format == ImportItem::ImportItemFormat::Undetermined)
        return nullptr;

    auto retval = std::make_shared<ImportMessage_make_shared_enabler>();

    retval->m_Request = Import;
    retval->m_item = item;
    return retval;
}

ImportMessage::Message ImportMessage::MakeImportRequest(ImportItem&& item)
{
    if (item.format == ImportItem::ImportItemFormat::Undetermined)
        return nullptr;

    auto retval = std::make_shared<ImportMessage_make_shared_enabler>();

    retval->m_Request = Import;
    std::swap(retval->m_item, item);
    return retval;
}

ImportMessage::Message ImportMessage::MakeExpandRequest(const ImportItem& item)
{
    if (item.format == ImportItem::ImportItemFormat::Undetermined)
        return nullptr;

    auto retval = std::make_shared<ImportMessage_make_shared_enabler>();

    retval->m_Request = Expand;
    retval->m_item = item;
    return retval;
}

ImportMessage::Message ImportMessage::MakeExpandRequest(ImportItem&& item)
{
    if (item.format == ImportItem::ImportItemFormat::Undetermined)
        return nullptr;

    auto retval = std::make_shared<ImportMessage_make_shared_enabler>();

    retval->m_Request = Expand;
    std::swap(retval->m_item, item);
    return retval;
}

ImportMessage::Message ImportMessage::MakeExtractRequest(const ImportItem& item)
{
    if (item.format == ImportItem::ImportItemFormat::Undetermined)
        return nullptr;

    auto retval = std::make_shared<ImportMessage_make_shared_enabler>();

    retval->m_Request = Extract;
    retval->m_item = item;
    return retval;
}

ImportMessage::Message ImportMessage::MakeExtractRequest(ImportItem&& item)
{
    if (item.format == ImportItem::ImportItemFormat::Undetermined)
        return nullptr;

    auto retval = std::make_shared<ImportMessage_make_shared_enabler>();

    retval->m_Request = Extract;
    std::swap(retval->m_item, item);
    return retval;
}

ImportMessage::Message ImportMessage::MakePipeImportRequest(const ImportItem& item)
{
    if (item.format == ImportItem::ImportItemFormat::Undetermined)
        return nullptr;

    auto retval = std::make_shared<ImportMessage_make_shared_enabler>();

    retval->m_Request = PipeImport;
    retval->m_item = item;
    return retval;
}

ImportMessage::Message ImportMessage::MakePipeImportRequest(ImportItem&& item)
{
    if (item.format == ImportItem::ImportItemFormat::Undetermined)
        return nullptr;

    auto retval = std::make_shared<ImportMessage_make_shared_enabler>();

    retval->m_Request = PipeImport;
    std::swap(retval->m_item, item);
    return retval;
}

ImportMessage::Message ImportMessage::MakePipeExtractRequest(const ImportItem& item)
{
    if (item.format == ImportItem::ImportItemFormat::Undetermined)
        return nullptr;

    auto retval = std::make_shared<ImportMessage_make_shared_enabler>();

    retval->m_Request = PipeExtract;
    retval->m_item = item;
    return retval;
}

ImportMessage::Message ImportMessage::MakePipeExtractRequest(ImportItem&& item)
{
    if (item.format == ImportItem::ImportItemFormat::Undetermined)
        return nullptr;

    auto retval = std::make_shared<ImportMessage_make_shared_enabler>();

    retval->m_Request = PipeExtract;
    std::swap(retval->m_item, item);
    return retval;
}
ImportMessage::Message ImportMessage::MakeAfterStatementRequest(const ImportStatement& statement)
{
    auto retval = std::make_shared<ImportMessage_make_shared_enabler>();

    retval->m_Request = AfterImport;
    retval->m_Statement = statement;
    return retval;
}

ImportMessage::Message ImportMessage::MakeAfterStatementRequest(ImportStatement&& statement)
{
    auto retval = std::make_shared<ImportMessage_make_shared_enabler>();

    retval->m_Request = AfterImport;
    std::swap(retval->m_Statement, statement);
    return retval;
}

ImportMessage::Message ImportMessage::MakeCompleteRequest()
{
    auto retval = std::make_shared<ImportMessage_make_shared_enabler>();

    retval->m_Request = Complete;
    return retval;
}

ImportMessage::~ImportMessage() {}
