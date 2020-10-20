//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "ImportNotification.h"

struct ImportNotification_make_shared_enabler : public Orc::ImportNotification
{
    inline ImportNotification_make_shared_enabler(HRESULT hr, ActionTaken action, const Orc::ImportItem& item)
        : Orc::ImportNotification(hr, action, item)
    {
    }
    inline ImportNotification_make_shared_enabler(HRESULT hr, ActionTaken action, Orc::ImportItem&& item)
        : Orc::ImportNotification(hr, action, std::move(item))
    {
    }
};

Orc::ImportNotification::Notification Orc::ImportNotification::MakeExtractNotification(const ImportItem& import_item)
{
    return std::make_shared<ImportNotification_make_shared_enabler>(S_OK, Extract, import_item);
}

Orc::ImportNotification::Notification Orc::ImportNotification::MakeExtractNotification(ImportItem&& import_item)
{
    return std::make_shared<ImportNotification_make_shared_enabler>(S_OK, Extract, import_item);
}

Orc::ImportNotification::Notification Orc::ImportNotification::MakeImportNotification(const ImportItem& import_item)
{
    return std::make_shared<ImportNotification_make_shared_enabler>(S_OK, Import, import_item);
}

Orc::ImportNotification::Notification Orc::ImportNotification::MakeImportNotification(ImportItem&& import_item)
{
    return std::make_shared<ImportNotification_make_shared_enabler>(S_OK, Import, import_item);
}

Orc::ImportNotification::Notification Orc::ImportNotification::MakeIgnoreNotification(const ImportItem& import_item)
{
    return std::make_shared<ImportNotification_make_shared_enabler>(S_OK, Ignore, import_item);
}

Orc::ImportNotification::Notification Orc::ImportNotification::MakeIgnoreNotification(ImportItem&& import_item)
{
    return std::make_shared<ImportNotification_make_shared_enabler>(S_OK, Ignore, import_item);
}

Orc::ImportNotification::Notification
Orc::ImportNotification::MakeFailureNotification(HRESULT hr, const ImportItem& import_item)
{
    return std::make_shared<ImportNotification_make_shared_enabler>(hr, Unknown, import_item);
}

Orc::ImportNotification::Notification
Orc::ImportNotification::MakeFailureNotification(HRESULT hr, ImportItem&& import_item)
{
    return std::make_shared<ImportNotification_make_shared_enabler>(hr, Unknown, import_item);
}

Orc::ImportNotification::~ImportNotification(void) {}
