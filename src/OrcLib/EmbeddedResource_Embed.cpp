//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <regex>

#include <agents.h>

#include <boost/algorithm/string.hpp>

#include "EmbeddedResource.h"

#include "MemoryStream.h"
#include "FileStream.h"
#include "ArchiveCreate.h"
#include "ZipCreate.h"

#include "RunningProcesses.h"
#include "ParameterCheck.h"
#include "SystemDetails.h"
#include "Temporary.h"
#include "Utils/String.h"
#include "XmlLiteExtension.h"

using namespace std;
using namespace Orc;

namespace {

void SplitResourceLink(
    const std::wstring& resourceLink,
    std::wstring& resourceName,
    std::wstring& resourceFormat,
    std::optional<std::wstring>& archiveItemName)
{
    // examples: 'res:#GetScript_little_config.xml', '7z:#Tools|autorunsc.exe', 'self:#'
    std::wregex regex(L"(.*):#(?:([^\\|]+)[\\|]?([^\\|]+)?)?", std::regex_constants::icase);

    std::wsmatch matches;
    if (!std::regex_search(resourceLink, matches, regex))
    {
        return;
    }

    resourceFormat = matches[1];
    resourceName = matches[2];
    if (matches.size() == 4 && matches[3].matched)
    {
        archiveItemName = matches[3];
    }
}

struct XmlString
{
    std::optional<std::filesystem::path> xmlPath;
    std::optional<size_t> xmlLine;
    std::wstring value;
};

class ResourceRegistryItem
{
public:
    ResourceRegistryItem(const XmlString& resourceLink)
        : m_resourceLink(resourceLink.value)
        , m_resourceName()
        , m_resourceFormat()
        , m_resourceArchiveItemName()
        , m_xmlFileName(resourceLink.xmlPath)
        , m_xmlLine(resourceLink.xmlLine)
        , m_hasBeenEmbedded(false)
    {
        SplitResourceLink(m_resourceLink, m_resourceName, m_resourceFormat, m_resourceArchiveItemName);

        if (boost::iequals(m_resourceFormat, L"self"))
        {
            m_hasBeenEmbedded = true;
        }
    }

    const std::wstring& ResourceLink() const { return m_resourceLink; }
    const std::wstring& ResourceName() const { return m_resourceName; }
    const std::wstring& ResourceFormat() const { return m_resourceFormat; }
    const std::optional<std::wstring>& ResourceArchiveItemName() const { return m_resourceArchiveItemName; }

    const std::optional<std::wstring>& XmlFileName() const { return m_xmlFileName; }
    const std::optional<size_t> XmlLine() const { return m_xmlLine; }

    bool HasBeenEmbedded() const { return m_hasBeenEmbedded; }
    void SetHasBeenEmbedded(bool hasBeenEmbedded) { m_hasBeenEmbedded = hasBeenEmbedded; }

    std::optional<std::wstring> GetXmlPosition() const
    {
        if (!m_xmlFileName.has_value())
        {
            return {};
        }

        std::wstring position = *m_xmlFileName;

        if (m_xmlLine)
        {
            position.append(L":" + std::to_wstring(*m_xmlLine));
        }

        return position;
    }

private:
    std::wstring m_resourceLink;
    std::wstring m_resourceName;
    std::wstring m_resourceFormat;
    std::optional<std::wstring> m_resourceArchiveItemName;

    std::optional<std::wstring> m_xmlFileName;
    std::optional<size_t> m_xmlLine;

    bool m_hasBeenEmbedded;
};

class ResourceRegistry
{
public:
    std::vector<ResourceRegistryItem>& Items() { return m_items; }
    const std::vector<ResourceRegistryItem>& Items() const { return m_items; }

    void MarkAsEmbedded(
        const std::wstring& resourceName,
        const std::wstring& resourceFormat,
        const std::wstring& resourceArchiveItemName)
    {
        for (auto& item : m_items)
        {
            if (!boost::iequals(item.ResourceName(), resourceName)
                || !boost::iequals(item.ResourceFormat(), resourceFormat))
            {
                continue;
            }

            if (!item.ResourceArchiveItemName().has_value()
                || !boost::iequals(item.ResourceArchiveItemName().value_or(L""), resourceArchiveItemName))
            {
                continue;
            }

            item.SetHasBeenEmbedded(true);
        }
    }

    void MarkAsEmbedded(const std::wstring& resourceName, const std::wstring& resourceFormat)
    {
        for (auto& item : m_items)
        {
            if (!boost::iequals(item.ResourceName(), resourceName)
                || !boost::iequals(item.ResourceFormat(), resourceFormat) || item.ResourceArchiveItemName().has_value())
            {
                continue;
            }

            item.SetHasBeenEmbedded(true);
        }
    }

private:
    std::vector<ResourceRegistryItem> m_items;
};

// Look into the current xml element for an attribute's name which match the provided regex
Result<std::optional<XmlString>>
GetXmlAttributeValueMatch(const CComPtr<IXmlReader>& reader, std::wstring_view attributeValueRegex)
{
    std::vector<std::wstring> values;

    UINT uiAttrCount = 0;
    HRESULT hr = reader->GetAttributeCount(&uiAttrCount);
    if (FAILED(hr))
    {
        XmlLiteExtension::LogError(hr, reader);
        return SystemError(hr);
    }

    if (uiAttrCount == 0L)
    {
        return std::optional<XmlString> {};
    }

    hr = reader->MoveToFirstAttribute();
    if (FAILED(hr))
    {
        XmlLiteExtension::LogError(hr, reader);
        return SystemError(hr);
    }

    while (hr == S_OK)
    {
        const WCHAR* pName = NULL;
        hr = reader->GetLocalName(&pName, NULL);
        if (FAILED(hr))
        {
            XmlLiteExtension::LogError(hr, reader);
            return SystemError(hr);
        }

        if (pName == nullptr)
        {
            return SystemError(E_FAIL);
        }

        const WCHAR* pValue = NULL;
        hr = reader->GetValue(&pValue, NULL);
        if (FAILED(hr))
        {
            XmlLiteExtension::LogError(hr, reader);
            return SystemError(hr);
        }

        if (pValue == nullptr)
        {
            return SystemError(E_FAIL);
        }

        std::wregex regex(attributeValueRegex.data(), std::regex_constants::icase);
        if (!std::regex_search(pValue, regex))
        {
            hr = reader->MoveToNextAttribute();
            if (FAILED(hr))
            {
                XmlLiteExtension::LogError(hr, reader);
                return SystemError(hr);
            }

            continue;
        }

        UINT lineNumber = 0;
        hr = reader->GetLineNumber(&lineNumber);
        if (FAILED(hr))
        {
            XmlLiteExtension::LogError(hr, reader);
            // return;
        }

        return XmlString {{}, lineNumber, pValue};
    }

    return std::optional<XmlString> {};
}

// Look into the current xml file for all attributes name which match the provided regex
Result<std::vector<XmlString>> GetXmlAttributeValuesMatch(
    const std::shared_ptr<ByteStream>& xmlStream,
    LPCWSTR szEncodingHint,
    std::wstring_view attributeValueRegex)
{
    std::vector<XmlString> values;

    HRESULT hr = xmlStream->SetFilePointer(0, FILE_BEGIN, NULL);
    if (FAILED(hr))
    {
        Log::Debug("Failed to seek for parsing xml [{}]", SystemError(hr));
        return SystemError(hr);
    }

    CComPtr<IStream> stream;
    if (FAILED(hr = ByteStream::Get_IStream(xmlStream, &stream)))
    {
        return SystemError(hr);
    }

    auto m_xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>();
    if (m_xmllite == nullptr)
    {
        Log::Debug("Failed to load xmllite extension library");
        return SystemError(E_FAIL);
    }

    CComPtr<IXmlReader> pReader;

    if (FAILED(hr = m_xmllite->CreateXmlReader(IID_IXmlReader, (PVOID*)&pReader, nullptr)))
    {
        XmlLiteExtension::LogError(hr, pReader);
        Log::Debug("Failed to instantiate Xml reader [{}]", SystemError(hr));
        return SystemError(hr);
    }

    if (szEncodingHint == NULL)
    {
        if (FAILED(hr = pReader->SetInput(stream)))
        {
            XmlLiteExtension::LogError(hr, pReader);
            Log::Debug("Failed to set input stream [{}]", SystemError(hr));
            return SystemError(hr);
        }
    }
    else
    {
        CComPtr<IXmlReaderInput> pInput;
        hr = m_xmllite->CreateXmlReaderInputWithEncodingName(stream, nullptr, szEncodingHint, FALSE, L"", &pInput);
        if (FAILED(hr))
        {
            XmlLiteExtension::LogError(hr, pReader);
            Log::Debug("Failed to set output stream [{}]", SystemError(hr));
            return SystemError(hr);
        }

        if (FAILED(hr = pReader->SetInput(pInput)))
        {
            XmlLiteExtension::LogError(hr, pReader);
            Log::Debug("Failed to set input stream [{}]", SystemError(hr));
            return SystemError(hr);
        }
    }

    XmlNodeType nodeType = XmlNodeType_None;
    while (S_OK == (hr = pReader->Read(&nodeType)))
    {
        if (nodeType != XmlNodeType_Element)
        {
            continue;
        }

        const WCHAR* pName = NULL;
        if (FAILED(hr = pReader->GetLocalName(&pName, NULL)))
        {
            XmlLiteExtension::LogError(hr, pReader);
            return SystemError(hr);
        }

        auto result = GetXmlAttributeValueMatch(pReader, attributeValueRegex);
        if (result.has_error())
        {
            Log::Error("Failed to retrieve attribute value [{}]", result.error());
            Log::Error("The embedding of some resource will not be checked");
            continue;
        }

        if ((*result).has_value())
        {
            values.emplace_back(std::move(**result));
        }
    }

    if (FAILED(hr))
    {
        XmlLiteExtension::LogError(hr, pReader);
        return SystemError(hr);
    }

    return values;
}

Result<std::vector<XmlString>> GetResourceLinks(std::wstring_view path)
{
    std::vector<XmlString> links;

    auto filestream = make_shared<FileStream>();
    HRESULT hr = filestream->ReadFrom(path.data());
    if (FAILED(hr))
    {
        Log::Debug(L"Failed to read file '{}' [{}]", path, SystemError(hr));
        return SystemError(hr);
    }

    // parse all resource links from xml files and register them to check later if they have been embedded
    auto result = GetXmlAttributeValuesMatch(filestream, L"utf-8", L"(7z|res):#.*");
    if (result.has_error())
    {
        Log::Debug(L"Failed to retrieve links attribute values [{}]", result.error());
        return result.error();
    }
    else
    {
        for (auto& link : *result)
        {
            link.xmlPath = path;
            links.emplace_back(link);
        }
    }

    return links;
}

// Register all resource link (ex: '7z:#TOOLS|Foo.exe') so later they can be marked as embedded or not
void RegisterResourceLinks(const std::vector<EmbeddedResource::EmbedSpec>& ToEmbed, ResourceRegistry& resourceRegistry)
{
    for (auto iter = ToEmbed.begin(); iter != ToEmbed.end(); ++iter)
    {
        const EmbeddedResource::EmbedSpec& item = *iter;

        switch (item.Type)
        {
            case EmbeddedResource::EmbedSpec::EmbedType::NameValuePair: {
                if (item.Name != L"RUN32_ARGS" && item.Name != L"RUN64_ARGS")
                {
                    // Location is current toolembed xml configuration file
                    XmlString resourceLink;
                    resourceLink.value = item.Value;
                    resourceRegistry.Items().emplace_back(resourceLink);
                }
                break;
            }
            case EmbeddedResource::EmbedSpec::EmbedType::File: {
                if (!EndsWith(item.Value, L".xml"))
                {
                    break;
                }

                auto links = GetResourceLinks(item.Value);
                if (links.has_error())
                {
                    Log::Error(L"Resource embedding check disabled for '{}' [{}]", item.Value, links.error());
                }
                else
                {
                    for (auto& link : *links)
                    {
                        resourceRegistry.Items().emplace_back(std::move(link));
                    }
                }
                break;
            }
            default:
                break;
        }
    }
}

void PrintMissingResources(ResourceRegistry resourceRegistry)
{
    for (const auto& item : resourceRegistry.Items())
    {
        if (!item.HasBeenEmbedded())
        {
            auto position = item.GetXmlPosition();
            if (position)
            {
                Log::Error(L"Unresolved resource reference: '{}' from '{}'", item.ResourceLink(), *position);
            }
            else
            {
                Log::Error(L"Unresolved resource reference: '{}'", item.ResourceLink());
            }
        }
    }
}

}  // namespace

HRESULT EmbeddedResource::_UpdateResource(
    HANDLE hOutput,
    const WCHAR* szModule,
    const WCHAR* szType,
    const WCHAR* szName,
    LPVOID pData,
    DWORD cbSize)
{
    HRESULT hr = E_FAIL;
    HANDLE hOut = hOutput;

    if (hOutput == INVALID_HANDLE_VALUE)
    {
        hOut = BeginUpdateResource(szModule, FALSE);
        if (hOut == NULL)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug(L"Failed to update resource in '{}' (BeginUpdateResource) [{}]", szModule, SystemError(hr));
            return hr;
        }
    }

    if (!UpdateResource(hOut, szType, szName, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), pData, cbSize))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Debug(L"Failed to add resource '{}' (UpdateResource) [{}]", szName, SystemError(hr));
        return hr;
    }

    if (hOutput == INVALID_HANDLE_VALUE)
    {
        if (!EndUpdateResource(hOut, FALSE))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Debug(L"Failed to update resource in '{}' (EndUpdateResource) [{}]", szModule, SystemError(hr));
            return hr;
        }
    }
    return S_OK;
}

// There is sometimes a race condition with Windows after modifying the module.
// This function will retry multiple time to access a file for updating its resource.
HRESULT EmbeddedResource::TryUpdateResource(
    HANDLE hOutput,
    const WCHAR* szModule,
    const WCHAR* szType,
    const WCHAR* szName,
    LPVOID pData,
    DWORD cbSize,
    uint8_t maxAttempt)
{

    HRESULT hr = E_FAIL;

    for (size_t i = 1; i <= maxAttempt; ++i)
    {
        hr = EmbeddedResource::_UpdateResource(hOutput, szModule, szType, szName, pData, cbSize);
        if (SUCCEEDED(hr))
        {
            return S_OK;
        }

        Log::Debug(L"Failed to update resource '{}' (attempt: {}) [{}]", szName, i, SystemError(hr));
        Sleep(1000);
    }

    Log::Error(L"Failed to update resource '{}' [{}]", szName, SystemError(hr));
    return hr;
}

HRESULT EmbeddedResource::UpdateResources(const std::wstring& strPEToUpdate, const std::vector<EmbedSpec>& ToEmbed)
{
    HRESULT hr = S_OK;
    const auto kMaxAttempt = 8;

    DWORD dwMajor = 0L, dwMinor = 0L;
    SystemDetails::GetOSVersion(dwMajor, dwMinor);

    ::ResourceRegistry resourceRegistry;
    ::RegisterResourceLinks(ToEmbed, resourceRegistry);

    HANDLE hOutput = INVALID_HANDLE_VALUE;

    bool bAtomicUpdate = false;
    if (dwMajor <= 5)
        bAtomicUpdate = true;

    if (!bAtomicUpdate)
    {
        hOutput = BeginUpdateResource(strPEToUpdate.c_str(), FALSE);
        if (hOutput == NULL)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error(
                L"Failed to update resources in '{}' (BeginUpdateResource) [{}]", strPEToUpdate, SystemError(hr));
            return hr;
        }
    }

    for (auto iter = ToEmbed.begin(); iter != ToEmbed.end(); ++iter)
    {
        const EmbeddedResource::EmbedSpec& item = *iter;

        switch (item.Type)
        {
            case EmbeddedResource::EmbedSpec::EmbedType::NameValuePair: {
                if (SUCCEEDED(
                        hr = TryUpdateResource(
                            hOutput,
                            strPEToUpdate.c_str(),
                            EmbeddedResource::VALUES(),
                            item.Name.c_str(),
                            (LPVOID)item.Value.c_str(),
                            (DWORD)item.Value.size() * sizeof(WCHAR),
                            kMaxAttempt)))
                {
                    Log::Info(L"Successfully added {}={}", item.Name, item.Value);
                    resourceRegistry.MarkAsEmbedded(item.Name, L"res");
                }

                break;
            }
            case EmbeddedResource::EmbedSpec::EmbedType::ValuesDeletion: {
                if (SUCCEEDED(
                        hr = TryUpdateResource(
                            hOutput,
                            strPEToUpdate.c_str(),
                            EmbeddedResource::VALUES(),
                            item.Name.c_str(),
                            NULL,
                            0L,
                            kMaxAttempt)))
                {
                    Log::Info(L"Successfully delete resource at position '{}' [{}]", item.Name, SystemError(hr));
                }

                break;
            }
            case EmbeddedResource::EmbedSpec::EmbedType::BinaryDeletion: {
                if (SUCCEEDED(
                        hr = TryUpdateResource(
                            hOutput,
                            strPEToUpdate.c_str(),
                            EmbeddedResource::BINARY(),
                            item.Name.c_str(),
                            NULL,
                            0L,
                            kMaxAttempt)))
                {
                    Log::Info(L"Successfully delete resource at position '{}' [{}]", item.Name, SystemError(hr));
                }

                break;
            }
            default:
                hr = S_OK;
                break;
        }
    }

    if (hr != S_OK)
    {
        return hr;
    }

    if (!bAtomicUpdate)
    {
        if (!EndUpdateResource(hOutput, FALSE))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            Log::Error(L"Failed to update resources in '{}' (EndUpdateResource) [{}]", strPEToUpdate, SystemError(hr));
            return hr;
        }
    }

    for (auto iter = ToEmbed.begin(); iter != ToEmbed.end(); ++iter)
    {
        const EmbeddedResource::EmbedSpec& item = *iter;

        switch (item.Type)
        {
            case EmbeddedResource::EmbedSpec::EmbedType::File: {
                auto filestream = make_shared<FileStream>();

                if (FAILED(hr = filestream->ReadFrom(item.Value.c_str())))
                {
                    Log::Error(L"Failed to update resources in '{}' (read failed) [{}]", item.Value, SystemError(hr));
                    return hr;
                }

                auto memstream = std::make_shared<MemoryStream>();

                if (FAILED(hr = memstream->OpenForReadWrite()))
                {
                    Log::Error(L"Failed to open mem resource in '{}' [{}]", item.Value, SystemError(hr));
                    return hr;
                }
                ULONGLONG ullFileSize = filestream->GetSize();

                if (FAILED(hr = memstream->SetSize((DWORD)ullFileSize)))
                {
                    Log::Error(L"Failed set size of mem resource in '{}' [{}]", item.Value, SystemError(hr));
                    return hr;
                }

                ULONGLONG ullBytesWritten = 0LL;
                if (FAILED(hr = filestream->CopyTo(memstream, &ullBytesWritten)))
                {
                    Log::Error(L"Failed to copy '{}' to a memory stream [{}]", item.Value, SystemError(hr));
                    return hr;
                }

                CBinaryBuffer data;
                memstream->GrabBuffer(data);

                if (SUCCEEDED(
                        hr = TryUpdateResource(
                            INVALID_HANDLE_VALUE,
                            strPEToUpdate.c_str(),
                            EmbeddedResource::BINARY(),
                            item.Name.c_str(),
                            data.GetData(),
                            (DWORD)data.GetCount(),
                            kMaxAttempt)))
                {
                    Log::Info(L"Successfully added '{}' at position '{}'", item.Value, item.Name);
                    resourceRegistry.MarkAsEmbedded(item.Name, L"res");
                }
                else
                {
                    return hr;
                }

                break;
            }
            case EmbeddedResource::EmbedSpec::EmbedType::Archive: {
                ArchiveFormat fmt = ArchiveFormat::Unknown;

                if (item.ArchiveFormat.empty())
                    fmt = ArchiveFormat::SevenZip;
                else
                    fmt = ArchiveCreate::GetArchiveFormat(item.ArchiveFormat);

                if (fmt == ArchiveFormat::Unknown)
                {
                    Log::Error(L"Failed to use archive format '{}'", item.ArchiveFormat);
                    return E_INVALIDARG;
                }
                auto creator = ArchiveCreate::MakeCreate(fmt, false);

                auto memstream = std::make_shared<MemoryStream>();

                if (FAILED(hr = memstream->OpenForReadWrite()))
                {
                    Log::Error("Failed to initialize memory stream [{}]", SystemError(hr));
                    return hr;
                }

                if (FAILED(hr = creator->InitArchive(memstream)))
                {
                    Log::Error(L"Failed to initialize archive stream [{}]", SystemError(hr));
                    return hr;
                }

                if (!item.ArchiveCompression.empty())
                {
                    if (FAILED(hr = creator->SetCompressionLevel(item.ArchiveCompression)))
                    {
                        Log::Error(L"Invalid compression level '{}' [{}]", item.ArchiveCompression, SystemError(hr));
                        return hr;
                    }
                }

                hr = S_OK;

                for (auto arch_item : item.ArchiveItems)
                {
                    hr = creator->AddFile(arch_item.Name.c_str(), arch_item.Path.c_str(), false);
                    if (FAILED(hr))
                    {
                        Log::Error(L"Failed to add file '{}' to archive", arch_item.Path);
                        return hr;
                    }
                    else
                    {
                        resourceRegistry.MarkAsEmbedded(item.Name, L"7z", arch_item.Name);
                        Log::Info(L"Successfully added '{}' to archive", arch_item.Path);
                    }
                }

                if (FAILED(hr = creator->Complete()))
                {
                    Log::Error("Failed to complete archive [{}]", SystemError(hr));
                    return hr;
                }

                CBinaryBuffer data;
                memstream->GrabBuffer(data);

                if (SUCCEEDED(
                        hr = TryUpdateResource(
                            INVALID_HANDLE_VALUE,
                            strPEToUpdate.c_str(),
                            EmbeddedResource::BINARY(),
                            item.Name.c_str(),
                            data.GetData(),
                            (DWORD)data.GetCount(),
                            kMaxAttempt)))
                {
                    Log::Info(L"Successfully added archive '{}'", item.Name);
                }
                else
                {
                    return hr;
                }

                break;
            }
            default:
                break;
        }
    }

    PrintMissingResources(resourceRegistry);

    return hr;
}
