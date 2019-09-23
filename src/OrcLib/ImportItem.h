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
    std::wstring Name;
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

    ImportItemFormat Format = ImportItemFormat::Undetermined;
    boost::logic::tribool isToIgnore = boost::logic::indeterminate;
    boost::logic::tribool isToImport = boost::logic::indeterminate;
    boost::logic::tribool isToExtract = boost::logic::indeterminate;
    boost::logic::tribool isToExpand = boost::logic::indeterminate;

    SYSTEMTIME ImportStart;
    SYSTEMTIME ImportEnd;

    ULONGLONG ullMemBytesCharged = 0LL;
    ULONGLONG ullFileBytesCharged = 0LL;

    ULONGLONG ullBytesExtracted = 0LL;
    ULONGLONG ullLinesImported = 0LL;

    const ImportDefinition::Item* DefinitionItem = nullptr;
    const ImportDefinition* Definitions = nullptr;

    std::wstring FullName;
    std::wstring Name;

    std::wstring FullComputerName;
    std::wstring ComputerName;
    std::wstring SystemType;
    std::wstring TimeStamp;

    std::shared_ptr<std::wstring> InputFile;
    std::shared_ptr<std::wstring> OutputFile;

    std::shared_ptr<ByteStream> Stream;

    bool bPrefixSubItem = false;

public:
    ImportItem() = default;

    ImportItem(ImportItem&& other)
        : DefinitionItem(other.DefinitionItem)
    {
        std::swap(InputFile, other.InputFile);
        std::swap(Definitions, other.Definitions);
        std::swap(DefinitionItem, other.DefinitionItem);
        std::swap(Format, other.Format);
        std::swap(FullName, other.FullName);
        std::swap(Name, other.Name);
        std::swap(ComputerName, other.ComputerName);
        std::swap(SystemType, other.SystemType);
        std::swap(TimeStamp, other.TimeStamp);
        std::swap(Stream, other.Stream);
        std::swap(isToIgnore, other.isToIgnore);
        std::swap(isToExtract, other.isToExtract);
        std::swap(isToImport, other.isToImport);
        std::swap(isToExpand, other.isToExpand);
    }

    ImportItem(const ImportItem&) = default;
    ImportItem& operator=(const ImportItem&) = default;

    std::shared_ptr<ByteStream> GetInputStream(const logger& pLog);
    std::shared_ptr<ByteStream> GetOutputStream(const logger& pLog);
    std::wstring GetBaseName(const logger& pLog);

    const ImportDefinition::Item* GetDefinitionItem() const { return DefinitionItem; }

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
