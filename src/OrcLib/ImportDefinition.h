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

class LogFileWriter;
class ConfigItem;

namespace Command::ImportData {
class Main;
}

class ORCLIB_API ImportDefinition
{
    friend class Command::ImportData::Main;

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

        std::wstring NameMatch;
        std::wstring Table;
        std::wstring Password;

        std::wstring BeforeStatement;
        std::wstring AfterStatement;

        Item(Item&& other)
        {
            std::swap(ToDo, other.ToDo);
            std::swap(NameMatch, other.NameMatch);
            std::swap(Table, other.Table);
            std::swap(BeforeStatement, other.BeforeStatement);
            std::swap(AfterStatement, other.AfterStatement);
            std::swap(Password, other.Password);
        };

        Item(const Item& other) = default;
        Item() = default;
    };

private:
    logger _L_;

    std::vector<ImportDefinition::Item> m_ItemDefinitions;

public:
    ImportDefinition(logger pLog)
        : _L_(std::move(pLog)) {};
    ImportDefinition(ImportDefinition&& other) noexcept = default;
    ImportDefinition(const ImportDefinition& other) = default;

    const std::vector<ImportDefinition::Item>& GetDefinitions() const { return m_ItemDefinitions; };

    ~ImportDefinition();
};

}  // namespace Orc

#pragma managed(pop)
