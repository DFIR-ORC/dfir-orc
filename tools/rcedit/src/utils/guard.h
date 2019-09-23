//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <utility>

#include <windows.h>

#include <spdlog/spdlog.h>

namespace rcedit {

namespace guard {

class MapView
{
public:
    MapView( LPVOID view = nullptr )
        : m_view( view, Deleter )
    {
    }

    static void Deleter( LPVOID view )
    {
        spdlog::debug( "{} {}", __FUNCTION__, view );

        if( view == nullptr ) {
            return;
        }

        if( UnmapViewOfFile( view ) == FALSE ) {
            spdlog::warn( "Failed on UnmapViewOfFile: {}", GetLastError() );
        }
    }

    HANDLE get() const { return m_view.get(); }
    operator bool() const { return m_view.get() != nullptr; }
    bool operator==( const MapView& o ) const { return m_view == o.m_view; }

private:
    std::shared_ptr< void > m_view;
};

class FileHandle
{
public:
    FileHandle( HANDLE handle = INVALID_HANDLE_VALUE )
        : m_handle( handle, Deleter )
    {
    }

    static void Deleter( HANDLE handle )
    {
        spdlog::debug( "{} {}", __FUNCTION__, handle );

        if( handle == INVALID_HANDLE_VALUE ) {
            return;
        }

        if( CloseHandle( handle ) == FALSE ) {
            spdlog::warn( "Failed on CloseHandle: {}", GetLastError() );
        }
    }

    HANDLE get() const { return m_handle.get(); }
    operator bool() const { return m_handle.get() != INVALID_HANDLE_VALUE; }
    bool operator==( const FileHandle& o ) const
    {
        return m_handle == o.m_handle;
    }

private:
    std::shared_ptr< void > m_handle;
};

class ModuleHandle
{
public:
    ModuleHandle( HMODULE hModule = nullptr )
        : m_hModule( hModule, Deleter )
    {
    }

    static void Deleter( HMODULE hModule )
    {
        spdlog::debug( "{} {}", __FUNCTION__, static_cast< void* >( hModule ) );

        if( hModule == nullptr ) {
            return;
        }

        if( FreeLibrary( hModule ) == FALSE ) {
            spdlog::warn( "Failed on FreeLibrary: {}", GetLastError() );
        }
    }

    HMODULE get() const
    {
        return reinterpret_cast< HMODULE >( m_hModule.get() );
    }

    operator bool() const { return m_hModule.get() != nullptr; }

    bool operator==( const ModuleHandle& o ) const
    {
        return m_hModule == o.m_hModule;
    }

private:
    std::shared_ptr< void > m_hModule;
};

class Handle
{
public:
    Handle( HANDLE handle = nullptr )
        : m_handle( handle, Deleter )
    {
    }

    static void Deleter( HANDLE handle )
    {
        spdlog::debug( "{} {}", __FUNCTION__, handle );

        if( handle == nullptr ) {
            return;
        }

        if( CloseHandle( handle ) == FALSE ) {
            spdlog::warn( "Failed on CloseHandle: {}", GetLastError() );
        }
    }

    HANDLE get() const { return m_handle.get(); }
    operator bool() const { return m_handle.get() != nullptr; }
    bool operator==( const Handle& o ) const { return m_handle == o.m_handle; }

private:
    std::shared_ptr< void > m_handle;
};

class BeginUpdateResourceHandleW
{
    struct Context
    {
        HANDLE handle;
    };

public:
    BeginUpdateResourceHandleW( HANDLE handle = nullptr )
        : m_context( { handle } )
        , m_ptr( &m_context, Deleter )
    {
    }

    static void Deleter( Context* context )
    {
        spdlog::debug( "{} {}", __FUNCTION__, context->handle );

        if( context->handle == nullptr ) {
            return;
        }

        if( EndUpdateResourceW( context->handle, FALSE ) == FALSE ) {
            spdlog::warn( "Failed on EndUpdateResourceW: {}", GetLastError() );
        }
    }

    HANDLE detach()
    {
        spdlog::debug( "{} {}", __FUNCTION__, m_context.handle );

        if( m_context.handle == nullptr ) {
            return nullptr;
        }

        auto handle = m_context.handle;
        m_context.handle = nullptr;
        return handle;
    }

    HANDLE get() const { return m_context.handle; }
    operator bool() const { return m_context.handle != nullptr; }
    bool operator==( const BeginUpdateResourceHandleW& o ) const
    {
        return m_context.handle == o.m_context.handle;
    }

private:
    struct Context m_context;
    std::shared_ptr< void > m_ptr;
};

template < typename T >
class ComPtr
{
public:
    ComPtr()
        : ComPtr( nullptr )
    {
    }

    ComPtr( T* com )
        : m_ptr( com )
    {
        AddRef();
    }

    ComPtr( const ComPtr& other )
    {
        m_ptr = other.m_ptr;
        m_ptr->AddRef();
    }

    ComPtr( ComPtr&& other ) noexcept
    {
        m_ptr = other.m_ptr;
        other.m_ptr = nullptr;
    }

    ComPtr& operator=( const ComPtr& other )
    {
        if( &other == this ) {
            return *this;
        }

        Release();
        m_ptr = other.m_ptr;
        m_ptr->AddRef();
        return *this;
    }

    ~ComPtr() { Release(); }

    T* get() const { return m_ptr; }

    T** operator&()
    {
        Release();
        return &m_ptr;
    }

    T* operator->() const { return m_ptr; }

    operator bool() const { return m_ptr != nullptr; }

    T* Detach()
    {
        auto ptr = m_ptr;
        m_ptr = nullptr;
        return ptr;
    }

private:
    void AddRef()
    {
        if( m_ptr == nullptr ) {
            return;
        }

        auto p = static_cast< IUnknown* >( m_ptr );
        p->AddRef();
    }

    void Release()
    {
        if( m_ptr == nullptr ) {
            return;
        }

        auto p = static_cast< IUnknown* >( m_ptr );
        m_ptr = nullptr;
        p->Release();
    }

    T* m_ptr;
};

};  // namespace guard

}  // namespace rcedit
