//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include <string>

#include "ConfigFileWriter.h"

#include "XmlLiteExtension.h"

#include "FileStream.h"

#include <spdlog/spdlog.h>

using namespace std;

using namespace Orc;

HRESULT ConfigFileWriter::WriteConfig(const CComPtr<IXmlWriter>& pWriter, const ConfigItem& config)
{
    HRESULT hr = E_FAIL;

    if (config)
    {
        switch (config.Type)
        {
            case ConfigItem::NODE:
            {
                pWriter->WriteStartElement(NULL, config.strName.c_str(), NULL);

                // Adding attributes
                std::for_each(begin(config.SubItems), end(config.SubItems), [this, &pWriter](const ConfigItem& item) {
                    HRESULT hr = E_FAIL;
                    if (item && item.Type & ConfigItem::ATTRIBUTE)
                    {
                        if (FAILED(
                                hr = pWriter->WriteAttributeString(
                                    NULL, item.strName.c_str(), NULL, item.c_str())))
                        {
                            spdlog::warn("Failed to write item (code: {:#x})", hr);
                            return;
                        }
                    }
                });

                bool bNoData = std::all_of(begin(config.strData), end(config.strData), [](const wchar_t aChar) {
                    return iswspace(aChar) || aChar == L'\r' || aChar == L'\n';
                });
                bool bOnlyAttr = std::all_of(begin(config.SubItems), end(config.SubItems), [](const ConfigItem& item) {
                    return item.Type == ConfigItem::ATTRIBUTE;
                });

                if (bOnlyAttr && bNoData)
                {
                    if (FAILED(hr = pWriter->WriteEndElement()))
                    {
                        XmlLiteExtension::LogError(hr, nullptr);
                        return hr;
                    }
                }
                else
                {
                    if (!bNoData)
                    {
                        if (FAILED(hr = pWriter->WriteString(config.strData.c_str())))
                        {
                            XmlLiteExtension::LogError(hr, nullptr);
                            return hr;
                        }
                    }

                    // sub nodes
                    bool bHasChild = false;
                    std::for_each(
                        begin(config.SubItems),
                        end(config.SubItems),
                        [this, &pWriter, &bHasChild](const ConfigItem& item) {
                            if (item && (item.Type == ConfigItem::NODE)
                                || (item.Type == ConfigItem::NODELIST))
                            {
                                WriteConfig(pWriter, item);
                                bHasChild = true;
                            }
                        });
                    if (FAILED(hr = pWriter->WriteEndElement()))
                    {
                        XmlLiteExtension::LogError(hr, nullptr);
                        return hr;
                    }
                }
            }

            break;
            case ConfigItem::ATTRIBUTE:
                if (FAILED(
                        hr = pWriter->WriteAttributeString(NULL, config.strName.c_str(), NULL, config.c_str())))
                {
                    XmlLiteExtension::LogError(hr, nullptr);
                }
                break;
            case ConfigItem::NODELIST:
                std::for_each(begin(config.NodeList), end(config.NodeList), [this, &pWriter](const ConfigItem& item) {
                    WriteConfig(pWriter, item);
                });
                break;
            default:
                break;
        }
    }
    return S_OK;
}

HRESULT
ConfigFileWriter::WriteConfig(const wstring& strConfigFile, const WCHAR* szDescription, const ConfigItem& config)
{
    HRESULT hr = E_FAIL;

    auto stream = std::make_shared<FileStream>();

    if (FAILED(hr = stream->WriteTo(strConfigFile.c_str())))
    {
        spdlog::error(L"Failed to open xml file '{}' for writing (code: {:#x})", strConfigFile, hr);
        return hr;
    }

    if (FAILED(hr = WriteConfig(stream, szDescription, config)))
    {
        stream->Close();
        return hr;
    }

    stream->Close();

    return S_OK;
}

HRESULT ConfigFileWriter::WriteConfig(
    const std::shared_ptr<ByteStream>& streamConfig,
    const WCHAR* szDescription,
    const ConfigItem& config)
{
    HRESULT hr = E_FAIL;

    CComPtr<IStream> stream;

    if (FAILED(hr = ByteStream::Get_IStream(streamConfig, &stream)))
        return hr;

    CComPtr<IXmlWriter> pWriter;
    m_xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>();
    if (!m_xmllite)
    {
        spdlog::error(L"Failed to load xmllite");
        return E_FAIL;
    }

    if (FAILED(hr = m_xmllite->CreateXmlWriter(IID_IXmlWriter, (PVOID*)&pWriter, nullptr)))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        spdlog::error(L"Failed to instantiate Xml writer (code: {:#x})", hr);
        return hr;
    }

    if (FAILED(hr = pWriter->SetOutput(stream)))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        spdlog::error(L"Failed to set output stream (code: {:#x})", hr);
        return hr;
    }

    if (FAILED(hr = pWriter->SetProperty(XmlWriterProperty_Indent, TRUE)))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        spdlog::error(L"Failed to set indentation property (code: {:#x})", hr);
        return hr;
    }

    if (FAILED(hr = pWriter->WriteStartDocument(XmlStandalone_Omit)))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        spdlog::error(L"Failed to write start document (code: {:#x})", hr);
        return hr;
    }

    if (FAILED(hr = pWriter->WriteComment(szDescription)))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        spdlog::error(L"Failed to write description (code: {:#x})", hr);
        return hr;
    }

    if (FAILED(hr = WriteConfig(pWriter, config)))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        spdlog::error(L"Failed to write configuration (code: {:#x})", hr);
        return hr;
    }

    // WriteEndDocument closes any open elements or attributes
    if (FAILED(hr = pWriter->WriteEndDocument()))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        spdlog::error(L"Failed to write end document (code: {:#x})", hr);
        return hr;
    }

    if (FAILED(hr = pWriter->Flush()))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        spdlog::error(L"Failed to flush (code: {:#x})", hr);
        return hr;
    }

    stream.Release();
    pWriter.Release();

    streamConfig->SetFilePointer(0LL, FILE_BEGIN, NULL);

    return S_OK;
}

ConfigFileWriter::~ConfigFileWriter(void) {}
