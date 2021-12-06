//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "yara/compiler.h"

#pragma managed(push, off)

namespace Orc {
class YaraStaticExtension
{
public:
    YaraStaticExtension() {}

    int yr_initialize(void);
    int yr_compiler_create(YR_COMPILER** compiler);
    int yr_compiler_add_string(YR_COMPILER* compiler, const char* string, const char* namespace_);

    void yr_compiler_set_callback(YR_COMPILER* compiler, YR_COMPILER_CALLBACK_FUNC callback, void* user_data);
    void yr_compiler_set_include_callback(
        YR_COMPILER* compiler,
        YR_COMPILER_INCLUDE_CALLBACK_FUNC callback,
        YR_COMPILER_INCLUDE_FREE_FUNC include_free,
        void* user_data);

    int yr_compiler_get_rules(YR_COMPILER* compiler, YR_RULES** rules);

    void yr_compiler_destroy(YR_COMPILER* compiler);

    void yr_rule_enable(YR_RULE* rule);
    void yr_rule_disable(YR_RULE* rule);

    int yr_rules_scan_mem(
        YR_RULES* rules,
        const uint8_t* buffer,
        size_t buffer_size,
        int flags,
        YR_CALLBACK_FUNC callback,
        void* user_data,
        int timeout);

    int yr_rules_scan_mem_blocks(
        YR_RULES* rules,
        YR_MEMORY_BLOCK_ITERATOR* iterator,
        int flags,
        YR_CALLBACK_FUNC callback,
        void* user_data,
        int timeout);

    int yr_finalize(void);
};

}  // namespace Orc

#pragma managed(pop)
