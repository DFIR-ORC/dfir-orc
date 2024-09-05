//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "ExtensionLibrary.h"

#include <windows.h>
#include <functional>
#include <winternl.h>

#include "yara.h"

#pragma managed(push, off)

namespace Orc {

class YaraExtension : public ExtensionLibrary
{
    friend class ExtensionLibrary;

public:
    YaraExtension()
        : ExtensionLibrary(L"libyara.dll", L"YARA_X86DLL", L"YARA_X64DLL") {};

    template <typename... Args>
    auto yr_initialize(Args&&... args)
    {
        return Call(m_yr_initialize, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto yr_compiler_create(Args&&... args)
    {
        return Call(m_yr_compiler_create, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto yr_compiler_add_string(Args&&... args)
    {
        return Call(m_yr_compiler_add_string, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto yr_compiler_set_callback(Args&&... args)
    {
        return Call(m_yr_compiler_set_callback, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto yr_compiler_set_include_callback(Args&&... args)
    {
        return Call(m_yr_compiler_set_include_callback, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto yr_compiler_get_rules(Args&&... args)
    {
        return Call(m_yr_compiler_get_rules, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto yr_rule_enable(Args&&... args)
    {
        return Call(m_yr_rule_enable, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto yr_rule_disable(Args&&... args)
    {
        return Call(m_yr_rule_disable, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto yr_rules_scan_mem(Args&&... args)
    {
        return Call(m_yr_rules_scan_mem, std::forward<Args>(args)...);
    }
    template <typename... Args>
    auto yr_compiler_destroy(Args&&... args)
    {
        return Call(m_yr_compiler_destroy, std::forward<Args>(args)...);
    }

    template <typename... Args>
    auto yr_finalize(Args&&... args)
    {
        return Call(m_yr_finalize, std::forward<Args>(args)...);
    }

    STDMETHOD(Initialize)();

private:
    int(__cdecl* m_yr_initialize)(void);

    int(__cdecl* m_yr_compiler_create)(YR_COMPILER** compiler);
    int(__cdecl* m_yr_compiler_add_string)(YR_COMPILER* compiler, const char* string, const char* namespace_);

    void(__cdecl* m_yr_compiler_set_callback)(
        YR_COMPILER* compiler,
        YR_COMPILER_CALLBACK_FUNC callback,
        void* user_data);
    void(__cdecl* m_yr_compiler_set_include_callback)(
        YR_COMPILER* compiler,
        YR_COMPILER_INCLUDE_CALLBACK_FUNC callback,
        YR_COMPILER_INCLUDE_FREE_FUNC include_free,
        void* user_data);

    int(__cdecl* m_yr_compiler_get_rules)(YR_COMPILER* compiler, YR_RULES** rules);

    void(__cdecl* m_yr_compiler_destroy)(YR_COMPILER* compiler);

    void(__cdecl* m_yr_rule_enable)(YR_RULE* rule);
    void(__cdecl* m_yr_rule_disable)(YR_RULE* rule);

    int(__cdecl* m_yr_rules_scan_mem)(
        YR_RULES* rules,
        const uint8_t* buffer,
        size_t buffer_size,
        int flags,
        YR_CALLBACK_FUNC callback,
        void* user_data,
        int timeout);

    int(__cdecl* m_yr_finalize)(void);
};
}  // namespace Orc

#pragma managed(pop)
