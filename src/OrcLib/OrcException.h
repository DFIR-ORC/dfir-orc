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

#include "Text/Fmt/optional.h"

#pragma managed(push, off)

namespace Orc {

enum class Severity : short
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
    Exception(Severity status, _In_ HRESULT hr, std::wstring_view fmt, Args&&... args)
        : m_severity(status)
        , m_HRESULT(hr)
    {
        Description = fmt::format(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    Exception(Severity status, std::wstring_view fmt, Args&&... args)
        : m_severity(status)
    {
        Description = fmt::format(fmt, std::forward<Args>(args)...);
    }

    Exception() = default;
    Exception(Severity status)
        : m_severity(status)
    {
    }
    Exception(std::wstring descr);
    Exception(Severity status, _In_ HRESULT hr)
        : m_severity(status)
        , m_HRESULT(hr)
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

    bool IsCritical() const { return m_severity == Severity::Fatal; }

    Severity m_severity = Severity::Unset;
    std::wstring Description;
    HRESULT m_HRESULT = E_FAIL;

    HRESULT PrintMessage() const;

    virtual const char* what() const override;

    virtual ~Exception();

private:
    mutable std::optional<std::string> What;
};

}  // namespace Orc

#pragma managed(pop)
