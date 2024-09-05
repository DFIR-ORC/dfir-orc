//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "YaraExtension.h"

#include "yara.h"

using namespace Orc;

HRESULT YaraExtension::Initialize()
{
    Orc::ScopedLock sl(m_cs);

    if (m_bInitialized)
        return S_OK;

    if (IsLoaded())
    {
        Get(m_yr_initialize, "yr_initialize");

        Get(m_yr_compiler_create, "yr_compiler_create");
        Get(m_yr_compiler_set_include_callback, "yr_compiler_set_include_callback");
        Get(m_yr_compiler_set_callback, "yr_compiler_set_callback");

        Get(m_yr_compiler_add_string, "yr_compiler_add_string");

        Get(m_yr_compiler_get_rules, "yr_compiler_get_rules");

        Get(m_yr_compiler_destroy, "yr_compiler_destroy");

        Get(m_yr_rule_enable, "yr_rule_enable");
        Get(m_yr_rule_disable, "yr_rule_disable");

        Get(m_yr_rules_scan_mem, "yr_rules_scan_mem");

        Get(m_yr_finalize, "yr_finalize");

        m_bInitialized = true;
    }

    return S_OK;
}
