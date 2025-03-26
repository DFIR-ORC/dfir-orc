//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "ToolEmbed.h"

#include <boost/scope_exit.hpp>

#include "Robustness.h"
#include "StructuredOutputWriter.h"
#include "EmbeddedResource.h"
#include "Configuration/ConfigFileReader.h"
#include "ConfigFile_ToolEmbed.h"

using namespace Orc::Command::ToolEmbed;
using namespace Orc;

HRESULT Main::WriteEmbedConfig(
    const std::wstring& outputPath,
    const std::wstring& mothershipPath,
    const std::vector<EmbeddedResource::EmbedSpec>& values)
{
    OutputSpec outputEmbedFile;

    outputEmbedFile.Type = OutputSpec::Kind::StructuredFile;
    outputEmbedFile.Path = outputPath;
    outputEmbedFile.OutputEncoding = OutputSpec::Encoding::UTF8;

    auto writer = StructuredOutputWriter::GetWriter(outputEmbedFile, nullptr);

    if (writer == nullptr)
    {
        Log::Error(L"Failed to create structured output writer for '{}'", outputPath);
        return E_FAIL;
    }

    writer->BeginElement(L"toolembed");

    writer->BeginElement(L"input");
    writer->Write(mothershipPath.c_str());
    writer->EndElement(L"input");

    for (const auto& item : values)
    {
        switch (item.Type)
        {
            case EmbeddedResource::EmbedSpec::EmbedType::File:
                writer->BeginElement(L"file");
                writer->WriteNamed(L"name", item.Name.c_str());
                writer->WriteNamed(L"path", item.Value.c_str());
                writer->EndElement(L"file");
                break;
            case EmbeddedResource::EmbedSpec::EmbedType::NameValuePair:
                writer->BeginElement(L"pair");
                writer->WriteNamed(L"name", item.Name.c_str());
                writer->WriteNamed(L"value", item.Value.c_str());
                writer->EndElement(L"pair");
                break;
            case EmbeddedResource::EmbedSpec::EmbedType::Archive:
                writer->BeginElement(L"archive");
                writer->WriteNamed(L"name", item.Name.c_str());
                writer->WriteNamed(L"format", item.ArchiveFormat.c_str());

                for (const auto& arch_item : item.ArchiveItems)
                {
                    writer->BeginElement(L"file");
                    writer->WriteNamed(L"name", arch_item.Name.c_str());
                    writer->WriteNamed(L"path", arch_item.Path.c_str());
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
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(
            L"Failed to copy file '{}' into '{}' [{}]", config.strInputFile, config.Output.Path, SystemError(hr));
        return hr;
    }

    m_console.Print(L"Updating resources in '{}'", config.Output.Path);

    hr = EmbeddedResource::UpdateResources(config.Output.Path, config.ToEmbed);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to update resources in file '{}' [{}]", config.Output.Path, SystemError(hr));

        if (!DeleteFile(config.Output.Path.c_str()))
        {
            HRESULT deleteHR = HRESULT_FROM_WIN32(GetLastError());
            Log::Error(L"Failed to delete failed output file '{}' [{}]", config.Output.Path, SystemError(deleteHR));
        }

        return hr;
    }

    Log::Info(L"Done updating resources in '{}'", config.Output.Path);

    return S_OK;
}

HRESULT Main::Run_Dump()
{
    HRESULT hr = E_FAIL;
    WCHAR szPreviousCurDir[ORC_MAX_PATH];

    GetCurrentDirectory(ORC_MAX_PATH, szPreviousCurDir);
    BOOST_SCOPE_EXIT((&szPreviousCurDir)) { SetCurrentDirectory(szPreviousCurDir); }
    BOOST_SCOPE_EXIT_END;

    SetCurrentDirectory(config.Output.Path.c_str());

    std::vector<EmbeddedResource::EmbedSpec> values;

    hr = EmbeddedResource::EnumValues(config.strInputFile, values);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to enumerate values from '{}' [{}]", config.strInputFile, SystemError(hr));
        return hr;
    }

    hr = EmbeddedResource::EnumBinaries(config.strInputFile, values);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to enumerate binaries from '{}' [{}]", config.strInputFile, SystemError(hr));
        return hr;
    }

    hr = EmbeddedResource::ExpandArchivesAndBinaries(L".", values);
    if (FAILED(hr))
    {
        Log::Error(
            L"Failed to expand files and archives from '{}' into '{}' [{}]",
            config.strInputFile,
            config.Output.Path,
            SystemError(hr));
        return hr;
    }

    hr = EmbeddedResource::DeleteEmbeddedResources(config.strInputFile, L".\\Mothership.exe", values);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to delete resources from '{}' [{}]", config.strInputFile, SystemError(hr));
        return hr;
    }

    hr = WriteEmbedConfig(L".\\Embed.xml", L".\\Mothership.exe", values);
    if (FAILED(hr))
    {
        Log::Error("Failed to write embedding configuration for dump [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}

HRESULT Main::Run_FromDump()
{
    HRESULT hr = E_FAIL;

    WCHAR szPreviousCurDir[ORC_MAX_PATH];

    GetCurrentDirectory(ORC_MAX_PATH, szPreviousCurDir);
    BOOST_SCOPE_EXIT((&szPreviousCurDir)) { SetCurrentDirectory(szPreviousCurDir); }
    BOOST_SCOPE_EXIT_END;

    SetCurrentDirectory(config.strInputFile.c_str());

    config.strConfigFile = L".\\Embed.xml";
    config.strInputFile = L".\\Mothership.exe";

    ConfigItem embed_config;
    hr = Orc::Config::ToolEmbed::root(embed_config);
    if (FAILED(hr))
    {
        Log::Error("Failed to create config item to embed [{}]", SystemError(hr));
        return hr;
    }

    ConfigFileReader reader;
    hr = reader.ReadConfig(config.strConfigFile.c_str(), embed_config);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to read embed config file '{}' [{}]", config.strConfigFile, SystemError(hr));
        return hr;
    }

    hr = GetConfigurationFromConfig(embed_config);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to obtain embed configuration '{}' [{}]", config.strConfigFile, SystemError(hr));
        return hr;
    }

    return Run_Embed();
}

HRESULT Main::Run()
{
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
