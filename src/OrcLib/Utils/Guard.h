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
class Scope final
{
public:
    explicit Scope(T&& handler)
        : handler_(std::forward<T>(handler))
        , cancelled_(false)
    {
    }
    ~Scope()
    {
        if (cancelled_)
        {
            return;
        }
        try
        {
            handler_();
        }
        catch (const std::system_error& e)
        {
            Log::Warn("Guard::Scope's handler has thrown an exception: {}", e.code());
        }
        catch (const std::exception& e)
        {
            std::cerr << "Guard::Scope's handler has thrown exception: " << e.what() << std::endl;
            Log::Warn("Guard::Scope's handler has thrown exception: {}", e.what());
        }
    }

    Scope(Scope&&) = default;
    Scope(const Scope&) = delete;
    void operator=(const Scope&) = delete;

    void Cancel() { cancelled_ = true; }

private:
    T handler_;
    bool cancelled_;
};

template <typename T>
Scope<T> CreateScopeGuard(T&& handler)
{
    return Scope<T>(std::forward<T>(handler));
}

class FileHandle final
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
    HANDLE operator*() const { return m_handle.get(); }
    operator bool() const { return IsValid(); }
    bool IsValid() const { return m_handle.get() != INVALID_HANDLE_VALUE; }

private:
    std::shared_ptr<void> m_handle;
};

class Handle final
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
            Log::Warn("Failed on CloseHandle [{}]", LastWin32Error());
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
