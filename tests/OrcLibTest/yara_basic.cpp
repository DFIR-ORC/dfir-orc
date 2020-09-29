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

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

using namespace std::string_literals;

namespace Orc::Test {
TEST_CLASS(YaraBasicTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize) {}

    TEST_METHOD_CLEANUP(Finalize) {}

    TEST_METHOD(YaraInitialize)
    {
        const auto& yara = std::make_shared<YaraStaticExtension>();

        yara->yr_initialize();

        yara->yr_finalize();
    }

    TEST_METHOD(YaraCompiler)
    {
        const auto& yara = std::make_shared<YaraStaticExtension>();

        yara->yr_initialize();

        YR_COMPILER* pCompiler = nullptr;

        yara->yr_compiler_create(&pCompiler);

        yara->yr_compiler_set_callback(pCompiler, compiler_callback, this);

        yara->yr_compiler_add_string(pCompiler, "rule rule_1 {condition:	false}", nullptr);
        yara->yr_compiler_add_string(pCompiler, "rule rule_2 {condition:	false}", nullptr);

        YR_RULES* pRules = nullptr;
        yara->yr_compiler_get_rules(pCompiler, &pRules);

        YR_RULE* pRule = nullptr;
        yr_rules_foreach(pRules, pRule) { spdlog::info("Compiled rule: '{}'", pRule->identifier); }

        yara->yr_compiler_destroy(pCompiler);

        yara->yr_finalize();
    }

    TEST_METHOD(YaraModules)
    {
        const auto& yara = std::make_shared<YaraStaticExtension>();

        yara->yr_initialize();

        YR_COMPILER* pCompiler = nullptr;

        yara->yr_compiler_create(&pCompiler);

        yara->yr_compiler_set_callback(pCompiler, compiler_callback, this);

        yara->yr_compiler_add_string(
            pCompiler,
            R"(
				import "pe" 
				import "hash" 
				/*
				This is a simple string lookup rule ...
				*/
				rule simple_string{
				strings:
					$text_string = "HelloWorld"
						condition :
						$text_string
				}
				rule single_section
				{
					condition:
						pe.number_of_sections == 1
				}

				rule control_panel_applet
				{
					condition:
						pe.exports("CPlApplet")
				}

				rule is_dll
				{
					condition:
						pe.characteristics & pe.DLL
				}
			)",
            nullptr);

        yara->yr_compiler_destroy(pCompiler);

        yara->yr_finalize();
    }

    TEST_METHOD(YaraScanMem)
    {
        const auto& yara = std::make_shared<YaraStaticExtension>();

        yara->yr_initialize();

        YR_COMPILER* pCompiler = nullptr;

        yara->yr_compiler_create(&pCompiler);

        yara->yr_compiler_add_string(
            pCompiler,
            R"(
					/*
						This is a simple string lookup rule ...
					*/
					rule simple_string {
						strings:
							$text_string = "HelloWorld"
						condition:	
							$text_string
					}
			)",
            nullptr);

        YR_RULES* pRules = nullptr;
        yara->yr_compiler_get_rules(pCompiler, &pRules);

        yara->yr_compiler_destroy(pCompiler);

        auto strText = "This is a text with HelloWorld inside it"s;

        yara->yr_rules_scan_mem(pRules, (const uint8_t*)strText.data(), strText.size(), 0L, scan_callback, this, 60);

        yara->yr_finalize();
    }

private:
    void compiler_message(int error_level, const char* file_name, int line_number, const char* message)
    {
        Log::Info("level: {}, line: {}, '{}'", error_level, line_number, message);
    }

    static void compiler_callback(
        int error_level, const char* file_name, int line_number, const char* message, void* user_data)
    {
        auto pThis = (YaraBasicTest*)user_data;
        pThis->compiler_message(error_level, file_name, line_number, message);
    }

    int scan_message(int message, void* message_data)
    {
        switch (message)
        {
            case CALLBACK_MSG_RULE_MATCHING:
                Log::Info("Text matched rule: '{}'", ((YR_RULE*)message_data)->identifier);
                break;
            case CALLBACK_MSG_RULE_NOT_MATCHING:
                Log::Info("Text did not match rule: '{}'", ((YR_RULE*)message_data)->identifier);
                break;
            case CALLBACK_MSG_SCAN_FINISHED:
                Log::Info("Scan finished");
                break;
            case CALLBACK_MSG_IMPORT_MODULE:
                Log::Info("Importing module: '{}'", ((YR_OBJECT_STRUCTURE*)message_data)->identifier);
                break;
            case CALLBACK_MSG_MODULE_IMPORTED:
                Log::Info("Module '{}' imported", ((YR_OBJECT_STRUCTURE*)message_data)->identifier);
                break;
            default:
                break;
        }
        return CALLBACK_CONTINUE;
    }
    static int scan_callback(int message, void* message_data, void* user_data)
    {
        auto pThis = (YaraBasicTest*)user_data;
        return pThis->scan_message(message, message_data);
    }
};
}  // namespace Orc::Test
