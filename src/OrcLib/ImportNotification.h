//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <string>
#include <memory>

#include <agents.h>

#include "BinaryBuffer.h"

#include "ImportItem.h"

#pragma managed(push, off)

namespace Orc {

class ImportNotification
{
    friend class std::shared_ptr<ImportNotification>;
    friend class std::_Ref_count_obj<ImportNotification>;

public:
    using Notification = std::shared_ptr<ImportNotification>;
    using UnboundedMessageBuffer = Concurrency::unbounded_buffer<Notification>;

    using ITarget = Concurrency::ITarget<Notification>;
    using ISource = Concurrency::ISource<Notification>;

    enum ActionTaken
    {
        Unknown,
        Ignore,
        Import,
        Extract
    };

private:
    ActionTaken m_action = Unknown;
    ImportItem m_item;
    HRESULT m_hr = E_FAIL;

protected:
    ImportNotification(HRESULT hr, ActionTaken action, const ImportItem& item)
        : m_hr(hr)
        , m_item(item)
        , m_action(action)
    {
    }
    ImportNotification(HRESULT hr, ActionTaken action, ImportItem&& item)
        : m_hr(hr)
        , m_action(action)
    {
        std::swap(m_item, item);
    }

public:
    static Notification MakeExtractNotification(const ImportItem& import_item)
    {
        return std::make_shared<ImportNotification>(S_OK, Extract, import_item);
    }
    static Notification MakeExtractNotification(ImportItem&& import_item)
    {
        return std::make_shared<ImportNotification>(S_OK, Extract, import_item);
    }
    static Notification MakeImportNotification(const ImportItem& import_item)
    {
        return std::make_shared<ImportNotification>(S_OK, Import, import_item);
    }
    static Notification MakeImportNotification(ImportItem&& import_item)
    {
        return std::make_shared<ImportNotification>(S_OK, Import, import_item);
    }
    static Notification MakeIgnoreNotification(const ImportItem& import_item)
    {
        return std::make_shared<ImportNotification>(S_OK, Ignore, import_item);
    }
    static Notification MakeIgnoreNotification(ImportItem&& import_item)
    {
        return std::make_shared<ImportNotification>(S_OK, Ignore, import_item);
    }
    static Notification MakeFailureNotification(HRESULT hr, const ImportItem& import_item)
    {
        return std::make_shared<ImportNotification>(hr, Unknown, import_item);
    }
    static Notification MakeFailureNotification(HRESULT hr, ImportItem&& import_item)
    {
        return std::make_shared<ImportNotification>(hr, Unknown, import_item);
    }

    HRESULT GetHR() const { return m_hr; }
    const ImportItem& Item() const { return m_item; }
    ActionTaken Action() const { return m_action; }

    ~ImportNotification(void);
};

}  // namespace Orc

#pragma managed(pop)
