//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "WideAnsi.h"
#include "BinaryBuffer.h"

#include <boost/io/ios_state.hpp>

#include "Log/Log.h"

using namespace std;

using namespace Orc;

/*

WideToAnsi

Converts a unicode string to ansi

Parameters:
pwszSrc     -   The unicode string to convert
pszDest     -   Pointer to where the ansi string will be stored
cchDest     -   Size in characters of the destination buffer
*/
HRESULT Orc::WideToAnsi(
    __in_ecount(cchSrc) PCWSTR pwszSrc,
    __in DWORD cchSrc,
    __out_ecount(cchDest) PSTR pszDest,
    __in DWORD cchDest)
{
    boost::io::ios_flags_saver fs(std::cerr);
    HRESULT hr = E_FAIL;
    DWORD cchSize = 0L;
    if (pszDest == NULL || cchDest == 0)
        return E_INVALIDARG;

    if (cchSrc == 0LL)
    {
        *pszDest = '0';
        return S_OK;
    }

    if (0 == (cchSize = WideCharToMultiByte(CP_UTF8, 0, pwszSrc, cchSrc, pszDest, cchDest, NULL, NULL))
        || pszDest[0] == 0)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("WideCharToMultiByte failed (code: {:#x})", hr);
        return hr;
    }
    if (cchSize < cchDest)
        pszDest[cchSize] = L'\0';
    return S_OK;
}
/*
WideToAnsi

Converts a unicode string to ansi

Parameters:
pwszSrc     -   The unicode string to convert
pszDest     -   Pointer to where the ansi string will be stored
cchDest     -   Size in characters of the destination buffer
*/
HRESULT
Orc::WideToAnsi(__in PCWSTR pwszSrc, __out_ecount(cchDest) PSTR pszDest, __in DWORD cchDest)
{
    boost::io::ios_flags_saver fs(std::cerr);
    HRESULT hr = E_FAIL;

    if (pszDest == NULL || cchDest == 0)
        return E_INVALIDARG;
    if (pwszSrc == NULL)
        return E_INVALIDARG;
    if (*pwszSrc == L'\0')
    {
        *pszDest = '\0';
        return S_OK;
    }
    if (0 == WideCharToMultiByte(CP_UTF8, 0, pwszSrc, -1, pszDest, cchDest, NULL, NULL) || pszDest[0] == 0)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("WideCharToMultiByte failed (code: {:#x})", hr);
        return hr;
    }

    return S_OK;
}

HRESULT Orc::WideToAnsi(__in const std::wstring& src, std::string& dest)
{
    boost::io::ios_flags_saver fs(std::cerr);
    if (src.empty())
    {
        dest.clear();
        return S_OK;
    }

    int cbNeeded = 0;
    if (0
        == (cbNeeded =
                WideCharToMultiByte(CP_UTF8, 0, src.c_str(), static_cast<int>(src.size()), NULL, 0L, NULL, NULL)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("WideCharToMultiByte failed (code: {:#x})", hr);
        return hr;
    }

    std::vector<CHAR> buffer;

    buffer.reserve(cbNeeded);
    buffer.resize(cbNeeded);
    if (0
        == (cbNeeded = WideCharToMultiByte(
                CP_UTF8,
                0,
                src.c_str(),
                static_cast<int>(src.size()),
                buffer.data(),
                static_cast<int>(buffer.size()),
                NULL,
                NULL)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("WideCharToMultiByte failed (code: {:#x})", hr);
        return hr;
    }

    dest.assign(buffer.data(), buffer.size());
    return S_OK;
}

HRESULT Orc::WideToAnsi(__in const std::wstring_view& src, std::string& dest)
{
    boost::io::ios_flags_saver fs(std::cerr);
    if (src.empty())
    {
        dest.clear();
        return S_OK;
    }

    int cbNeeded = 0;
    if (0
        == (cbNeeded = WideCharToMultiByte(CP_UTF8, 0, src.data(), static_cast<int>(src.size()), NULL, 0L, NULL, NULL)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("WideCharToMultiByte failed (code: {:#x})", hr);
        return hr;
    }

    std::vector<CHAR> buffer;

    buffer.reserve(cbNeeded);
    buffer.resize(cbNeeded);
    if (0
        == (cbNeeded = WideCharToMultiByte(
                CP_UTF8,
                0,
                src.data(),
                static_cast<int>(src.size()),
                buffer.data(),
                static_cast<int>(buffer.size()),
                NULL,
                NULL)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("WideCharToMultiByte failed (code: {:#x})", hr);
        return hr;
    }

    dest.assign(buffer.data(), buffer.size());
    return S_OK;
}

HRESULT
Orc::WideToAnsi(__in_ecount(cchSrc) PCWSTR pwszSrc, __in DWORD cchSrc, CBinaryBuffer& dest)
{
    boost::io::ios_flags_saver fs(std::cerr);
    int cbNeeded = 0;

    if (cchSrc == 0)
    {
        dest.RemoveAll();
        return S_OK;
    }

    if (0 == (cbNeeded = WideCharToMultiByte(CP_UTF8, 0, pwszSrc, cchSrc, NULL, 0L, NULL, NULL)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("WideCharToMultiByte failed (code: {:#x})", hr);
        return hr;
    }

    dest.SetCount(cbNeeded);
    if (0
        == (cbNeeded = WideCharToMultiByte(
                CP_UTF8,
                0,
                pwszSrc,
                cchSrc,
                static_cast<LPSTR>((CHAR*)dest.GetData()),
                static_cast<int>(dest.GetCount()),
                NULL,
                NULL)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("WideCharToMultiByte failed (code: {:#x})", hr);
        return hr;
    }

    return S_OK;
}

HRESULT Orc::WideToAnsi(PCWSTR pwszSrc, CBinaryBuffer& dest)
{
    if (pwszSrc == nullptr)
        return E_INVALIDARG;
    if (*pwszSrc == L'\0')
    {
        dest.RemoveAll();
        return S_OK;
    }

    boost::io::ios_flags_saver fs(std::cerr);
    int cbNeeded = 0;
    if (0 == (cbNeeded = WideCharToMultiByte(CP_UTF8, 0, pwszSrc, -1, NULL, 0L, NULL, NULL)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("WideCharToMultiByte failed (code: {:#x})", hr);
        return hr;
    }

    dest.SetCount(cbNeeded);
    if (0
        == (cbNeeded = WideCharToMultiByte(
                CP_UTF8,
                0,
                pwszSrc,
                -1,
                static_cast<LPSTR>((CHAR*)dest.GetData()),
                static_cast<int>(dest.GetCount()),
                NULL,
                NULL)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("WideCharToMultiByte failed (code: {:#x})", hr);
        return hr;
    }

    return S_OK;
}

HRESULT ORCLIB_API Orc::WideToAnsi(PCWSTR pszSrc, std::string& dest)
{
    if (pszSrc == nullptr)
        return E_INVALIDARG;
    if (*pszSrc == L'\0')
    {
        dest.clear();
        return S_OK;
    }

    boost::io::ios_flags_saver fs(std::cerr);
    int cbNeeded = 0;
    if (0 == (cbNeeded = WideCharToMultiByte(CP_UTF8, 0, pszSrc, -1, NULL, 0L, NULL, NULL)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("WideCharToMultiByte failed (code: {:#x})", hr);
        return hr;
    }

    CBinaryBuffer buffer;
    buffer.SetCount(cbNeeded);
    if (0
        == (cbNeeded = WideCharToMultiByte(
                CP_UTF8,
                0,
                pszSrc,
                -1,
                static_cast<LPSTR>((CHAR*)buffer.GetData()),
                static_cast<int>(buffer.GetCount()),
                NULL,
                NULL)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("WideCharToMultiByte failed (code: {:#x})", hr);
        return hr;
    }

    dest.assign(buffer.GetP<CHAR>(), static_cast<int>(buffer.GetCount() - 1));
    return S_OK;
}

std::pair<HRESULT, std::string> ORCLIB_API Orc::WideToAnsi(PCWSTR pwszSrc)
{
    HRESULT hr = E_FAIL;
    std::string retval;

    if (FAILED(hr = WideToAnsi(pwszSrc, retval)))
        return {hr, std::string()};
    else
        return {S_OK, std::move(retval)};
}

std::pair<HRESULT, std::string> ORCLIB_API Orc::WideToAnsi(const std::wstring& strSrc)
{
    HRESULT hr = E_FAIL;
    std::string retval;

    if (FAILED(hr = WideToAnsi(strSrc, retval)))
        return {hr, std::string()};
    else
        return {S_OK, std::move(retval)};
}

std::pair<HRESULT, std::string> ORCLIB_API Orc::WideToAnsi(const std::wstring_view& strSrc)
{
    HRESULT hr = E_FAIL;
    std::string retval;

    if (FAILED(hr = WideToAnsi(strSrc, retval)))
        return {hr, std::string()};
    else
        return {S_OK, std::move(retval)};
}

HRESULT
Orc::AnsiToWide(__in PCSTR pszSrc, __out_ecount(cchDest) PWSTR pwzDest, __in DWORD cchDest)
{
    boost::io::ios_flags_saver fs(std::cerr);
    HRESULT hr = E_FAIL;
    DWORD cchSize = 0;

    if (pwzDest == NULL || cchDest == 0)
        return E_INVALIDARG;

    if (pszSrc == nullptr)
    {
        *pwzDest = L'\0';
        return S_OK;
    }
    else if (*pszSrc == L'\0')
    {
        *pwzDest = L'\0';
        return S_OK;
    }
    if (0 == (cchSize = MultiByteToWideChar(CP_UTF8, 0, pszSrc, -1, pwzDest, cchDest)) || pwzDest[0] == 0)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("MultiByteToWideChar failed (code: {:#x})", hr);
        return hr;
    }

    return S_OK;
}

HRESULT Orc::AnsiToWide(
    __in_ecount(cchSrc) PCSTR pszSrc,
    __in DWORD cchSrc,
    __out_ecount(cchDest) PWSTR pwzDest,
    __in DWORD cchDest)
{
    DBG_UNREFERENCED_PARAMETER(cchSrc);
    boost::io::ios_flags_saver fs(std::cerr);
    HRESULT hr = E_FAIL;
    DWORD cchSize = 0;

    if (pwzDest == NULL || cchDest == 0)
        return E_INVALIDARG;

    if (*pszSrc == '\0')
    {
        *pwzDest = L'\0';
        return S_OK;
    }

    if (0 == (cchSize = MultiByteToWideChar(CP_UTF8, 0, pszSrc, -1, pwzDest, cchDest)) || pwzDest[0] == 0)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("MultiByteToWideChar failed (code: {:#x})", hr);
        return hr;
    }

    if (cchSize < cchDest)
        pwzDest[cchSize] = L'\0';
    return S_OK;
}

HRESULT Orc::AnsiToWide(__in const std::string& src, std::wstring& dest)
{
    boost::io::ios_flags_saver fs(std::cerr);
    HRESULT hr = E_FAIL;
    DWORD cchSize = 0;
    if (src.empty())
    {
        dest.clear();
        return S_OK;
    };

    if (0 == (cchSize = MultiByteToWideChar(CP_UTF8, 0, src.c_str(), static_cast<int>(src.size()), NULL, 0)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("MultiByteToWideChar failed (code: {:#x})", hr);
        return hr;
    }

    std::vector<WCHAR> buffer;

    buffer.resize(cchSize);
    buffer.resize(cchSize);

    if (0
        == (cchSize = MultiByteToWideChar(
                CP_UTF8, 0, src.c_str(), static_cast<int>(src.size()), buffer.data(), static_cast<int>(buffer.size()))))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("MultiByteToWideChar failed (code: {:#x})", hr);
        return hr;
    }

    if (buffer.size() > 1)
    {
        if (buffer[buffer.size() - 1] == 0)
            dest.assign(buffer.data(), buffer.size() - 1);  // we need to remove the trailing \0
        else
            dest.assign(buffer.data(), buffer.size());
    }
    else
        dest.clear();
    return S_OK;
}

HRESULT Orc::AnsiToWide(__in const std::string_view& src, std::wstring& dest)
{
    boost::io::ios_flags_saver fs(std::cerr);
    HRESULT hr = E_FAIL;
    DWORD cchSize = 0;
    if (src.empty())
    {
        dest.clear();
        return S_OK;
    };

    if (0 == (cchSize = MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.size()), NULL, 0)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"MultiByteToWideChar failed (code: {:#x})", hr);
        return hr;
    }

    std::vector<WCHAR> buffer;

    buffer.resize(cchSize);
    buffer.resize(cchSize);

    if (0
        == (cchSize = MultiByteToWideChar(
                CP_UTF8, 0, src.data(), static_cast<int>(src.size()), buffer.data(), static_cast<int>(buffer.size()))))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"MultiByteToWideChar failed (code: {:#x})", hr);
        return hr;
    }

    if (buffer.size() > 1)
    {
        if (buffer[buffer.size() - 1] == 0)
            dest.assign(buffer.data(), buffer.size() - 1);  // we need to remove the trailing \0
        else
            dest.assign(buffer.data(), buffer.size());
    }
    else
        dest.clear();
    return S_OK;
}

HRESULT
Orc::AnsiToWide(__in_ecount(cchSrc) PCSTR pszSrc, __in DWORD cchSrc, CBinaryBuffer& dest)
{
    if (pszSrc == nullptr)
        return E_INVALIDARG;

    if (cchSrc == 0 || *pszSrc == '\0')
    {
        dest.RemoveAll();
        return S_OK;
    }

    boost::io::ios_flags_saver fs(std::cerr);
    HRESULT hr = E_FAIL;
    DWORD cchSize = 0;

    if (0 == (cchSize = MultiByteToWideChar(CP_UTF8, 0, pszSrc, cchSrc, NULL, 0)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("MultiByteToWideChar failed (code: {:#x})", hr);
        return hr;
    }

    dest.SetCount(msl::utilities::SafeInt<DWORD>(cchSize) * sizeof(WCHAR));

    if (0
        == (cchSize = MultiByteToWideChar(
                CP_UTF8,
                0,
                pszSrc,
                cchSrc,
                static_cast<LPWSTR>((WCHAR*)dest.GetData()),
                static_cast<int>(dest.GetCount() / sizeof(WCHAR)))))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("MultiByteToWideChar failed (code: {:#x})", hr);
        dest.RemoveAll();
        return hr;
    }

    return S_OK;
}

HRESULT ORCLIB_API Orc::AnsiToWide(LPCSTR pszSrc, CBinaryBuffer& dest)
{
    boost::io::ios_flags_saver fs(std::cerr);
    HRESULT hr = E_FAIL;
    DWORD cchSize = 0;

    if (pszSrc == nullptr)
        return E_INVALIDARG;

    if (pszSrc == '\0')
    {
        dest.RemoveAll();
        return S_OK;
    }

    if (0 == (cchSize = MultiByteToWideChar(CP_UTF8, 0, pszSrc, -1, NULL, 0)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("MultiByteToWideChar failed (code: {:#x})", hr);
        return hr;
    }

    dest.SetCount(msl::utilities::SafeInt(cchSize) * sizeof(WCHAR));

    if (0
        == (cchSize = MultiByteToWideChar(
                CP_UTF8,
                0,
                pszSrc,
                -1,
                static_cast<LPWSTR>((WCHAR*)dest.GetData()),
                static_cast<int>(dest.GetCount() / sizeof(WCHAR)))))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("MultiByteToWideChar failed (code: {:#x})", hr);
        dest.RemoveAll();
        return hr;
    }

    return S_OK;
}

HRESULT ORCLIB_API Orc::AnsiToWide(PCSTR pszSrc, DWORD cchSrc, std::wstring& dest)
{
    HRESULT hr = E_FAIL;

    if (cchSrc == 0)
    {
        dest.clear();
        return S_OK;
    }

    CBinaryBuffer buffer;

    if (FAILED(hr = AnsiToWide(pszSrc, cchSrc, buffer)))
        return hr;

    if (buffer.GetCount() > sizeof(WCHAR))
    {
        dest.assign(buffer.GetP<WCHAR>(), (buffer.GetCount() / sizeof(WCHAR)) - 1);
    }
    else
        dest.clear();
    return S_OK;
}

HRESULT ORCLIB_API Orc::AnsiToWide(PCSTR pszSrc, std::wstring& dest)
{
    HRESULT hr = E_FAIL;
    CBinaryBuffer buffer;

    if (FAILED(hr = AnsiToWide(pszSrc, buffer)))
        return hr;

    if (buffer.GetCount() > sizeof(WCHAR))
        dest.assign(buffer.GetP<WCHAR>(), (buffer.GetCount() / sizeof(WCHAR)) - 1);
    else
        dest.clear();
    return S_OK;
}

std::pair<HRESULT, std::wstring> ORCLIB_API Orc::AnsiToWide(PCSTR pszSrc)
{
    HRESULT hr = E_FAIL;
    std::wstring retval;

    if (FAILED(hr = AnsiToWide(pszSrc, retval)))
        return std::make_pair(hr, std::wstring());
    else
        return std::make_pair(S_OK, std::move(retval));
}

std::pair<HRESULT, std::wstring> ORCLIB_API Orc::AnsiToWide(const std::string& pszSrc)
{
    HRESULT hr = E_FAIL;
    std::wstring retval;

    if (FAILED(hr = AnsiToWide(pszSrc, retval)))
        return std::make_pair(hr, std::wstring());
    else
        return std::make_pair(S_OK, std::move(retval));
}

std::pair<HRESULT, std::wstring> ORCLIB_API Orc::AnsiToWide(const std::string_view& pszSrc)
{
    HRESULT hr = E_FAIL;
    std::wstring retval;

    if (FAILED(hr = AnsiToWide(pszSrc, retval)))
        return {hr, std::wstring()};
    else
        return {S_OK, std::move(retval)};
}
