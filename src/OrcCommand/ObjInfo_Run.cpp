//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "ObjInfo.h"

#include "ObjectDirectory.h"
#include "FileDirectory.h"
#include "LogFileWriter.h"

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
                inchar == L'<'  ||  //< (less than)
                inchar == L'>'  ||  //> (greater than)
                inchar == L':'  ||  //: (colon)
                inchar == L'\"' ||  //" (double quote)
                inchar == L'/'  ||  /// (forward slash)
                inchar == L'\\' ||  //\ (backslash)
                inchar == L'|'  ||  //| (vertical bar or pipe)
                inchar == L'?'  ||  //? (question mark)
                inchar == L'*');    //* (asterisk)
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
        if (FAILED(hr = m_outputs.Prepare(config.output)))
        {
            log::Error(_L_, hr, L"Failed to prepare archive for %s\r\n", config.output.Path.c_str());
            return hr;
        }
    }

    BOOST_SCOPE_EXIT(&config, &m_outputs) { m_outputs.CloseAll(config.output); }
    BOOST_SCOPE_EXIT_END;

    if (FAILED(hr = m_outputs.GetWriters(config.output, L"ObjInfo")))
    {
        log::Error(_L_, hr, L"Failed to create objinfo output writers\r\n");
        return hr;
    }

    log::Info(_L_, L"\r\nEnumerating object directories:\r\n");

    m_outputs.ForEachOutput(config.output, [this](const MultipleOutput<DirectoryOutput>::OutputPair& dir) -> HRESULT {

        using namespace std::string_view_literals;

        HRESULT hr = E_FAIL;
        // Actually enumerate objects here

        ObjectDirectory objectdir(_L_);
        FileDirectory filedir(_L_);

        std::vector<ObjectDirectory::ObjectInstance> objects;
        std::vector<FileDirectory::FileInstance> files;

        log::Info(
            _L_,
            L"\tDirectory %s %s\r\n",
            dir.first.m_Type == ObjectDir ? L"ObjInfo" : L"FileInfo",
            dir.first.m_Directory.c_str());

        switch (dir.first.m_Type)
        {
            case DirectoryType::ObjectDir:
                if (SUCCEEDED(hr = objectdir.ParseObjectDirectory(dir.first.m_Directory, objects)))
                {
                    if (dir.second == nullptr)
                    {
                        log::Error(
                            _L_, E_INVALIDARG, L"Not output writer configured for \"%s\" directory, skipped\r\n");
                        return hr;
                    }

                    for (auto& obj : objects)
                    {
                        obj.Write(_L_, dir.second->GetTableOutput(), L""s);
                    }
                }
                else
                {
                    log::Error(
                        _L_, hr, L"Failed to enumerate objects in directory %s\r\n", dir.first.m_Directory.c_str());
                    return hr;
                }
                break;

            case DirectoryType::FileDir:

                if (SUCCEEDED(hr = filedir.ParseFileDirectory(dir.first.m_Directory, files)))
                {
                    for (auto& file : files)
                    {
                        file.Write(_L_, dir.second->GetTableOutput(), L""s);
                    }
                }
                else
                {
                    log::Error(
                        _L_, hr, L"Failed to enumerate objects in directory %s\r\n", dir.first.m_Directory.c_str());
                    return hr;
                }

                break;
        }
        return S_OK;
    });

    return S_OK;
}
