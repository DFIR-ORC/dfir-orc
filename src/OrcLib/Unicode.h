//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#pragma managed(push, off)

namespace Orc {

struct IsUnicodeValidTable
{
    bool bIsValid;
};

extern const IsUnicodeValidTable xml_element_table[65536];
extern const IsUnicodeValidTable xml_attr_value_table[65536];
extern const IsUnicodeValidTable xml_string_table[65536];
extern const IsUnicodeValidTable xml_comment_table[65536];

bool IsUnicodeValid(const IsUnicodeValidTable table[], WCHAR wCode);

bool IsUnicodeStringValid(const IsUnicodeValidTable table[], LPCWSTR szString, size_t dwLen);

inline bool IsUnicodeStringValid(IsUnicodeValidTable table[], const std::wstring& str)
{
    return IsUnicodeStringValid(table, str.c_str(), str.size());
}

HRESULT ReplaceInvalidChars(
    const IsUnicodeValidTable table[],
    LPCWSTR szString,
    size_t dwLen,
    std::wstring& dst,
    const WCHAR cReplacement = L'_');

inline HRESULT ReplaceInvalidChars(
    const IsUnicodeValidTable table[],
    LPCWSTR szString,
    std::wstring& dst,
    const WCHAR cReplacement = L'_')
{
    return ReplaceInvalidChars(table, szString, wcslen(szString), dst, cReplacement);
}

inline HRESULT ReplaceInvalidChars(
    const IsUnicodeValidTable table[],
    std::wstring_view sv,
    std::wstring& dst,
    const WCHAR cReplacement = L'_')
{
    return ReplaceInvalidChars(table, sv.data(), sv.size(), dst, cReplacement);
}

inline HRESULT ReplaceInvalidChars(
    IsUnicodeValidTable table[],
    const std::wstring& str,
    std::wstring& dst,
    const WCHAR cReplacement = L'_')
{
    return ReplaceInvalidChars(table, str.c_str(), str.size(), dst, cReplacement);
}

HRESULT SanitizeString(const IsUnicodeValidTable table[], LPCWSTR szString, size_t dwLen, std::wstring& dst);

inline HRESULT SanitizeString(const IsUnicodeValidTable table[], LPCWSTR szString, std::wstring& dst)
{
    return SanitizeString(table, szString, wcslen(szString), dst);
}
inline HRESULT SanitizeString(const IsUnicodeValidTable table[], const std::wstring& str, std::wstring& dst)
{
    return SanitizeString(table, str.c_str(), (DWORD)str.size(), dst);
}
inline HRESULT SanitizeString(const IsUnicodeValidTable table[], const std::wstring_view& str, std::wstring& dst)
{
    return SanitizeString(table, str.data(), (DWORD)str.size(), dst);
}

}  // namespace Orc

#pragma managed(pop)
