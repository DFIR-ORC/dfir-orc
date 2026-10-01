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

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/join.hpp>

#include "Robustness.h"
#include "StructuredOutputWriter.h"
#include "EmbeddedResource.h"
#include "OutputSpec.h"
#include "Configuration/ConfigFileReader.h"
#include "ConfigFile_ToolEmbed.h"
#include "Command/WolfLauncher/ConfigFile_WOLFLauncher.h"

using namespace Orc::Command::ToolEmbed;
using namespace Orc;

namespace {

constexpr auto kWolfLauncherConfig = L"WOLFLAUNCHER_CONFIG";

// Resource names under which this embedding designates its WolfLauncher configuration. Two forms exist:
//   <File name="WOLFLAUNCHER_CONFIG" path="..."/>                                              (direct)
//   <Pair name="WOLFLAUNCHER_CONFIG" value="res:#wolf_config"/> + <File name="wolf_config" .../>
// In the indirect form the File spec carries the resource id, not the pair name, so matching a File spec
// against WOLFLAUNCHER_CONFIG alone would never fire and the parse gate below would be dead for that config.
std::vector<std::wstring> WolfConfigResourceNames(const std::vector<EmbeddedResource::EmbedSpec>& toEmbed)
{
    std::vector<std::wstring> names {kWolfLauncherConfig};

    for (const auto& spec : toEmbed)
    {
        if (spec.Type != EmbeddedResource::EmbedSpec::EmbedType::NameValuePair
            || !boost::iequals(spec.Name, kWolfLauncherConfig))
        {
            continue;
        }

        // Only a "res:#<name>" value points at a File spec embedded here; anything else names no local
        // resource and is left to EmbeddedResource's own reference check.
        const size_t hash = spec.Value.rfind(L'#');
        if (hash == std::wstring::npos || hash + 1 >= spec.Value.size())
        {
            Log::Debug(L"ToolEmbed: WOLFLAUNCHER_CONFIG pair value '{}' is not a local resource link", spec.Value);
            continue;
        }

        names.push_back(spec.Value.substr(hash + 1));
    }

    return names;
}

bool IsWolfConfigResourceName(const std::vector<std::wstring>& wolfConfigNames, const std::wstring& name)
{
    for (const auto& candidate : wolfConfigNames)
    {
        if (boost::iequals(candidate, name))
        {
            return true;
        }
    }

    return false;
}

// Renders an <upload> entry's 'include'/'exclude' patterns for a log line.
std::wstring FilterText(const OutputSpec::Upload& upload)
{
    if (upload.FilterInclude.empty() && upload.FilterExclude.empty())
    {
        return L"no filter";
    }

    std::wstring text;

    if (!upload.FilterInclude.empty())
    {
        text = fmt::format(L"include='{}'", boost::join(upload.FilterInclude, L";"));
    }

    if (!upload.FilterExclude.empty())
    {
        if (!text.empty())
        {
            text += L" ";
        }

        text += fmt::format(L"exclude='{}'", boost::join(upload.FilterExclude, L";"));
    }

    return text;
}

// Warns when the <upload> entries of one configuration do not agree on their 'include'/'exclude'
// patterns. Each destination applies its own, so a file the first entry drops is still offered to a
// fallback that accepts it - which is the point of a fallback, but surprises a configuration that used
// 'exclude' expecting the file to stay on the machine. Advisory only: differing filters are legal, and
// refusing them would break configurations already deployed.
void WarnOnUploadFilterMismatch(const std::wstring& path, const std::vector<OutputSpec::Upload>& uploads)
{
    for (size_t i = 1; i < uploads.size(); ++i)
    {
        if (uploads[i].FilterInclude == uploads[0].FilterInclude
            && uploads[i].FilterExclude == uploads[0].FilterExclude)
        {
            continue;
        }

        Log::Warn(
            L"ToolEmbed: '{}': <upload> #{} ({}) does not filter like <upload> #1 ({}); each destination "
            L"applies its own patterns, so a file one of them drops is still sent to the other",
            path,
            i + 1,
            FilterText(uploads[i]),
            FilterText(uploads[0]));
    }
}

// Validates the embedded WolfLauncher configuration. Refuses (Critical) a WOLFLAUNCHER_CONFIG resource
// that does not parse against the schema, and any <upload> section that is insecure without the
// required flag - so an invalid or dangerous binary cannot be produced - and warns on advisory issues
// (e.g. BITS). Returns E_FAIL if anything is refused. The same OutputSpec::Upload::Validate() drives
// the runtime gate, so the two stay consistent.
HRESULT ValidateEmbeddedUploadSecurity(const std::vector<EmbeddedResource::EmbedSpec>& toEmbed)
{
    bool refused = false;
    const std::vector<std::wstring> wolfConfigNames = WolfConfigResourceNames(toEmbed);

    for (const auto& spec : toEmbed)
    {
        if (spec.Type != EmbeddedResource::EmbedSpec::EmbedType::File)
        {
            continue;
        }

        const std::wstring& path = spec.Value;

        // For a resource this embedding designates as the WolfLauncher config (see WolfConfigResourceNames),
        // a parse failure means an invalid configuration would be embedded - refuse it (Critical) rather than
        // produce a binary that fails at deployment. Any OTHER embedded .xml may legitimately not be a
        // WolfLauncher config, so an unreadable one is skipped as before.
        const bool isWolfConfig = IsWolfConfigResourceName(wolfConfigNames, spec.Name);

        // The WolfLauncher config is checked whatever it is called on disk: the resource name is what
        // makes it the configuration, not the file extension, so a gate keyed on ".xml" would be avoided
        // by renaming the file to .cfg. Other embedded files stay filtered by extension - they are
        // scanned on the off chance they are a config, and only .xml is worth parsing.
        if (!isWolfConfig && !boost::iends_with(path, L".xml"))
        {
            continue;
        }

        ConfigItem schema;
        if (FAILED(Orc::Config::Wolf::root(schema)))
        {
            continue;
        }

        ConfigFileReader reader;
        if (FAILED(reader.ReadConfig(path.c_str(), schema)))
        {
            if (isWolfConfig)
            {
                Log::Error(L"ToolEmbed: refusing to embed invalid WolfLauncher config '{}'", path);
                refused = true;
            }
            else
            {
                Log::Debug(L"ToolEmbed: '{}' is not a readable WolfLauncher config; skipping upload check", path);
            }

            continue;
        }

        std::vector<OutputSpec::Upload> uploads;

        for (const auto& uploadItem : schema[WOLFLAUNCHER_UPLOAD].NodeList)
        {
            OutputSpec::Upload upload;
            if (FAILED(upload.Configure(uploadItem)))
            {
                Log::Error(L"ToolEmbed: invalid <upload> section in embedded config '{}'", path);
                refused = true;
                continue;
            }

            const auto check = upload.Validate();
            if (check.verdict == OutputSpec::Upload::Verdict::Refuse)
            {
                Log::Error(L"ToolEmbed: refusing to embed '{}': {}", path, check.message);
                refused = true;
            }
            else if (check.verdict == OutputSpec::Upload::Verdict::Warn)
            {
                Log::Warn(L"ToolEmbed: '{}': {}", path, check.message);
            }

            uploads.push_back(std::move(upload));
        }

        WarnOnUploadFilterMismatch(path, uploads);
    }

    if (refused)
    {
        return E_FAIL;
    }

    return S_OK;
}

}  // namespace

HRESULT Main::WriteEmbedConfig(const std::wstring& outputPath, const std::vector<EmbeddedResource::EmbedSpec>& values)
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

    hr = WriteEmbedConfig(L".\\Embed.xml", values);
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

    if (config.m_force)
    {
        std::error_code ec;
        std::filesystem::remove(output, ec);
        if (ec)
        {
            Log::Debug(L"Failed to remove file (path: {}) [{}]", output, ec);
        }
    }

    if (config.m_embedPath)
    {
        // Cleanup existing resources to avoid conflicts with new ones and new configuration
        hr = EmbeddedResource::DeleteEmbeddedResources(input, output, values);
        if (FAILED(hr))
        {
            Log::Error(L"Failed to delete resources from '{}' [{}]", output, SystemError(hr));
            return hr;
        }
    }
    else
    {
        if (!CopyFileExW(input.c_str(), output.c_str(), nullptr, nullptr, nullptr, COPY_FILE_FAIL_IF_EXISTS))
        {
            Log::Error(L"Failed CopyFileExW (input: {}, output: {}) [{}]", input, output, SystemError(hr));
            return hr;
        }
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
            // Validate the upload security of the configuration being embedded - only on a fresh
            // embed, not when reconstructing from a dump (that source was validated when first built).
            if (config.Todo == Main::Action::Embed)
            {
                if (HRESULT hrSec = ValidateEmbeddedUploadSecurity(config.ToEmbed); FAILED(hrSec))
                {
                    Log::Error(L"ToolEmbed: aborting - an embedded upload configuration is insecure or invalid");
                    return hrSec;
                }
            }

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
