//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "TemporaryStream.h"

#include <boost/algorithm/string/replace.hpp>

#include "MemoryStream.h"
#include "FileStream.h"
#include "Temporary.h"

using namespace std;
using namespace Orc;

namespace fs = std::filesystem;

STDMETHODIMP TemporaryStream::CanRead()
{
    if (m_pMemStream)
        return m_pMemStream->CanRead();
    if (m_pFileStream)
        return m_pFileStream->CanRead();
    return S_FALSE;
}

STDMETHODIMP TemporaryStream::CanWrite()
{
    if (m_pMemStream)
        return m_pMemStream->CanWrite();
    if (m_pFileStream)
        return m_pFileStream->CanWrite();
    return S_FALSE;
}

STDMETHODIMP TemporaryStream::CanSeek()
{
    if (m_pMemStream)
        return m_pMemStream->CanSeek();
    if (m_pFileStream)
        return m_pFileStream->CanSeek();
    return S_FALSE;
}

STDMETHODIMP TemporaryStream::Open(const fs::path& output, DWORD dwMemThreshold, bool bReleaseOnClose)
{
    return TemporaryStream::Open(
        output.parent_path().wstring(), output.filename().wstring(), dwMemThreshold, bReleaseOnClose);
}

STDMETHODIMP TemporaryStream::Open(
    const std::wstring& strTempDir,
    const std::wstring& strID,
    DWORD dwMemThreshold,
    bool bReleaseOnClose)
{
    HRESULT hr = E_FAIL;

    m_bReleaseOnClose = bReleaseOnClose;

    if (strTempDir.empty())
    {
        WCHAR szTempDir[MAX_PATH] = {0};
        if (FAILED(hr = UtilGetTempDirPath(szTempDir, MAX_PATH)))
        {
            Log::Warn("Failed to create temporary path (code: {:#x})", hr);
            return hr;
        }
        m_strTemp = szTempDir;
    }
    else
        m_strTemp = strTempDir;

    m_strIdentifier = strID;
    std::replace(begin(m_strIdentifier), end(m_strIdentifier), L'\r', L'_');
    std::replace(begin(m_strIdentifier), end(m_strIdentifier), L'\n', L'_');
    std::replace(begin(m_strIdentifier), end(m_strIdentifier), L'\\', L'_');
    std::replace(begin(m_strIdentifier), end(m_strIdentifier), L'\'', L'_');
    std::replace(begin(m_strIdentifier), end(m_strIdentifier), L'\"', L'_');
    std::replace(begin(m_strIdentifier), end(m_strIdentifier), L':', L'_');

    m_dwMemThreshold = dwMemThreshold;

    m_pMemStream = make_shared<MemoryStream>();

    if (FAILED(hr = m_pMemStream->SetSize(dwMemThreshold)))
    {
        Log::Error("Failed to resize memory buffer (code: {:#x})", hr);
        return hr;
    }

    if (FAILED(hr = m_pMemStream->OpenForReadWrite(dwMemThreshold)))
    {
        Log::Debug("Failed to open memstream for {} bytes, using file stream", dwMemThreshold);
        if (FAILED(hr = MoveToFileStream(nullptr)))
        {
            Log::Error("Failed to Open temporary stream into a file stream (code: {:#x})", hr);
            return hr;
        }
    }

    return S_OK;
}

STDMETHODIMP TemporaryStream::Read(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesRead)
{
    if (m_pMemStream)
        return m_pMemStream->Read(pBuffer, cbBytes, pcbBytesRead);
    if (m_pFileStream)
        return m_pFileStream->Read(pBuffer, cbBytes, pcbBytesRead);

    return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
}

HRESULT TemporaryStream::MoveToFileStream(const std::shared_ptr<ByteStream>& aStream)
{
    HRESULT hr = E_FAIL;

    Log::Debug("INFO: Moving TemporaryStream to a file stream");

    m_pFileStream = std::make_shared<FileStream>();

    HANDLE hTempFile = INVALID_HANDLE_VALUE;

    if (FAILED(
            hr = ::UtilGetUniquePath(
                m_strTemp.c_str(),
                m_strIdentifier.empty() ? L"TempStream" : m_strIdentifier.c_str(),
                m_strFileName,
                hTempFile,
                FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED)))
        return hr;

    if (FAILED(hr = m_pFileStream->OpenHandle(hTempFile)))
        return hr;

    ULONGLONG ullCurPos = 0LL;
    if (aStream != nullptr)
    {

        ULONGLONG ullBytes = 0LL;
        if (FAILED(hr = aStream->CopyTo(m_pFileStream, &ullBytes)))
            return hr;

        if (FAILED(hr = aStream->SetFilePointer(0LL, FILE_CURRENT, &ullCurPos)))
            return hr;

        aStream->Close();
    }

    if (FAILED(hr = m_pFileStream->SetFilePointer(ullCurPos, FILE_BEGIN, NULL)))
        return hr;

    return S_OK;
}

STDMETHODIMP TemporaryStream::Write(
    __in_bcount(cbBytes) const PVOID pBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesWritten)
{
    HRESULT hr = E_FAIL;

    if (m_pFileStream)
        return m_pFileStream->Write(pBuffer, cbBytes, pcbBytesWritten);

    if (m_pMemStream == nullptr)
        return E_POINTER;

    ULONGLONG ullMemStreamSize = m_pMemStream->GetSize();

    if ((ullMemStreamSize + cbBytes) > m_dwMemThreshold)
    {
        if (FAILED(hr = MoveToFileStream(m_pMemStream)))
            return hr;

        m_pMemStream = nullptr;

        if (FAILED(hr = m_pFileStream->Write(pBuffer, cbBytes, pcbBytesWritten)))
            return hr;
    }
    else
    {
        return m_pMemStream->Write(pBuffer, cbBytes, pcbBytesWritten);
    }

    return S_OK;
}

STDMETHODIMP
TemporaryStream::SetFilePointer(__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer)
{
    if (m_pMemStream)
        return m_pMemStream->SetFilePointer(DistanceToMove, dwMoveMethod, pCurrPointer);
    if (m_pFileStream)
        return m_pFileStream->SetFilePointer(DistanceToMove, dwMoveMethod, pCurrPointer);

    return S_OK;
}

ULONG64 TemporaryStream::GetSize()
{
    if (m_pMemStream)
        return m_pMemStream->GetSize();
    if (m_pFileStream)
        return m_pFileStream->GetSize();
    return 0LL;
}

STDMETHODIMP TemporaryStream::SetSize(ULONG64 ullSize)
{
    HRESULT hr = E_FAIL;

    if (m_pMemStream)
        if (FAILED(hr = m_pMemStream->SetSize(ullSize)))
            return hr;

    if (m_pFileStream)
        if (FAILED(hr = m_pFileStream->SetSize(ullSize)))
            return hr;

    return S_OK;
}

STDMETHODIMP TemporaryStream::Close()
{
    HRESULT hr = E_FAIL;

    if (!m_bReleaseOnClose)
        return S_OK;

    if (m_pMemStream)
    {
        if (FAILED(hr = m_pMemStream->Close()))
            return hr;
        m_pMemStream = nullptr;
    }
    if (m_pFileStream)
    {
        if (FAILED(hr = m_pFileStream->Close()))
            return hr;
        m_pFileStream = nullptr;
    }
    return S_OK;
}

STDMETHODIMP TemporaryStream::MoveTo(LPCWSTR lpszNewFileName)
{
    HRESULT hr = E_FAIL;

    if (m_pMemStream)
    {
        FileStream fileStream;

        if (FAILED(hr = fileStream.WriteTo(lpszNewFileName)))
        {
            return hr;
        }

        ULONGLONG ullWritten = 0LL;
        if (FAILED(hr = m_pMemStream->CopyTo(fileStream, &ullWritten)))
        {
            return hr;
        }
        m_pMemStream->Close();
        m_pMemStream = nullptr;
    }
    else if (m_pFileStream)
    {
        m_pFileStream->Close();
        m_pFileStream = nullptr;

        if (!MoveFileEx(m_strFileName.c_str(), lpszNewFileName, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            UtilDeleteTemporaryFile(m_strFileName.c_str());  // even if the move fails, we try to delete de temp file
            return hr;
        }
        m_strFileName.clear();
    }
    return S_OK;
}

STDMETHODIMP TemporaryStream::MoveTo(const std::shared_ptr<ByteStream> pStream)
{
    HRESULT hr = E_FAIL;

    if (m_pMemStream)
    {
        ULONGLONG ullWritten = 0LL;
        if (FAILED(hr = m_pMemStream->CopyTo(pStream, &ullWritten)))
        {
            return hr;
        }
        m_pMemStream->Close();
        m_pMemStream = nullptr;
    }
    if (m_pFileStream)
    {
        ULONGLONG ullWritten = 0LL;
        if (FAILED(hr = m_pFileStream->CopyTo(pStream, &ullWritten)))
        {
            return hr;
        }
        if (FAILED(hr = m_pFileStream->Close()))
        {
            return hr;
        }

        if (FAILED(hr = UtilDeleteTemporaryFile(m_strFileName.c_str())))
        {
            Log::Error(L"Failed to delete temp file '{}' (code: {:#x})", m_strFileName, hr);
            return hr;
        }
        m_strFileName.clear();
        m_pFileStream = nullptr;
    }
    return S_OK;
}

STDMETHODIMP TemporaryStream::IsMemoryStream()
{
    if (m_pMemStream)
        return S_OK;
    return S_FALSE;
}

STDMETHODIMP TemporaryStream::IsFileStream()
{
    if (m_pFileStream)
        return S_OK;
    return S_FALSE;
}

TemporaryStream::~TemporaryStream(void)
{
    HRESULT hr = E_FAIL;

    m_pMemStream = nullptr;

    if (m_pFileStream)
    {
        m_pFileStream->Close();
        m_pFileStream = nullptr;
    }

    if (!m_strFileName.empty())
    {
        if (FAILED(hr = UtilDeleteTemporaryFile(m_strFileName.c_str())))
        {
            Log::Error(L"Failed to delete temp file '{}' (code: {:#x})", m_strFileName, hr);
        }
        m_strFileName.clear();
    }
}
