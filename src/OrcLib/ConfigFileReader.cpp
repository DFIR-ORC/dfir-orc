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

using namespace std;

using namespace Orc;

ConfigFileReader::ConfigFileReader(logger pLog, bool bLogComment)
    : ConfigFile(std::move(pLog))
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
        log::Error(_L_, hr, L"Failed to parse node %s attributes\r\n", config.strName.c_str());
        return hr;
    }
    if (FAILED(hr = NavigateConfigNode(pReader, config.NodeList.back())))
    {
        log::Error(_L_, hr, L"Error reading element in node list\r\n");
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
            XmlLiteExtension::LogError(_L_, hr, pReader);
            return hr;
        }

        while (hr == S_OK)
        {
            const WCHAR* pName = NULL;
            if (FAILED(hr = pReader->GetLocalName(&pName, NULL)))
            {
                XmlLiteExtension::LogError(_L_, hr, pReader);
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
                log::Warning(
                    _L_, HRESULT_FROM_NT(NTE_NOT_FOUND), L"Unexpected attribute %s in configuration\r\n", pName);
            }
            else
            {
                it->Status = ConfigItem::ConfigItemStatus::PRESENT;
                const WCHAR* pValue = NULL;
                if (FAILED(hr = pReader->GetValue(&pValue, NULL)))
                {
                    XmlLiteExtension::LogError(_L_, hr, pReader);
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
        XmlLiteExtension::LogError(_L_, hr, pReader);
        return hr;
    }

    if (equalCaseInsensitive(config.strName, pName) == false)
    {
        log::Error(
            _L_,
            E_INVALIDARG,
            L"Error parsing configuration, incorrect element %s for item %s\r\n",
            pName,
            config.strName.c_str());
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
                    XmlLiteExtension::LogError(_L_, hr, pReader);
                    return hr;
                }

                auto it =
                    std::find_if(begin(config.SubItems), end(config.SubItems), [pLocalName](ConfigItem& item) -> bool {
                        return equalCaseInsensitive(item.strName, pLocalName);
                    });

                if (it == end(config.SubItems))
                {
                    log::Warning(
                        _L_, HRESULT_FROM_NT(NTE_NOT_FOUND), L"Unexpected element %s in configuration\r\n", pLocalName);
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
                                log::Error(_L_, hr, L"Failed to parse node %s attributes\r\n", pLocalName);
                                return hr;
                            }
                        }
                        else if (it->Type == ConfigItem::ConfigItemType::NODE)
                        {
                            if (FAILED(hr = NavigateConfigAttributes(pReader, *it)))
                            {
                                log::Error(_L_, hr, L"Failed to parse node %s attributes\r\n", pLocalName);
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
                                log::Error(_L_, hr, L"Failed to parse node %s attributes\r\n", pLocalName);
                                return hr;
                            }
                            if (FAILED(hr = NavigateConfigNode(pReader, *it)))
                            {
                                log::Error(_L_, hr, L"Failed to parse node %s\r\n", pLocalName);
                                return hr;
                            }
                        }
                        else if (it->Type == ConfigItem::ConfigItemType::NODELIST)
                        {
                            if (FAILED(hr = NavigateConfigNodeList(pReader, *it)))
                            {
                                log::Error(_L_, hr, L"Failed to parse node list %s\r\n", pLocalName);
                                return hr;
                            }
                        }
                        else
                        {
                            log::Error(_L_, E_INVALIDARG, L"Failed to element %s: invalid node type\r\n", pLocalName);
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
                    XmlLiteExtension::LogError(_L_, hr, pReader);
                    return hr;
                }
                if (!pValue || L'\0' == (*pValue))
                {
                    log::Error(_L_, E_UNEXPECTED, L"Error empty value\r\n");
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
                    XmlLiteExtension::LogError(_L_, hr, pReader);
                    return hr;
                }

                if (equalCaseInsensitive(config.strName, pLocalName) == false)
                {
                    log::Warning(
                        _L_,
                        HRESULT_FROM_NT(NTE_NOT_FOUND),
                        L"Unexpected of element %s in configuration item %s\r\n",
                        pLocalName,
                        config.strName.c_str());
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
        XmlLiteExtension::LogError(_L_, hr, pReader);
        return hr;
    }
    return S_OK;
}

HRESULT ConfigFileReader::ReadConfig(const WCHAR* pCfgFile, ConfigItem& config, LPCWSTR szEncodingHint)
{
    HRESULT hr = E_FAIL;

    auto filestream = std::make_shared<FileStream>(_L_);

    if (filestream == nullptr)
        return E_OUTOFMEMORY;

    if (FAILED(hr = filestream->ReadFrom(pCfgFile)))
    {
        log::Error(_L_, hr, L"Failed to open configuration file %s\r\n", pCfgFile);
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

    m_xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>(_L_);

    if (m_xmllite == nullptr)
    {
        log::Error(_L_, E_FAIL, L"Failed to load xmllite extension library\r\n");
        return E_FAIL;
    }

    CComPtr<IXmlReader> pReader;

    if (FAILED(hr = m_xmllite->CreateXmlReader(IID_IXmlReader, (PVOID*)&pReader, nullptr)))
    {
        XmlLiteExtension::LogError(_L_, hr, pReader);
        log::Error(_L_, hr, L"Failed to instantiate Xml reader\r\n");
        return hr;
    }

    if (szEncodingHint == NULL)
    {
        if (FAILED(hr = pReader->SetInput(stream)))
        {
            XmlLiteExtension::LogError(_L_, hr, pReader);
            log::Error(_L_, hr, L"Failed to set input stream\r\n");
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
            XmlLiteExtension::LogError(_L_, hr, pReader);
            log::Error(_L_, hr, L"Failed to set output stream\r\n");
            return hr;
        }
        if (FAILED(hr = pReader->SetInput(pInput)))
        {
            XmlLiteExtension::LogError(_L_, hr, pReader);
            log::Error(_L_, hr, L"Failed to set input stream\r\n");
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
                        XmlLiteExtension::LogError(_L_, hr, pReader);
                        return hr;
                    }
                    if (equalCaseInsensitive(config.strName, pName))
                    {
                        if (FAILED(hr = NavigateConfigAttributes(pReader, config)))
                        {
                            log::Error(_L_, hr, L"Failed to parse node %s attributes\r\n", pName);
                            return hr;
                        }

                        if (FAILED(hr = NavigateConfigNode(pReader, config)))
                        {
                            log::Error(_L_, hr, L"Error parsing root %s element\r\n", pName);
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
        XmlLiteExtension::LogError(_L_, hr, pReader);
        return hr;
    }
    return S_OK;
}

ConfigFileReader::~ConfigFileReader(void) {}
