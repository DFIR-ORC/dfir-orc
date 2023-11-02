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

/*!
 *  \brief Base class to provides RAII cleanup features to a raw pointer.
 *
 *  Inherit this class and define your custom destructor managing m_data.
 *
 *  BEWARE: Pointer is not free with default implementation.
 */
template <typename T>
class PointerGuard
{
public:
    PointerGuard(T* p = nullptr)
        : m_data(p)
    {
    }

    PointerGuard& operator=(const PointerGuard&) = delete;
    PointerGuard(const PointerGuard&) = delete;

    PointerGuard(PointerGuard&& o)
    {
        m_data = o.m_data;
        o.m_data = nullptr;
    }

    virtual ~PointerGuard() { m_data = nullptr; }

    const T* get() const { return m_data; }
    T* get() { return m_data; }

    const T& operator*() const { return *m_data; }
    T& operator*() { return *m_data; }

    T** data() { return &m_data; }
    const T** data() const { return &m_data; }

    explicit operator bool() const { return m_data != nullptr; }
    bool operator==(const T& value) const { return m_data == value; }
    bool operator==(const PointerGuard<T>& o) const { return m_data == o.m_pointer; }

protected:
    T* m_data;
};

/*!
 *  \brief Base class to provide RAII cleanup features to a descriptor or handle.
 *
 *  Inherit this class and define your custom destructor managing m_data.
 *
 */
template <typename T>
class DescriptorGuard
{
public:
    DescriptorGuard(T data, T invalidValue)
        : m_invalidValue(invalidValue)
        , m_data(data)
    {
    }

    DescriptorGuard(const DescriptorGuard&) = delete;
    DescriptorGuard& operator=(const DescriptorGuard&) = delete;

    DescriptorGuard(DescriptorGuard&& o) noexcept
        : m_invalidValue(std::move(o.m_invalidValue))
        , m_data(std::move(o.m_data))
    {
        o.m_data = m_invalidValue;
    }

    virtual ~DescriptorGuard() { m_data = m_invalidValue; }

    DescriptorGuard& operator=(DescriptorGuard&& o) noexcept
    {
        if (this != &o)
        {
            m_data = std::move(o.m_data);
            m_invalidValue = o.m_invalidValue;
            o.m_data = m_invalidValue;
        }

        return *this;
    }

    const T& value() const { return m_data; }
    T& value() { return m_data; }

    const T operator*() const { return m_data; }

    T* data() { return &m_data; }
    const T* data() const { return &m_data; }

    bool IsValid() const { return m_data != m_invalidValue; }

    explicit operator bool() const { return m_data != m_invalidValue; }
    bool operator==(const T& value) const { return m_data == value; }
    bool operator==(const DescriptorGuard<T>& o) const { return m_data == o.m_data; }

protected:
    T m_invalidValue;
    T m_data;
};

class FileHandle final : public DescriptorGuard<HANDLE>
{
public:
    FileHandle(HANDLE handle = INVALID_HANDLE_VALUE)
        : DescriptorGuard(handle, INVALID_HANDLE_VALUE)
    {
    }

    FileHandle(FileHandle&& handle) noexcept = default;
    FileHandle& operator=(FileHandle&& o) = default;

    ~FileHandle()
    {
        if (m_data == m_invalidValue)
        {
            return;
        }

        if (CloseHandle(m_data) == FALSE)
        {
            Log::Warn("Failed CloseHandle [{}]", LastWin32Error());
            return;
        }
    }
};

class ServiceHandle final : public DescriptorGuard<SC_HANDLE>
{
public:
    ServiceHandle(SC_HANDLE handle = NULL)
        : DescriptorGuard(handle, NULL)
    {
    }

    ServiceHandle(ServiceHandle&& handle) noexcept = default;
    ServiceHandle& operator=(ServiceHandle&& o) = default;

    ~ServiceHandle()
    {
        if (m_data == m_invalidValue)
        {
            return;
        }

        if (CloseServiceHandle(m_data) == FALSE)
        {
            Log::Warn("Failed CloseServiceHandle [{}]", LastWin32Error());
            return;
        }
    }
};

class Handle final : public DescriptorGuard<HANDLE>
{
public:
    Handle(HANDLE data = NULL)
        : DescriptorGuard(data, NULL)
    {
    }

    Handle(Handle&& o) noexcept = default;
    Handle& operator=(Handle&& o) = default;

    ~Handle()
    {
        if (m_data == m_invalidValue)
        {
            return;
        }

        if (::CloseHandle(m_data) == FALSE)
        {
            Log::Warn("Failed on CloseHandle [{}]", LastWin32Error());
            return;
        }
    }
};

class Module final : public DescriptorGuard<HMODULE>
{
public:
    Module(HMODULE module = NULL)
        : DescriptorGuard(module, NULL)
    {
    }

    Module(Module&& o) noexcept = default;
    Module& operator=(Module&& o) = default;

    ~Module()
    {
        if (m_data == m_invalidValue)
        {
            return;
        }

        if (::FreeLibrary(m_data) == FALSE)
        {
            Log::Warn("Failed FreeLibrary [{}]", LastWin32Error());
        }
    }
};

}  // namespace Guard
}  // namespace Orc
