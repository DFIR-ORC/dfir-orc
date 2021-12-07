//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <string>

#include "OrcLib.h"

#include "CriticalSection.h"
#include "WideAnsi.h"
#include "Robustness.h"

#include <optional>

#pragma managed(push, off)

namespace Orc {

template <class Ext>
class ExtensionLibraryHandler : public TerminationHandler
{

public:
    ExtensionLibraryHandler(const std::wstring& strDescr)
        : TerminationHandler(strDescr, ROBUSTNESS_UNLOAD_DLLS) {};

    HRESULT operator()();
};

class ExtensionLibrary
{

    template <class Ext>
    friend class ExtensionLibraryHandler;

public:
    ExtensionLibrary(
        const std::wstring& strKeyword,
        const std::wstring& strX86LibRef,
        const std::wstring& strX64LibRef);

    ExtensionLibrary(ExtensionLibrary&&) noexcept = default;

    template <class Library>
    static std::wstring Name()
    {
        using namespace std::string_literals;
        if (auto [hr, wstr] = AnsiToWide(typeid(Library).name()); SUCCEEDED(hr))
            return wstr;
        else
            return L"(invalid library name)"s;
    }

    template <class Library>
    static const std::shared_ptr<Library>
    GetLibrary(std::optional<std::filesystem::path> tempDir = std::nullopt, bool bShared = true)
    {
        try
        {
            const std::shared_ptr<Library>& pLib = bShared ? GetShared<Library>(true) : std::make_shared<Library>();

            if (!pLib)
            {
                Log::Error(L"Failed to get shared ptr to library {}", Name<Library>());
                return nullptr;
            }

            if (!pLib->IsLoaded())
            {
                Log::Debug(L"Library {} is not yet loaded", Name<Library>());

                if (auto hr = pLib->Load(tempDir); FAILED(hr))
                {
                    Log::Error(L"Library {} is not loaded", Name<Library>());
                    return nullptr;
                }
            }

            if (!pLib->IsInitialized())
            {
                if (auto hr = pLib->Initialize(); FAILED(hr))
                {
                    Log::Error(L"Library {} is loaded but could not be initialized", Name<Library>());
                    return nullptr;
                }
            }
            if (!pLib->m_UnLoadHandler)
            {
                pLib->m_UnLoadHandler = std::make_shared<ExtensionLibraryHandler<Library>>(L"ExtensionLibraryUnLoad");
                Robustness::AddTerminationHandler(pLib->m_UnLoadHandler);
            }
            return pLib;
        }
        catch (const std::exception& e)
        {
            auto [hr, wstr] = AnsiToWide(e.what());
            Log::Critical(L"Library {} raised an exception: {}", Name<Library>(), wstr);
            return nullptr;
        }
    }

    HRESULT
    Load(std::optional<std::filesystem::path> tempDir = std::nullopt);
    const std::filesystem::path& LibraryFile() const { return m_libFile; }

    virtual HRESULT UnLoad();
    virtual HRESULT Cleanup();
    virtual HRESULT UnloadAndCleanup();

    bool IsLoaded() const { return m_hModule != NULL ? true : false; }
    bool IsInitialized() const { return m_bInitialized; }

    virtual ~ExtensionLibrary(void);

    static const std::filesystem::path&
    DefaultExtensionDirectory(std::optional<std::filesystem::path> aDefaultDir = std::nullopt);

protected:
    bool m_bInitialized = false;

    CriticalSection m_cs;

    HRESULT TryLoad(const std::wstring& strFileRef);

    template <typename FuncType>
    void Get(FuncType& func, LPCSTR szFuncName)
    {
        func = GetExtension<FuncType>(szFuncName, true);
    };
    template <typename FuncType>
    void Try(FuncType& func, LPCSTR szFuncName)
    {
        func = GetExtension<FuncType>(szFuncName, false);
    };

    HRESULT Initialize()
    {
        // by default, we have nothing to initialize
        return S_OK;
    };

    virtual std::pair<HRESULT, HINSTANCE> LoadThisLibrary(const std::filesystem::path& libFile);

    virtual void FreeThisLibrary(HINSTANCE hInstance)
    {
        FreeLibrary(hInstance);
        return;
    }

    template <typename FuncTor, typename... Args>
    auto Win32Call(FuncTor& func, Args&&... args)
    {
        if (!m_bInitialized && FAILED(Initialize()))
            return E_FAIL;
        if (!func(std::forward<Args>(args)...))
            return HRESULT_FROM_WIN32(GetLastError());
        return S_OK;
    }

    template <typename FuncTor, typename... Args>
    auto NtCall(FuncTor& func, Args&&... args)
    {
        if (!m_bInitialized && FAILED(Initialize()))
            return E_FAIL;
        return HRESULT_FROM_NT(func(std::forward<Args>(args)...));
    }

    template <typename FuncTor, typename... Args>
    auto VoidCall(FuncTor& func, Args&&... args)
    {
        if (!m_bInitialized && FAILED(Initialize()))
            return E_FAIL;
        func(std::forward<Args>(args)...);
        return S_OK;
    }

    template <typename FuncTor, typename... Args>
    HRESULT COMCall(FuncTor& func, Args&&... args)
    {
        if (!m_bInitialized && FAILED(Initialize()))
            return E_FAIL;
        return func(std::forward<Args>(args)...);
    }

    template <typename FuncTor, typename... Args>
    auto Call(FuncTor& func, Args&&... args)
    {
        static_cast<void>(Initialize());  // We cannot care about Initialize() return value...
        return func(std::forward<Args>(args)...);
    }
    HMODULE m_hModule = nullptr;

    std::wstring m_strKeyword;

    // There are the default Library refs
    std::wstring m_strX86LibRef;
    std::wstring m_strX64LibRef;

    std::wstring m_strLibRef;  // Effective, contextual ref used to locate extension lib

    std::filesystem::path m_libFile;
    std::filesystem::path m_tempDir;
    bool m_bDeleteTemp = false;

    std::optional<std::wstring> m_strDesiredName;

    bool m_bDeleteOnClose = false;
    std::shared_ptr<TerminationHandler> m_UnLoadHandler;

    HRESULT ToDesiredName(const std::wstring& libName);

    template <class Library>
    static const std::shared_ptr<Library> GetShared(bool bMakeNew = true)
    {
        static std::weak_ptr<Library> g_pLibrary;

        auto shared = g_pLibrary.lock();

        if (shared == nullptr && bMakeNew)
        {
            static CriticalSection g_cs;
            ScopedLock sc(g_cs);

            shared = std::make_shared<Library>();
            g_pLibrary = shared;
        }

        return shared;
    };

    FARPROC GetEntryPoint(LPCSTR szFunctionName, bool bMandatory = false);

    template <typename T>
    T GetExtension(LPCSTR szFunctionName, bool bMandatory = false)
    {
        return (T)GetEntryPoint(szFunctionName, bMandatory);
    };
};  // namespace Orc

template <class Ext>
HRESULT ExtensionLibraryHandler<Ext>::operator()()
{
    const auto ext = ExtensionLibrary::GetShared<Ext>(false);
    if (ext)
        ext->UnloadAndCleanup();
    return S_OK;
}

class TemplateExtension : public ExtensionLibrary
{
    friend class ExtensionLibrary;

public:
    TemplateExtension()
        : ExtensionLibrary(L"template", L"template.dll", L"template.dll") {};
    virtual ~TemplateExtension() {}
    STDMETHOD(Initialize)() { return S_OK; }
};
}  // namespace Orc

#pragma managed(pop)
