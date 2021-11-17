//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <string>

#include "OrcLib.h"

#include "FastFind.h"

#include "TableOutput.h"
#include "TableOutputWriter.h"

#include "SystemDetails.h"
#include "FileFind.h"
#include "MFTWalker.h"
#include "Location.h"
#include "FileFind.h"
#include "DevNullStream.h"
#include "CryptoHashStream.h"

#include "SnapshotVolumeReader.h"

#include "CaseInsensitive.h"

#include <boost/scope_exit.hpp>
#include <fmt/chrono.h>

using namespace std;

using namespace Orc;
using namespace Orc::Command::FastFind;

namespace {

template <typename T>
void PrintStatistics(Orc::Text::Tree<T>& root, const std::vector<std::shared_ptr<FileFind::SearchTerm>>& searchTerms)
{
    auto statsNode = root.AddNode(L"Statistics for 'ntfs_find' rules:");
    statsNode.AddEmptyLine();

    if (searchTerms.empty())
    {
        statsNode.Add(L"<No rule defined>");
        statsNode.AddEmptyLine();
        return;
    }

    for (const auto& searchTerm : searchTerms)
    {
        const auto& stats = searchTerm->GetProfilingStatistics();

        auto& rule = searchTerm->GetRule();
        if (rule.empty())
        {
            Log::Warn("Failed to display statistics for an 'ntfs_find' empty rule");
            continue;
        }

        auto ruleNode = statsNode.AddNode(searchTerm->GetRule());
        ruleNode.Add(
            "Match: {:%H:%M:%S}, items: {}/{}, read: {}",
            std::chrono::duration_cast<std::chrono::seconds>(stats.MatchTime()),
            stats.Match(),
            stats.Match() + stats.Miss(),
            Traits::ByteQuantity(stats.MatchReadLength()));

        ruleNode.AddEmptyLine();
    }
}

template <typename T>
void PrintFoundFile(
    Orc::Text::Tree<T>& root,
    const std::wstring& path,
    const std::wstring& matchDescription,
    bool isDeleted)
{
    root.AddWithoutEOL(L"{:<24} {} ({})", L"Found file:", path, matchDescription);
    if (isDeleted)
    {
        root.Append(" [DELETED]");
    }

    root.AddEOL();
}

template <typename T>
void PrintFoundKey(Orc::Text::Tree<T>& root, const std::string& path)
{
    root.Add("{:<24} {}", "Found registry key:", path);
}

template <typename T>
void PrintFoundValue(Orc::Text::Tree<T>& root, const std::string& key, const std::string& value)
{
    root.Add(L"{:<24} {} ({})", L"Found registry value:", key, value);
}

template <typename T>
void PrintFoundWindowsObject(Orc::Text::Tree<T>& root, const std::wstring& name, const std::wstring& description)
{
    root.Add(L"{:>24} {} ({})", L"Found windows object:", name, description);
}

}  // namespace

HRESULT Main::RegFlushKeys()
{
    bool bSuccess = true;
    DWORD dwGLE = 0L;

    Log::Debug("Flushing HKEY_LOCAL_MACHINE");
    dwGLE = RegFlushKey(HKEY_LOCAL_MACHINE);
    if (dwGLE != ERROR_SUCCESS)
        bSuccess = false;

    Log::Debug("Flushing HKEY_USERS");
    dwGLE = RegFlushKey(HKEY_USERS);
    if (dwGLE != ERROR_SUCCESS)
        bSuccess = false;

    if (!bSuccess)
        return HRESULT_FROM_WIN32(dwGLE);
    return S_OK;
}

HRESULT Main::RunFileSystem()
{
    HRESULT hr = E_FAIL;

    if (pStructuredOutput)
        pStructuredOutput->BeginCollection(L"filesystem");

    hr = config.FileSystem.Files.Find(
        config.FileSystem.Locations,
        [this](const std::shared_ptr<FileFind::Match>& aMatch, bool& bStop) {
            wstring strMatchDescr = aMatch->Term->GetDescription();

            bool bDeleted = aMatch->DeletedRecord;
            std::for_each(
                begin(aMatch->MatchingNames),
                end(aMatch->MatchingNames),
                [strMatchDescr, bDeleted, this](const FileFind::Match::NameMatch& aNameMatch) {
                    ::PrintFoundFile(m_console.OutputTree(), aNameMatch.FullPathName, strMatchDescr, bDeleted);
                });

            if (pFileSystemTableOutput)
                aMatch->Write(*pFileSystemTableOutput);
            if (pStructuredOutput)
                aMatch->Write(*pStructuredOutput, nullptr);

            return;
        },
        true);

    if (FAILED(hr))
    {
        Log::Error(L"Failed while parsing locations");
        return hr;
    }

    if (pStructuredOutput)
        pStructuredOutput->EndCollection(L"filesystem");

    m_console.PrintNewLine();
    ::PrintStatistics(m_console.OutputTree(), config.FileSystem.Files.AllSearchTerms());
    return S_OK;
}

HRESULT Main::RunRegistry()
{
    HRESULT hr = E_FAIL;

    RegFlushKeys();

    hr = config.Registry.Files.Find(
        config.Registry.Locations,
        [this](const std::shared_ptr<FileFind::Match>& aFileMatch, bool& bStop) {
            Log::Debug(
                L"Hive '{}' matches '{}'",
                aFileMatch->MatchingNames.front().FullPathName,
                aFileMatch->Term->GetDescription());
        },
        false);

    if (FAILED(hr))
    {
        Log::Error(L"Failed to parse location while searching for registry hives");
    }

    if (pStructuredOutput)
    {
        pStructuredOutput->BeginCollection(L"registry");
    }

    for (const auto& aFileMatch : config.Registry.Files.Matches())
    {
        Log::Debug(L"Parsing registry hive '{}'", aFileMatch->MatchingNames.front().FullPathName);

        if (pStructuredOutput)
        {
            pStructuredOutput->BeginElement(nullptr);
            pStructuredOutput->WriteNamed(L"volume_id", aFileMatch->VolumeReader->VolumeSerialNumber(), true);

            auto reader = std::dynamic_pointer_cast<SnapshotVolumeReader>(aFileMatch->VolumeReader);
            if (reader)
            {
                pStructuredOutput->WriteNamed(L"snapshot_id", reader->GetSnapshotID());
            }
            else
            {
                pStructuredOutput->WriteNamed(L"snapshot_id", GUID_NULL);
            }

            pStructuredOutput->WriteNamed(L"hive_path", aFileMatch->MatchingNames.front().FullPathName.c_str());
        }

        for (const auto& data : aFileMatch->MatchingAttributes)
        {
            for (auto& aregfind : config.Registry.RegistryFind)
            {
                data.DataStream->SetFilePointer(0LL, FILE_BEGIN, NULL);

                if (FAILED(hr = aregfind.Find(data.DataStream, nullptr, nullptr)))
                {
                    Log::Error(
                        L"Failed while parsing registry hive '{}' [{}]",
                        aFileMatch->MatchingNames.front().FullPathName,
                        SystemError(hr));
                }
                else
                {
                    Log::Debug(L"Successfully parsed hive '{}'", aFileMatch->MatchingNames.front().FullPathName);
                    // write matching elements

                    auto& Results = aregfind.Matches();

                    for (const auto& elt : Results)
                    {
                        // write matching keys
                        for (const auto& key : elt.second->MatchingKeys)
                        {
                            ::PrintFoundKey(m_console.OutputTree(), key.KeyName);
                        }

                        for (const auto& value : elt.second->MatchingValues)
                        {
                            ::PrintFoundValue(m_console.OutputTree(), value.ValueName, value.KeyName);
                        }

                        if (pStructuredOutput)
                            elt.second->Write(*pStructuredOutput);
                    }
                }
            }
        }

        if (pStructuredOutput)
            pStructuredOutput->EndElement(nullptr);
    }

    if (pStructuredOutput)
    {
        pStructuredOutput->EndCollection(L"registry");
    }

    return S_OK;
}

HRESULT
Main::LogObjectMatch(const ObjectSpec::ObjectItem& spec, const ObjectDirectory::ObjectInstance& obj, LPCWSTR szElement)
{
    HRESULT hr = E_FAIL;

    if (pStructuredOutput)
    {
        pStructuredOutput->BeginElement(szElement);
        pStructuredOutput->WriteNamed(L"description", spec.Description().c_str());
        obj.Write(*pStructuredOutput);
        pStructuredOutput->EndElement(szElement);
    }
    if (pObjectTableOutput)
    {
        obj.Write(*pObjectTableOutput, spec.Description());
    }

    return S_OK;
}

HRESULT
Main::LogObjectMatch(const ObjectSpec::ObjectItem& spec, const FileDirectory::FileInstance& file, LPCWSTR szElement)
{
    HRESULT hr = E_FAIL;

    if (pStructuredOutput)
    {
        pStructuredOutput->BeginElement(szElement);
        pStructuredOutput->WriteNamed(L"description", spec.Description().c_str());
        file.Write(*pStructuredOutput);
        pStructuredOutput->EndElement(szElement);
    }

    if (pObjectTableOutput)
    {
        file.Write(*pObjectTableOutput, spec.Description());
    }

    return S_OK;
}

HRESULT Main::RunObject()
{
    HRESULT hr = E_FAIL;

    if (pStructuredOutput)
        pStructuredOutput->BeginCollection(L"object_directory");

    for (const auto& objdir : ObjectDirs)
    {
        ObjectDirectory objectdir;
        std::vector<ObjectDirectory::ObjectInstance> objects;

        if (FAILED(hr = objectdir.ParseObjectDirectory(objdir, objects)))
        {
            Log::Error(L"Failed to parse object directory '{}' [{}]", objdir, SystemError(hr));
        }
        else
        {
            for (const auto& object : objects)
            {
                for (const auto& spec : config.Object.Items)
                {
                    if (spec.ObjType != ObjectDirectory::Invalid && spec.ObjType != object.Type)
                        continue;  // object is of a wrong type

                    if (!spec.strName.empty())
                    {
                        switch (spec.name_typeofMatch)
                        {
                            case ObjectSpec::MatchType::Exact:
                                if (!equalCaseInsensitive(object.Name, spec.strName))
                                    continue;  //
                                break;
                            case ObjectSpec::MatchType::Match:
                                if (!PathMatchSpec(object.Name.c_str(), spec.strName.c_str()))
                                    continue;  //
                                break;
                            case ObjectSpec::MatchType::Regex: {
                                if (spec.name_regexp != nullptr)
                                {
                                    wsmatch m;
                                    if (!regex_match(object.Name, m, *spec.name_regexp))
                                        continue;
                                }
                            }
                            break;
                            default:
                                break;
                        }
                    }

                    if (!spec.strPath.empty())
                    {
                        switch (spec.path_typeofMatch)
                        {
                            case ObjectSpec::MatchType::Exact:
                                if (!equalCaseInsensitive(object.Path, spec.strPath))
                                    continue;  //
                                break;
                            case ObjectSpec::MatchType::Match:
                                if (!PathMatchSpec(object.Path.c_str(), spec.strPath.c_str()))
                                    continue;  //
                                break;
                            case ObjectSpec::MatchType::Regex: {
                                if (spec.path_regexp != nullptr)
                                {
                                    wsmatch m;
                                    if (!regex_match(object.Path, m, *spec.path_regexp))
                                        continue;
                                }
                            }
                            break;
                            default:
                                break;
                        }
                    }

                    // Dropping here means no previous test rejected the object
                    ::PrintFoundWindowsObject(m_console.OutputTree(), object.Path, spec.Description());
                    LogObjectMatch(spec, object, nullptr);
                }
            }
        }
    }

    for (const auto& filedir : FileDirs)
    {
        FileDirectory filedirectory;
        std::vector<FileDirectory::FileInstance> files;

        if (FAILED(hr = filedirectory.ParseFileDirectory(filedir, files)))
        {
            Log::Error(L"Failed to parse file directory {} [{}]", filedir, SystemError(hr));
        }
        else
        {
            for (const auto& file : files)
            {
                for (const auto& spec : config.Object.Items)
                {
                    if (spec.ObjType != ObjectDirectory::File)
                        continue;  // object is of a wrong type

                    if (!spec.strName.empty())
                    {
                        switch (spec.name_typeofMatch)
                        {
                            case ObjectSpec::MatchType::Exact:
                                if (!equalCaseInsensitive(file.Name, spec.strName))
                                    continue;  //
                                break;
                            case ObjectSpec::MatchType::Match:
                                if (!PathMatchSpec(file.Name.c_str(), spec.strName.c_str()))
                                    continue;  //
                                break;
                            case ObjectSpec::MatchType::Regex: {
                                if (spec.name_regexp != nullptr)
                                {
                                    wsmatch m;
                                    if (!regex_match(file.Name, m, *spec.name_regexp))
                                        continue;
                                }
                            }
                            break;
                            default:
                                break;
                        }
                    }

                    if (!spec.strPath.empty())
                    {
                        switch (spec.path_typeofMatch)
                        {
                            case ObjectSpec::MatchType::Exact:
                                if (!equalCaseInsensitive(file.Path, spec.strPath))
                                    continue;  //
                                break;
                            case ObjectSpec::MatchType::Match:
                                if (!PathMatchSpec(file.Path.c_str(), spec.strPath.c_str()))
                                    continue;  //
                                break;
                            case ObjectSpec::MatchType::Regex: {
                                if (spec.path_regexp != nullptr)
                                {
                                    wsmatch m;
                                    if (!regex_match(file.Path, m, *spec.path_regexp))
                                        continue;
                                }
                            }
                            break;
                            default:
                                break;
                        }
                    }

                    // Dropping here means no previous test rejected the object
                    LogObjectMatch(spec, file, nullptr);
                }
            }
        }
    }

    if (pStructuredOutput)
        pStructuredOutput->EndCollection(L"object_directory");

    return S_OK;
}

#include "StructuredOutputWriter.h"

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    RegFlushKeys();

    SYSTEMTIME CollectionTime;
    GetSystemTime(&CollectionTime);
    SystemTimeToFileTime(&CollectionTime, &CollectionDate);

    SystemDetails::GetOrcFullComputerName(ComputerName);

    if (config.outFileSystem.Type != OutputSpec::Kind::None)
        pFileSystemTableOutput = TableOutput::GetWriter(config.outFileSystem);

    if (config.outRegsitry.Type != OutputSpec::Kind::None)
        pRegistryTableOutput = TableOutput::GetWriter(config.outRegsitry);

    if (config.outObject.Type != OutputSpec::Kind::None)
        pObjectTableOutput = TableOutput::GetWriter(config.outObject);

    if (HasFlag(config.outStructured.Type, OutputSpec::Kind::StructuredFile))
    {
        pStructuredOutput = StructuredOutputWriter::GetWriter(config.outStructured, nullptr);
    }
    else if (config.outStructured.Type == OutputSpec::Kind::Directory)
    {
        auto pStructuredOutput = StructuredOutputWriter::GetWriter(
            config.outStructured, L"{Name}_{SystemType}_{ComputerName}.xml", L"FastFind", nullptr);
    }

    if (pStructuredOutput != nullptr)
    {
        pStructuredOutput->BeginElement(L"fast_find");
        pStructuredOutput->WriteNamed(L"computer", ComputerName.c_str());

        std::wstring strSystemDescr;
        if (SUCCEEDED(SystemDetails::GetDescriptionString(strSystemDescr)))
            pStructuredOutput->WriteNamed(L"os", strSystemDescr.c_str());

        std::wstring strSystemRole;
        if (SUCCEEDED(SystemDetails::GetSystemType(strSystemRole)))
            pStructuredOutput->WriteNamed(L"role", strSystemRole.c_str());
    }

    RunFileSystem();
    RunRegistry();
    RunObject();

    if (pStructuredOutput != nullptr)
        pStructuredOutput->EndElement(L"fast_find");

    if (pFileSystemTableOutput != nullptr)
    {
        pFileSystemTableOutput->Close();
        pFileSystemTableOutput = nullptr;
    }

    if (pRegistryTableOutput != nullptr)
    {
        pRegistryTableOutput->Close();
        pRegistryTableOutput = nullptr;
    }

    if (pObjectTableOutput != nullptr)
    {
        pObjectTableOutput->Close();
        pObjectTableOutput = nullptr;
    }

    if (pStructuredOutput != nullptr)
    {
        pStructuredOutput->Close();
        pStructuredOutput = nullptr;
    }

    return S_OK;
}
