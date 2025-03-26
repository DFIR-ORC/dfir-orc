//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "OrcLib.h"

#include "BinaryBuffer.h"

#include "CryptoUtilities.h"

#include "OrcException.h"

using namespace Orc;

HCRYPTPROV CBinaryBuffer::g_hProv = NULL;

using namespace std;

HANDLE GetBinaryBufferHeap()
{
    static HANDLE g_Win32BinaryBufferHeap = NULL;

    if (g_Win32BinaryBufferHeap != nullptr)
        return g_Win32BinaryBufferHeap;

    g_Win32BinaryBufferHeap = HeapCreate(0L, 0L, 0L);
    if (g_Win32BinaryBufferHeap != nullptr)
        return g_Win32BinaryBufferHeap;

    g_Win32BinaryBufferHeap = GetProcessHeap();
    if (g_Win32BinaryBufferHeap != nullptr)
        return g_Win32BinaryBufferHeap;

    throw Exception(
        Severity::Fatal,
        HRESULT_FROM_WIN32(GetLastError()),
        L"Binary buffer heap could not be created nor did process heap"sv);
}

CBinaryBuffer::CBinaryBuffer(const CBinaryBuffer& other)
    : m_pData(nullptr)
    , m_size(0L)
    , m_bOwnMemory(true)
    , m_bVirtualAlloc(other.m_bVirtualAlloc)
    , m_bJunk(true)
{
    if (other.m_size > 0)
    {
        if (other.m_bOwnMemory)
        {
            if (m_bVirtualAlloc)
            {
                m_pData = (BYTE*)VirtualAlloc(NULL, other.m_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                m_bOwnMemory = true;
                if (m_pData == nullptr)
                    throw Exception(Severity::Fatal, E_OUTOFMEMORY, L"out of memory"sv);

                if (!other.m_bJunk)
                    CopyMemory(m_pData, other.m_pData, other.m_size);
                m_bJunk = false;
                m_size = other.m_size;
            }
            else
            {

                m_pData = (BYTE*)HeapAlloc(GetBinaryBufferHeap(), 0L, other.m_size);
                m_bOwnMemory = true;
                if (m_pData == nullptr)
                    throw L"out of memory";
                else
                {
                    if (!other.m_bJunk)
                        CopyMemory(m_pData, other.m_pData, other.m_size);
                    m_bJunk = false;
                    m_size = other.m_size;
                }
            }
        }
        else
        {
            m_pData = other.m_pData;
            m_size = other.m_size;
            m_bOwnMemory = other.m_bOwnMemory;
            m_bJunk = other.m_bJunk;
        }
    }
}

bool CBinaryBuffer::SetCount(size_t NewSize)
{
    if (m_bOwnMemory)
    {
        if (NewSize == m_size)
            return true;

        if (NewSize > 0)
        {
            if (m_bVirtualAlloc)
            {
                BYTE* NewData = (BYTE*)VirtualAlloc(NULL, NewSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if (NewData == NULL)
                    return false;

                if (m_pData != nullptr && !m_bJunk)
                {
                    CopyMemory(NewData, m_pData, min(m_size, NewSize));
                    VirtualFree(m_pData, 0L, MEM_RELEASE);
                }
                m_size = NewSize;
                m_pData = NewData;
                m_bJunk = false;
            }
            else
            {
                if (m_pData != nullptr)
                {
                    if (m_bJunk)
                    {
                        HeapFree(GetBinaryBufferHeap(), 0L, m_pData);
                        m_pData = (BYTE*)HeapAlloc(GetBinaryBufferHeap(), 0L, NewSize);
                    }
                    else
                    {
                        m_pData = (BYTE*)HeapReAlloc(GetBinaryBufferHeap(), 0L, m_pData, NewSize);
                    }
                }
                else
                {
                    m_pData = (BYTE*)HeapAlloc(GetBinaryBufferHeap(), 0L, NewSize);
                }
                m_bJunk = false;
                if (m_pData == nullptr)
                {
                    m_size = 0L;
                    return false;
                }
                m_size = NewSize;
            }
        }
        else
        {
            RemoveAll();
        }
    }
    else
    {
        if (NewSize > m_size)
            return false;
        m_size = NewSize;
    }
    return true;
}

HRESULT CBinaryBuffer::SetData(LPCBYTE pBuffer, size_t cbSize)
{
    if (!SetCount(cbSize))
        return E_OUTOFMEMORY;

    CopyMemory(m_pData, pBuffer, cbSize);
    m_bJunk = false;
    return S_OK;
}

HRESULT CBinaryBuffer::CopyTo(LPBYTE pBuffer, size_t cbSize)
{
    if (m_pData == nullptr)
    {
        return S_OK;
    }

    size_t to_copy = min(m_size, cbSize);
    CopyMemory(pBuffer, m_pData, to_copy);
    return S_OK;
}

std::wstring CBinaryBuffer::ToHex(bool b0xPrefix) const
{
    wstring retval;

    static const wchar_t szNibbleToHex[] = {L"0123456789ABCDEF"};

    size_t nLength = m_size * 2;
    size_t nStart = 0;

    if (b0xPrefix)
    {
        nLength += 2;
        retval.resize(nLength);
        retval[0] = L'0';
        retval[1] = L'x';
        nStart = 2;
    }
    else
        retval.resize(nLength);

    for (size_t i = 0; i < m_size; i++)
    {
        size_t nNibble = ((UCHAR)m_pData[i]) >> 4;
        retval[2 * i + nStart] = szNibbleToHex[nNibble];

        nNibble = m_pData[i] & 0x0F;
        retval[2 * i + 1 + nStart] = szNibbleToHex[nNibble];
    }
    return retval;
}

void CBinaryBuffer::ZeroMe()
{
    if (m_pData && m_size > 0)
        ZeroMemory(m_pData, m_size);
    return;
}

void CBinaryBuffer::RemoveAll()
{
    if (m_bOwnMemory && m_pData)
    {
        if (m_bVirtualAlloc)
        {
            VirtualFree(m_pData, 0L, MEM_RELEASE);
        }
        else
        {
            HeapFree(GetBinaryBufferHeap(), 0L, m_pData);
        }
    }

    m_pData = nullptr;
    m_size = 0L;
    m_bOwnMemory = true;
    m_bJunk = true;
}

HRESULT CBinaryBuffer::GetSHA1(CBinaryBuffer& SHA1)
{
    if (m_pData == nullptr)
    {
        return S_OK;
    }

    if (g_hProv == NULL)
        if (!CryptAcquireContext(&g_hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
            return HRESULT_FROM_WIN32(GetLastError());

    HCRYPTHASH hSha1 = NULL;
    if (!CryptCreateHash(g_hProv, CALG_SHA1, 0, 0, &hSha1))
        return HRESULT_FROM_WIN32(GetLastError());

    if (hSha1 && !CryptHashData(hSha1, m_pData, (DWORD)m_size, 0))
    {
        CryptDestroyHash(hSha1);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    DWORD cbHash = BYTES_IN_SHA1_HASH;
    SHA1.SetCount(BYTES_IN_SHA1_HASH);
    if (!CryptGetHashParam(hSha1, HP_HASHVAL, SHA1.GetData(), &cbHash, 0))
    {
        CryptDestroyHash(hSha1);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (hSha1 != NULL)
        CryptDestroyHash(hSha1);

    return S_OK;
}

HRESULT CBinaryBuffer::GetMD5(CBinaryBuffer& MD5)
{
    if (m_pData == nullptr)
    {
        return S_OK;
    }

    if (g_hProv == NULL)
        if (!CryptAcquireContext(&g_hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
            return HRESULT_FROM_WIN32(GetLastError());

    HCRYPTHASH hMD5 = NULL;

    if (!CryptCreateHash(g_hProv, CALG_MD5, 0, 0, &hMD5))
        return HRESULT_FROM_WIN32(GetLastError());

    if (hMD5 && !CryptHashData(hMD5, m_pData, (DWORD)m_size, 0))
    {
        CryptDestroyHash(hMD5);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    DWORD cbHash = BYTES_IN_MD5_HASH;
    MD5.SetCount(BYTES_IN_MD5_HASH);

    if (!CryptGetHashParam(hMD5, HP_HASHVAL, MD5.GetData(), &cbHash, 0))
    {
        CryptDestroyHash(hMD5);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (hMD5 != NULL)
        CryptDestroyHash(hMD5);

    return S_OK;
}
