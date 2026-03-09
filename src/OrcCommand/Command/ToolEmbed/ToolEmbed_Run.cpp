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

    if (!mothershipPath.empty())
    {
        writer->BeginElement(L"input");
        writer->Write(mothershipPath.c_str());
        writer->EndElement(L"input");
    }

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

    std::wstring input = m_capsule.value_or(config.strInputFile);

    std::vector<EmbeddedResource::EmbedSpec> values;
    hr = EmbeddedResource::EnumValues(input, values);
    if (FAILED(hr))
    {
        if (hr != HRESULT_FROM_WIN32(ERROR_RESOURCE_TYPE_NOT_FOUND))
        {
            Log::Debug(L"Failed to enumerate values from '{}' [{}]", input, SystemError(hr));
            return hr;
        }
    }

    hr = EmbeddedResource::EnumBinaries(input, values);
    if (FAILED(hr))
    {
        if (hr != HRESULT_FROM_WIN32(ERROR_RESOURCE_TYPE_NOT_FOUND))
        {
            Log::Debug(L"Failed to enumerate binaries from '{}' [{}]", input, SystemError(hr));
            return hr;
        }
    }

    hr = EmbeddedResource::ExpandArchivesAndBinaries(L".", values);
    if (FAILED(hr))
    {
        Log::Error(
            L"Failed to expand files and archives from '{}' into '{}' [{}]",
            input,
            config.Output.Path,
            SystemError(hr));
        return hr;
    }

    std::wstring bootstrap;
    if (!m_capsule)
    {
        bootstrap = L".\\Mothership.exe";

        hr = EmbeddedResource::DeleteEmbeddedResources(input, bootstrap, values);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to delete resources from '{}' [{}]", input, SystemError(hr));
            return hr;
        }
    }

    hr = WriteEmbedConfig(L".\\Embed.xml", bootstrap, values);
    if (FAILED(hr))
    {
        Log::Error("Failed to write embedding configuration for dump [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}

HRESULT Main::Run_EmbedCapsule()
{
    HRESULT hr = E_FAIL;

    if (!m_capsule)
    {
        Log::Error(L"Unknown Capsule path");
        return E_INVALIDARG;
    }

    const auto input = *m_capsule;
    const auto output = config.Output.Path;

    std::vector<EmbeddedResource::EmbedSpec> values;
    hr = EmbeddedResource::ExpandArchivesAndBinaries(L".", values);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to expand files and archives from '{}' into '{}' [{}]", input, output, SystemError(hr));
        return hr;
    }

    hr = EmbeddedResource::DeleteEmbeddedResources(input, output, values);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to delete resources from '{}' [{}]", output, SystemError(hr));
        return hr;
    }

    m_console.Print(L"Updating resources in '{}'", output);

    hr = EmbeddedResource::UpdateResources(output, config.ToEmbed);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to update resources in file '{}' [{}]", output, SystemError(hr));

        if (!DeleteFileW(output.c_str()))
        {
            HRESULT deleteHR = HRESULT_FROM_WIN32(GetLastError());
            Log::Error(L"Failed to delete failed output file '{}' [{}]", output, SystemError(deleteHR));
        }

        return hr;
    }

    Log::Info(L"Done updating resources in '{}'", output);
    return S_OK;
}

HRESULT Main::Run()
{
    WCHAR szPreviousCurDir[ORC_MAX_PATH];
    GetCurrentDirectoryW(ORC_MAX_PATH, szPreviousCurDir);

    switch (config.Todo)
    {
        case Main::Action::Dump: {
            if (config.Output.Path == szPreviousCurDir)
            {
                return Run_Dump();
            }

            BOOST_SCOPE_EXIT((&szPreviousCurDir))
            {
                SetCurrentDirectoryW(szPreviousCurDir);
            }
            BOOST_SCOPE_EXIT_END;

            SetCurrentDirectoryW(config.Output.Path.c_str());
            return Run_Dump();
        }
        case Main::Action::Embed:
        case Main::Action::FromDump: {
            if (!config.m_embedDirectory || config.m_embedDirectory == szPreviousCurDir)
            {
                if (m_capsule)
                {
                    return Run_EmbedCapsule();
                }
                else
                {
                    return Run_Embed();
                }
            }
            BOOST_SCOPE_EXIT((&szPreviousCurDir))
            {
                SetCurrentDirectoryW(szPreviousCurDir);
            }
            BOOST_SCOPE_EXIT_END;

            SetCurrentDirectoryW(config.m_embedDirectory->c_str());

            if (m_capsule)
            {
                return Run_EmbedCapsule();
            }
            else
            {
                return Run_Embed();
            }
        }

        default:
            return E_FAIL;
    }
}
