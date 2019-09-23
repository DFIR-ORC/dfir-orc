//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "YaraStaticExtension.h"
#include "yara.h"

using namespace Orc;

int YaraStaticExtension::yr_initialize()
{
    return ::yr_initialize();
}

int YaraStaticExtension::yr_compiler_create(YR_COMPILER** compiler)
{
    return ::yr_compiler_create(compiler);
}

int YaraStaticExtension::yr_compiler_add_string(YR_COMPILER* compiler, const char* string, const char* namespace_)
{
    return ::yr_compiler_add_string(compiler, string, namespace_);
}

void YaraStaticExtension::yr_compiler_set_callback(
    YR_COMPILER* compiler,
    YR_COMPILER_CALLBACK_FUNC callback,
    void* user_data)
{
    ::yr_compiler_set_callback(compiler, callback, user_data);
}

void YaraStaticExtension::yr_compiler_set_include_callback(
    YR_COMPILER* compiler,
    YR_COMPILER_INCLUDE_CALLBACK_FUNC callback,
    YR_COMPILER_INCLUDE_FREE_FUNC include_free,
    void* user_data)
{
    ::yr_compiler_set_include_callback(compiler, callback, include_free, user_data);
}

int YaraStaticExtension::yr_compiler_get_rules(YR_COMPILER* compiler, YR_RULES** rules)
{
    return ::yr_compiler_get_rules(compiler, rules);
}

void YaraStaticExtension::yr_compiler_destroy(YR_COMPILER* compiler)
{
    ::yr_compiler_destroy(compiler);
}

void YaraStaticExtension::yr_rule_enable(YR_RULE* rule)
{
    ::yr_rule_enable(rule);
}

void YaraStaticExtension::yr_rule_disable(YR_RULE* rule)
{
    ::yr_rule_disable(rule);
}

int YaraStaticExtension::yr_rules_scan_mem(
    YR_RULES* rules,
    const uint8_t* buffer,
    size_t buffer_size,
    int flags,
    YR_CALLBACK_FUNC callback,
    void* user_data,
    int timeout)
{
    return ::yr_rules_scan_mem(rules, buffer, buffer_size, flags, callback, user_data, timeout);
}

int YaraStaticExtension::yr_finalize()
{
    return ::yr_finalize();
}
