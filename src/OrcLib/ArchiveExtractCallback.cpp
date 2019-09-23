//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <7zip/7zip.h>

#include "ArchiveExtractCallback.h"

#include "FileStream.h"
#include "XORStream.h"
#include "CryptoHashStream.h"
#include "OutByteStreamWrapper.h"
#include "InByteStreamWrapper.h"
#include "ParameterCheck.h"
#include "LogFileWriter.h"

#include "PropVariant.h"

#include <memory>

const std::wstring EmptyFileAlias = _T( "[Content]" );

using namespace std;
using namespace lib7z;

using namespace Orc;

ArchiveExtractCallback::ArchiveExtractCallback(
    logger pLog,
    const CComPtr<IInArchive>& archiveHandler,
    const ArchiveExtract::ItemShouldBeExtractedCallback pShouldBeExtracted,
    Archive::ArchiveItems& Extracted,
    ArchiveExtract::MakeOutputStream MakeWriteAbleStream,
    Archive::ArchiveCallback pCallback,
    bool bComputeHash,
    const std::wstring& pwd)
    : m_refCount(0)
    , _L_(std::move(pLog))
    , m_bComputeHash(bComputeHash)
    , m_ShouldBeExtracted(pShouldBeExtracted)
    , m_Extracted(Extracted)
    , m_archiveHandler(archiveHandler)
    , m_MakeWriteAbleStream(MakeWriteAbleStream)
    , m_pCallback(pCallback)
    , m_Password(pwd)
{
}

ArchiveExtractCallback::~ArchiveExtractCallback() {}

STDMETHODIMP ArchiveExtractCallback::QueryInterface(REFIID iid, void** ppvObject)
{
    if (iid == __uuidof(IUnknown))
    {
        *ppvObject = reinterpret_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }

    if (iid == IID_IArchiveExtractCallback)
    {
        *ppvObject = static_cast<IArchiveExtractCallback*>(this);
        AddRef();
        return S_OK;
    }

    if (iid == IID_ICryptoGetTextPassword)
    {
        *ppvObject = static_cast<ICryptoGetTextPassword*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) ArchiveExtractCallback::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
}

STDMETHODIMP_(ULONG) ArchiveExtractCallback::Release()
{
    ULONG res = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
    if (res == 0)
    {
        delete this;
    }
    return res;
}

STDMETHODIMP ArchiveExtractCallback::SetTotal(UInt64 size)
{
    DBG_UNREFERENCED_PARAMETER(size);
    return S_OK;
}

STDMETHODIMP ArchiveExtractCallback::SetCompleted(const UInt64* completeValue)
{
    DBG_UNREFERENCED_PARAMETER(completeValue);
    return S_OK;
}

std::shared_ptr<ByteStream> ArchiveExtractCallback::GetStreamToWrite()
{
    HRESULT hr = E_FAIL;
    std::shared_ptr<ByteStream> retval;
    shared_ptr<XORStream> pXORStream;

    if (XORStream::IsNameXORPrefixed(m_currentItem.NameInArchive.c_str()) == S_OK)
    {
        WCHAR szUnprefix[MAX_PATH];
        DWORD dwPattern;

        pXORStream = make_shared<XORStream>(_L_);

        if (FAILED(hr = pXORStream->GetXORPatternFromName(m_currentItem.NameInArchive.c_str(), dwPattern)))
            return nullptr;

        if (FAILED(hr = pXORStream->XORUnPrefixFileName(m_currentItem.NameInArchive.c_str(), szUnprefix, MAX_PATH)))
            return nullptr;

        pXORStream->SetXORPattern(dwPattern);
        m_currentItem.NameInArchive = szUnprefix;
    }

    auto filestream = m_MakeWriteAbleStream(m_currentItem);

    if (!filestream)
    {
        log::Error(_L_, E_FAIL, L"Failed to create writeable stream for %s\r\n", m_currentItem.NameInArchive.c_str());
        return nullptr;
    }

    if (pXORStream)
    {

        if (m_bComputeHash)
        {
            auto hashstream = make_shared<CryptoHashStream>(_L_);
            if (FAILED(
                    hr = hashstream->OpenToRead(
                        static_cast<SupportedAlgorithm>(SupportedAlgorithm::MD5 | SupportedAlgorithm::SHA1),
                        filestream)))
                return nullptr;
            if (FAILED(hr = pXORStream->OpenForXOR(hashstream)))
                return nullptr;
        }
        else
        {
            if (FAILED(hr = pXORStream->OpenForXOR(filestream)))
                return nullptr;
        }
        retval = pXORStream;
    }
    else
    {
        if (m_bComputeHash)
        {
            auto hashstream = make_shared<CryptoHashStream>(_L_);
            if (FAILED(
                    hr = hashstream->OpenToRead(
                        static_cast<SupportedAlgorithm>(SupportedAlgorithm::MD5 | SupportedAlgorithm::SHA1),
                        filestream)))
                return nullptr;
            retval = hashstream;
        }
        else
        {
            retval = filestream;
        }
    }
    return retval;
}

STDMETHODIMP ArchiveExtractCallback::GetStream(UInt32 index, ISequentialOutStream** outStream, Int32 askExtractMode)
{
    HRESULT hr = E_FAIL;

    // Retrieve all the various properties for the file at this index.
    if (FAILED(hr = GetPropertyFilePath(index)))
        return hr;
    if (askExtractMode != NArchive::NExtract::NAskMode::kExtract)
    {
        m_currentItem.NameInArchive.clear();
        return S_OK;
    }

    if (m_ShouldBeExtracted != nullptr)
    {
        if (!m_ShouldBeExtracted(m_currentItem.NameInArchive))
        {
            m_currentItem.NameInArchive.clear();
            return S_OK;
        }
    }

    if (FAILED(hr = GetPropertyAttributes(index)))
        return hr;
    if (FAILED(hr = GetPropertyIsDir(index)))
        return hr;
    if (FAILED(hr = GetPropertyModifiedTime(index)))
        return hr;
    if (FAILED(hr = GetPropertySize(index)))
        return hr;

    m_currentItem.Stream = GetStreamToWrite();

    if (!m_currentItem.Stream)
        return E_FAIL;

    CComQIPtr<ISequentialOutStream, &IID_ISequentialOutStream> outstream =
        new OutByteStreamWrapper(m_currentItem.Stream);

    *outStream = outstream.Detach();

    return S_OK;
}

STDMETHODIMP ArchiveExtractCallback::PrepareOperation(Int32 askExtractMode)
{
    DBG_UNREFERENCED_PARAMETER(askExtractMode);
    return S_OK;
}

STDMETHODIMP ArchiveExtractCallback::SetOperationResult(Int32 operationResult)
{
    switch (operationResult)
    {
        case NArchive::NExtract::NOperationResult::kCRCError:
            log::Error(
                _L_,
                HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
                L"CRC error when extracting %s\r\n",
                m_currentItem.NameInArchive.c_str());
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        case NArchive::NExtract::NOperationResult::kDataError:
            log::Error(
                _L_,
                HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
                L"Data error when extracting %s\r\n",
                m_currentItem.NameInArchive.c_str());
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        case NArchive::NExtract::NOperationResult::kUnsupportedMethod:
            log::Error(
                _L_,
                HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_COMPRESSION),
                L"Unsupported method Error when extracting %s\r\n",
                m_currentItem.NameInArchive.c_str());
            return HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_COMPRESSION);
        case NArchive::NExtract::NOperationResult::kOK:
            break;
    }

    if (m_currentItem.NameInArchive.empty())
    {
        return S_OK;
    }
    auto hashstream = std::dynamic_pointer_cast<CryptoHashStream>(ByteStream::GetHashStream(m_currentItem.Stream));
    if (hashstream)
    {
        hashstream->GetMD5(m_currentItem.MD5);
        hashstream->GetSHA1(m_currentItem.SHA1);
    }

    if (m_pCallback)
        m_pCallback(m_currentItem);

    m_Extracted.push_back(std::move(m_currentItem));
    return S_OK;
}

STDMETHODIMP ArchiveExtractCallback::CryptoGetTextPassword(BSTR* password)
{
    if (password == nullptr)
        return E_POINTER;
    *password = SysAllocString(m_Password.c_str());
    return S_OK;
}

STDMETHODIMP ArchiveExtractCallback::GetPropertyFilePath(UInt32 index)
{
    CPropVariant prop;

    HRESULT hr = m_archiveHandler->GetProperty(index, kpidPath, &prop);
    if (hr != S_OK)
        return hr;

    m_currentItem.Index = index;

    if (prop.vt == VT_EMPTY)
    {
        m_currentItem.NameInArchive = EmptyFileAlias;
    }
    else if (prop.vt != VT_BSTR)
    {
        return E_FAIL;
    }
    else
    {
        if (prop.bstrVal != nullptr)
            m_currentItem.NameInArchive = (std::wstring)CComBSTR(prop.bstrVal);
    }
    return S_OK;
}

STDMETHODIMP ArchiveExtractCallback::GetPropertyAttributes(UInt32 index)
{
    CPropVariant prop;
    HRESULT hr = m_archiveHandler->GetProperty(index, kpidAttrib, &prop);
    if (hr != S_OK)
        return hr;

    if (m_currentItem.Index == index)
    {
        if (prop.vt == VT_EMPTY)
        {
            m_currentItem.attrib = 0;
        }
        else if (prop.vt != VT_UI4)
        {
            return E_FAIL;
        }
        else
        {
            m_currentItem.attrib = prop.ulVal;
        }
    }
    return S_OK;
}

STDMETHODIMP ArchiveExtractCallback::GetPropertyIsDir(UInt32 index)
{
    CPropVariant prop;
    HRESULT hr = m_archiveHandler->GetProperty(index, kpidIsDir, &prop);
    if (hr != S_OK)
        return hr;

    if (m_currentItem.Index == index)
    {
        if (prop.vt == VT_EMPTY)
        {
            m_currentItem.isDir = false;
        }
        else if (prop.vt != VT_BOOL)
        {
            return E_FAIL;
        }
        else
        {
            m_currentItem.isDir = prop.boolVal != VARIANT_FALSE;
        }
    }
    return S_OK;
}

STDMETHODIMP ArchiveExtractCallback::GetPropertyModifiedTime(UInt32 index)
{
    CPropVariant prop;
    HRESULT hr = m_archiveHandler->GetProperty(index, kpidMTime, &prop);
    if (hr != S_OK)
        return hr;

    if (m_currentItem.Index == index)
    {
        if (prop.vt != VT_FILETIME)
        {
            return E_FAIL;
        }
        else
        {
            m_currentItem.modifiedTime = prop.filetime;
        }
    }
    return S_OK;
}

STDMETHODIMP ArchiveExtractCallback::GetPropertySize(UInt32 index)
{
    CPropVariant prop;
    HRESULT hr = m_archiveHandler->GetProperty(index, kpidSize, &prop);
    if (hr != S_OK)
        return hr;

    if (m_currentItem.Index == index)
    {
        switch (prop.vt)
        {
            case VT_EMPTY:
                m_currentItem.Size = false;
                return S_OK;
            case VT_UI1:
                m_currentItem.Size = prop.bVal;
                break;
            case VT_UI2:
                m_currentItem.Size = prop.uiVal;
                break;
            case VT_UI4:
                m_currentItem.Size = prop.ulVal;
                break;
            case VT_UI8:
                m_currentItem.Size = (UInt64)prop.uhVal.QuadPart;
                break;
            default:
                return E_FAIL;
        }
    }

    return S_OK;
}
