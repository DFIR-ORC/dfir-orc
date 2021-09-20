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
#include "LogFileWriter.h"

#include "boost/scope_exit.hpp"

using namespace std;

using namespace Orc;
using namespace Orc::Command::FastFind;

HRESULT Main::RegFlushKeys()
{
    bool bSuccess = true;
    DWORD dwGLE = 0L;

    log::Info(_L_, L"\r\nFlushing HKEY_LOCAL_MACHINE\r\n");
    dwGLE = RegFlushKey(HKEY_LOCAL_MACHINE);
    if (dwGLE != ERROR_SUCCESS)
        bSuccess = false;

    log::Info(_L_, L"Flushing HKEY_USERS\r\n");
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

    if (FAILED(
            hr = config.FileSystem.Files.Find(
                config.FileSystem.Locations,
                [this](const std::shared_ptr<FileFind::Match>& aMatch, bool& bStop) {
                    wstring strMatchDescr = aMatch->Term->GetDescription();

                    bool bDeleted = aMatch->DeletedRecord;
                    std::for_each(
                        begin(aMatch->MatchingNames),
                        end(aMatch->MatchingNames),
                        [strMatchDescr, bDeleted, this](const FileFind::Match::NameMatch& aNameMatch) {
                            if (bDeleted)
                            {
                                log::Info(
                                    _L_,
                                    L"Found (deleted)       : %s (%s)\r\n",
                                    aNameMatch.FullPathName.c_str(),
                                    strMatchDescr.c_str());
                            }
                            else
                            {
                                log::Info(
                                    _L_,
                                    L"Found                 : %s (%s)\r\n",
                                    aNameMatch.FullPathName.c_str(),
                                    strMatchDescr.c_str());
                            }
                        });

                    if (pFileSystemTableOutput)
                        aMatch->Write(_L_, *pFileSystemTableOutput);
                    if (pStructuredOutput)
                        aMatch->Write(_L_, *pStructuredOutput, nullptr);

                    return;
                },
                true)))
    {
        log::Error(_L_, hr, L"Failed while parsing locations\r\n");
        return hr;
    }

    if (pStructuredOutput)
        pStructuredOutput->EndCollection(L"filesystem");
    return S_OK;
}

HRESULT Main::RunRegistry()
{
    HRESULT hr = E_FAIL;

    RegFlushKeys();

    if (FAILED(
            hr = config.Registry.Files.Find(
                config.Registry.Locations,
                [this](const std::shared_ptr<FileFind::Match>& aFileMatch, bool& bStop) {
                    log::Verbose(
                        _L_,
                        L"Hive %s matches %s\t\n",
                        aFileMatch->MatchingNames.front().FullPathName.c_str(),
                        aFileMatch->Term->GetDescription().c_str());
                },
                false)))
    {
        log::Error(_L_, hr, L"Failed to parse location while searching for registry hives\r\n");
    }

    if (pStructuredOutput)
    {
        pStructuredOutput->BeginCollection(L"registry");
    }

    for (const auto& aFileMatch : config.Registry.Files.Matches())
    {
        log::Verbose(_L_, L"Parsing registry hive %s\t\n", aFileMatch->MatchingNames.front().FullPathName.c_str());

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
                    log::Error(
                        _L_,
                        hr,
                        L"Failed while parsing registry hive %s\r\n",
                        aFileMatch->MatchingNames.front().FullPathName.c_str());
                }
                else
                {
                    log::Verbose(
                        _L_,
                        L"Successfully parsed hive %s\r\n",
                        aFileMatch->MatchingNames.front().FullPathName.c_str());
                    // write matching elements

                    auto& Results = aregfind.Matches();

                    for (const auto& elt : Results)
                    {
                        // write matching keys
                        for (const auto& key : elt.second->MatchingKeys)
                        {
                            log::Info(_L_, L"Found Key          : %S\r\n", key.KeyName.c_str());
                        }

                        for (const auto& value : elt.second->MatchingValues)
                        {
                            log::Info(
                                _L_,
                                L"Found Value        : %S (Key %S)\r\n",
                                value.ValueName.c_str(),
                                value.KeyName.c_str());
                        }

                        if (pStructuredOutput)
                            elt.second->Write(_L_, *pStructuredOutput);
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

HRESULT Main::LogObjectMatch(const ObjectSpec::ObjectItem& spec, const ObjectDirectory::ObjectInstance& obj, LPCWSTR szElement)
{
    HRESULT hr = E_FAIL;

    if (pStructuredOutput)
    {
        pStructuredOutput->BeginElement(szElement);
        pStructuredOutput->WriteNamed(L"description", spec.Description().c_str());
        obj.Write(_L_, *pStructuredOutput);
        pStructuredOutput->EndElement(szElement);
    }
    if (pObjectTableOutput)
    {
        obj.Write(_L_, *pObjectTableOutput, spec.Description());
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
        file.Write(_L_, *pStructuredOutput);
        pStructuredOutput->EndElement(szElement);

    }

    if (pObjectTableOutput)
    {
        file.Write(_L_, *pObjectTableOutput, spec.Description());
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
        ObjectDirectory objectdir(_L_);
        std::vector<ObjectDirectory::ObjectInstance> objects;

        if (FAILED(hr = objectdir.ParseObjectDirectory(objdir, objects)))
        {
            log::Error(_L_, hr, L"Failed to parse object directory %s\r\n", objdir.c_str());
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
                            case ObjectSpec::MatchType::Regex:
                            {
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
                            case ObjectSpec::MatchType::Regex:
                            {
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
                    log::Info(
                        _L_, L"Found                 : %s (%s)\r\n", object.Path.c_str(), spec.Description().c_str());

                    LogObjectMatch(spec, object, nullptr);
                }
            }
        }
    }

    for (const auto& filedir : FileDirs)
    {
        FileDirectory filedirectory(_L_);
        std::vector<FileDirectory::FileInstance> files;

        if (FAILED(hr = filedirectory.ParseFileDirectory(filedir, files)))
        {
            log::Error(_L_, hr, L"Failed to parse file directory %s\r\n", filedir.c_str());
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
                            case ObjectSpec::MatchType::Regex:
                            {
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
                            case ObjectSpec::MatchType::Regex:
                            {
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
        pFileSystemTableOutput = TableOutput::GetWriter(_L_, config.outFileSystem);

    if (config.outRegsitry.Type != OutputSpec::Kind::None)
        pRegistryTableOutput = TableOutput::GetWriter(_L_, config.outRegsitry);

    if (config.outObject.Type != OutputSpec::Kind::None)
        pObjectTableOutput = TableOutput::GetWriter(_L_, config.outObject);

    if (config.outStructured.Type & OutputSpec::Kind::StructuredFile)
    {
        pStructuredOutput = StructuredOutputWriter::GetWriter(_L_, config.outStructured, nullptr);
    }
    else if (config.outStructured.Type == OutputSpec::Kind::Directory)
    {
        auto writer = StructuredOutputWriter::GetWriter(
            _L_, config.outStructured, L"{Name}_{SystemType}_{ComputerName}.xml", L"FastFind", nullptr);
        pStructuredOutput = std::dynamic_pointer_cast<StructuredOutputWriter>(writer);
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
