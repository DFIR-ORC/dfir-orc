//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "ImportItem.h"

#include "FileStream.h"

#include "LogFileWriter.h"

#include "ParameterCheck.h"

#include "ImportDefinition.h"

#include "CaseInsensitive.h"

#include <filesystem>
#include <boost\scope_exit.hpp>

namespace fs = std::experimental::filesystem;

using namespace Orc;

std::shared_ptr<ByteStream> ImportItem::GetInputStream(const logger& pLog)
{
    if (Stream)
    {
        if (Stream->CanRead() == S_OK)
            return Stream;
    }
    if (InputFile)
    {
        auto retval = std::make_shared<FileStream>(pLog);
        HRESULT hr = E_FAIL;
        if (FAILED(hr = retval->ReadFrom(InputFile->c_str())))
        {
            log::Error(pLog, hr, L"Failed to open import item %s\r\n", FullName.c_str());
            return nullptr;
        }
        return Stream = retval;
    }
    return nullptr;
}

std::shared_ptr<ByteStream> ImportItem::GetOutputStream(const logger& pLog)
{
    if (Stream)
    {
        if (Stream->CanRead())
            return Stream;
    }

    if (!FullName.empty())
    {
        auto retval = std::make_shared<FileStream>(pLog);
        HRESULT hr = E_FAIL;
        if (FAILED(
                hr = retval->OpenFile(
                    FullName.c_str(),
                    GENERIC_READ | GENERIC_WRITE,
                    0L,
                    NULL,
                    OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL)))
        {
            log::Error(pLog, hr, L"Failed to open import item %s\r\n", FullName.c_str());
            return nullptr;
        }
        return Stream = retval;
    }
    return nullptr;
}

std::wstring ImportItem::GetBaseName(const logger& pLog)
{
    if (!Name.empty())
    {
        HRESULT hr = E_FAIL;
        WCHAR szBasename[MAX_PATH];
        if (FAILED(hr = GetBaseNameForFile(Name.c_str(), szBasename, MAX_PATH)))
        {
            log::Error(pLog, hr, L"Failed to extract basename for file %s\r\n", Name.c_str());
            return L"";
        }
        return szBasename;
    }
    if (!FullName.empty())
    {
        HRESULT hr = E_FAIL;
        WCHAR szBasename[MAX_PATH];
        if (FAILED(hr = GetBaseNameForFile(FullName.c_str(), szBasename, MAX_PATH)))
        {
            log::Error(pLog, hr, L"Failed to extract basename for file %s\r\n", FullName.c_str());
            return L"";
        }
        return szBasename;
    }
    return L"";
}

ImportItem::ImportItemFormat ImportItem::GetFileFormat()
{
    if (Format != Undetermined)
        return Format;

    fs::path path(Name);
    std::wstring ext = path.extension();

    if (!ext.empty())
    {
        if (equalCaseInsensitive(ext, L".p7b"))
            return Format = ImportItem::ImportItemFormat::Envelopped;
        if (equalCaseInsensitive(ext, L".7z"))
            return Format = ImportItem::ImportItemFormat::Archive;
        if (equalCaseInsensitive(ext, L".csv"))
            return Format = ImportItem::ImportItemFormat::CSV;
        if (equalCaseInsensitive(ext, L".xml"))
            return Format = ImportItem::ImportItemFormat::XML;
        if (equalCaseInsensitive(ext, L".txt"))
            return Format = ImportItem::ImportItemFormat::Text;
        if (equalCaseInsensitive(ext, L".log"))
            return Format = ImportItem::ImportItemFormat::Text;
        if (equalCaseInsensitive(ext, L".zip"))
            return Format = ImportItem::ImportItemFormat::Archive;
        if (equalCaseInsensitive(ext, L".cab"))
            return Format = ImportItem::ImportItemFormat::Archive;
    }

    if (Stream != nullptr)
    {
        ULONGLONG ullPrevious = 0LL;
        if (SUCCEEDED(Stream->SetFilePointer(0, FILE_CURRENT, &ullPrevious)))
        {
            BOOST_SCOPE_EXIT(ullPrevious, Stream) { Stream->SetFilePointer(ullPrevious, FILE_BEGIN, NULL); }
            BOOST_SCOPE_EXIT_END;

            if (SUCCEEDED(Stream->SetFilePointer(0, FILE_BEGIN, NULL)))
            {
                BYTE Header[16];
                ULONGLONG ullRead = 0LL;
                if (SUCCEEDED(Stream->Read(Header, 16, &ullRead)))
                {
                    if (Header[0] == 'r' && Header[1] == 'e' && Header[2] == 'g' && Header[3] == 'f')
                    {
                        return Format = ImportItem::ImportItemFormat::RegistryHive;
                    }
                    else if (
                        Header[0] == 'E' && Header[1] == 'l' && Header[2] == 'f' && Header[3] == 'F' && Header[4] == 'i'
                        && Header[5] == 'l' && Header[6] == 'e')
                    {
                        return Format = ImportItem::ImportItemFormat::EventLog;
                    }
                }
            }
        }
    }
    return Format = ImportItem::ImportItemFormat::Data;
}

const ImportDefinition::Item* ImportItem::DefinitionItemLookup(ImportDefinition::Action action)
{
    if (DefinitionItem)
        return DefinitionItem;

    if (Definitions)
    {
        for (const auto& def : Definitions->GetDefinitions())
        {
            if (def.ToDo == action)
            {
                if (PathMatchSpec(Name.c_str(), def.NameMatch.c_str()))
                {
                    return DefinitionItem = &def;
                }
            }
        }
    }
    return nullptr;
}

bool ImportItem::IsToIgnore()
{
    if (isToIgnore != boost::indeterminate)
        return (bool)isToIgnore;
    return (bool)(isToIgnore = IsToIgnore(Definitions, Name, &DefinitionItem));
}

bool ImportItem::IsToIgnore(
    const ImportDefinition* pDefs,
    const std::wstring& strName,
    const ImportDefinition::Item** ppItem)
{
    if (pDefs)
    {
        for (const auto& def : pDefs->GetDefinitions())
        {
            if (def.ToDo == ImportDefinition::Ignore)
            {
                if (PathMatchSpec(strName.c_str(), def.NameMatch.c_str()))
                {
                    if (ppItem)
                        *ppItem = &def;
                    return true;
                }
            }
        }
    }
    return false;
}

bool ImportItem::IsToImport()
{
    if (isToImport != boost::indeterminate)
        return (bool)isToImport;
    return (bool)(isToImport = IsToImport(Definitions, Name, &DefinitionItem));
}

bool ImportItem::IsToImport(
    const ImportDefinition* pDefs,
    const std::wstring& strName,
    const ImportDefinition::Item** ppItem)
{
    if (pDefs)
    {
        for (const auto& def : pDefs->GetDefinitions())
        {
            if (def.ToDo == ImportDefinition::Import)
            {
                if (PathMatchSpec(strName.c_str(), def.NameMatch.c_str()))
                {
                    if (ppItem)
                        *ppItem = &def;
                    return true;
                }
            }
        }
    }
    return false;
}

bool ImportItem::IsToExtract()
{
    if (!boost::indeterminate(isToExtract))
        return (bool)isToExtract;
    return (bool)(isToExtract = IsToExtract(Definitions, Name, &DefinitionItem));
}

bool ImportItem::IsToExtract(
    const ImportDefinition* pDefs,
    const std::wstring& strName,
    const ImportDefinition::Item** ppItem)
{
    if (pDefs)
    {
        for (const auto& def : pDefs->GetDefinitions())
        {
            if (def.ToDo == ImportDefinition::Extract)
            {
                if (PathMatchSpec(strName.c_str(), def.NameMatch.c_str()))
                {
                    if (ppItem)
                        *ppItem = &def;
                    return true;
                }
            }
        }
    }
    return false;
}

bool ImportItem::IsToExpand()
{
    if (!boost::indeterminate(isToExpand))
        return (bool)isToExpand;
    if (IsToImport())
    {
        isToExpand = false;
        return false;
    }
    return (bool)(isToExpand = IsToExpand(Definitions, Name, &DefinitionItem));
}

bool ImportItem::IsToExpand(
    const ImportDefinition* pDefs,
    const std::wstring& strName,
    const ImportDefinition::Item** ppItem)
{
    return !IsToExtract(pDefs, strName, ppItem);
}
