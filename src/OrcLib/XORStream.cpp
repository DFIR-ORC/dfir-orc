//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include <filesystem>

#include "XORStream.h"

#include "LogFileWriter.h"

using namespace std;
namespace fs = std::experimental::filesystem;

using namespace Orc;

XORStream::~XORStream(void)
{
    Close();
}

HRESULT XORStream::SetXORPattern(DWORD dwPattern)
{
    if (IsOpen() == S_OK)
        return E_UNEXPECTED;
    m_OriginalXORPattern = dwPattern;
    m_CurrentXORPattern = dwPattern;
    return S_OK;
}

HRESULT XORStream::OpenForXOR(const shared_ptr<ByteStream>& pChained)
{
    if (pChained == NULL)
        return E_POINTER;

    if (pChained->IsOpen() != S_OK)
    {
        log::Error(_L_, E_FAIL, L"Chained stream to XORStream must be opened\r\n");
        return E_FAIL;
    }
    m_pChainedStream = pChained;
    return S_OK;
}

HRESULT XORStream::XORMemory(LPBYTE pDest, DWORD cbDestBytes, LPBYTE pSrc, DWORD cbSrcBytes, LPDWORD pdwXORed)
{
    DWORD dwToXOR = min(cbSrcBytes, cbDestBytes);
    DWORD* pdwDstCurr = (DWORD*)pDest;
    DWORD* pdwSrcCurr = (DWORD*)pSrc;

    DWORD dwXORed = 0;

    for (DWORD i = 0; i < (dwToXOR / sizeof(DWORD)); i++)
    {
        *pdwDstCurr = *pdwSrcCurr ^ m_CurrentXORPattern;
        pdwDstCurr++;
        pdwSrcCurr++;
        dwXORed += 4;
    }

    if (dwToXOR % sizeof(DWORD))
    {
        DWORD bytestoshift = dwToXOR % sizeof(DWORD);
        PBYTE pbSrcCur = (PBYTE)pdwSrcCurr;
        PBYTE pbDstCur = (PBYTE)pdwDstCurr;
        PBYTE pbXORPattCur = (PBYTE)&m_CurrentXORPattern;

        for (DWORD i = 0; i < bytestoshift; i++)
        {
            *pbDstCur = *pbSrcCur ^ *pbXORPattCur;
            pbDstCur++;
            pbSrcCur++;
            pbXORPattCur++;
            dwXORed++;
        }

        m_CurrentXORPattern = (DWORD)RotateRight32(m_CurrentXORPattern, bytestoshift * 8);
    }
    if (pdwXORed)
        *pdwXORed = dwXORed;
    return S_OK;
}

HRESULT XORStream::XORPrefixFileName(const WCHAR* szFileName, WCHAR* szPrefixedFileName, DWORD dwPrefixedNameLen)
{
    if (m_OriginalXORPattern)
    {
        LPWSTR szSep = StrRChrW(szFileName, NULL, L'\\');
        if (szSep != NULL)
        {
            wcsncpy_s(szPrefixedFileName, dwPrefixedNameLen, szFileName, szSep - szFileName + 1);
            StringCchPrintf(
                szPrefixedFileName + (szSep - szFileName + 1),
                dwPrefixedNameLen - (szSep - szFileName + 1),
                L"XOR_%*.*X_%s",
                sizeof(m_OriginalXORPattern) * 2,
                sizeof(m_OriginalXORPattern) * 2,
                m_OriginalXORPattern,
                szSep + 1);
        }
        else
        {
            StringCchPrintf(
                szPrefixedFileName,
                dwPrefixedNameLen,
                L"XOR_%*.*X_%s",
                sizeof(m_OriginalXORPattern) * 2,
                sizeof(m_OriginalXORPattern) * 2,
                m_OriginalXORPattern,
                szFileName);
        }
    }
    else
        StringCchCopy(szPrefixedFileName, dwPrefixedNameLen, szFileName);

    return S_OK;
}

HRESULT XORStream::IsNameXORPrefixed(const WCHAR* szFileName)
{
    // Name is potentially prefixed with a folder name
    wstring base = fs::path(szFileName).stem();

    if (wcsncmp(base.c_str(), L"XOR_", wcslen(L"XOR_")))
        return S_FALSE;

    size_t dwNameLen = base.length();

    for (unsigned int i = 4; i < dwNameLen; i++)
    {
        if (base[i] >= L'0' && base[i] <= L'9')
            continue;
        if (base[i] >= L'A' && base[i] <= L'F')
            continue;

        if (base[i] == L'_' && i > 4)
            return S_OK;

        return S_FALSE;
    }
    return S_FALSE;
}

HRESULT XORStream::GetXORPatternFromName(const WCHAR* szFileName, DWORD& dwXORPattern)
{
    if (szFileName == NULL)
        return E_POINTER;

    dwXORPattern = 0;

    DWORD mult = 1;
    DWORD lastChar = 0;

    // Name is potentially prefixed with a folder name
    wstring base = fs::path(szFileName).stem();

    size_t dwNameLen = base.length();

    for (lastChar = 4; lastChar < dwNameLen; lastChar++)
    {
        if (base[lastChar] >= L'0' && base[lastChar] <= L'9')
            continue;
        if (base[lastChar] >= L'A' && base[lastChar] <= L'F')
            continue;

        if (base[lastChar] == L'_' && lastChar > 4)
            break;

        return E_INVALIDARG;
    }
    lastChar--;

    for (int j = lastChar; j >= 4; j--)
    {
        if (base[j] >= L'0' && base[j] <= L'9')
            dwXORPattern += (base[j] - L'0') * mult;
        else if (base[j] >= L'A' && base[j] <= L'F')
            dwXORPattern += (base[j] - L'A' + 10) * mult;
        mult *= 16;
    }
    return S_OK;
}

HRESULT
XORStream::XORUnPrefixFileName(const WCHAR* szPrefixedFileName, WCHAR* szUnPrefixedFileName, DWORD dwUnPrefixedNameLen)
{
    DWORD lastChar = 0;

    // Name is potentially prefixed with a folder name
    fs::path prefixedpath(szPrefixedFileName);
    wstring parent = prefixedpath.parent_path();
    wstring file = prefixedpath.filename();

    size_t dwNameLen = file.length();

    for (lastChar = 4; lastChar < dwNameLen; lastChar++)
    {
        if (file[lastChar] >= L'0' && file[lastChar] <= L'9')
            continue;
        if (file[lastChar] >= L'A' && file[lastChar] <= L'F')
            continue;

        if (file[lastChar] == L'_' && lastChar > 4)
            break;

        return E_INVALIDARG;
    }

    if (parent.empty())
    {
        wstring strUnprefixed(file.substr(lastChar + 1));
        StringCchCopy(szUnPrefixedFileName, dwUnPrefixedNameLen, strUnprefixed.c_str());
    }
    else
    {
        fs::path apath(parent);
        apath /= file.substr(lastChar + 1);

        StringCchCopy(szUnPrefixedFileName, dwUnPrefixedNameLen, apath.c_str());
    }
    return S_OK;
}

HRESULT XORStream::Read(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesRead)
{
    HRESULT hr = E_FAIL;

    if (pReadBuffer == NULL)
        return E_INVALIDARG;
    if (cbBytes > MAXDWORD)
        return E_INVALIDARG;
    if (pcbBytesRead == NULL)
        return E_POINTER;
    if (m_pChainedStream == NULL)
        return E_POINTER;

    if (pcbBytesRead != nullptr)
        *pcbBytesRead = 0;

    if (m_pChainedStream->CanRead() != S_OK)
        return HRESULT_FROM_WIN32(ERROR_INVALID_ACCESS);

    ULONGLONG cbBytesRead = 0L;
    if (FAILED(hr = m_pChainedStream->Read(pReadBuffer, cbBytes, &cbBytesRead)))
        return hr;

    if (m_OriginalXORPattern)
    {
        DWORD dwXORed = 0;
        if (FAILED(
                hr = XORMemory(
                    (PBYTE)pReadBuffer, (DWORD)cbBytesRead, (PBYTE)pReadBuffer, (DWORD)cbBytesRead, &dwXORed)))
            return hr;

        if (dwXORed != cbBytesRead)
        {
            log::Error(_L_, E_FAIL, L"XOR after Read failed\r\n");
            return E_FAIL;
        }
    }

    *pcbBytesRead = cbBytesRead;
    return S_OK;
}

HRESULT XORStream::Write(
    __in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
    __in ULONGLONG cbBytesToWrite,
    __out_opt PULONGLONG pcbBytesWritten)
{
    HRESULT hr = E_FAIL;

    if (pWriteBuffer == NULL)
        return E_POINTER;
    if (pcbBytesWritten == NULL)
        return E_POINTER;
    if (m_pChainedStream == NULL)
        return E_POINTER;

    if (m_pChainedStream->CanWrite() != S_OK)
        return HRESULT_FROM_WIN32(ERROR_INVALID_ACCESS);

    *pcbBytesWritten = 0;

    if (cbBytesToWrite > MAXDWORD)
    {
        log::Error(_L_, E_INVALIDARG, L"Too many bytes to XOR\r\n");
        return E_INVALIDARG;
    }

    CBinaryBuffer buffer;

    if (!buffer.SetCount((DWORD)cbBytesToWrite))
        return E_OUTOFMEMORY;

    if (m_OriginalXORPattern)
    {
        DWORD dwXORed = 0;

        XORMemory(
            (PBYTE)buffer.GetData(),
            static_cast<DWORD>(buffer.GetCount()),
            (PBYTE)pWriteBuffer,
            static_cast<DWORD>(cbBytesToWrite),
            &dwXORed);

        if (dwXORed != cbBytesToWrite)
        {
            log::Error(_L_, E_FAIL, L"XOR before WriteFile failed\r\n");
            return E_FAIL;
        }
    }
    ULONGLONG cbBytesWritten = 0;

    if (FAILED(hr = m_pChainedStream->Write(buffer.GetData(), buffer.GetCount(), &cbBytesWritten)))
        return hr;

    *pcbBytesWritten = cbBytesWritten;

    return S_OK;
}
