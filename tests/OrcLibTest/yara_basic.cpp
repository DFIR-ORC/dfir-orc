//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "LogFileWriter.h"
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
    logger _L_;
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
        _L_ = std::make_shared<LogFileWriter>();
        helper.InitLogFileWriter(_L_);
    }

    TEST_METHOD_CLEANUP(Finalize) { helper.FinalizeLogFileWriter(_L_); }

    TEST_METHOD(YaraInitialize)
    {
        const auto& yara = std::make_shared<YaraStaticExtension>(_L_);

        yara->yr_initialize();

        yara->yr_finalize();
    }

    TEST_METHOD(YaraCompiler)
    {
        const auto& yara = std::make_shared<YaraStaticExtension>(_L_);

        yara->yr_initialize();

        YR_COMPILER* pCompiler = nullptr;

        yara->yr_compiler_create(&pCompiler);

        yara->yr_compiler_set_callback(pCompiler, compiler_callback, this);

        yara->yr_compiler_add_string(pCompiler, "rule rule_1 {condition:	false}", nullptr);
        yara->yr_compiler_add_string(pCompiler, "rule rule_2 {condition:	false}", nullptr);

        YR_RULES* pRules = nullptr;
        yara->yr_compiler_get_rules(pCompiler, &pRules);

        YR_RULE* pRule = nullptr;
        yr_rules_foreach(pRules, pRule) { log::Info(_L_, L"Compiled rule: %S\r\n", pRule->identifier); }

        yara->yr_compiler_destroy(pCompiler);

        yara->yr_finalize();
    }

    TEST_METHOD(YaraModules)
    {
        const auto& yara = std::make_shared<YaraStaticExtension>(_L_);

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
        const auto& yara = std::make_shared<YaraStaticExtension>(_L_);

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
        log::Info(_L_, L"level:%d, line:%d, \"%S\"\r\n", error_level, line_number, message);
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
                log::Info(_L_, L"Text matched rule: %S\r\n", ((YR_RULE*)message_data)->identifier);
                break;
            case CALLBACK_MSG_RULE_NOT_MATCHING:
                log::Info(_L_, L"Text did not match rule: %S\r\n", ((YR_RULE*)message_data)->identifier);
                break;
            case CALLBACK_MSG_SCAN_FINISHED:
                log::Info(_L_, L"Scan finished\r\n");
                break;
            case CALLBACK_MSG_IMPORT_MODULE:
                log::Info(_L_, L"Importing module %S\r\n", ((YR_OBJECT_STRUCTURE*)message_data)->identifier);
                break;
            case CALLBACK_MSG_MODULE_IMPORTED:
                log::Info(_L_, L"Module %S imported\r\n", ((YR_OBJECT_STRUCTURE*)message_data)->identifier);
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
