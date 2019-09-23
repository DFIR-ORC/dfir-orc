//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "ToolEmbed.h"

#include "Robustness.h"

#include "StructuredOutputWriter.h"
#include "EmbeddedResource.h"
#include "ConfigFileReader.h"
#include "LogFileWriter.h"

#include "ConfigFile_ToolEmbed.h"

#include "boost\scope_exit.hpp"

using namespace std;

using namespace Orc;
using namespace Orc::Command::ToolEmbed;

HRESULT Main::WriteEmbedConfig(
    const std::wstring& strOutputFile,
    const std::wstring& strMothership,
    const std::vector<EmbeddedResource::EmbedSpec>& values)
{
    OutputSpec outputEmbedFile;

    outputEmbedFile.Type = OutputSpec::Kind::StructuredFile;
    outputEmbedFile.Path = strOutputFile;
    outputEmbedFile.OutputEncoding = OutputSpec::Encoding::UTF8;

    auto writer = StructuredOutputWriter::GetWriter(_L_, outputEmbedFile);

    if (writer == nullptr)
    {
        log::Error(_L_, E_FAIL, L"Failed to create structured output writer for %s\r\n", strOutputFile.c_str());
        return E_FAIL;
    }

    writer->BeginElement(L"toolembed");

    writer->BeginElement(L"input");
    writer->WriteString(strMothership.c_str());
    writer->EndElement(L"input");

    for (const auto& item : values)
    {
        switch (item.Type)
        {
            case EmbeddedResource::EmbedSpec::File:
                writer->BeginElement(L"file");
                writer->WriteNameValuePair(L"name", item.Name.c_str());
                writer->WriteNameValuePair(L"path", item.Value.c_str());
                writer->EndElement(L"file");
                break;
            case EmbeddedResource::EmbedSpec::NameValuePair:
                writer->BeginElement(L"pair");
                writer->WriteNameValuePair(L"name", item.Name.c_str());
                writer->WriteNameValuePair(L"value", item.Value.c_str());
                writer->EndElement(L"pair");
                break;
            case EmbeddedResource::EmbedSpec::Archive:
                writer->BeginElement(L"archive");
                writer->WriteNameValuePair(L"name", item.Name.c_str());
                writer->WriteNameValuePair(L"format", item.ArchiveFormat.c_str());

                for (const auto& arch_item : item.ArchiveItems)
                {
                    writer->BeginElement(L"file");
                    writer->WriteNameValuePair(L"name", arch_item.Name.c_str());
                    writer->WriteNameValuePair(L"path", arch_item.Path.c_str());
                    writer->EndElement(L"file");
                }

                writer->EndElement(L"archive");
        }
    }

    writer->EndElement(L"toolembed");

    return S_OK;
}

HRESULT Main::Run_Embed()
{
    HRESULT hr = E_FAIL;

    // First, create the output file.
    if (!CopyFile(config.strInputFile.c_str(), config.Output.Path.c_str(), FALSE))
    {
        log::Error(
            _L_,
            HRESULT_FROM_WIN32(GetLastError()),
            L"Failed to copy file %s into %s\n",
            config.strInputFile.c_str(),
            config.Output.Path.c_str());
        return hr;
    }

    log::Info(_L_, L"\r\nUpdating resources in %s\r\n", config.Output.Path.c_str());

    if (FAILED(hr = EmbeddedResource::UpdateResources(_L_, config.Output.Path, config.ToEmbed)))
    {
        log::Error(_L_, hr, L"Failed to update resources in file %s\r\n", config.Output.Path.c_str());
        if (!DeleteFile(config.Output.Path.c_str()))
        {
            log::Error(
                _L_,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Failed to delete failed output file %s\n",
                config.Output.Path.c_str());
            return hr;
        }
        return hr;
    }

    log::Info(_L_, L"Done updating resources in %s\r\n\r\n", config.Output.Path.c_str());

    return S_OK;
}

HRESULT Main::Run_Dump()
{
    HRESULT hr = E_FAIL;
    WCHAR szPreviousCurDir[MAX_PATH];

    GetCurrentDirectory(MAX_PATH, szPreviousCurDir);
    BOOST_SCOPE_EXIT((&szPreviousCurDir)) { SetCurrentDirectory(szPreviousCurDir); }
    BOOST_SCOPE_EXIT_END;

    SetCurrentDirectory(config.Output.Path.c_str());

    std::vector<EmbeddedResource::EmbedSpec> values;

    if (FAILED(hr = EmbeddedResource::EnumValues(_L_, config.strInputFile, values)))
    {
        log::Error(_L_, hr, L"Failed to enumerate values from %s\n", config.strInputFile.c_str());
        return hr;
    }
    if (FAILED(hr = EmbeddedResource::EnumBinaries(_L_, config.strInputFile, values)))
    {
        log::Error(_L_, hr, L"Failed to enumerate binaries from %s\n", config.strInputFile.c_str());
        return hr;
    }

    if (FAILED(hr = EmbeddedResource::ExpandArchivesAndBinaries(_L_, L".", values)))
    {
        log::Error(
            _L_,
            hr,
            L"Failed to expand files and archivesfrom %s into Ms\n",
            config.strInputFile.c_str(),
            config.Output.Path.c_str());
        return hr;
    }

    if (FAILED(hr = EmbeddedResource::DeleteEmbeddedRessources(_L_, config.strInputFile, L".\\Mothership.exe", values)))
    {
        log::Error(_L_, hr, L"Failed to delete ressources from %s\n", config.strInputFile.c_str());
        return hr;
    }

    if (FAILED(hr = WriteEmbedConfig(L".\\Embed.xml", L".\\Mothership.exe", values)))
    {
        log::Error(_L_, hr, L"Failed to write embedding configuration for dump\r\n");
        return hr;
    }

    return S_OK;
}

HRESULT Main::Run_FromDump()
{
    HRESULT hr = E_FAIL;

    WCHAR szPreviousCurDir[MAX_PATH];

    GetCurrentDirectory(MAX_PATH, szPreviousCurDir);
    BOOST_SCOPE_EXIT((&szPreviousCurDir)) { SetCurrentDirectory(szPreviousCurDir); }
    BOOST_SCOPE_EXIT_END;

    SetCurrentDirectory(config.strInputFile.c_str());

    config.strConfigFile = L".\\Embed.xml";
    config.strInputFile = L".\\Mothership.exe";

    ConfigFileReader reader(_L_);

    ConfigItem embed_config;
    if (FAILED(hr = Orc::Config::ToolEmbed::root(embed_config)))
    {
        log::Error(_L_, hr, L"Failed to create config item to embed\r\n");
        return hr;
    }

    if (FAILED(hr = reader.ReadConfig(config.strConfigFile.c_str(), embed_config)))
    {
        log::Error(_L_, hr, L"Failed to read embed config file %s\r\n", config.strConfigFile.c_str());
        return hr;
    }

    if (FAILED(hr = GetConfigurationFromConfig(embed_config)))
    {
        log::Error(_L_, hr, L"Failed to obtain embed configuration\r\n", config.strConfigFile.c_str());
        return hr;
    }

    return Run_Embed();
}

HRESULT Main::Run()
{
    HRESULT hr = E_FAIL;

    switch (config.Todo)
    {
        case Main::Action::Embed:
            return Run_Embed();
        case Main::Action::Dump:
            return Run_Dump();
        case Main::Action::FromDump:
            return Run_FromDump();
    }

    return S_OK;
}
