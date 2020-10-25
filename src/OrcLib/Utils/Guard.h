//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <utility>

#include <windows.h>

#include "Log/Log.h"

namespace Orc {
namespace Guard {

template <typename T>
class ScopeGuard
{
public:
    explicit ScopeGuard(T&& handler)
        : handler_(std::forward<T>(handler))
        , cancelled_(false)
    {
    }
    ~ScopeGuard()
    {
        if (cancelled_)
        {
            return;
        }
        try
        {
            handler_();
        }
        catch (const std::exception& e)
        {
            std::cerr << e.what() << std::endl;
        }
    }

    ScopeGuard(ScopeGuard&&) = default;
    ScopeGuard(const ScopeGuard&) = delete;
    void operator=(const ScopeGuard&) = delete;

    void Cancel() { cancelled_ = true; }

private:
    T handler_;
    bool cancelled_;
};

template <typename T>
ScopeGuard<T> CreateScopeGuard(T&& handler)
{
    return ScopeGuard<T>(std::forward<T>(handler));
}

class FileHandle
{
public:
    FileHandle(HANDLE handle = INVALID_HANDLE_VALUE)
        : m_handle(handle, Deleter)
    {
    }

    static void Deleter(HANDLE handle)
    {
        if (handle == INVALID_HANDLE_VALUE)
        {
            return;
        }

        if (CloseHandle(handle) == FALSE)
        {
            Log::Warn("Failed on CloseHandle [{}]", LastWin32Error());
        }
    }

    operator ::HANDLE() const { return m_handle.get(); }
    HANDLE get() const { return m_handle.get(); }

private:
    std::shared_ptr<void> m_handle;
};

class Handle
{
public:
    Handle(HANDLE handle = nullptr)
        : m_handle(handle, Deleter)
    {
    }

    static void Deleter(HANDLE handle)
    {
        if (handle == nullptr)
        {
            return;
        }

        if (CloseHandle(handle) == FALSE)
        {
            Log::Warn("Failed on CloseHandle: {}", GetLastError());
        }
    }

    HANDLE get() const { return m_handle.get(); }
    operator bool() const { return m_handle.get() != nullptr; }
    bool operator==(const Handle& o) const { return m_handle == o.m_handle; }

private:
    std::shared_ptr<void> m_handle;
};

}  // namespace Guard
}  // namespace Orc
