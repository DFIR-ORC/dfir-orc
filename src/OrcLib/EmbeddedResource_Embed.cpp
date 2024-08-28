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
#include "YaraStaticExtension.h"
#include "Utils/Guard.h"

using namespace std;
using namespace Orc;

namespace {

const auto kEncodingHint = L"utf-8";

const uint8_t kDefaultAttemptLimit = 20;
const uint32_t kDefaultAttemptDelay = 200;

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

Result<CComPtr<IXmlReader>> CreateXmlReader(const std::shared_ptr<ByteStream>& xmlStream, bool printErrorMessage = true)
{
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

    CComPtr<IXmlReader> reader;

    if (FAILED(hr = m_xmllite->CreateXmlReader(IID_IXmlReader, (PVOID*)&reader, nullptr)))
    {
        if (printErrorMessage)
        {
            XmlLiteExtension::LogError(hr, reader);
        }

        Log::Debug("Failed to instantiate Xml reader [{}]", SystemError(hr));
        return SystemError(hr);
    }

    CComPtr<IXmlReaderInput> pInput;
    hr = m_xmllite->CreateXmlReaderInputWithEncodingName(stream, nullptr, kEncodingHint, FALSE, L"", &pInput);
    if (FAILED(hr))
    {
        if (printErrorMessage)
        {
            XmlLiteExtension::LogError(hr, reader);
        }

        Log::Debug("Failed to set output stream [{}]", SystemError(hr));
        return SystemError(hr);
    }

    if (FAILED(hr = reader->SetInput(pInput)))
    {
        if (printErrorMessage)
        {
            XmlLiteExtension::LogError(hr, reader);
        }

        Log::Debug("Failed to set input stream [{}]", SystemError(hr));
        return SystemError(hr);
    }

    return reader;
}

struct XmlString
{
    std::optional<std::filesystem::path> xmlPath;
    std::optional<size_t> xmlLine;
    std::wstring value;
};

struct XmlStringCompare
{
    bool operator()(const XmlString& lhs, const XmlString& rhs) const { return (lhs.value < rhs.value); }
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
        bool found = false;

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
            found = true;
        }

        if (!found)
        {
            Log::Warn(
                L"Cannot find any reference to embedded resource '{}:#{}|{}'",
                resourceFormat,
                resourceName,
                resourceArchiveItemName);
        }
    }

    void MarkAsEmbedded(const std::wstring& resourceName, const std::wstring& resourceFormat)
    {
        bool found = false;

        for (auto& item : m_items)
        {
            if (!boost::iequals(item.ResourceName(), resourceName)
                || !boost::iequals(item.ResourceFormat(), resourceFormat) || item.ResourceArchiveItemName().has_value())
            {
                continue;
            }

            item.SetHasBeenEmbedded(true);
            found = true;
        }

        if (!found)
        {
            Log::Warn(L"Cannot find any reference to embedded resource '{}'", resourceName);
        }
    }

private:
    std::vector<ResourceRegistryItem> m_items;
};

// Look into the current xml element for an attributes name which match the provided regex
Result<std::vector<XmlString>>
GetXmlAttributeValueMatch(const CComPtr<IXmlReader>& reader, std::wstring_view attributeValueRegex)
{
    std::vector<XmlString> values;

    UINT uiAttrCount = 0;
    HRESULT hr = reader->GetAttributeCount(&uiAttrCount);
    if (FAILED(hr))
    {
        XmlLiteExtension::LogError(hr, reader);
        return SystemError(hr);
    }

    if (uiAttrCount == 0L)
    {
        return std::vector<XmlString>();
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
        if (std::regex_search(pValue, regex))
        {
            UINT lineNumber = 0;
            hr = reader->GetLineNumber(&lineNumber);
            if (FAILED(hr))
            {
                XmlLiteExtension::LogError(hr, reader);
                // return;
            }

            values.emplace_back(XmlString {{}, lineNumber, pValue});
        }

        hr = reader->MoveToNextAttribute();
        if (FAILED(hr))
        {
            XmlLiteExtension::LogError(hr, reader);
            return SystemError(hr);
        }
    }

    return values;
}

// Look into the current xml file for all attributes name which match the provided regex
Result<std::vector<XmlString>>
GetXmlAttributesValueMatch(const std::shared_ptr<ByteStream>& xmlStream, std::wstring_view attributeValueRegex)
{
    HRESULT hr = E_FAIL;
    std::vector<XmlString> values;

    auto xmlReader = CreateXmlReader(xmlStream);
    if (!xmlReader)
    {
        return xmlReader.error();
    }

    auto& pReader = xmlReader.value();

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

        if (!result->empty())
        {
            std::move(std::begin(*result), std::end(*result), std::back_inserter(values));
            result->clear();
        }
    }

    if (FAILED(hr))
    {
        XmlLiteExtension::LogError(hr, pReader);
        return SystemError(hr);
    }

    return values;
}

// Look into the current xml element for an attribute's name which match the provided regex
Result<std::optional<XmlString>>
GetXmlElementValueMatch(const CComPtr<IXmlReader>& reader, std::wstring_view elementValueRegex)
{
    std::vector<std::wstring> values;

    const WCHAR* pValue;
    UINT cchValue = 0;
    HRESULT hr = reader->GetValue(&pValue, &cchValue);
    if (FAILED(hr))
    {
        XmlLiteExtension::LogError(hr, reader);
        return SystemError(hr);
    }

    if (pValue == NULL || cchValue == 0L)
    {
        return std::optional<XmlString> {};
    }

    std::wstring value(pValue);
    std::wsmatch matches;
    std::wregex regex(elementValueRegex.data(), std::regex_constants::icase);
    if (!std::regex_search(value, matches, regex))
    {
        return std::optional<XmlString> {};
    }

    if (matches.size() > 1)
    {
        value = matches[1];
    }

    UINT lineNumber = 0;
    hr = reader->GetLineNumber(&lineNumber);
    if (FAILED(hr))
    {
        XmlLiteExtension::LogError(hr, reader);
        // return;
    }

    return XmlString {{}, lineNumber, value};
}

// Look into the current xml file for all attributes name which match the provided regex
Result<std::vector<XmlString>>
GetXmlElementsValueMatch(const std::shared_ptr<ByteStream>& xmlStream, std::wstring_view valueRegex)
{
    HRESULT hr = E_FAIL;
    std::vector<XmlString> values;

    auto xmlReader = CreateXmlReader(xmlStream);
    if (!xmlReader)
    {
        return xmlReader.error();
    }

    auto& reader = xmlReader.value();

    XmlNodeType nodeType = XmlNodeType_None;
    while (S_OK == (hr = reader->Read(&nodeType)))
    {
        if (nodeType != XmlNodeType_Text)
        {
            continue;
        }

        const WCHAR* pName = NULL;
        if (FAILED(hr = reader->GetLocalName(&pName, NULL)))
        {
            XmlLiteExtension::LogError(hr, reader);
            return SystemError(hr);
        }

        auto result = GetXmlElementValueMatch(reader, valueRegex);
        if (result.has_error())
        {
            Log::Error("Failed to retrieve element value [{}]", result.error());
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
        XmlLiteExtension::LogError(hr, reader);
        return SystemError(hr);
    }

    return values;
}

bool IsXmlFile(const std::filesystem::path& path)
{
    // Check if this is a resource path
    if (path.filename().string().find_last_of(":#") != std::string::npos)
    {
        return false;
    }

    // No extension: default toolembed configuration dump some XML files without extension
    if (path.has_extension() && !boost::iequals(path.extension().c_str(), ".xml"))
    {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec)
    {
        return false;
    }

    auto filestream = make_shared<FileStream>();
    HRESULT hr = filestream->ReadFrom(path.c_str());
    if (FAILED(hr))
    {
        Log::Debug(L"Failed to open file '{}', cannot check if it looks like XML [{}]", path, SystemError(hr));
        return false;
    }

    auto xmlReader = CreateXmlReader(filestream, false);
    if (xmlReader.has_error())
    {
        return false;
    }

    XmlNodeType nodeType = XmlNodeType_None;
    return SUCCEEDED(xmlReader.value()->Read(&nodeType));
}

// parse all resource links from xml files and register them to check later if they have been embedded
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

    auto attributesValues = GetXmlAttributesValueMatch(filestream, L"(7z|res):#.*");
    if (attributesValues.has_error())
    {
        Log::Debug(L"Failed to retrieve links attribute values [{}]", attributesValues.error());
        return attributesValues.error();
    }
    else
    {
        for (auto& link : *attributesValues)
        {
            link.xmlPath = path;
            links.emplace_back(link);
        }
    }

    auto elementsValues = GetXmlElementsValueMatch(filestream, L"((?:7z|res):#[^\\s\\\"\\']+)");
    if (elementsValues.has_error())
    {
        Log::Debug(L"Failed to retrieve links attribute values [{}]", elementsValues.error());
        return elementsValues.error();
    }
    else
    {
        for (auto& link : *elementsValues)
        {
            link.xmlPath = path;
            links.emplace_back(link);
        }
    }

    return links;
}

void RegisterResourceLinksFromFile(const std::filesystem::path& path, ResourceRegistry& resourceRegistry)
{
    if (!IsXmlFile(path))
    {
        return;
    }

    auto links = GetResourceLinks(path.wstring());
    if (links.has_error())
    {
        Log::Error(L"Resource embedding check disabled for '{}' [{}]", path, links.error());
    }
    else
    {
        for (auto& link : *links)
        {
            resourceRegistry.Items().emplace_back(std::move(link));
        }
    }
}

// Register resource link from attributes and elements like '7z:#TOOLS|Foo.exe' so later they can be marked as embedded
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
            case EmbeddedResource::EmbedSpec::EmbedType::Archive: {
                for (auto& archiveItem : item.ArchiveItems)
                {
                    RegisterResourceLinksFromFile(archiveItem.Path, resourceRegistry);
                }
                break;
            }
            case EmbeddedResource::EmbedSpec::EmbedType::File: {
                if (boost::iequals(item.Name, L"WOLFLAUNCHER_CONFIG"))
                {
                    // Register to pass the referenced check before exit
                    XmlString resourceLink;
                    resourceLink.value = std::wstring(L"res:#") + item.Name;
                    resourceRegistry.Items().emplace_back(resourceLink);
                }
                else if (boost::iends_with(item.Value, L"_SQLSCHEMA"))
                {
                    // Register to pass the referenced check before exit
                    XmlString resourceLink;
                    resourceLink.value = std::wstring(L"res:#") + item.Name;
                    resourceRegistry.Items().emplace_back(resourceLink);

                    continue;  // nothing else to register
                }

                RegisterResourceLinksFromFile(item.Value, resourceRegistry);
                break;
            }
            default:
                break;
        }
    }
}

Result<std::vector<XmlString>> GetXmlAttributesValue(
    const std::shared_ptr<ByteStream>& xmlStream,
    std::wstring_view element,
    std::wstring_view attribute)
{
    HRESULT hr = E_FAIL;
    std::vector<XmlString> values;

    auto xmlReader = CreateXmlReader(xmlStream);
    if (!xmlReader)
    {
        return xmlReader.error();
    }

    auto& reader = xmlReader.value();

    XmlNodeType nodeType = XmlNodeType_None;
    while (S_OK == (hr = reader->Read(&nodeType)))
    {
        if (nodeType != XmlNodeType_Element)
        {
            continue;
        }

        const WCHAR* pName = NULL;
        if (FAILED(hr = reader->GetLocalName(&pName, NULL)))
        {
            XmlLiteExtension::LogError(hr, reader);
            return SystemError(hr);
        }

        if (pName == nullptr)
        {
            continue;
        }

        if (!boost::iequals(pName, element))
        {
            continue;
        }

        UINT uiAttrCount = 0;
        HRESULT hr = reader->GetAttributeCount(&uiAttrCount);
        if (FAILED(hr))
        {
            XmlLiteExtension::LogError(hr, reader);
            return SystemError(hr);
        }

        if (uiAttrCount == 0L)
        {
            continue;
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

            if (!boost::iequals(pName, attribute))
            {
                hr = reader->MoveToNextAttribute();
                if (FAILED(hr))
                {
                    XmlLiteExtension::LogError(hr, reader);
                    return SystemError(hr);
                }

                continue;
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

            UINT lineNumber = 0;
            hr = reader->GetLineNumber(&lineNumber);
            if (FAILED(hr))
            {
                XmlLiteExtension::LogError(hr, reader);
                // return;
            }

            values.emplace_back(XmlString {{}, lineNumber, pValue});

            hr = reader->MoveToNextAttribute();
            if (FAILED(hr))
            {
                XmlLiteExtension::LogError(hr, reader);
                return SystemError(hr);
            }
        }
    }

    return values;
}

Result<std::vector<XmlString>> FindYaraRules(std::wstring_view path)
{
    std::vector<XmlString> yaraRules;

    auto stream = make_shared<FileStream>();

    HRESULT hr = stream->ReadFrom(path.data());
    if (FAILED(hr))
    {
        auto ec = SystemError(hr);
        Log::Debug(L"Failed to read file '{}' [{}]", path, ec);
        return ec;
    }

    auto result = GetXmlAttributesValue(stream, L"yara", L"source");
    if (!result)
    {
        return result.error();
    }

    for (auto& attribute : result.value())
    {
        attribute.xmlPath = path;
        yaraRules.emplace_back(std::move(attribute));
    }

    return yaraRules;
}

void RegisterYaraRules(const std::filesystem::path& path, std::vector<XmlString>& yaraRules)
{
    if (!IsXmlFile(path))
    {
        return;
    }

    auto result = FindYaraRules(path.wstring());
    if (!result)
    {
        Log::Error(L"Failed to parse configuration while looking for Yara rules '{}' [{}]", path, result.error());
        return;
    }

    for (const auto& rule : result.value())
    {
        yaraRules.emplace_back(rule);
    }
}

void RegisterYaraRules(const std::vector<EmbeddedResource::EmbedSpec>& ToEmbed, std::vector<XmlString>& yaraRules)
{
    for (auto iter = ToEmbed.begin(); iter != ToEmbed.end(); ++iter)
    {
        const EmbeddedResource::EmbedSpec& item = *iter;

        switch (item.Type)
        {
            case EmbeddedResource::EmbedSpec::EmbedType::NameValuePair:
            case EmbeddedResource::EmbedSpec::EmbedType::File: {
                RegisterYaraRules(item.Value, yaraRules);
                break;
            }
            case EmbeddedResource::EmbedSpec::EmbedType::Archive: {
                for (const auto& archiveItem : item.ArchiveItems)
                {
                    RegisterYaraRules(archiveItem.Path, yaraRules);
                }
                break;
            }

            default:
                continue;
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

inline void compiler_callback(
    int errorLevel,
    const char* filename,
    int lineNumber,
    const YR_RULE* rule,
    const char* message,
    void* userData)
{
    XmlString yaraSource;
    if (userData)
    {
        yaraSource = *(reinterpret_cast<XmlString*>(userData));
    }

    std::error_code ec;
    switch (errorLevel)
    {
        case YARA_ERROR_LEVEL_ERROR:
            Log::Error("Yara compiler: {} (line: {}, source: {})", message, lineNumber, ToUtf8(yaraSource.value, ec));
            break;
        case YARA_ERROR_LEVEL_WARNING:
            Log::Warn("Yara compiler: {} (line: {}, source: {})", message, lineNumber, ToUtf8(yaraSource.value, ec));
            break;
        default:
            Log::Info(
                "Yara compiler: {} (level: {}, line: {}, source: {})",
                message,
                errorLevel,
                lineNumber,
                ToUtf8(yaraSource.value, ec));
            break;
    }
}

void CompileYaraRule(YaraStaticExtension& yara, CBinaryBuffer& buffer, const XmlString& yaraSource)
{
    if (buffer.GetCount() == 0)
    {
        Log::Error(L"Failed Yara rule compilation for '{}' as rule is empty)", yaraSource.value);
        return;
    }

    YR_COMPILER* compiler = nullptr;

    auto ret = yara.yr_compiler_create(&compiler);
    if (!compiler)
    {
        Log::Error(
            L"Failed Yara compiler initialization for '{}' (yr_compiler_create exit: {})", yaraSource.value, ret);
        return;
    }

    auto onScopeExit = Guard::CreateScopeGuard([&] { yara.yr_compiler_destroy(compiler); });
    yara.yr_compiler_set_callback(
        compiler, compiler_callback, const_cast<void*>(reinterpret_cast<const void*>(&yaraSource)));

    // we need to make sure that buffer is null terminated because libyara heavily relies on this
    if (buffer.Get<UCHAR>(buffer.GetCount<UCHAR>() - sizeof(UCHAR)) != '\0')
    {
        buffer.SetCount(buffer.GetCount<UCHAR>() + sizeof(UCHAR));
        buffer.Get<UCHAR>(buffer.GetCount<UCHAR>() - sizeof(UCHAR)) = '\0';
    }

    auto errnum = yara.yr_compiler_add_string(compiler, buffer.GetP<const char>(), nullptr);

    if (errnum > 0)
    {
        Log::Error(L"Failed Yara rule compilation for '{}' (error(s): {})", yaraSource.value, errnum);
    }
}

void CompileYaraRuleFromPlainResource(
    YaraStaticExtension& yara,
    std::filesystem::path peFile,
    std::wstring_view resourceName,
    const XmlString& yaraSource)
{
    CBinaryBuffer buffer;

    // BEWARE: use c_str() to avoid any fmt specialization which would do fancy stuff (add quotes...)
    HRESULT hr = EmbeddedResource::ExtractBuffer(peFile.c_str(), std::wstring(resourceName), buffer);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to extract Yara rule from resource '{}'", yaraSource.value);
        return;
    }

    CompileYaraRule(yara, buffer, yaraSource);
}

void CompileYaraRuleFromArchiveResource(
    YaraStaticExtension& yara,
    std::filesystem::path peFile,
    std::wstring_view resourceName,
    std::wstring_view archiveItemName,
    const XmlString& yaraSource)
{
    // BEWARE: use c_str() to avoid any fmt specialization which would do fancy stuff (add quotes...)
    std::wstring imageFileResourceId = fmt::format(L"7z:{}#{}|{}", peFile.c_str(), resourceName, archiveItemName);

    CBinaryBuffer buffer;

    HRESULT hr = EmbeddedResource::ExtractToBuffer(imageFileResourceId, buffer);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to extract Yara rule from resource '{}'", yaraSource.value);
        return;
    }

    CompileYaraRule(yara, buffer, yaraSource);
}

void CheckYaraRules(const std::filesystem::path& peFile, const std::vector<XmlString>& yaraSources)
{
    std::set<XmlString, XmlStringCompare> yaraSourcesSet(std::cbegin(yaraSources), std::cend(yaraSources));

    auto yara = std::make_shared<YaraStaticExtension>();

    auto ret = yara->yr_initialize();
    if (ret != ERROR_SUCCESS)
    {
        Log::Error("Failed Yara initialization (yr_initialize exit: {})", ret);
        Log::Error("Cannot check any Yara rule", ret);
        return;
    }

    for (auto& yaraSource : yaraSourcesSet)
    {
        {
            std::wregex resRegex(L"^res:#(.*)", std::regex_constants::icase);
            std::wsmatch resMatches;
            if (std::regex_search(yaraSource.value, resMatches, resRegex))
            {
                Log::Info(L"Compiling Yara rules '{}' ({})", yaraSource.value, peFile);

                auto resourceName = resMatches[1];
                CompileYaraRuleFromPlainResource(*yara, peFile, resourceName.str(), yaraSource);
                continue;
            }
        }

        {
            std::wregex archiveRegex(L"^7z:#(.*)\\|(.*)", std::regex_constants::icase);
            std::wsmatch archiveMatches;
            if (std::regex_search(yaraSource.value, archiveMatches, archiveRegex))
            {
                Log::Info(L"Compiling Yara rules '{}' ({})", yaraSource.value, peFile);

                auto resourceName = archiveMatches[1];
                auto archiveItemName = archiveMatches[2];
                CompileYaraRuleFromArchiveResource(
                    *yara, peFile, resourceName.str(), archiveItemName.str(), yaraSource);
                continue;
            }
        }

        std::wregex badRegex(L"(#|\\|)");
        if (std::regex_search(yaraSource.value, badRegex))
        {
            Log::Error(
                L"Suspicious yara rule source attribute '{}' ({}:{})",
                yaraSource.value,
                yaraSource.xmlPath.value_or(L""),
                yaraSource.xmlLine.value_or(0));
        }
        else
        {
            Log::Warn(
                L"Cannot check yara rule '{}' ({}:{})",
                yaraSource.value,
                yaraSource.xmlPath.value_or(L""),
                yaraSource.xmlLine.value_or(0));
        }
    }
}

HANDLE TryBeginUpdateResource(
    LPCWSTR pFileName,
    BOOL bDeleteExistingResources,
    uint8_t maxAttempt = kDefaultAttemptLimit,
    uint32_t delayms = kDefaultAttemptDelay)
{
    for (size_t i = 1; i <= maxAttempt; ++i)
    {
        HANDLE hResource = BeginUpdateResourceW(pFileName, bDeleteExistingResources);
        if (hResource != NULL)
        {
            return hResource;
        }

        Log::Debug(L"Failed BeginUpdateResourceW (path: {}, attempt: #{}) [{}]", pFileName, i, LastWin32Error());
        Sleep(delayms);
    }

    return NULL;
}

BOOL TryUpdateResource(
    HANDLE hUpdate,
    LPCWSTR lpType,
    LPCWSTR lpName,
    WORD wLanguage,
    LPVOID lpData,
    DWORD cb,
    uint8_t maxAttempt = kDefaultAttemptLimit,
    uint32_t delayms = kDefaultAttemptDelay)
{
    for (size_t i = 1; i <= maxAttempt; ++i)
    {
        if (UpdateResourceW(hUpdate, lpType, lpName, wLanguage, lpData, cb))
        {
            return TRUE;
        }

        Log::Debug(L"Failed EndUpdateResource (handle: {}, attempt: {}) [{}]", hUpdate, i, LastWin32Error());
        Sleep(delayms);
    }

    return FALSE;
}

// There is sometimes a race condition with Windows after modifying the module. I guess it is only EndUpdateResource
// which has some issue but I wrapped other api aswell.
BOOL TryEndUpdateResource(
    HANDLE hUpdate,
    BOOL fDiscard,
    uint8_t maxAttempt = kDefaultAttemptLimit,
    uint32_t delayms = kDefaultAttemptDelay)
{
    for (size_t i = 1; i <= maxAttempt; ++i)
    {
        if (EndUpdateResourceW(hUpdate, fDiscard))
        {
            return TRUE;
        }

        Log::Debug(L"Failed EndUpdateResource (handle: {}, attempt: {}) [{}]", hUpdate, i, LastWin32Error());
        Sleep(delayms);
    }

    return FALSE;
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

    if (hOutput == NULL)
    {
        hOutput = INVALID_HANDLE_VALUE;
    }

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

    std::vector<XmlString> yaraRules;
    ::RegisterYaraRules(ToEmbed, yaraRules);

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
                    Log::Info(L"Successfully added resource link {} -> {}", item.Name, item.Value);
                }
                else
                {
                    Log::Error(L"Failed to add resource {} -> {}", item.Name, item.Value);
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
                    Log::Info(L"Successfully delete resource '{}'", item.Name);
                }
                else
                {
                    Log::Error(L"Failed to delete resource {} [{}]", item.Name, SystemError(hr));
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
                    Log::Info(L"Successfully delete resource '{}'", item.Name);
                }
                else
                {
                    Log::Error(L"Failed to delete resource {} [{}]", item.Name, SystemError(hr));
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
        if (!TryEndUpdateResource(hOutput, FALSE))
        {
            Log::Error(L"Failed to update resources in '{}'", strPEToUpdate);
            return E_FAIL;
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
                    Log::Info(L"Successfully added '{}' as resource '{}'", item.Value, item.Name);
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

    Sleep(3000);  // Wait for the UpdateResource delay
    CheckYaraRules(strPEToUpdate, yaraRules);

    return hr;
}
