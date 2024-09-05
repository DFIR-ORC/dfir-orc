//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "ObjInfo.h"

#include "ObjectDirectory.h"
#include "FileDirectory.h"

#include "SystemDetails.h"

#include <boost\scope_exit.hpp>

using namespace std;

using namespace Orc;
using namespace Orc::Command::ObjInfo;

std::wstring Main::DirectoryOutput::GetIdentifier()
{
    std::wstring retval = m_Directory.c_str();
    std::replace_if(
        begin(retval),
        end(retval),
        [](const WCHAR inchar) -> bool {
            return (
                inchar == L'<' ||  //< (less than)
                inchar == L'>' ||  //> (greater than)
                inchar == L':' ||  //: (colon)
                inchar == L'\"' ||  //" (double quote)
                inchar == L'/' ||  /// (forward slash)
                inchar == L'\\' ||  //\ (backslash)
                inchar == L'|' ||  //| (vertical bar or pipe)
                inchar == L'?' ||  //? (question mark)
                inchar == L'*');  //* (asterisk)
        },
        L'_');

    return retval;
}

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    SYSTEMTIME CollectionTime;
    GetSystemTime(&CollectionTime);
    SystemTimeToFileTime(&CollectionTime, &CollectionDate);

    if (config.output.Type == OutputSpec::Kind::Archive)
    {
        hr = m_outputs.Prepare(config.output);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to prepare archive for '{}' [{}]", config.output.Path, SystemError(hr));
            return hr;
        }
    }

    BOOST_SCOPE_EXIT(&config, &m_outputs) { m_outputs.CloseAll(config.output); }
    BOOST_SCOPE_EXIT_END;

    hr = m_outputs.GetWriters(config.output, L"ObjInfo", OutputInfo::DataType::kObjInfo);
    if (FAILED(hr))
    {
        Log::Error("Failed to create objinfo output writers [{}]", SystemError(hr));
        return hr;
    }

    m_console.Print(L"Enumerating object directories:");

    m_outputs.ForEachOutput(config.output, [this](const MultipleOutput<DirectoryOutput>::OutputPair& dir) -> HRESULT {
        using namespace std::string_view_literals;

        HRESULT hr = E_FAIL;
        // Actually enumerate objects here

        ObjectDirectory objectdir;
        FileDirectory filedir;

        std::vector<ObjectDirectory::ObjectInstance> objects;
        std::vector<FileDirectory::FileInstance> files;

        auto directoryNode = m_console.OutputTree();
        directoryNode.AddNode(
            L"Directory {} {}", dir.first.m_Type == ObjectDir ? L"ObjInfo" : L"FileInfo", dir.first.m_Directory);

        switch (dir.first.m_Type)
        {
            case DirectoryType::ObjectDir:
                if (SUCCEEDED(hr = objectdir.ParseObjectDirectory(dir.first.m_Directory, objects)))
                {
                    if (dir.second.Writer() == nullptr)
                    {
                        Log::Error(L"No output writer configured for '{}' directory, skipped", dir.first.m_Directory);
                        return hr;
                    }

                    for (auto& obj : objects)
                    {
                        obj.Write(*dir.second.Writer(), L""s);
                    }
                }
                else
                {
                    Log::Error(
                        L"Failed to enumerate objects in directory '{}' [{}]", dir.first.m_Directory, SystemError(hr));
                    return hr;
                }
                break;

            case DirectoryType::FileDir:

                if (SUCCEEDED(hr = filedir.ParseFileDirectory(dir.first.m_Directory, files)))
                {
                    for (auto& file : files)
                    {
                        file.Write(*dir.second.Writer(), L""s);
                    }
                }
                else
                {
                    Log::Error(
                        L"Failed to enumerate objects in directory '{}' [{}]", dir.first.m_Directory, SystemError(hr));
                    return hr;
                }

                break;
        }

        return S_OK;
    });

    return S_OK;
}
