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

#include "ParameterCheck.h"

#include "ImportDefinition.h"

#include "CaseInsensitive.h"

#include <filesystem>
#include <boost\scope_exit.hpp>

namespace fs = std::filesystem;

using namespace Orc;

std::shared_ptr<ByteStream> ImportItem::GetInputStream()
{
    if (Stream)
    {
        if (Stream->CanRead() == S_OK)
            return Stream;
    }
    if (inputFile)
    {
        auto retval = std::make_shared<FileStream>();
        HRESULT hr = E_FAIL;
        if (FAILED(hr = retval->ReadFrom(inputFile->c_str())))
        {
            Log::Error(L"Failed to open import item {} [{}]", fullName, SystemError(hr));
            return nullptr;
        }
        return Stream = retval;
    }
    return nullptr;
}

std::shared_ptr<ByteStream> ImportItem::GetOutputStream()
{
    if (Stream)
    {
        if (Stream->CanRead())
            return Stream;
    }

    if (!fullName.empty())
    {
        auto retval = std::make_shared<FileStream>();
        HRESULT hr = E_FAIL;
        if (FAILED(
                hr = retval->OpenFile(
                    fullName.c_str(),
                    GENERIC_READ | GENERIC_WRITE,
                    0L,
                    NULL,
                    OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL)))
        {
            Log::Error(L"Failed to open import item '{}' [{}]", fullName, SystemError(hr));
            return nullptr;
        }
        return Stream = retval;
    }
    return nullptr;
}

std::wstring ImportItem::GetBaseName()
{
    if (!name.empty())
    {
        HRESULT hr = E_FAIL;
        WCHAR szBasename[MAX_PATH];
        if (FAILED(hr = GetBaseNameForFile(name.c_str(), szBasename, MAX_PATH)))
        {
            Log::Error(L"Failed to extract basename for file '{}' [{}]", name, SystemError(hr));
            return L"";
        }

        return szBasename;
    }
    if (!fullName.empty())
    {
        HRESULT hr = E_FAIL;
        WCHAR szBasename[MAX_PATH];
        if (FAILED(hr = GetBaseNameForFile(fullName.c_str(), szBasename, MAX_PATH)))
        {
            Log::Error(L"Failed to extract basename for file '{}' [{}]", fullName, SystemError(hr));
            return L"";
        }
        return szBasename;
    }
    return L"";
}

ImportItem::ImportItemFormat ImportItem::GetFileFormat()
{
    if (format != Undetermined)
        return format;

    fs::path path(name);
    std::wstring ext = path.extension();

    if (!ext.empty())
    {
        if (equalCaseInsensitive(ext, L".p7b"))
            return format = ImportItem::ImportItemFormat::Envelopped;
        if (equalCaseInsensitive(ext, L".7z"))
            return format = ImportItem::ImportItemFormat::Archive;
        if (equalCaseInsensitive(ext, L".csv"))
            return format = ImportItem::ImportItemFormat::CSV;
        if (equalCaseInsensitive(ext, L".xml"))
            return format = ImportItem::ImportItemFormat::XML;
        if (equalCaseInsensitive(ext, L".txt"))
            return format = ImportItem::ImportItemFormat::Text;
        if (equalCaseInsensitive(ext, L".log"))
            return format = ImportItem::ImportItemFormat::Text;
        if (equalCaseInsensitive(ext, L".zip"))
            return format = ImportItem::ImportItemFormat::Archive;
        if (equalCaseInsensitive(ext, L".cab"))
            return format = ImportItem::ImportItemFormat::Archive;
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
                        return format = ImportItem::ImportItemFormat::RegistryHive;
                    }
                    else if (
                        Header[0] == 'E' && Header[1] == 'l' && Header[2] == 'f' && Header[3] == 'F' && Header[4] == 'i'
                        && Header[5] == 'l' && Header[6] == 'e')
                    {
                        return format = ImportItem::ImportItemFormat::EventLog;
                    }
                }
            }
        }
    }
    return format = ImportItem::ImportItemFormat::Data;
}

const ImportDefinition::Item* ImportItem::DefinitionItemLookup(ImportDefinition::Action action)
{
    if (definitionItem)
        return definitionItem;

    if (definitions)
    {
        for (const auto& def : definitions->GetDefinitions())
        {
            if (def.ToDo == action)
            {
                if (PathMatchSpec(name.c_str(), def.nameMatch.c_str()))
                {
                    return definitionItem = &def;
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
    return (bool)(isToIgnore = IsToIgnore(definitions, name, &definitionItem));
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
                if (PathMatchSpec(strName.c_str(), def.nameMatch.c_str()))
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
    return (bool)(isToImport = IsToImport(definitions, name, &definitionItem));
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
                if (PathMatchSpec(strName.c_str(), def.nameMatch.c_str()))
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
    return (bool)(isToExtract = IsToExtract(definitions, name, &definitionItem));
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
                if (PathMatchSpec(strName.c_str(), def.nameMatch.c_str()))
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
    return (bool)(isToExpand = IsToExpand(definitions, name, &definitionItem));
}

bool ImportItem::IsToExpand(
    const ImportDefinition* pDefs,
    const std::wstring& strName,
    const ImportDefinition::Item** ppItem)
{
    return !IsToExtract(pDefs, strName, ppItem);
}
