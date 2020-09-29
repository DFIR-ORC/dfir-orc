//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "ChakraBridge.h"

#include "LogFileWriter.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace Orc::Test {
TEST_CLASS(ChakraBasicTest)
{
private:
    UnitTestHelper helper;

public:
    TEST_METHOD_INITIALIZE(Initialize)
    {
        _L_ = std::make_shared<LogFileWriter>();
        helper.InitLogFileWriter(_L_);
    }

    TEST_METHOD_CLEANUP(Finalize) { helper.FinalizeLogFileWriter(_L_); }

    TEST_METHOD(ChakraInitialize)
    {
        using namespace std::string_literals;

        auto chakra = jsc::details::chakra(_L_);
        Assert::IsNotNull(chakra.get(), L"Failed to initialize chakra library");

        jsc::runtime runtime;
        jsc::context ctx;
        try
        {
            // Create a runtime
            check(runtime.create(JsRuntimeAttributeNone));

            // Create a context
            check(ctx.create(runtime));

            // Make this a current context for this scope
            jsc::scoped_context sc(ctx);

            // JavaScript code
            auto script_body =
                LR"==(
					function sum(arg1, arg2) {
						return arg1 + arg2;
					}
					)=="s;

            // Execute script
            auto result = jsc::RunScript(script_body.c_str(), 0, L"");
            // 1. Run JavaScript function sum to add two integer values

            auto op1 = 2;
            auto op2 = 3;

            auto sum = jsc::value::global()[L"sum"](nullptr, 2, 3).as_int();

            log::Info(_L_, L"sum(%d,%d) = %d\r\n", op1, op2, sum);  // prints 5
            Assert::AreEqual(op1 + op2, sum);
        }
        catch (const jsc::exception& e)
        {
            log::Error(_L_, E_ABORT, L"Exception code: %d\r\n", e.code());
            Assert::Fail(L"Chakra raised an exception");
        }
    }
    TEST_METHOD(ChakraExternalFunction)
    {
        using namespace std::string_literals;

        auto chakra = jsc::details::chakra(_L_);
        Assert::IsNotNull(chakra.get(), L"Failed to initialize chakra library");

        jsc::runtime runtime;
        jsc::context ctx;

        try
        {
            // Create a runtime
            check(runtime.create(JsRuntimeAttributeNone));

            // Create a context
            check(ctx.create(runtime));

            // Make this a current context for this scope
            jsc::scoped_context sc(ctx);

            // JavaScript code
            auto script_body =
                LR"==(
					function testExternalFunction() {
						external_function("string value", true, { a: 20, b: [ "a1", null, undefined ] });
					}
				)=="s;

            // Execute script
            auto result = jsc::RunScript(script_body.c_str(), 0, L"");

            // 2. Add an external function to a script
            jsc::value::global()[L"external_function"] = jsc::value::function<4>(
                [this](jsc::value this_, const std::wstring& sval, bool bval, jsc::value object) {
                    log::Info(_L_, L"String argument: %s\r\n", sval.c_str());
                    log::Info(_L_, L"Boolean argument: %d\r\n", bval);
                    log::Info(_L_, L"Integer argument in object: %d\r\n", static_cast<int>(object[L"a"]));
                    log::Info(_L_, L"Length of JavaScript array: %d\r\n", object[L"b"][L"length"].as<int>());

                    // Return value to JavaScript
                    return true;
                });

            // 3. Run JavaScript function. Alternative call syntax
            jsc::value::global().call(L"testExternalFunction");  // will make a lambda above called
        }
        catch (const jsc::exception& e)
        {
            log::Error(_L_, E_ABORT, L"Exception code: %d\r\n", e.code());
            Assert::Fail(L"Chakra raised an exception");
        }
    }
    TEST_METHOD(ChakraObject)
    {
        using namespace std::string_literals;

        auto chakra = jsc::details::chakra(_L_);
        Assert::IsNotNull(chakra.get(), L"Failed to initialize chakra library");

        jsc::runtime runtime;
        jsc::context ctx;

        try
        {
            // Create a runtime
            check(runtime.create(JsRuntimeAttributeNone));

            // Create a context
            check(ctx.create(runtime));

            // Make this a current context for this scope
            jsc::scoped_context sc(ctx);

            // JavaScript code
            auto script_body =
                LR"==(
					function testExternalObject(obj) {
						obj.print("a: " + obj.a);
						obj.print("b: " + obj.b);
						obj.print("c: " + obj.c);

						// re-assign property (will cause property put accessor to be called)
						obj.c = 2;
						obj.print("c: " + obj.c);
					}
				)=="s;

            // Execute script
            auto result1 = jsc::RunScript(script_body.c_str(), 0, L"");

            // Create a JavaScript object
            int c = 42;
            auto obj =
                jsc::value::object()
                    .field(L"a", 10)  // constant property value
                    .property(L"b", [](jsc::value this_) { return L"Read-only property"; })
                    .property(
                        L"c", [&](jsc::value this_) { return c; }, [&](JsValueRef this_, int new_c) { c = new_c; })
                    .method<2>(L"print", [this](jsc::value this_, const std::wstring& message) {
                        log::Info(_L_, L"%s\r\n", message.c_str());
                    });

            // Run JavaScript function
            auto result2 = jsc::value::global().call(L"testExternalObject", obj);
        }
        catch (const jsc::exception& e)
        {
            log::Error(_L_, E_ABORT, L"Exception code: %d\r\n", e.code());
            Assert::Fail(L"Chakra raised an exception");
        }
    }
    TEST_METHOD(ChakraObjectFromPrototype)
    {
        using namespace std::string_literals;

        auto chakra = jsc::details::chakra(_L_);
        Assert::IsNotNull(chakra.get(), L"Failed to initialize chakra library");

        jsc::runtime runtime;
        jsc::context ctx;

        try
        {
            // Create a runtime
            check(runtime.create(JsRuntimeAttributeNone));

            // Create a context
            check(ctx.create(runtime));

            // Make this a current context for this scope
            jsc::scoped_context sc(ctx);

            // JavaScript code
            auto script_body =
                LR"==(
				)=="s;

            // Execute script
            auto result1 = jsc::RunScript(script_body.c_str(), 0, L"");

            auto proto = jsc::prototype::object();

            int c = 12;
            proto.field(L"a", 15);  // constant property value
            proto.property(L"b", [](jsc::value this_) { return L"Read-only property"; });
            proto.property(
                L"c", [&](jsc::value this_) { return c; }, [&](jsc::value this_, int new_c) { c = new_c; });
            proto.method<2>(L"print", [this](jsc::value this_, const std::wstring& message) {
                log::Info(_L_, L"%s\r\n", message.c_str());
            });

            for (int i = 0; i < 10000; i++)
            {
                auto obj_from_proto = jsc::value::object(proto);
                auto toto = obj_from_proto[L"b"].as_string();
                auto titi = obj_from_proto[L"a"].as<int>();
            }
        }
        catch (const jsc::exception& e)
        {
            log::Error(_L_, E_ABORT, L"Exception code: %d\r\n", e.code());
            Assert::Fail(L"Chakra raised an exception");
        }
    }
    TEST_METHOD(ChakraObjectExternalData)
    {
        using namespace std::string_literals;

        auto chakra = jsc::details::chakra(_L_);
        Assert::IsNotNull(chakra.get(), L"Failed to initialize chakra library");

        jsc::runtime runtime;
        jsc::context ctx;

        try
        {
            // Create a runtime
            check(runtime.create(JsRuntimeAttributeNone));

            // Create a context
            check(ctx.create(runtime));

            // Make this a current context for this scope
            jsc::scoped_context sc(ctx);

            // JavaScript code
            auto script_body =
                LR"==(
					function testExternalObject(obj) {
						if(obj.boolean) {
							return obj.integer * 2;
						} else {
							return obj.integer;
						}
					}
				)=="s;

            // Execute script
            auto result1 = jsc::RunScript(script_body.c_str(), 0, L"");

            auto proto = jsc::prototype::object();

            struct ExternalData
            {
                int dword;
                bool boolean;
                std::wstring strFile;
            };
            proto.property(L"integer", [](jsc::value this_) { return ((ExternalData*)this_.data())->dword; });
            proto.property(L"strFile", [](jsc::value this_) { return ((ExternalData*)this_.data())->strFile; });
            proto.property(
                L"boolean",
                [](jsc::value this_) { return ((ExternalData*)this_.data())->boolean; },
                [](jsc::value this_, bool new_c) { ((ExternalData*)this_.data())->boolean = new_c; });
            proto.method<2>(L"print", [this](jsc::value this_, const std::wstring& message) {
                log::Info(_L_, L"%s\r\n", message.c_str());
            });

            auto obj_from_proto = jsc::value::object(proto, nullptr, [](void*) {});

            for (int i = 0; i < 10000; i++)
            {
                ExternalData data;
                data.dword = i;
                data.boolean = i % 2;
                data.strFile = L"This is a test string"s;

                obj_from_proto.set_data(&data);

                // Run JavaScript function
                auto result = jsc::value::global().call(L"testExternalObject", obj_from_proto);

                Assert::AreEqual(
                    data.boolean ? data.dword * 2 : data.dword, result.as_int(), L"Unxpected test function result");

                // log::Info(L, L"bool : %d, integer: %d, result : %d\r\n", data.boolean, data.dword, result.as_int());
            }
        }
        catch (const jsc::exception& e)
        {
            log::Error(_L_, E_ABORT, L"Exception code: %d\r\n", e.code());
            Assert::Fail(L"Chakra raised an exception");
        }
    }
};
}  // namespace Orc::Test
