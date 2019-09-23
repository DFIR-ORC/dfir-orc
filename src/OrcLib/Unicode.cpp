//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "Unicode.h"

#include <sstream>

using namespace std;

using namespace Orc;

bool Orc::IsUnicodeValid(const IsUnicodeValidTable table[], WCHAR wCode)
{
    return table[wCode].bIsValid;
}

bool Orc::IsUnicodeStringValid(const IsUnicodeValidTable table[], LPCWSTR szString, size_t dwLen)
{
    const WCHAR* pCur = szString;
    size_t CurIndex = 0L;
    while (pCur && CurIndex < dwLen)
    {
        if (!table[*pCur].bIsValid)
            return false;
        pCur++;
        CurIndex++;
    }
    return true;
}

HRESULT Orc::ReplaceInvalidChars(
    const IsUnicodeValidTable table[],
    LPCWSTR szString,
    size_t dwLen,
    std::wstring& dst,
    const WCHAR cReplacement)
{
    const WCHAR* pCur = szString;
    size_t CurIndex = 0L;

    wstringstream stream(ios_base::out);

    stream << hex << uppercase;

    while (pCur && CurIndex < dwLen)
    {
        if (!table[*pCur].bIsValid)
        {
            stream << cReplacement;
        }
        else
        {
            stream << *pCur;
        }
        pCur++;
        CurIndex++;
    }

    dst = std::move(stream.str());
    return S_OK;
}

HRESULT Orc::SanitizeString(const IsUnicodeValidTable table[], LPCWSTR szString, size_t dwLen, std::wstring& dst)
{
    const WCHAR* pCur = szString;
    size_t CurIndex = 0L;

    wstringstream stream(ios_base::out);

    stream << hex << uppercase;

    while (pCur && CurIndex < dwLen)
    {
        if (!table[*pCur].bIsValid)
        {
            stream << L"#x";
            stream.width(4);
            stream.fill(L'0');
            stream << (unsigned short)*pCur << ";";
        }
        else
        {
            stream << *pCur;
        }
        pCur++;
        CurIndex++;
    }

    dst = std::move(stream.str());
    return S_OK;
}
