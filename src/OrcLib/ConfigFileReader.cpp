//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include <cwctype>

#include "ConfigFileReader.h"

#include "FileStream.h"

#include "XmlLiteExtension.h"

#include "CaseInsensitive.h"

#include <spdlog/spdlog.h>

using namespace std;

using namespace Orc;

ConfigFileReader::ConfigFileReader(bool bLogComment)
    : ConfigFile()
{
    m_bLogComments = bLogComment;
}

HRESULT ConfigFileReader::NavigateConfigNodeList(const CComPtr<IXmlReader>& pReader, ConfigItem& config)
{
    HRESULT hr = E_FAIL;

    // First config item HAS to be a Node
    if (config.Type != ConfigItem::NODELIST)
        return E_INVALIDARG;

    ConfigItem newitem;
    newitem = config;
    newitem.Type = ConfigItem::NODE;
    newitem.Status = ConfigItem::PRESENT;
    config.NodeList.push_back(std::move(newitem));

    if (FAILED(hr = NavigateConfigAttributes(pReader, config.NodeList.back())))
    {
        spdlog::error(L"Failed to parse node '{}'' attributes (code: {:#x})", config.strName.c_str(), hr);
        return hr;
    }
    if (FAILED(hr = NavigateConfigNode(pReader, config.NodeList.back())))
    {
        spdlog::error(L"Error reading element in node list (code: {:#x})", hr);
        return hr;
    }

    config.Status = ConfigItem::PRESENT;
    return S_OK;
}

HRESULT ConfigFileReader::NavigateConfigAttributes(const CComPtr<IXmlReader>& pReader, ConfigItem& config)
{
    HRESULT hr = E_FAIL;

    UINT uiAttrCount = 0;
    if (SUCCEEDED(pReader->GetAttributeCount(&uiAttrCount)))
    {
        if (uiAttrCount == 0L)
            return S_OK;

        if (FAILED(hr = pReader->MoveToFirstAttribute()))
        {
            XmlLiteExtension::LogError(hr, pReader);
            return hr;
        }

        while (hr == S_OK)
        {
            const WCHAR* pName = NULL;
            if (FAILED(hr = pReader->GetLocalName(&pName, NULL)))
            {
                XmlLiteExtension::LogError(hr, pReader);
                pReader->MoveToElement();
                return hr;
            }

            auto it = std::find_if(begin(config.SubItems), end(config.SubItems), [pName](ConfigItem& item) -> bool {
                if (item.Type == ConfigItem::ConfigItemType::ATTRIBUTE)
                    return equalCaseInsensitive(item.strName, pName);
                return false;
            });

            if (it == end(config.SubItems))
            {
                spdlog::warn(L"Unexpected attribute '{}' in configuration", pName);
            }
            else
            {
                it->Status = ConfigItem::ConfigItemStatus::PRESENT;
                const WCHAR* pValue = NULL;
                if (FAILED(hr = pReader->GetValue(&pValue, NULL)))
                {
                    XmlLiteExtension::LogError(hr, pReader);
                    pReader->MoveToElement();
                    return hr;
                }
                it->strData = pValue;
            }

            hr = pReader->MoveToNextAttribute();
        }
    }
    pReader->MoveToElement();
    return S_OK;
}

HRESULT ConfigFileReader::NavigateConfigNode(const CComPtr<IXmlReader>& pReader, ConfigItem& config)
{
    HRESULT hr = E_FAIL;

    // First config item HAS to be a Node
    if (config.Type != ConfigItem::NODE)
        return E_INVALIDARG;

    const WCHAR* pName = NULL;
    if (FAILED(hr = pReader->GetLocalName(&pName, NULL)))
    {
        XmlLiteExtension::LogError(hr, pReader);
        return hr;
    }

    if (equalCaseInsensitive(config.strName, pName) == false)
    {
        spdlog::error(L"Error parsing configuration, incorrect element '{}' for item '{}'", pName, config.strName);
        return E_INVALIDARG;
    }

    config.Status = ConfigItem::ConfigItemStatus::PRESENT;

    // read until there are no more nodes
    XmlNodeType nodeType = XmlNodeType_None;

    DWORD dwCurrentIndex = 0L;

    while (S_OK == (hr = pReader->Read(&nodeType)))
    {
        switch (nodeType)
        {
            case XmlNodeType_Element:
            {

                // if you are not interested in the length it may be faster to use
                // NULL for the length parameter
                const WCHAR* pLocalName = NULL;
                if (FAILED(hr = pReader->GetLocalName(&pLocalName, NULL)))
                {
                    XmlLiteExtension::LogError(hr, pReader);
                    return hr;
                }

                auto it =
                    std::find_if(begin(config.SubItems), end(config.SubItems), [pLocalName](ConfigItem& item) -> bool {
                        return equalCaseInsensitive(item.strName, pLocalName);
                    });

                if (it == end(config.SubItems))
                {
                    spdlog::warn(L"Unexpected element '{}' in configuration", pLocalName);
                }
                else
                {
                    it->dwOrderIndex = dwCurrentIndex;

                    if (pReader->IsEmptyElement())
                    {
                        it->Status = ConfigItem::ConfigItemStatus::PRESENT;

                        if (it->Type == ConfigItem::ConfigItemType::NODELIST)
                        {
                            ConfigItem newitem = *it;
                            newitem.Type = ConfigItem::ConfigItemType::NODE;

                            it->NodeList.push_back(newitem);

                            if (FAILED(hr = NavigateConfigAttributes(pReader, it->NodeList.back())))
                            {
                                spdlog::error(L"Failed to parse node '{}' attributes (code: {:#x})", pLocalName, hr);
                                return hr;
                            }
                        }
                        else if (it->Type == ConfigItem::ConfigItemType::NODE)
                        {
                            if (FAILED(hr = NavigateConfigAttributes(pReader, *it)))
                            {
                                spdlog::error(L"Failed to parse node '{}' attributes (code: {:#x})", pLocalName, hr);
                                return hr;
                            }
                        }
                    }
                    else
                    {
                        if (it->Type == ConfigItem::ConfigItemType::NODE)
                        {
                            if (FAILED(hr = NavigateConfigAttributes(pReader, *it)))
                            {
                                spdlog::error(L"Failed to parse node '{}' attributes (code: {:#x})", pLocalName, hr);
                                return hr;
                            }
                            if (FAILED(hr = NavigateConfigNode(pReader, *it)))
                            {
                                spdlog::error(L"Failed to parse node '{}' (code: {:#x})", pLocalName, hr);
                                return hr;
                            }
                        }
                        else if (it->Type == ConfigItem::ConfigItemType::NODELIST)
                        {
                            if (FAILED(hr = NavigateConfigNodeList(pReader, *it)))
                            {
                                spdlog::error(L"Failed to parse node list '{}' (code: {:#x})", pLocalName, hr);
                                return hr;
                            }
                        }
                        else
                        {
                            spdlog::error(L"Failed to element '{}': invalid node type", pLocalName);
                            return E_INVALIDARG;
                        }
                    }
                }

                dwCurrentIndex++;
            }
            break;
            case XmlNodeType_Text:
            {
                const WCHAR* pValue = NULL;
                if (FAILED(hr = pReader->GetValue(&pValue, NULL)))
                {
                    XmlLiteExtension::LogError(hr, pReader);
                    return hr;
                }
                if (!pValue || L'\0' == (*pValue))
                {
                    spdlog::error("Error empty value");
                    return hr;
                }
                config.strData = pValue;
            }
            break;
            case XmlNodeType_EndElement:
            {
                const WCHAR* pLocalName = NULL;
                if (FAILED(hr = pReader->GetLocalName(&pLocalName, NULL)))
                {
                    XmlLiteExtension::LogError(hr, pReader);
                    return hr;
                }

                if (equalCaseInsensitive(config.strName, pLocalName) == false)
                {
                    spdlog::warn(L"Unexpected of element '{}' in configuration item '{}'", pLocalName, config.strName);
                }
                return S_OK;
            }
            break;
            default:
            {
            }
            break;
        }
    }
    if (FAILED(hr))
    {
        XmlLiteExtension::LogError(hr, pReader);
        return hr;
    }
    return S_OK;
}

HRESULT ConfigFileReader::ReadConfig(const WCHAR* pCfgFile, ConfigItem& config, LPCWSTR szEncodingHint)
{
    HRESULT hr = E_FAIL;

    auto filestream = std::make_shared<FileStream>();

    if (filestream == nullptr)
        return E_OUTOFMEMORY;

    if (FAILED(hr = filestream->ReadFrom(pCfgFile)))
    {
        spdlog::error(L"Failed to open configuration file '{}'", pCfgFile);
        return hr;
    }

    return ReadConfig(filestream, config, szEncodingHint);
}

HRESULT ConfigFileReader::ReadConfig(
    const std::shared_ptr<ByteStream>& streamConfig,
    ConfigItem& config,
    LPCWSTR szEncodingHint)
{
    HRESULT hr = E_FAIL;

    CComPtr<IStream> stream;

    if (FAILED(hr = ByteStream::Get_IStream(streamConfig, &stream)))
        return hr;

    m_xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>();

    if (m_xmllite == nullptr)
    {
        spdlog::error("Failed to load xmllite extension library");
        return E_FAIL;
    }

    CComPtr<IXmlReader> pReader;

    if (FAILED(hr = m_xmllite->CreateXmlReader(IID_IXmlReader, (PVOID*)&pReader, nullptr)))
    {
        XmlLiteExtension::LogError(hr, pReader);
        spdlog::error("Failed to instantiate Xml reader (code: {:#x})", hr);
        return hr;
    }

    if (szEncodingHint == NULL)
    {
        if (FAILED(hr = pReader->SetInput(stream)))
        {
            XmlLiteExtension::LogError(hr, pReader);
            spdlog::error("Failed to set input stream (code: {:#x})", hr);
            return hr;
        }
    }
    else
    {

        CComPtr<IXmlReaderInput> pInput;
        if (FAILED(
                hr = m_xmllite->CreateXmlReaderInputWithEncodingName(
                    stream, nullptr, szEncodingHint, FALSE, L"", &pInput)))
        {
            XmlLiteExtension::LogError(hr, pReader);
            spdlog::error("Failed to set output stream (code: {:#x})", hr);
            return hr;
        }
        if (FAILED(hr = pReader->SetInput(pInput)))
        {
            XmlLiteExtension::LogError(hr, pReader);
            spdlog::error("Failed to set input stream (code: {:#x})", hr);
            return hr;
        }
    }
    // read until we find the root node
    XmlNodeType nodeType = XmlNodeType_None;

    while (S_OK == (hr = pReader->Read(&nodeType)))
    {
        switch (nodeType)
        {
            case XmlNodeType_Element:
            {
                if (!pReader->IsEmptyElement())
                {
                    // if you are not interested in the length it may be faster to use
                    // NULL for the length parameter
                    const WCHAR* pName = NULL;
                    if (FAILED(hr = pReader->GetLocalName(&pName, NULL)))
                    {
                        XmlLiteExtension::LogError(hr, pReader);
                        return hr;
                    }
                    if (equalCaseInsensitive(config.strName, pName))
                    {
                        if (FAILED(hr = NavigateConfigAttributes(pReader, config)))
                        {
                            spdlog::error(L"Failed to parse node '{}' attributes (code: {:#x})", pName, hr);
                            return hr;
                        }

                        if (FAILED(hr = NavigateConfigNode(pReader, config)))
                        {
                            spdlog::error(L"Error parsing root '{}' element (code: {:#x})", pName, hr);
                            return hr;
                        }
                    }
                }
            }
            break;
            default:
                break;
        }
    }

    if (FAILED(hr))
    {
        XmlLiteExtension::LogError(hr, pReader);
        return hr;
    }

    return S_OK;
}

ConfigFileReader::~ConfigFileReader(void) {}
