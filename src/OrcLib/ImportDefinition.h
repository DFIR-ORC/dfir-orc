//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#pragma managed(push, off)

namespace Orc {

class ConfigItem;

namespace Command::ImportData {
class Main;
}

namespace Command::ExtractData {
class Main;
}

class ORCLIB_API ImportDefinition
{
    friend class Command::ImportData::Main;
    friend class Command::ExtractData::Main;

public:
    enum Action
    {
        Undetermined,
        Ignore,
        Import,
        Extract,
        Expand
    };

    class Item
    {
    public:
        Action ToDo = Undetermined;

        std::wstring nameMatch;
        std::wstring tableName;
        std::wstring Password;

        std::wstring BeforeStatement;
        std::wstring AfterStatement;

        Item(Item&& other)
        {
            std::swap(ToDo, other.ToDo);
            std::swap(nameMatch, other.nameMatch);
            std::swap(tableName, other.tableName);
            std::swap(BeforeStatement, other.BeforeStatement);
            std::swap(AfterStatement, other.AfterStatement);
            std::swap(Password, other.Password);
        };

        Item(const Item& other) = default;
        Item() = default;
    };

private:
    std::vector<ImportDefinition::Item> m_itemDefinitions;

public:
    ImportDefinition() noexcept = default;
    ImportDefinition(ImportDefinition&& other) noexcept = default;
    ImportDefinition(const ImportDefinition& other) = default;

    const std::vector<ImportDefinition::Item>& GetDefinitions() const { return m_itemDefinitions; };

    ~ImportDefinition();
};

}  // namespace Orc

#pragma managed(pop)
