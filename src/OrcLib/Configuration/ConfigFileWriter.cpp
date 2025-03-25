//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include <string>

#include "ConfigFileWriter.h"

#include "XmlLiteExtension.h"

#include "FileStream.h"

#include "Log/Log.h"

using namespace std;

using namespace Orc;

HRESULT ConfigFileWriter::WriteConfigNode(const CComPtr<IXmlWriter>& pWriter, const ConfigItem& config)
{
    HRESULT hr = E_FAIL;

    if (config)
    {
        switch (config.Type)
        {
            case ConfigItem::NODE: {
                pWriter->WriteStartElement(NULL, config.strName.c_str(), NULL);

                // Adding attributes
                std::for_each(begin(config.SubItems), end(config.SubItems), [&pWriter](const ConfigItem& item) {
                    HRESULT hr = E_FAIL;
                    if (item && item.Type & ConfigItem::ATTRIBUTE)
                    {
                        if (FAILED(hr = pWriter->WriteAttributeString(NULL, item.strName.c_str(), NULL, item.c_str())))
                        {
                            Log::Warn("Failed to write item [{}]", SystemError(hr));
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
                        begin(config.SubItems), end(config.SubItems), [&pWriter, &bHasChild](const ConfigItem& item) {
                            if (item && (item.Type == ConfigItem::NODE) || (item.Type == ConfigItem::NODELIST))
                            {
                                WriteConfigNode(pWriter, item);
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
                if (FAILED(hr = pWriter->WriteAttributeString(NULL, config.strName.c_str(), NULL, config.c_str())))
                {
                    XmlLiteExtension::LogError(hr, nullptr);
                }
                break;
            case ConfigItem::NODELIST:
                std::for_each(begin(config.NodeList), end(config.NodeList), [&pWriter](const ConfigItem& item) {
                    WriteConfigNode(pWriter, item);
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
        Log::Error(L"Failed to open xml file '{}' for writing [{}]", strConfigFile, SystemError(hr));
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
        Log::Error(L"Failed to load xmllite");
        return E_FAIL;
    }

    if (FAILED(hr = m_xmllite->CreateXmlWriter(IID_IXmlWriter, (PVOID*)&pWriter, nullptr)))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        Log::Error(L"Failed to instantiate Xml writer [{}]", SystemError(hr));
        return hr;
    }

    if (FAILED(hr = pWriter->SetOutput(stream)))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        Log::Error(L"Failed to set output stream [{}]", SystemError(hr));
        return hr;
    }

    if (FAILED(hr = pWriter->SetProperty(XmlWriterProperty_Indent, TRUE)))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        Log::Error(L"Failed to set indentation property [{}]", SystemError(hr));
        return hr;
    }

    if (FAILED(hr = pWriter->WriteStartDocument(XmlStandalone_Omit)))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        Log::Error(L"Failed to write start document [{}]", SystemError(hr));
        return hr;
    }

    if (FAILED(hr = pWriter->WriteComment(szDescription)))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        Log::Error(L"Failed to write description [{}]", SystemError(hr));
        return hr;
    }

    if (FAILED(hr = WriteConfigNode(pWriter, config)))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        Log::Error(L"Failed to write configuration [{}]", SystemError(hr));
        return hr;
    }

    // WriteEndDocument closes any open elements or attributes
    if (FAILED(hr = pWriter->WriteEndDocument()))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        Log::Error(L"Failed to write end document [{}]", SystemError(hr));
        return hr;
    }

    if (FAILED(hr = pWriter->Flush()))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        Log::Error(L"Failed to flush [{}]", SystemError(hr));
        return hr;
    }

    stream.Release();
    pWriter.Release();

    streamConfig->SetFilePointer(0LL, FILE_BEGIN, NULL);

    return S_OK;
}

ConfigFileWriter::~ConfigFileWriter(void) {}
