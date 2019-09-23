//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <optional>
#include <exception>

#include <strsafe.h>

#pragma managed(push, off)

namespace Orc {

enum ExceptionSeverity : short
{
    Unset,
    Fatal,
    Continue,
    NotImplemented
};

class Exception : public std::exception
{
public:
    template <typename... Args>
    Exception(ExceptionSeverity status, _In_ HRESULT hr, LPCWSTR szFmt, Args&&... args)
        : Severity(status)
        , m_HRESULT(hr)
    {
        WCHAR szMsg[MAX_DESCR];
        size_t remanining = 0;
        if (FAILED(StringCchPrintfExW(
                szMsg, MAX_DESCR, NULL, &remanining, STRSAFE_FILL_ON_FAILURE, szFmt, std::forward<Args>(args)...)))
        {
            Description.assign(szFmt);
        }
        else
        {
            Description.reserve(MAX_DESCR - remanining);
            Description.assign(szMsg);
        }
    }

    template <typename... Args>
    Exception(ExceptionSeverity status, LPCWSTR szFmt, Args&&... args)
        : Severity(status)
    {
        WCHAR szMsg[MAX_DESCR];
        size_t remanining = 0;
        if (FAILED(StringCchPrintfExW(
                szMsg, MAX_DESCR, NULL, &remanining, STRSAFE_FILL_ON_FAILURE, szFmt, std::forward<Args>(args)...)))
        {
            Description.assign(szFmt);
        }
        else
        {
            Description.reserve(MAX_DESCR - remanining);
            Description.assign(szMsg);
        }
    }

    Exception(std::wstring descr);
    Exception(ExceptionSeverity status, std::wstring descr);
    Exception(ExceptionSeverity status, std::wstring descr, std::wstring typeName, std::wstring field);
    Exception(ExceptionSeverity status, _In_ HRESULT hr, std::wstring descr)
        : Description(std::move(descr))
        , m_HRESULT(hr)
    {
    }
    Exception(ExceptionSeverity status, _In_ HRESULT hr)
        : m_HRESULT(hr)
    {
    }

    Exception(Exception&& other) noexcept = default;
    Exception(const Exception& other) = default;
    Exception& operator=(Exception&& other) noexcept = default;
    Exception& operator=(const Exception& other) = default;

    HRESULT GetHRESULT(void) const { return m_HRESULT; }
    HRESULT SetHRESULT(_In_ HRESULT Status)
    {
        m_HRESULT = Status;
        return Status;
    }

    bool IsCritical() const { return Severity == ExceptionSeverity::Fatal; }

    ExceptionSeverity Severity = ExceptionSeverity::Unset;
    std::wstring Description;
    std::optional<std::wstring> TypeName;
    std::optional<std::wstring> FieldName;
    HRESULT m_HRESULT = E_FAIL;

    HRESULT PrintMessage(const Orc::logger& L);

    virtual const char* what() const override;

    virtual ~Exception();

private:
    mutable std::optional<std::string> What;

    static const int MAX_DESCR = 1024;
};

}  // namespace Orc

#pragma managed(pop)
