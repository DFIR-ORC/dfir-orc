//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "ImportDefinition.h"

#include "ByteStream.h"

#include "TableOutputWriter.h"

#include <memory>

#include <boost/logic/tribool.hpp>

#pragma managed(push, off)

namespace Orc {

class ImportMessage;

enum TableDisposition
{
    AsIs = 0,
    Truncate,
    CreateNew
};

class TableDescription
{
public:
    std::wstring name;
    TableDisposition Disposition = TableDisposition::AsIs;
    DWORD dwConcurrency = 1;
    std::wstring Key;
    std::wstring Schema;
    std::wstring BeforeStatement;
    std::wstring AfterStatement;
    bool bCompress = false;
    bool bTABLOCK = true;
};

using TableDefinition = std::pair<TableDescription, TableOutput::Schema>;

class ORCLIB_API ImportItem
{
    friend class ImportMessage;

public:
    enum ImportItemFormat
    {
        Undetermined = 0,
        Envelopped,
        Archive,
        CSV,
        XML,
        Text,
        RegistryHive,
        EventLog,
        Data
    };

    ImportItemFormat format = ImportItemFormat::Undetermined;
    boost::logic::tribool isToIgnore = boost::logic::indeterminate;
    boost::logic::tribool isToImport = boost::logic::indeterminate;
    boost::logic::tribool isToExtract = boost::logic::indeterminate;
    boost::logic::tribool isToExpand = boost::logic::indeterminate;

    SYSTEMTIME importStart;
    SYSTEMTIME importEnd;

    ULONGLONG ullMemBytesCharged = 0LL;
    ULONGLONG ullFileBytesCharged = 0LL;

    ULONGLONG ullBytesExtracted = 0LL;
    ULONGLONG ullLinesImported = 0LL;

    const ImportDefinition::Item* definitionItem = nullptr;
    const ImportDefinition* definitions = nullptr;

    std::wstring fullName;
    std::wstring name;

    std::wstring fullComputerName;
    std::wstring computerName;
    std::wstring systemType;
    std::wstring timeStamp;

    std::shared_ptr<std::wstring> inputFile;
    std::shared_ptr<std::wstring> outputFile;

    std::shared_ptr<ByteStream> Stream;

    bool bPrefixSubItem = false;

public:
    ImportItem() = default;

    ImportItem(ImportItem&& other)
        : definitionItem(other.definitionItem)
    {
        std::swap(inputFile, other.inputFile);
        std::swap(definitions, other.definitions);
        std::swap(definitionItem, other.definitionItem);
        std::swap(format, other.format);
        std::swap(fullName, other.fullName);
        std::swap(name, other.name);
        std::swap(computerName, other.computerName);
        std::swap(systemType, other.systemType);
        std::swap(timeStamp, other.timeStamp);
        std::swap(Stream, other.Stream);
        std::swap(isToIgnore, other.isToIgnore);
        std::swap(isToExtract, other.isToExtract);
        std::swap(isToImport, other.isToImport);
        std::swap(isToExpand, other.isToExpand);
    }

    ImportItem(const ImportItem&) = default;
    ImportItem& operator=(const ImportItem&) = default;

    std::shared_ptr<ByteStream> GetInputStream();
    std::shared_ptr<ByteStream> GetOutputStream();
    std::wstring GetBaseName();

    const ImportDefinition::Item* GetDefinitionItem() const { return definitionItem; }

    bool IsToIgnore();
    bool IsToImport();
    bool IsToExtract();
    bool IsToExpand();

    static bool IsToIgnore(
        const ImportDefinition* pDefs,
        const std::wstring& strName,
        const ImportDefinition::Item** ppItem = nullptr);
    static bool IsToImport(
        const ImportDefinition* pDefs,
        const std::wstring& strName,
        const ImportDefinition::Item** ppItem = nullptr);
    static bool IsToExtract(
        const ImportDefinition* pDefs,
        const std::wstring& strName,
        const ImportDefinition::Item** ppItem = nullptr);
    static bool IsToExpand(
        const ImportDefinition* pDefs,
        const std::wstring& strName,
        const ImportDefinition::Item** ppItem = nullptr);

    ImportItemFormat GetFileFormat();

    const ImportDefinition::Item* DefinitionItemLookup(ImportDefinition::Action action);
};
}  // namespace Orc
#pragma managed(pop)
