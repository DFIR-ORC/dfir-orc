//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): DFIR-ORC Team
//
// What a logged REST request path may contain.
//
// A request path is built by substituting values into a template, and those values do not all come
// from the same place: the {upload.*} built-ins are generated here, while a <response> capture value
// is chosen by the server and routinely carries a session token. So the path is resolved twice - once
// against the real captures, which is what gets sent, and once against MaskCapturesForLog, which is
// what gets logged. These tests pin that second resolution.
//
#include "stdafx.h"

#include "RESTStep.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;

namespace Orc::Test {

TEST_CLASS(RestLoggedPathMaskingTest)
{
public:
    TEST_METHOD(AnOperatorCaptureIsReplacedByAMarkerNamingIt)
    {
        const std::map<std::wstring, std::wstring> captures = {{L"rt", L"ABCDEF:XYZ123"}};
        const std::map<std::wstring, std::wstring> masked = MaskCapturesForLog(captures);

        Assert::AreEqual(size_t(1), masked.size());
        Assert::AreEqual(std::wstring(L"[redacted:rt]"), masked.at(L"rt"));
    }

    // An 'upload.' key can only be an engine-generated {upload.random:...} value: a <response> name
    // under that prefix is refused at embed time. It names no secret and says which upload directory
    // a request used, so masking it would cost the diagnosis and buy nothing.
    TEST_METHOD(AnEngineRandomValueIsKept)
    {
        const std::map<std::wstring, std::wstring> captures = {{L"upload.random:hex:16", L"a1b2c3d4"}};
        Assert::AreEqual(std::wstring(L"a1b2c3d4"), MaskCapturesForLog(captures).at(L"upload.random:hex:16"));
    }

    // The point of the whole exercise: the offset and the random directory survive into the log while
    // the captured token does not.
    TEST_METHOD(BuiltInsResolveWhileACaptureIsMasked)
    {
        const UploadTemplateContext ctx {64, 32, 893, L"SystemInfo.7z", L"C:\\out\\SystemInfo.7z"};
        const std::map<std::wstring, std::wstring> captures = {
            {L"upload.random:hex:16", L"a1b2c3d4"}, {L"tusloc", L"sess-TOPSECRET-token"}};

        const std::wstring logged = ResolveUploadTemplate(
            L"/up/{upload.random:hex:16}/{upload.range_start}/{tusloc}/{upload.filename}",
            ctx,
            MaskCapturesForLog(captures));

        Assert::AreEqual(std::wstring(L"/up/a1b2c3d4/64/[redacted:tusloc]/SystemInfo.7z"), logged);
        Assert::IsTrue(logged.find(L"TOPSECRET") == std::wstring::npos, L"the captured value reached the log");
    }

    // A server chooses the captured text, so it may be crafted. None of it can reach the log at all,
    // which closes the fmt question by construction rather than by trusting the format string: the
    // masked path holds no server bytes, and it is passed to Log:: as an ARGUMENT regardless.
    TEST_METHOD(AHostileCaptureValueCannotReachTheLog)
    {
        const UploadTemplateContext ctx {0, 0, 0, L"a.7z", L"C:\\a.7z"};
        for (const wchar_t* hostile : {L"{0:>9999999}", L"{}{}{}", L"%n%s", L"../../etc", L"a\r\nX-Evil: 1"})
        {
            const std::map<std::wstring, std::wstring> captures = {{L"c", hostile}};
            const std::wstring logged = ResolveUploadTemplate(L"/a/{c}", ctx, MaskCapturesForLog(captures));

            Assert::AreEqual(std::wstring(L"/a/[redacted:c]"), logged);
        }
    }

    TEST_METHOD(AnEmptyCaptureMapMasksToAnEmptyMap)
    {
        Assert::AreEqual(size_t(0), MaskCapturesForLog({}).size());
    }
};

}  // namespace Orc::Test
