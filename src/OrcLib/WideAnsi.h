//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "Buffer.h"

#include <safeint.h>
#include <iostream>

#include "Log/Log.h"
#include "Utils/Result.h"

#pragma managed(push, off)

namespace Orc {

class CBinaryBuffer;

HRESULT WideToAnsi(
    __in_ecount(cchSrc) PCWSTR pwszSrc,
    __in DWORD cchSrc,
    __out_ecount(cchDest) PSTR pszDest,
    __in DWORD cchDest);
HRESULT WideToAnsi(__in PCWSTR pwszSrc, __out_ecount(cchDest) PSTR pszDest, __in DWORD cchDest);
HRESULT WideToAnsi(__in const std::wstring& src, std::string& dest);
HRESULT WideToAnsi(__in const std::wstring_view& src, std::string& dest);
HRESULT WideToAnsi(__in_ecount(cchSrc) PCWSTR pwszSrc, __in DWORD cchSrc, CBinaryBuffer& dest);
HRESULT WideToAnsi(__in PCWSTR pwszSrc, CBinaryBuffer& dest);
HRESULT WideToAnsi(__in PCWSTR pszSrc, std::string& dest);

template <size_t _DeclElts>
HRESULT WideToAnsi(__in const std::wstring_view src, Buffer<CHAR, _DeclElts>& dest)
{

    using namespace std;

    int cbNeeded = 0;
    if (0
        == (cbNeeded = WideCharToMultiByte(CP_UTF8, 0, src.data(), static_cast<int>(src.size()), NULL, 0L, NULL, NULL)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hr))
        {
            Log::Error(L"Failed WideCharToMultiByte [{}]", SystemError(hr));
        }
        else
        {
            std::cerr << "WideCharToMultiByte failed:" << hex << SystemError(hr) << endl;
        }
        return hr;
    }

    dest.resize(cbNeeded);
    if (0
        == (cbNeeded = WideCharToMultiByte(
                CP_UTF8,
                0,
                src.data(),
                static_cast<int>(src.size()),
                static_cast<LPSTR>(dest.get()),
                static_cast<int>(dest.size()),
                NULL,
                NULL)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hr))
        {
            Log::Error(L"Failed WideCharToMultiByte [{}]", SystemError(hr));
        }
        else
        {
            std::cerr << "WideCharToMultiByte failed:" << hex << SystemError(hr) << endl;
        }
        dest.clear();
        return hr;
    }

    return S_OK;
}

template <size_t _DeclElts>
HRESULT WideToAnsi(__in_ecount(cchSrc) PCWSTR pwszSrc, __in DWORD cchSrc, Buffer<CHAR, _DeclElts>& dest)
{

    using namespace std;

    int cbNeeded = 0;
    if (0 == (cbNeeded = WideCharToMultiByte(CP_UTF8, 0, pwszSrc, static_cast<int>(cchSrc), NULL, 0L, NULL, NULL)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hr))
        {
            Log::Error(L"Failed WideCharToMultiByte [{}]", SystemError(hr));
        }
        else
        {
            cerr << "WideCharToMultiByte failed:" << hex << SystemError(hr) << endl;
        }
        return hr;
    }

    dest.resize(cbNeeded);
    if (0
        == (cbNeeded = WideCharToMultiByte(
                CP_UTF8,
                0,
                pwszSrc,
                static_cast<int>(cchSrc),
                static_cast<LPSTR>(dest.get()),
                static_cast<int>(dest.size()),
                NULL,
                NULL)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hr))
        {
            Log::Error(L"Failed WideCharToMultiByte [{}]", SystemError(hr));
        }
        else
        {
            cerr << "WideCharToMultiByte failed:" << hex << SystemError(hr) << endl;
        }
        dest.clear();
        return hr;
    }

    return S_OK;
}

template <size_t _DeclElts>
HRESULT WideToAnsi(PCWSTR pwszSrc, Buffer<CHAR, _DeclElts>& dest)
{

    using namespace std;

    int cbNeeded = 0;
    if (0 == (cbNeeded = WideCharToMultiByte(CP_UTF8, 0, pwszSrc, -1, NULL, 0L, NULL, NULL)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hr))
        {
            Log::Error(L"Failed WideCharToMultiByte [{}]", SystemError(hr));
        }
        else
        {
            cerr << "WideCharToMultiByte failed:" << hex << SystemError(hr) << endl;
        }
        return hr;
    }

    dest.resize(cbNeeded);
    if (0
        == (cbNeeded = WideCharToMultiByte(
                CP_UTF8, 0, pwszSrc, -1, static_cast<LPSTR>(dest.get()), static_cast<int>(dest.size()), NULL, NULL)))
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hr))
        {
            Log::Error(L"Failed WideCharToMultiByte [{}]", SystemError(hr));
        }
        else
        {
            cerr << "WideCharToMultiByte failed:" << hex << SystemError(hr) << endl;
        }
        dest.clear();
        return hr;
    }

    return S_OK;
}

std::pair<HRESULT, std::string> WideToAnsi(__in PCWSTR pwszSrc);
std::pair<HRESULT, std::string> WideToAnsi(__in const std::wstring& strSrc);
std::pair<HRESULT, std::string> WideToAnsi(__in const std::wstring_view& strSrc);

HRESULT AnsiToWide(
    __in_ecount(cchSrc) PCSTR pszSrc,
    __in DWORD cchSrc,
    __out_ecount(cchDest) PWSTR pwzDest,
    __in DWORD cchDest);
HRESULT AnsiToWide(__in PCSTR pszSrc, __out_ecount(cchDest) PWSTR pwzDest, __in DWORD cchDest);
HRESULT AnsiToWide(__in const std::string& src, std::wstring& dest);
HRESULT AnsiToWide(__in const std::string_view& src, std::wstring& dest);
HRESULT AnsiToWide(__in_ecount(cchSrc) PCSTR pszSrc, __in DWORD cchSrc, CBinaryBuffer& dest);
HRESULT AnsiToWide(__in PCSTR pszSrc, CBinaryBuffer& dest);

HRESULT AnsiToWide(__in PCSTR pszSrc, __in DWORD cchSrc, std::wstring& dest);
HRESULT AnsiToWide(__in PCSTR pszSrc, std::wstring& dest);

template <size_t _DeclElts>
HRESULT AnsiToWide(__in const std::string_view src, Buffer<WCHAR, _DeclElts>& dest)
{
    HRESULT hr = E_FAIL;
    DWORD cchSize = 0;

    if (0 == (cchSize = MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.size()), NULL, 0)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hr))
        {
            Log::Error(L"Failed MultiByteToWideChar [{}]", SystemError(hr));
        }
        else
        {
            std::cerr << "MultiByteToWideChar failed:" << std::hex << SystemError(hr) << std::endl;
        }
        return hr;
    }

    dest.resize(msl::utilities::SafeInt<DWORD>(cchSize) * sizeof(WCHAR));

    if (0
        == (cchSize =
                MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.size()), dest.get(), dest.size())))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hr))
        {
            Log::Error(L"Failed MultiByteToWideChar [{}]", SystemError(hr));
        }
        else
        {
            std::cerr << "MultiByteToWideChar failed:" << std::hex << SystemError(hr) << std::endl;
        }
        dest.clear();
        return hr;
    }

    return S_OK;
}

template <size_t _DeclElts>
HRESULT AnsiToWide(__in_ecount(cchSrc) PCSTR pwszSrc, __in DWORD cchSrc, Buffer<WCHAR, _DeclElts>& dest)
{

    HRESULT hr = E_FAIL;
    DWORD cchSize = 0;

    if (0 == (cchSize = MultiByteToWideChar(CP_UTF8, 0, pwszSrc, static_cast<int>(cchSrc), NULL, 0)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hr))
        {
            Log::Error(L"Failed MultiByteToWideChar [{}]", SystemError(hr));
        }
        else
        {
            std::cerr << "MultiByteToWideChar failed:" << std::hex << SystemError(hr) << std::endl;
        }
        return hr;
    }

    dest.resize(msl::utilities::SafeInt<DWORD>(cchSize * sizeof(WCHAR)));

    if (0 == (cchSize = MultiByteToWideChar(CP_UTF8, 0, pwszSrc, static_cast<int>(cchSrc), dest.get(), dest.size())))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hr))
        {
            Log::Error(L"Failed MultiByteToWideChar [{}]", SystemError(hr));
        }
        else
        {
            std::cerr << "MultiByteToWideChar failed:" << std::hex << SystemError(hr) << std::endl;
        }
        dest.clear();
        return hr;
    }

    return S_OK;
}

template <size_t _DeclElts>
HRESULT AnsiToWide(PCSTR pwszSrc, Buffer<WCHAR, _DeclElts>& dest)
{
    HRESULT hr = E_FAIL;
    DWORD cchSize = 0;

    if (0 == (cchSize = MultiByteToWideChar(CP_UTF8, 0, pwszSrc, -1, NULL, 0)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hr))
        {
            Log::Error(L"Failed MultiByteToWideChar [{}]", SystemError(hr));
        }
        else
        {
            std::cerr << "MultiByteToWideChar failed:" << std::hex << SystemError(hr) << std::endl;
        }
        return hr;
    }

    dest.resize(msl::utilities::SafeInt<DWORD>(cchSize) * sizeof(WCHAR));

    if (0 == (cchSize = MultiByteToWideChar(CP_UTF8, 0, pwszSrc, -1, dest.get(), dest.size())))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hr))
        {
            Log::Error(L"Failed MultiByteToWideChar [{}]", SystemError(hr));
        }
        else
        {
            std::cerr << "MultiByteToWideChar failed:" << std::hex << SystemError(hr) << std::endl;
        }
        dest.clear();
        return hr;
    }

    return S_OK;
}

std::pair<HRESULT, std::wstring> AnsiToWide(__in PCSTR pwszSrc);
std::pair<HRESULT, std::wstring> AnsiToWide(__in const std::string& strSrc);
std::pair<HRESULT, std::wstring> AnsiToWide(__in const std::string_view& strSrc);

}  // namespace Orc

#pragma managed(pop)
