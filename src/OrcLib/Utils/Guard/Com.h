//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
// RAII wrappers for the three COM resources this codebase used ATL for: an interface pointer, a BSTR and
// a VARIANT. They intentionally mirror CComPtr / CComBSTR / CComVariant (implicit conversion to the raw
// type, ComVariant deriving from VARIANT) so call sites read the same, without pulling in <atlbase.h>
// and the ATL component it requires from the Visual Studio installation.
//
#pragma once

#include <string>
#include <utility>

#include <windows.h>
#include <unknwn.h>
#include <oaidl.h>
#include <oleauto.h>
#include <objbase.h>

namespace Orc {
namespace Guard {

// A reference-counted COM interface pointer. Constructing or assigning from a raw pointer AddRefs it,
// like CComPtr: a freshly created object (refcount 0 in the 7-Zip and OrcLib callbacks) therefore ends
// up owned by the first ComPtr that holds it.
template <typename T>
class ComPtr final
{
public:
    ComPtr() noexcept = default;

    ComPtr(std::nullptr_t) noexcept {}

    ComPtr(T* p) noexcept
        : m_p(p)
    {
        InternalAddRef();
    }

    ComPtr(const ComPtr& o) noexcept
        : m_p(o.m_p)
    {
        InternalAddRef();
    }

    ComPtr(ComPtr&& o) noexcept
        : m_p(o.m_p)
    {
        o.m_p = nullptr;
    }

    ~ComPtr() { InternalRelease(); }

    ComPtr& operator=(const ComPtr& o) noexcept
    {
        if (this != &o && m_p != o.m_p)
        {
            T* previous = m_p;
            m_p = o.m_p;
            InternalAddRef();
            ReleasePointer(previous);
        }

        return *this;
    }

    ComPtr& operator=(ComPtr&& o) noexcept
    {
        if (this != &o)
        {
            InternalRelease();
            m_p = o.m_p;
            o.m_p = nullptr;
        }

        return *this;
    }

    ComPtr& operator=(T* p) noexcept
    {
        if (m_p != p)
        {
            T* previous = m_p;
            m_p = p;
            InternalAddRef();
            ReleasePointer(previous);
        }

        return *this;
    }

    // Implicit, as CComPtr: the pointer is passed to COM APIs and compared to nullptr all over the place.
    // It is also what gives the type its boolean test, so there is deliberately no operator bool (the two
    // would be ambiguous).
    operator T*() const noexcept { return m_p; }

    T* operator->() const noexcept { return m_p; }

    T* Get() const noexcept { return m_p; }

    // The out-parameter form, 'hr = Something(..., &ptr)'. Releasing first is the one deliberate departure
    // from CComPtr, which only asserted in debug builds and leaked the previous interface in release ones.
    T** operator&() noexcept
    {
        InternalRelease();
        return &m_p;
    }

    // Take ownership of an already AddRef'd pointer (no AddRef of our own).
    void Attach(T* p) noexcept
    {
        InternalRelease();
        m_p = p;
    }

    // Hand ownership to the caller, which becomes responsible for the reference.
    T* Detach() noexcept
    {
        T* p = m_p;
        m_p = nullptr;
        return p;
    }

    void Release() noexcept { InternalRelease(); }

    // Hand a counted reference to 'out', as an [out] interface parameter of a COM method must.
    HRESULT CopyTo(T** out) const noexcept
    {
        if (out == nullptr)
        {
            return E_POINTER;
        }

        *out = m_p;
        if (m_p != nullptr)
        {
            m_p->AddRef();
        }

        return S_OK;
    }

    HRESULT CoCreateInstance(REFCLSID classId, IUnknown* outer = nullptr, DWORD context = CLSCTX_ALL) noexcept
    {
        InternalRelease();
        return ::CoCreateInstance(classId, outer, context, __uuidof(T), reinterpret_cast<void**>(&m_p));
    }

private:
    void InternalAddRef() noexcept
    {
        if (m_p != nullptr)
        {
            m_p->AddRef();
        }
    }

    void InternalRelease() noexcept
    {
        T* p = m_p;
        m_p = nullptr;
        ReleasePointer(p);
    }

    static void ReleasePointer(T* p) noexcept
    {
        if (p != nullptr)
        {
            p->Release();
        }
    }

    T* m_p = nullptr;
};

// Query 'source' for 'iid' into 'target', the CComQIPtr replacement. 'target' is emptied first, so it is
// left empty when the interface is not supported.
template <typename T, typename U>
HRESULT QueryInterfaceInto(U* source, REFIID iid, ComPtr<T>& target)
{
    target.Release();
    if (source == nullptr)
    {
        return E_POINTER;
    }

    return source->QueryInterface(iid, reinterpret_cast<void**>(&target));
}

// An owned BSTR. Implicitly converts to BSTR so it can be passed to a COM method expecting one, and
// frees it on scope exit.
class ComBStr final
{
public:
    ComBStr() noexcept = default;

    explicit ComBStr(const wchar_t* text) noexcept
        : m_bstr(text != nullptr ? ::SysAllocString(text) : nullptr)
    {
    }

    explicit ComBStr(const std::wstring& text) noexcept
        : m_bstr(::SysAllocStringLen(text.c_str(), static_cast<UINT>(text.size())))
    {
    }

    ComBStr(const ComBStr&) = delete;
    ComBStr& operator=(const ComBStr&) = delete;

    ComBStr(ComBStr&& o) noexcept
        : m_bstr(o.m_bstr)
    {
        o.m_bstr = nullptr;
    }

    ComBStr& operator=(ComBStr&& o) noexcept
    {
        if (this != &o)
        {
            Free();
            m_bstr = o.m_bstr;
            o.m_bstr = nullptr;
        }

        return *this;
    }

    ~ComBStr() { Free(); }

    operator BSTR() const noexcept { return m_bstr; }

    BSTR Get() const noexcept { return m_bstr; }

    // The allocation failed when a non-empty string yields an empty BSTR: SysAllocString returns null on
    // an out-of-memory condition, and every COM method here would then be handed a null string.
    bool IsValid() const noexcept { return m_bstr != nullptr; }

private:
    void Free() noexcept
    {
        if (m_bstr != nullptr)
        {
            ::SysFreeString(m_bstr);
            m_bstr = nullptr;
        }
    }

    BSTR m_bstr = nullptr;
};

// A BSTR as a std::wstring. A BSTR carries its own length, so an embedded NUL is preserved; a null BSTR
// is an empty string.
inline std::wstring BStrToString(BSTR bstr)
{
    if (bstr == nullptr)
    {
        return {};
    }

    return std::wstring(bstr, ::SysStringLen(bstr));
}

// An owned VARIANT. It derives from VARIANT, as CComVariant does, so that a call site keeps reading
// 'v.vt' / 'v.bstrVal' and passing '&v' to a COM method. The type is final and no member is virtual:
// the base has no destructor to hide.
class ComVariant final : public VARIANT
{
public:
    ComVariant() noexcept { ::VariantInit(this); }

    explicit ComVariant(const wchar_t* text) noexcept
    {
        ::VariantInit(this);
        vt = VT_BSTR;
        bstrVal = (text != nullptr) ? ::SysAllocString(text) : nullptr;
    }

    explicit ComVariant(INT32 value) noexcept
    {
        ::VariantInit(this);
        vt = VT_I4;
        lVal = value;
    }

    explicit ComVariant(UINT32 value) noexcept
    {
        ::VariantInit(this);
        vt = VT_UI4;
        ulVal = value;
    }

    explicit ComVariant(IUnknown* unknown) noexcept
    {
        ::VariantInit(this);
        vt = VT_UNKNOWN;
        punkVal = unknown;
        if (punkVal != nullptr)
        {
            punkVal->AddRef();
        }
    }

    ComVariant(const ComVariant&) = delete;
    ComVariant& operator=(const ComVariant&) = delete;

    ~ComVariant() { Clear(); }

    void Clear() noexcept { ::VariantClear(this); }

    HRESULT ChangeType(VARTYPE type) noexcept { return ::VariantChangeType(this, this, 0, type); }
};

}  // namespace Guard
}  // namespace Orc
