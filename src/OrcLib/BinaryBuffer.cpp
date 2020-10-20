//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
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
        Fatal, HRESULT_FROM_WIN32(GetLastError()), L"Binary buffer heap could not be created nor did process heap");
}

CBinaryBuffer::CBinaryBuffer(const CBinaryBuffer& other)
    : m_pData(nullptr)
    , m_size(0L)
    , m_bOwnMemory(true)
    , m_bVirtualAlloc(other.m_bVirtualAlloc)
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
                    throw Exception(Fatal, E_OUTOFMEMORY, L"out of memory");

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

// Quick way of checking if a character value is displayable ascii
static bool isAscii[0x100] =
    /*          0     1     2     3        4     5     6     7        8     9     A     B        C     D     E     F
     */
    /* 0x00 */ {
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0x10 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0x20 */ true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        /* 0x30 */ true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        /* 0x40 */ true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        /* 0x50 */ true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        /* 0x60 */ true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        /* 0x70 */ true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        false,
        /* 0x80 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0x90 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0xA0 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0xB0 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0xC0 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0xD0 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0xE0 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0xF0 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false};

inline CHAR GetChar(BYTE aByte)
{
    return isAscii[aByte] ? (CHAR)aByte : '.';
}

HRESULT CBinaryBuffer::PrintHex(LPCWSTR szIndent) const
{
    size_t offset = 0;
    while (offset + 16 < m_size)
    {
        spdlog::info(
            L"%s[%.4u] %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X "
            L"%2.2X\t%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c",
            szIndent,
            offset,
            Get<BYTE>(offset + 0),
            Get<BYTE>(offset + 1),
            Get<BYTE>(offset + 2),
            Get<BYTE>(offset + 3),
            Get<BYTE>(offset + 4),
            Get<BYTE>(offset + 5),
            Get<BYTE>(offset + 6),
            Get<BYTE>(offset + 7),
            Get<BYTE>(offset + 8),
            Get<BYTE>(offset + 9),
            Get<BYTE>(offset + 10),
            Get<BYTE>(offset + 11),
            Get<BYTE>(offset + 12),
            Get<BYTE>(offset + 13),
            Get<BYTE>(offset + 14),
            Get<BYTE>(offset + 15),
            GetChar(Get<BYTE>(offset + 0)),
            GetChar(Get<BYTE>(offset + 1)),
            GetChar(Get<BYTE>(offset + 2)),
            GetChar(Get<BYTE>(offset + 3)),
            GetChar(Get<BYTE>(offset + 4)),
            GetChar(Get<BYTE>(offset + 5)),
            GetChar(Get<BYTE>(offset + 6)),
            GetChar(Get<BYTE>(offset + 7)),
            GetChar(Get<BYTE>(offset + 8)),
            GetChar(Get<BYTE>(offset + 9)),
            GetChar(Get<BYTE>(offset + 10)),
            GetChar(Get<BYTE>(offset + 11)),
            GetChar(Get<BYTE>(offset + 12)),
            GetChar(Get<BYTE>(offset + 13)),
            GetChar(Get<BYTE>(offset + 14)),
            GetChar(Get<BYTE>(offset + 15)));
        offset += 16;
    }
    if (offset < m_size)
    {
        switch (m_size - offset)
        {
            case 1:
                spdlog::info(
                    L"%s[%.4u] %2.2X                                                                                   "
                    L"       \t%c",
                    szIndent,
                    offset,
                    Get<BYTE>(offset + 0),
                    GetChar(Get<BYTE>(offset + 0)));
                break;
            case 2:
                spdlog::info(
                    L"%s[%.4u] %2.2X %2.2X                                                                             "
                    L"       \t%c%c",
                    szIndent,
                    offset,
                    Get<BYTE>(offset + 0),
                    Get<BYTE>(offset + 1),
                    GetChar(Get<BYTE>(offset + 0)),
                    GetChar(Get<BYTE>(offset + 1)));
                break;
            case 3:
                spdlog::info(
                    L"%s[%.4u] %2.2X %2.2X %2.2X                                                                       "
                    L"       \t%c%c%c",
                    szIndent,
                    offset,
                    Get<BYTE>(offset + 0),
                    Get<BYTE>(offset + 1),
                    Get<BYTE>(offset + 2),
                    GetChar(Get<BYTE>(offset + 0)),
                    GetChar(Get<BYTE>(offset + 1)),
                    GetChar(Get<BYTE>(offset + 2)));
                break;
            case 4:
                spdlog::info(
                    L"%s[%.4u] %2.2X %2.2X %2.2X %2.2X                                                                 "
                    L"       \t%c%c%c%c",
                    szIndent,
                    offset,
                    Get<BYTE>(offset + 0),
                    Get<BYTE>(offset + 1),
                    Get<BYTE>(offset + 2),
                    Get<BYTE>(offset + 3),
                    GetChar(Get<BYTE>(offset + 0)),
                    GetChar(Get<BYTE>(offset + 1)),
                    GetChar(Get<BYTE>(offset + 2)),
                    GetChar(Get<BYTE>(offset + 3)));
                break;
            case 5:
                spdlog::info(
                    L"%s[%.4u] %2.2X %2.2X %2.2X %2.2X %2.2X                                                           "
                    L"       \t%c%c%c%c%c",
                    szIndent,
                    offset,
                    Get<BYTE>(offset + 0),
                    Get<BYTE>(offset + 1),
                    Get<BYTE>(offset + 2),
                    Get<BYTE>(offset + 3),
                    Get<BYTE>(offset + 4),
                    GetChar(Get<BYTE>(offset + 0)),
                    GetChar(Get<BYTE>(offset + 1)),
                    GetChar(Get<BYTE>(offset + 2)),
                    GetChar(Get<BYTE>(offset + 3)),
                    GetChar(Get<BYTE>(offset + 4)));
                break;
            case 6:
                spdlog::info(
                    L"%s[%.4u] %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X                                                     "
                    L"       \t%c%c%c%c%c%c",
                    szIndent,
                    offset,
                    Get<BYTE>(offset + 0),
                    Get<BYTE>(offset + 1),
                    Get<BYTE>(offset + 2),
                    Get<BYTE>(offset + 3),
                    Get<BYTE>(offset + 4),
                    Get<BYTE>(offset + 5),
                    GetChar(Get<BYTE>(offset + 0)),
                    GetChar(Get<BYTE>(offset + 1)),
                    GetChar(Get<BYTE>(offset + 2)),
                    GetChar(Get<BYTE>(offset + 3)),
                    GetChar(Get<BYTE>(offset + 4)),
                    GetChar(Get<BYTE>(offset + 5)));
                break;
            case 7:
                spdlog::info(
                    L"%s[%.4u] %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X                                               "
                    L"       \t%c%c%c%c%c%c%c",
                    szIndent,
                    offset,
                    Get<BYTE>(offset + 0),
                    Get<BYTE>(offset + 1),
                    Get<BYTE>(offset + 2),
                    Get<BYTE>(offset + 3),
                    Get<BYTE>(offset + 4),
                    Get<BYTE>(offset + 5),
                    Get<BYTE>(offset + 6),
                    GetChar(Get<BYTE>(offset + 0)),
                    GetChar(Get<BYTE>(offset + 1)),
                    GetChar(Get<BYTE>(offset + 2)),
                    GetChar(Get<BYTE>(offset + 3)),
                    GetChar(Get<BYTE>(offset + 4)),
                    GetChar(Get<BYTE>(offset + 5)),
                    GetChar(Get<BYTE>(offset + 6)));
                break;
            case 8:
                spdlog::info(
                    L"%s[%.4u] %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X                                         "
                    L"       \t%c%c%c%c%c%c%c%c",
                    szIndent,
                    offset,
                    Get<BYTE>(offset + 0),
                    Get<BYTE>(offset + 1),
                    Get<BYTE>(offset + 2),
                    Get<BYTE>(offset + 3),
                    Get<BYTE>(offset + 4),
                    Get<BYTE>(offset + 5),
                    Get<BYTE>(offset + 6),
                    Get<BYTE>(offset + 7),
                    GetChar(Get<BYTE>(offset + 0)),
                    GetChar(Get<BYTE>(offset + 1)),
                    GetChar(Get<BYTE>(offset + 2)),
                    GetChar(Get<BYTE>(offset + 3)),
                    GetChar(Get<BYTE>(offset + 4)),
                    GetChar(Get<BYTE>(offset + 5)),
                    GetChar(Get<BYTE>(offset + 6)),
                    GetChar(Get<BYTE>(offset + 7)));
                break;
            case 9:
                spdlog::info(
                    L"%s[%.4u] %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X                                   "
                    L"       \t%c%c%c%c%c%c%c%c%c",
                    szIndent,
                    offset,
                    Get<BYTE>(offset + 0),
                    Get<BYTE>(offset + 1),
                    Get<BYTE>(offset + 2),
                    Get<BYTE>(offset + 3),
                    Get<BYTE>(offset + 4),
                    Get<BYTE>(offset + 5),
                    Get<BYTE>(offset + 6),
                    Get<BYTE>(offset + 7),
                    Get<BYTE>(offset + 8),
                    GetChar(Get<BYTE>(offset + 0)),
                    GetChar(Get<BYTE>(offset + 1)),
                    GetChar(Get<BYTE>(offset + 2)),
                    GetChar(Get<BYTE>(offset + 3)),
                    GetChar(Get<BYTE>(offset + 4)),
                    GetChar(Get<BYTE>(offset + 5)),
                    GetChar(Get<BYTE>(offset + 6)),
                    GetChar(Get<BYTE>(offset + 7)),
                    GetChar(Get<BYTE>(offset + 8)));
                break;
            case 10:
                spdlog::info(
                    L"%s[%.4u] %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X                             "
                    L"       \t%c%c%c%c%c%c%c%c%c%c",
                    szIndent,
                    offset,
                    Get<BYTE>(offset + 0),
                    Get<BYTE>(offset + 1),
                    Get<BYTE>(offset + 2),
                    Get<BYTE>(offset + 3),
                    Get<BYTE>(offset + 4),
                    Get<BYTE>(offset + 5),
                    Get<BYTE>(offset + 6),
                    Get<BYTE>(offset + 7),
                    Get<BYTE>(offset + 8),
                    Get<BYTE>(offset + 9),
                    GetChar(Get<BYTE>(offset + 0)),
                    GetChar(Get<BYTE>(offset + 1)),
                    GetChar(Get<BYTE>(offset + 2)),
                    GetChar(Get<BYTE>(offset + 3)),
                    GetChar(Get<BYTE>(offset + 4)),
                    GetChar(Get<BYTE>(offset + 5)),
                    GetChar(Get<BYTE>(offset + 6)),
                    GetChar(Get<BYTE>(offset + 7)),
                    GetChar(Get<BYTE>(offset + 8)),
                    GetChar(Get<BYTE>(offset + 9)));
                break;
            case 11:
                spdlog::info(
                    L"%s[%.4u] %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X                       "
                    L"       \t%c%c%c%c%c%c%c%c%c%c%c",
                    szIndent,
                    offset,
                    Get<BYTE>(offset + 0),
                    Get<BYTE>(offset + 1),
                    Get<BYTE>(offset + 2),
                    Get<BYTE>(offset + 3),
                    Get<BYTE>(offset + 4),
                    Get<BYTE>(offset + 5),
                    Get<BYTE>(offset + 6),
                    Get<BYTE>(offset + 7),
                    Get<BYTE>(offset + 8),
                    Get<BYTE>(offset + 9),
                    Get<BYTE>(offset + 10),
                    GetChar(Get<BYTE>(offset + 0)),
                    GetChar(Get<BYTE>(offset + 1)),
                    GetChar(Get<BYTE>(offset + 2)),
                    GetChar(Get<BYTE>(offset + 3)),
                    GetChar(Get<BYTE>(offset + 4)),
                    GetChar(Get<BYTE>(offset + 5)),
                    GetChar(Get<BYTE>(offset + 6)),
                    GetChar(Get<BYTE>(offset + 7)),
                    GetChar(Get<BYTE>(offset + 8)),
                    GetChar(Get<BYTE>(offset + 9)),
                    GetChar(Get<BYTE>(offset + 10)));
                break;
            case 12:
                spdlog::info(
                    L"%s[%.4u] %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X                 "
                    L"       \t%c%c%c%c%c%c%c%c%c%c%c%c",
                    szIndent,
                    offset,
                    Get<BYTE>(offset + 0),
                    Get<BYTE>(offset + 1),
                    Get<BYTE>(offset + 2),
                    Get<BYTE>(offset + 3),
                    Get<BYTE>(offset + 4),
                    Get<BYTE>(offset + 5),
                    Get<BYTE>(offset + 6),
                    Get<BYTE>(offset + 7),
                    Get<BYTE>(offset + 8),
                    Get<BYTE>(offset + 9),
                    Get<BYTE>(offset + 10),
                    Get<BYTE>(offset + 11),
                    GetChar(Get<BYTE>(offset + 0)),
                    GetChar(Get<BYTE>(offset + 1)),
                    GetChar(Get<BYTE>(offset + 2)),
                    GetChar(Get<BYTE>(offset + 3)),
                    GetChar(Get<BYTE>(offset + 4)),
                    GetChar(Get<BYTE>(offset + 5)),
                    GetChar(Get<BYTE>(offset + 6)),
                    GetChar(Get<BYTE>(offset + 7)),
                    GetChar(Get<BYTE>(offset + 8)),
                    GetChar(Get<BYTE>(offset + 9)),
                    GetChar(Get<BYTE>(offset + 10)),
                    GetChar(Get<BYTE>(offset + 11)));
                break;
            case 13:
                spdlog::info(
                    L"%s[%.4u] %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X           "
                    L"       \t%c%c%c%c%c%c%c%c%c%c%c%c%c",
                    szIndent,
                    offset,
                    Get<BYTE>(offset + 0),
                    Get<BYTE>(offset + 1),
                    Get<BYTE>(offset + 2),
                    Get<BYTE>(offset + 3),
                    Get<BYTE>(offset + 4),
                    Get<BYTE>(offset + 5),
                    Get<BYTE>(offset + 6),
                    Get<BYTE>(offset + 7),
                    Get<BYTE>(offset + 8),
                    Get<BYTE>(offset + 9),
                    Get<BYTE>(offset + 10),
                    Get<BYTE>(offset + 11),
                    Get<BYTE>(offset + 12),
                    GetChar(Get<BYTE>(offset + 0)),
                    GetChar(Get<BYTE>(offset + 1)),
                    GetChar(Get<BYTE>(offset + 2)),
                    GetChar(Get<BYTE>(offset + 3)),
                    GetChar(Get<BYTE>(offset + 4)),
                    GetChar(Get<BYTE>(offset + 5)),
                    GetChar(Get<BYTE>(offset + 6)),
                    GetChar(Get<BYTE>(offset + 7)),
                    GetChar(Get<BYTE>(offset + 8)),
                    GetChar(Get<BYTE>(offset + 9)),
                    GetChar(Get<BYTE>(offset + 10)),
                    GetChar(Get<BYTE>(offset + 11)),
                    GetChar(Get<BYTE>(offset + 12)));
                break;
            case 14:
                spdlog::info(
                    L"%s[%.4u] %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X     "
                    L"       \t%c%c%c%c%c%c%c%c%c%c%c%c%c%c",
                    szIndent,
                    offset,
                    Get<BYTE>(offset + 0),
                    Get<BYTE>(offset + 1),
                    Get<BYTE>(offset + 2),
                    Get<BYTE>(offset + 3),
                    Get<BYTE>(offset + 4),
                    Get<BYTE>(offset + 5),
                    Get<BYTE>(offset + 6),
                    Get<BYTE>(offset + 7),
                    Get<BYTE>(offset + 8),
                    Get<BYTE>(offset + 9),
                    Get<BYTE>(offset + 10),
                    Get<BYTE>(offset + 11),
                    Get<BYTE>(offset + 12),
                    Get<BYTE>(offset + 13),
                    GetChar(Get<BYTE>(offset + 0)),
                    GetChar(Get<BYTE>(offset + 1)),
                    GetChar(Get<BYTE>(offset + 2)),
                    GetChar(Get<BYTE>(offset + 3)),
                    GetChar(Get<BYTE>(offset + 4)),
                    GetChar(Get<BYTE>(offset + 5)),
                    GetChar(Get<BYTE>(offset + 6)),
                    GetChar(Get<BYTE>(offset + 7)),
                    GetChar(Get<BYTE>(offset + 8)),
                    GetChar(Get<BYTE>(offset + 9)),
                    GetChar(Get<BYTE>(offset + 10)),
                    GetChar(Get<BYTE>(offset + 11)),
                    GetChar(Get<BYTE>(offset + 12)),
                    GetChar(Get<BYTE>(offset + 13)));
                break;
            case 15:
                spdlog::info(
                    L"%s[%.4u] %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X "
                    L"%2.2X     \t%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c  ",
                    szIndent,
                    offset,
                    Get<BYTE>(offset + 0),
                    Get<BYTE>(offset + 1),
                    Get<BYTE>(offset + 2),
                    Get<BYTE>(offset + 3),
                    Get<BYTE>(offset + 4),
                    Get<BYTE>(offset + 5),
                    Get<BYTE>(offset + 6),
                    Get<BYTE>(offset + 7),
                    Get<BYTE>(offset + 8),
                    Get<BYTE>(offset + 9),
                    Get<BYTE>(offset + 10),
                    Get<BYTE>(offset + 11),
                    Get<BYTE>(offset + 12),
                    Get<BYTE>(offset + 13),
                    Get<BYTE>(offset + 14),
                    GetChar(Get<BYTE>(offset + 0)),
                    GetChar(Get<BYTE>(offset + 1)),
                    GetChar(Get<BYTE>(offset + 2)),
                    GetChar(Get<BYTE>(offset + 3)),
                    GetChar(Get<BYTE>(offset + 4)),
                    GetChar(Get<BYTE>(offset + 5)),
                    GetChar(Get<BYTE>(offset + 6)),
                    GetChar(Get<BYTE>(offset + 7)),
                    GetChar(Get<BYTE>(offset + 8)),
                    GetChar(Get<BYTE>(offset + 9)),
                    GetChar(Get<BYTE>(offset + 10)),
                    GetChar(Get<BYTE>(offset + 11)),
                    GetChar(Get<BYTE>(offset + 12)),
                    GetChar(Get<BYTE>(offset + 13)),
                    GetChar(Get<BYTE>(offset + 14)));
                break;
        }
    }
    return S_OK;
}

void CBinaryBuffer::RemoveAll()
{
    if (m_bOwnMemory)
    {
        if (m_bVirtualAlloc)
        {
            VirtualFree(m_pData, 0L, MEM_RELEASE);
        }
        else
        {
            if (m_pData != nullptr)
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
