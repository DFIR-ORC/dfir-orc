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

    if (pWriterOutput)
        pWriterOutput->BeginCollection(L"filesystem");

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

                    if (pFileSystemWriter)
                        aMatch->Write(_L_, pFileSystemWriter->GetTableOutput());
                    if (pWriterOutput)
                        aMatch->Write(_L_, pWriterOutput);

                    return;
                },
                true)))
    {
        log::Error(_L_, hr, L"Failed while parsing locations\r\n");
        return hr;
    }

    if (pWriterOutput)
        pWriterOutput->EndCollection(L"filesystem");
    return S_OK;
}

HRESULT Main::RunRegistry()
{
    HRESULT hr = E_FAIL;

    RegFlushKeys();

    if (pWriterOutput)
        pWriterOutput->BeginCollection(L"registry");

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

    for (const auto& aFileMatch : config.Registry.Files.Matches())
    {
        log::Verbose(_L_, L"Parsing registry hive %s\t\n", aFileMatch->MatchingNames.front().FullPathName.c_str());

        if (pWriterOutput)
        {
            pWriterOutput->BeginCollection(L"hive");
            pWriterOutput->WriteNameValuePair(L"volume_id", aFileMatch->VolumeReader->VolumeSerialNumber(), true);

            auto reader = std::dynamic_pointer_cast<SnapshotVolumeReader>(aFileMatch->VolumeReader);
            if (reader)
            {
                pWriterOutput->WriteNameGUIDPair(L"snapshot_id", reader->GetSnapshotID());
            }
            else
            {
                pWriterOutput->WriteNameGUIDPair(L"snapshot_id", GUID_NULL);
            }

            pWriterOutput->WriteNameValuePair(L"hive_path", aFileMatch->MatchingNames.front().FullPathName.c_str());
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

                        if (pWriterOutput)
                            elt.second->Write(_L_, pWriterOutput, CollectionDate);
                    }
                }
            }
        }

        if (pWriterOutput)
            pWriterOutput->EndCollection(L"hive");
    }

    if (pWriterOutput)
        pWriterOutput->EndCollection(L"registry");

    return S_OK;
}

HRESULT Main::LogObjectMatch(const ObjectDirectory::ObjectInstance& obj)
{
    HRESULT hr = E_FAIL;

    if (pWriterOutput)
    {
        obj.Write(_L_, pWriterOutput, CollectionDate);
    }
    if (pObjectWriter)
    {
        obj.Write(_L_, pObjectWriter->GetTableOutput(), CollectionDate);
    }
    return S_OK;
}

HRESULT Main::LogObjectMatch(const FileDirectory::FileInstance& file)
{
    HRESULT hr = E_FAIL;

    if (pWriterOutput)
    {
        file.Write(_L_, pWriterOutput, CollectionDate);
    }

    if (pObjectWriter)
    {
        file.Write(_L_, pObjectWriter->GetTableOutput(), CollectionDate);
    }
    return S_OK;
}

HRESULT Main::RunObject()
{
    HRESULT hr = E_FAIL;

    if (pWriterOutput)
        pWriterOutput->BeginCollection(L"object");

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

                    LogObjectMatch(object);
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
                    LogObjectMatch(file);
                }
            }
        }
    }

    if (pWriterOutput)
        pWriterOutput->EndCollection(L"object");

    return S_OK;
}

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    RegFlushKeys();

    SYSTEMTIME CollectionTime;
    GetSystemTime(&CollectionTime);
    SystemTimeToFileTime(&CollectionTime, &CollectionDate);

    SystemDetails::GetOrcComputerName(ComputerName);

    if (config.outFileSystem.Type != OutputSpec::Kind::None)
        pFileSystemWriter = TableOutput::GetWriter(_L_, config.outFileSystem);

    if (config.outRegsitry.Type != OutputSpec::Kind::None)
        pRegistryWriter = TableOutput::GetWriter(_L_, config.outRegsitry);

    if (config.outRegsitry.Type != OutputSpec::Kind::None)
        pObjectWriter = TableOutput::GetWriter(_L_, config.outObject);

    if (config.outStructured.Type & OutputSpec::Kind::StructuredFile)
    {
        pWriterOutput = StructuredOutputWriter::GetWriter(_L_, config.outStructured);
    }
    else if (config.outStructured.Type == OutputSpec::Kind::Directory)
    {
        pWriterOutput = StructuredOutputWriter::GetWriter(
            _L_, config.outStructured, L"{Name}_{SystemType}_{ComputerName}.xml", L"FastFind");
    }

    if (pWriterOutput != nullptr)
    {
        pWriterOutput->BeginElement(L"fast_find");
        pWriterOutput->WriteNameValuePair(L"computer", ComputerName.c_str());

        std::wstring strSystemDescr;
        if (SUCCEEDED(SystemDetails::GetDescriptionString(strSystemDescr)))
            pWriterOutput->WriteNameValuePair(L"os", strSystemDescr.c_str());

        std::wstring strSystemRole;
        if (SUCCEEDED(SystemDetails::GetSystemType(strSystemRole)))
            pWriterOutput->WriteNameValuePair(L"role", strSystemRole.c_str());
    }

    RunFileSystem();
    RunRegistry();
    RunObject();

    if (pWriterOutput != nullptr)
        pWriterOutput->EndElement(L"fast_find");

    if (pFileSystemWriter != nullptr)
    {
        pFileSystemWriter->Close();
        pFileSystemWriter = nullptr;
    }

    if (pRegistryWriter != nullptr)
    {
        pRegistryWriter->Close();
        pRegistryWriter = nullptr;
    }

    if (pObjectWriter != nullptr)
    {
        pObjectWriter->Close();
        pObjectWriter = nullptr;
    }

    if (pWriterOutput != nullptr)
    {
        pWriterOutput->Close();
        pWriterOutput = nullptr;
    }
    return S_OK;
}
