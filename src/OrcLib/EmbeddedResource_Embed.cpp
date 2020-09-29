//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <agents.h>

#include "EmbeddedResource.h"

#include "MemoryStream.h"
#include "FileStream.h"
#include "ArchiveCreate.h"
#include "ZipCreate.h"

#include "RunningProcesses.h"
#include "ParameterCheck.h"
#include "SystemDetails.h"
#include "Temporary.h"

#include "LogFileWriter.h"

using namespace std;
using namespace Orc;

HRESULT EmbeddedResource::_UpdateResource(
    const logger& pLog,
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
            log::Error(
                pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to update ressource in %s\r\n", szModule);
            return hr;
        }
    }

    if (!UpdateResource(hOut, szType, szName, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), pData, cbSize))
    {
        log::Error(pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to add ressource %s\r\n", szName);
        return hr;
    }

    if (hOutput == INVALID_HANDLE_VALUE)
    {
        if (!EndUpdateResource(hOut, FALSE))
        {
            log::Error(
                pLog, hr = HRESULT_FROM_WIN32(GetLastError()), L"Failed to update ressource in %s\r\n", szModule);
            return hr;
        }
    }
    return S_OK;
}

HRESULT EmbeddedResource::UpdateResources(
    const logger& pLog,
    const std::wstring& strPEToUpdate,
    const std::vector<EmbedSpec>& ToEmbed)
{
    HRESULT hr = S_OK;

    DWORD dwMajor = 0L, dwMinor = 0L;
    SystemDetails::GetOSVersion(dwMajor, dwMinor);

    HANDLE hOutput = INVALID_HANDLE_VALUE;

    bool bAtomicUpdate = false;
    if (dwMajor <= 5)
        bAtomicUpdate = true;

    if (!bAtomicUpdate)
    {
        hOutput = BeginUpdateResource(strPEToUpdate.c_str(), FALSE);
        if (hOutput == NULL)
        {
            log::Error(
                pLog,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Failed to update ressource in %s\r\n",
                strPEToUpdate.c_str());
            return hr;
        }
    }

    for (auto iter = ToEmbed.begin(); iter != ToEmbed.end(); ++iter)
    {
        const EmbeddedResource::EmbedSpec& item = *iter;

        switch (item.Type)
        {
            case EmbeddedResource::EmbedSpec::NameValuePair:

                if (SUCCEEDED(
                        hr = _UpdateResource(
                            pLog,
                            hOutput,
                            strPEToUpdate.c_str(),
                            EmbeddedResource::VALUES(),
                            item.Name.c_str(),
                            (LPVOID)item.Value.c_str(),
                            (DWORD)item.Value.size() * sizeof(WCHAR))))
                {
                    log::Verbose(pLog, L"Successfully added %s=%s\r\n", item.Name.c_str(), item.Value.c_str());
                }
                break;
            case EmbeddedResource::EmbedSpec::ValuesDeletion:
                if (SUCCEEDED(
                        hr = _UpdateResource(
                            pLog,
                            hOutput,
                            strPEToUpdate.c_str(),
                            EmbeddedResource::VALUES(),
                            item.Name.c_str(),
                            NULL,
                            0L)))
                {
                    log::Verbose(pLog, L"Successfully delete resource at position %s\r\n", item.Name.c_str());
                }
                break;
            case EmbeddedResource::EmbedSpec::BinaryDeletion:
                if (SUCCEEDED(
                        hr = _UpdateResource(
                            pLog,
                            hOutput,
                            strPEToUpdate.c_str(),
                            EmbeddedResource::BINARY(),
                            item.Name.c_str(),
                            NULL,
                            0L)))
                {
                    log::Verbose(pLog, L"Successfully delete resource at position %s\r\n", item.Name.c_str());
                }
                break;
            default:
                break;
        }
    }
    if (!bAtomicUpdate)
    {
        if (!EndUpdateResource(hOutput, FALSE))
        {
            log::Error(
                pLog,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Failed to update ressource in %s\n",
                strPEToUpdate.c_str());
            return hr;
        }
    }

    for (auto iter = ToEmbed.begin(); iter != ToEmbed.end(); ++iter)
    {
        const EmbeddedResource::EmbedSpec& item = *iter;

        switch (item.Type)
        {
            case EmbeddedResource::EmbedSpec::File:
            {
                auto filestream = make_shared<FileStream>(pLog);

                if (FAILED(hr = filestream->ReadFrom(item.Value.c_str())))
                {
                    log::Error(pLog, hr, L"Failed to update ressource in %s\r\n", item.Value.c_str());
                    return hr;
                }

                auto memstream = std::make_shared<MemoryStream>(pLog);

                if (FAILED(hr = memstream->OpenForReadWrite()))
                {
                    log::Error(pLog, hr, L"Failed to open mem resource in %s\r\n", item.Value.c_str());
                    return hr;
                }
                ULONGLONG ullFileSize = filestream->GetSize();

                if (FAILED(hr = memstream->SetSize((DWORD)ullFileSize)))
                {
                    log::Error(pLog, hr, L"Failed set size of mem resource in %s\r\n", item.Value.c_str());
                    return hr;
                }

                ULONGLONG ullBytesWritten = 0LL;
                if (FAILED(hr = filestream->CopyTo(memstream, &ullBytesWritten)))
                {
                    log::Error(pLog, hr, L"Failed to copy %s to a memory stream\r\n", item.Value.c_str());
                    return hr;
                }

                CBinaryBuffer data;
                memstream->GrabBuffer(data);

                if (SUCCEEDED(
                        hr = _UpdateResource(
                            pLog,
                            INVALID_HANDLE_VALUE,
                            strPEToUpdate.c_str(),
                            EmbeddedResource::BINARY(),
                            item.Name.c_str(),
                            data.GetData(),
                            (DWORD)data.GetCount())))
                {
                    log::Verbose(
                        pLog, L"Successfully added %s at position %s\r\n", item.Value.c_str(), item.Name.c_str());
                }
            }
            break;
            case EmbeddedResource::EmbedSpec::Archive:
            {
                ArchiveFormat fmt = ArchiveFormat::Unknown;

                if (item.ArchiveFormat.empty())
                    fmt = ArchiveFormat::SevenZip;
                else
                    fmt = ArchiveCreate::GetArchiveFormat(item.ArchiveFormat);

                if (fmt == ArchiveFormat::Unknown)
                {
                    log::Error(pLog, E_INVALIDARG, L"Failed to use archive format %s\r\n", item.ArchiveFormat.c_str());
                    return E_INVALIDARG;
                }
                auto creator = ArchiveCreate::MakeCreate(fmt, pLog, false);

                auto memstream = std::make_shared<MemoryStream>(pLog);

                if (FAILED(hr = memstream->OpenForReadWrite()))
                {
                    log::Error(pLog, hr, L"Failed to initialize memory stream\r\n");
                    return hr;
                }

                if (FAILED(hr = creator->InitArchive(memstream)))
                {
                    log::Error(pLog, hr, L"Failed to initialize archive stream\r\n");
                    return hr;
                }

				if(!item.ArchiveCompression.empty())
                {
					if (FAILED(hr = creator->SetCompressionLevel(item.ArchiveCompression)))
					{
						log::Error(pLog, hr, L"Invalid compression level %s\r\n", item.ArchiveCompression.c_str());
						return hr;
					}
				}

                hr = S_OK;

                for (auto arch_item : item.ArchiveItems)
                {
                    if (FAILED(creator->AddFile(arch_item.Name.c_str(), arch_item.Path.c_str(), false)))
                    {
                        log::Error(pLog, hr, L"Failed to add file %s to archive\r\n", arch_item.Path.c_str());
                        return hr;
                    }
                    else
                    {
                        log::Verbose(pLog, L"Successfully added %s to archive\r\n", arch_item.Path.c_str());
                    }
                }

                if (FAILED(hr = creator->Complete()))
                {
                    log::Error(pLog, hr, L"Failed to complete archive\r\n");
                    return hr;
                }

                CBinaryBuffer data;
                memstream->GrabBuffer(data);

                if (SUCCEEDED(
                        hr = _UpdateResource(
                            pLog,
                            INVALID_HANDLE_VALUE,
                            strPEToUpdate.c_str(),
                            EmbeddedResource::BINARY(),
                            item.Name.c_str(),
                            data.GetData(),
                            (DWORD)data.GetCount())))
                {
                    log::Verbose(pLog, L"Successfully added archive %s\r\n", item.Name.c_str());
                }
                else
                    return hr;
            }
            break;
            default:
                break;
        }
    }
    return hr;
}
