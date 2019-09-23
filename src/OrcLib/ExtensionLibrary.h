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

#include "Robustness.h"

#pragma managed(push, off)

namespace Orc {

template <class Ext>
class ExtensionLibraryHandler : public TerminationHandler
{

public:
    ExtensionLibraryHandler(const std::wstring& strDescr)
        : TerminationHandler(strDescr, ROBUSTNESS_UNLOAD_DLLS) {};

    HRESULT operator()()
    {
        const auto ext = ExtensionLibrary::GetShared<Ext>(false, nullptr);
        if (ext)
            ext->UnloadAndCleanup();
        return S_OK;
    }
};

class ORCLIB_API ExtensionLibrary
{

    template <class Ext>
    friend class ExtensionLibraryHandler;

public:
    ExtensionLibrary(
        logger pLog,
        const std::wstring& strKeyword,
        const std::wstring& strX86LibRef,
        const std::wstring& strX64LibRef,
        const std::wstring& strTempDir = L"");

    ExtensionLibrary(ExtensionLibrary&&) noexcept = default;

    bool CheckInitialized();
    bool CheckLoaded();

    template <class Library>
    static const std::shared_ptr<Library> GetLibrary(logger pLog = nullptr, bool bInitialize = true)
    {
        try
        {
            HRESULT hr = E_FAIL;
            const std::shared_ptr<Library>& pLib = GetShared<Library>(true, std::move(pLog));

            if (!bInitialize)
                return pLib;

            if (pLib != nullptr)
            {
                if (!pLib->IsLoaded())
                {
                    if (pLib->CheckLoaded())
                    {
                        if (!pLib->m_UnLoadHandler)
                        {
                            pLib->m_UnLoadHandler =
                                std::make_shared<ExtensionLibraryHandler<Library>>(L"ExtensionLibraryUnLoad");
                            Robustness::AddTerminationHandler(pLib->m_UnLoadHandler);
                        }
                        return pLib;
                    }
                    else
                        return nullptr;
                }

                return pLib->CheckInitialized() ? pLib : nullptr;
            }
            return nullptr;
        }
        catch (std::exception e)
        {
            return nullptr;
        }
    }

    HRESULT Load(const std::wstring& strAlternateFileRef = L"");
    const std::wstring& LibraryFile() const { return m_strLibFile; }

    STDMETHOD(UnLoad());
    STDMETHOD(Cleanup());
    STDMETHOD(UnloadAndCleanup());

    bool IsLoaded() const { return m_hModule != NULL ? true : false; }
    bool IsInitialized() const { return m_bInitialized; }

    virtual ~ExtensionLibrary(void);

protected:
    logger _L_;
    bool m_bInitialized = false;

    CriticalSection m_cs;

    HRESULT TryLoad(const std::wstring& strFileRef);

    template <typename FuncType>
    void Get(FuncType& func, LPSTR szFuncName)
    {
        func = GetExtension<FuncType>(szFuncName, true);
    };
    template <typename FuncType>
    void Try(FuncType& func, LPSTR szFuncName)
    {
        func = GetExtension<FuncType>(szFuncName, false);
    };

    STDMETHOD(Initialize)()
    {
        // by default, we have nothing to initialize
        return S_OK;
    };

    virtual std::pair<HRESULT, HINSTANCE> LoadThisLibrary(const std::wstring& strLibFile);

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

    std::wstring m_strLibFile;
    std::wstring m_strTempDir;

    bool m_bDeleteOnClose = false;

    std::shared_ptr<TerminationHandler> m_UnLoadHandler;

    template <class Library>
    static const std::shared_ptr<Library> GetShared(bool bMakeNew = true, logger pLog = nullptr)
    {
        static std::weak_ptr<Library> g_pLibrary;

        auto shared = g_pLibrary.lock();

        if (shared == nullptr && bMakeNew && pLog != nullptr)
        {
            static CriticalSection g_cs;
            ScopedLock sc(g_cs);

            shared = std::make_shared<Library>(pLog);
            g_pLibrary = shared;
        }

        return shared;
    };

    FARPROC GetEntryPoint(const CHAR* szFunctionName, bool bMandatory = false);

    template <typename T>
    T GetExtension(const CHAR* szFunctionName, bool bMandatory = false)
    {
        return (T)GetEntryPoint(szFunctionName, bMandatory);
    };
};

class ORCLIB_API TemplateExtension : public ExtensionLibrary
{
    friend class ExtensionLibrary;

public:
    TemplateExtension(logger pLog)
        : ExtensionLibrary(pLog, L"template", L"template.dll", L"template.dll") {};
    virtual ~TemplateExtension() {}
    STDMETHOD(Initialize)() { return S_OK; }
};
}  // namespace Orc

#pragma managed(pop)
