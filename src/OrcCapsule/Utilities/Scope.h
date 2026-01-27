//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <windows.h>

#include <memory>

struct ModuleDeleter
{
    void operator()(HMODULE module) const noexcept { FreeLibrary(module); }
};
using ScopedModule = std::unique_ptr<std::remove_pointer<HMODULE>::type, ModuleDeleter>;

struct FileHandleDeleter
{
    void operator()(HANDLE h) const
    {
        if (h && h != INVALID_HANDLE_VALUE)
        {
            CloseHandle(h);
        }
    }
};
using ScopedFileHandle = std::unique_ptr<void, FileHandleDeleter>;

struct LocalFreeDeleter
{
    void operator()(void* ptr) const
    {
        if (ptr)
        {
            LocalFree(ptr);
        }
    }
};
template <typename T>
using LocalPtr = std::unique_ptr<T, LocalFreeDeleter>;

template <typename Func>
class ScopeGuard
{
public:
    explicit ScopeGuard(Func&& f) noexcept
        : m_func(std::forward<Func>(f))
        , m_active(true)
    {
    }

    ~ScopeGuard() noexcept
    {
        if (m_active)
        {
            m_func();
        }
    }

    void Dismiss() noexcept { m_active = false; }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard&&) = delete;
    ScopeGuard& operator=(ScopeGuard&&) = delete;

private:
    Func m_func;
    bool m_active;
};

template <typename Func>
ScopeGuard<Func> MakeScopeGuard(Func&& f)
{
    return ScopeGuard<Func>(std::forward<Func>(f));
}
