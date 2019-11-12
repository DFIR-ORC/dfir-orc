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
                            log::Warning(_L_, hr, L"Failed to write item");
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
                        XmlLiteExtension::LogError(_L_, hr, nullptr);
                        return hr;
                    }
                }
                else
                {
                    if (!bNoData)
                    {
                        if (FAILED(hr = pWriter->WriteString(config.strData.c_str())))
                        {
                            XmlLiteExtension::LogError(_L_, hr, nullptr);
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
                        XmlLiteExtension::LogError(_L_, hr, nullptr);
                        return hr;
                    }
                }
            }

            break;
            case ConfigItem::ATTRIBUTE:
                if (FAILED(
                        hr = pWriter->WriteAttributeString(NULL, config.strName.c_str(), NULL, config.c_str())))
                {
                    XmlLiteExtension::LogError(_L_, hr, nullptr);
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

    auto stream = std::make_shared<FileStream>(_L_);

    if (FAILED(hr = stream->WriteTo(strConfigFile.c_str())))
    {
        log::Error(_L_, hr, L"Failed to open xml file %s for writing\r\n", strConfigFile.c_str());
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
    m_xmllite = ExtensionLibrary::GetLibrary<XmlLiteExtension>(_L_);
    if (!m_xmllite)
    {
        log::Error(_L_, E_FAIL, L"Failed to load xmllite\r\n");
        return E_FAIL;
    }

    if (FAILED(hr = m_xmllite->CreateXmlWriter(IID_IXmlWriter, (PVOID*)&pWriter, nullptr)))
    {
        XmlLiteExtension::LogError(_L_, hr, nullptr);
        log::Error(_L_, hr, L"Failed to instantiate Xml writer\r\n");
        return hr;
    }

    if (FAILED(hr = pWriter->SetOutput(stream)))
    {
        XmlLiteExtension::LogError(_L_, hr, nullptr);
        log::Error(_L_, hr, L"Failed to set output stream\r\n", hr);
        return hr;
    }

    if (FAILED(hr = pWriter->SetProperty(XmlWriterProperty_Indent, TRUE)))
    {
        XmlLiteExtension::LogError(_L_, hr, nullptr);
        log::Error(_L_, hr, L"Failed to set indentation property\r\n", hr);
        return hr;
    }

    if (FAILED(hr = pWriter->WriteStartDocument(XmlStandalone_Omit)))
    {
        XmlLiteExtension::LogError(_L_, hr, nullptr);
        log::Error(_L_, hr, L"Failed to write start document\r\n", hr);
        return hr;
    }

    if (FAILED(hr = pWriter->WriteComment(szDescription)))
    {
        XmlLiteExtension::LogError(_L_, hr, nullptr);
        log::Error(_L_, hr, L"Failed to write description\r\n", hr);
        return hr;
    }

    if (FAILED(hr = WriteConfig(pWriter, config)))
    {
        XmlLiteExtension::LogError(_L_, hr, nullptr);
        log::Error(_L_, hr, L"Failed to write configuration\r\n", hr);
        return hr;
    }

    // WriteEndDocument closes any open elements or attributes
    if (FAILED(hr = pWriter->WriteEndDocument()))
    {
        XmlLiteExtension::LogError(_L_, hr, nullptr);
        log::Error(_L_, hr, L"Failed to write end document\r\n", hr);
        return hr;
    }

    if (FAILED(hr = pWriter->Flush()))
    {
        XmlLiteExtension::LogError(_L_, hr, nullptr);
        log::Error(_L_, hr, L"Failed to flush\r\n", hr);
        return hr;
    }

    stream.Release();
    pWriter.Release();

    streamConfig->SetFilePointer(0LL, FILE_BEGIN, NULL);

    return S_OK;
}

ConfigFileWriter::~ConfigFileWriter(void) {}
