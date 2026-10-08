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
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include "Robustness.h"
#include "StructuredOutputWriter.h"
#include "EmbeddedResource.h"
#include "OutputSpec.h"
#include "Configuration/ConfigFileReader.h"
#include "Configuration/UploadDestinationReader.h"
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

// One of the four side files a collection produces beside its archives, with the schema indices that
// say whether it is encrypted and whether it is uploaded.
struct CollectionArtifact
{
    DWORD node;
    DWORD encrypt;
    DWORD upload;
    const wchar_t* name;
};

// Whether one of those four reaches a destination in clear text.
//
// A node the configuration does not declare is not produced, so it is not reported: this warning
// names what actually leaves. 'encrypt' and 'upload' are both yes-by-default, so an absent attribute
// means encrypted - when a <recipient> exists at all - and uploaded.
bool LeavesHostInClear(const ConfigItem& schema, const CollectionArtifact& artifact, bool hasRecipient)
{
    const ConfigItem& node = schema[artifact.node];
    if (!node)
    {
        return false;
    }

    // upload="no" keeps the file on the machine, so no destination ever sees it.
    if (node[artifact.upload] && Orc::Config::Wolf::IsExplicitNo(node[artifact.upload]))
    {
        return false;
    }

    if (!hasRecipient)
    {
        return true;
    }

    return node[artifact.encrypt] && Orc::Config::Wolf::IsExplicitNo(node[artifact.encrypt]);
}

// Whether any <recipient> covers this <archive>, mirroring WolfExecution::SetRecipients: a recipient's
// 'archive' attribute is a ','/';' separated list of globs, each matched with PathMatchSpec against the
// archive's keyword (the command set) and against its name. Reproducing that match here is what lets
// the warning distinguish "encrypted" from "a recipient exists, but not for this archive".
bool ArchiveHasRecipient(const ConfigItem& schema, const ConfigItem& archiveNode)
{
    for (const ConfigItem& recipient : schema[WOLFLAUNCHER_RECIPIENT].NodeList)
    {
        if (!recipient[WOLFLAUNCHER_RECIPIENT_ARCHIVE])
        {
            continue;
        }

        std::vector<std::wstring> specs;
        boost::split(specs, (const std::wstring&)recipient[WOLFLAUNCHER_RECIPIENT_ARCHIVE], boost::is_any_of(L",;"));

        for (const std::wstring& spec : specs)
        {
            for (const DWORD index : {WOLFLAUNCHER_ARCHIVE_KEYWORD, WOLFLAUNCHER_ARCHIVE_NAME})
            {
                if (archiveNode[index]
                    && PathMatchSpecW(((const std::wstring&)archiveNode[index]).c_str(), spec.c_str()))
                {
                    return true;
                }
            }
        }
    }

    return false;
}

// How an <archive> is named in a warning: its keyword, else its name, else its position.
std::wstring ArchiveLabel(const ConfigItem& archiveNode, size_t index)
{
    for (const DWORD attribute : {WOLFLAUNCHER_ARCHIVE_KEYWORD, WOLFLAUNCHER_ARCHIVE_NAME})
    {
        if (archiveNode[attribute])
        {
            return fmt::format(L"archive '{}'", (const std::wstring&)archiveNode[attribute]);
        }
    }

    return fmt::format(L"archive #{}", index + 1);
}

// Warns when a collection's output reaches a destination in clear text.
//
// The threat model is that the collector is lost, and with it the configuration embedded in it: it
// runs on the host under investigation, which is the host one suspects. A credential in that
// configuration is then no protection at all - whoever holds the binary holds the key - and ToolEmbed
// cannot see whether the destination lets that key read back what was written there. So this warning
// deliberately does NOT care which authscheme is configured. What it reports is which artifacts leave
// the machine unencrypted, because CMS enveloping (<recipient>) is the only thing in this
// configuration that still protects them once it is in someone else's hands.
//
// The authscheme and the transport do appear, as aggravating detail on the lines that follow, never as
// a condition: an anonymous destination widens who can write there, and insecure="true" exposes the
// same clear text in flight as well as at rest.
//
// Advisory only. A plaintext collection to a trusted sink is a legitimate deployment, and refusing it
// would break configurations already in the field. The point is that it be a decision rather than an
// oversight.
void WarnOnPlaintextUpload(
    const std::wstring& path,
    const ConfigItem& schema,
    const std::vector<OutputSpec::Upload>& uploads)
{
    if (uploads.empty())
    {
        return;  // nothing leaves this host, so nothing is exposed by leaving it
    }

    static const CollectionArtifact kArtifacts[] = {
        {WOLFLAUNCHER_LOG, WOLFLAUNCHER_LOG_ENCRYPT, WOLFLAUNCHER_LOG_UPLOAD, L"log"},
        {WOLFLAUNCHER_CONSOLE, WOLFLAUNCHER_CONSOLE_ENCRYPT, WOLFLAUNCHER_CONSOLE_UPLOAD, L"console"},
        {WOLFLAUNCHER_OUTLINE, WOLFLAUNCHER_OUTLINE_ENCRYPT, WOLFLAUNCHER_OUTLINE_UPLOAD, L"outline"},
        {WOLFLAUNCHER_OUTCOME, WOLFLAUNCHER_OUTCOME_ENCRYPT, WOLFLAUNCHER_OUTCOME_UPLOAD, L"outcome"}};

    const bool hasRecipient = !schema[WOLFLAUNCHER_RECIPIENT].NodeList.empty();

    std::vector<std::wstring> clear;

    // The archives first: each command set has its own, and a <recipient> covers one only when one of
    // its globs matches, so a configuration can encrypt some archives and not others.
    size_t index = 0;
    for (const ConfigItem& archiveNode : schema[WOLFLAUNCHER_ARCHIVE].NodeList)
    {
        if (!ArchiveHasRecipient(schema, archiveNode))
        {
            clear.push_back(ArchiveLabel(archiveNode, index));
        }

        ++index;
    }

    for (const CollectionArtifact& artifact : kArtifacts)
    {
        if (LeavesHostInClear(schema, artifact, hasRecipient))
        {
            clear.push_back(artifact.name);
        }
    }

    if (clear.empty())
    {
        return;
    }

    // A single artifact reads as "outcome leaves ... (no <recipient> covers it)", several as "... leave
    // ... covers them": the message is the only place an operator meets this check, so it should not
    // read like a template.
    const bool one = (clear.size() == 1);
    const std::wstring reason = hasRecipient
        ? fmt::format(L"no <recipient> covers {}, or encrypt=\"no\"", one ? L"it" : L"them")
        : std::wstring(L"the configuration declares no <recipient>, so nothing it produces is encrypted");

    Log::Warn(
        L"ToolEmbed: '{}': {} {} this host in clear text ({}); whoever obtains this collector learns "
        L"where it uploads, so that data is then protected only by the destination's own read policy",
        path,
        boost::join(clear, L", "),
        one ? L"leaves" : L"leave",
        reason);

    for (size_t i = 0; i < uploads.size(); ++i)
    {
        const OutputSpec::Upload& upload = uploads[i];

        if (upload.AuthScheme == OutputSpec::UploadAuthScheme::Anonymous)
        {
            Log::Warn(
                L"ToolEmbed: '{}': <upload> #{} presents no credential, so that clear text is written to a "
                L"destination open to anyone who reaches it",
                path,
                i + 1);
        }

        if (upload.Method == OutputSpec::UploadMethod::REST && upload.rest.Insecure)
        {
            Log::Warn(
                L"ToolEmbed: '{}': <upload> #{} sets insecure=\"true\", so that clear text is exposed in "
                L"flight as well as at rest",
                path,
                i + 1);
        }
        else if (upload.bitsMode == OutputSpec::BITSMode::HTTP)
        {
            Log::Warn(
                L"ToolEmbed: '{}': <upload> #{} uploads over plain HTTP, so that clear text is exposed in "
                L"flight as well as at rest",
                path,
                i + 1);
        }
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

        // One form or the other, never both - the same rule the run enforces, applied here so the
        // operator learns of it on their workstation rather than on a collected host that then refuses
        // to start and loses the collection.
        if (schema[WOLFLAUNCHER_UPLOAD] && schema[WOLFLAUNCHER_UPLOADS])
        {
            Log::Error(
                L"ToolEmbed: refusing to embed '{}': it uses both a top-level <upload> list and an "
                L"<uploads> block; use one form or the other",
                path);
            refused = true;
        }

        // The <uploads> form goes through the same reader the run uses, so every refusal it carries -
        // operation="move" across destinations, a duplicate or empty name, a required destination with
        // no usable <upload> - is caught here too. Its entries are then appended to the same vector the
        // advisory warnings read, so a config migrated from <upload> to <uploads> keeps getting the
        // clear-text and credential warnings instead of silently losing them.
        if (schema[WOLFLAUNCHER_UPLOADS])
        {
            std::vector<OutputSpec::UploadDestination> destinations;
            bool deleteWhenDone = false;
            if (FAILED(Orc::Config::ReadUploadDestinations(schema[WOLFLAUNCHER_UPLOADS], destinations, deleteWhenDone)))
            {
                Log::Error(L"ToolEmbed: refusing to embed '{}': invalid <uploads> section", path);
                refused = true;
            }

            for (const auto& destination : destinations)
            {
                std::vector<OutputSpec::Upload> chain;

                for (const auto& upload : destination.Chain)
                {
                    const auto check = upload->Validate();
                    if (check.verdict == OutputSpec::Upload::Verdict::Refuse)
                    {
                        Log::Error(
                            L"ToolEmbed: refusing to embed '{}': destination '{}': {}",
                            path,
                            destination.Name,
                            check.message);
                        refused = true;
                    }
                    else if (check.verdict == OutputSpec::Upload::Verdict::Warn)
                    {
                        Log::Warn(L"ToolEmbed: '{}': destination '{}': {}", path, destination.Name, check.message);
                    }

                    chain.push_back(*upload);
                }

                // Per destination: entries of ONE chain disagreeing on their filters is the surprise
                // this warns about. Destinations disagreeing with each other is routing, which is what
                // the feature is for, so comparing across them would warn on every correct config.
                WarnOnUploadFilterMismatch(path, chain);

                uploads.insert(uploads.end(), chain.begin(), chain.end());
            }
        }
        else
        {
            WarnOnUploadFilterMismatch(path, uploads);
        }

        // Clear-text exposure is about what leaves the host at all, so every entry of every destination
        // counts here.
        WarnOnPlaintextUpload(path, schema, uploads);
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
