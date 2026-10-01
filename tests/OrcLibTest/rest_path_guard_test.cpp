//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): DFIR-ORC Team
//
// The guards a REST request path and header line pass through AFTER placeholder substitution.
//
// A <response> capture value is chosen by the server, and ResolveUploadTemplate substitutes it
// blindly, so the only place that knows whether a value is about to become part of a path or a
// header is the call site. These are the checks it runs there. A path cannot simply be
// percent-encoded instead: a captured TUS Location IS a path, so encoding its separators would
// break the very dialog it serves.
//
#include "stdafx.h"

#include "RESTStep.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;

namespace Orc::Test {

TEST_CLASS(RestDestinationGuardTest)
{
public:
    TEST_METHOD(OrdinaryPathsPass)
    {
        Assert::IsTrue(IsSafeRequestPath(L"/public.php/dav/files/token/archive.7z"));
        Assert::IsTrue(IsSafeRequestPath(L"/remote.php/dav/uploads/orc/abc123/0"));
    }

    // A real TUS Location path must keep working: this guard must not become a blanket encoder.
    TEST_METHOD(TusLocationPathPasses)
    {
        Assert::IsTrue(IsSafeRequestPath(L"/files/24e533e02ec3bc40c387f1a0e460e216?session=abc"));
    }

    TEST_METHOD(TraversalIsRejectedInEveryPosition)
    {
        Assert::IsFalse(IsSafeRequestPath(L"/../etc/passwd"));
        Assert::IsFalse(IsSafeRequestPath(L"/a/../../b"));
        Assert::IsFalse(IsSafeRequestPath(L"/a/.."));
        Assert::IsFalse(IsSafeRequestPath(L"/.."));
    }

    // A single dot segment and a dot inside a name are both legitimate.
    TEST_METHOD(DotsThatAreNotTraversalPass)
    {
        Assert::IsTrue(IsSafeRequestPath(L"/a/archive.7z"));
        Assert::IsTrue(IsSafeRequestPath(L"/a/..b/c"));
        Assert::IsTrue(IsSafeRequestPath(L"/a/b../c"));
    }

    TEST_METHOD(BackslashIsRejected)
    {
        Assert::IsFalse(IsSafeRequestPath(L"/a\\..\\b"));
    }

    TEST_METHOD(FragmentIsRejected)
    {
        Assert::IsFalse(IsSafeRequestPath(L"/a#b"));
    }

    TEST_METHOD(ControlCharactersAreRejected)
    {
        Assert::IsFalse(IsSafeRequestPath(L"/a\r\nX-Evil: 1"));
        Assert::IsFalse(IsSafeRequestPath(L"/a\x01b"));
    }

    TEST_METHOD(EmptyOrRelativePathIsRejected)
    {
        Assert::IsFalse(IsSafeRequestPath(L""));
        Assert::IsFalse(IsSafeRequestPath(L"a/b"));
    }

    TEST_METHOD(OrdinaryHeaderLinesPass)
    {
        Assert::IsTrue(IsSafeHeaderLine(L"Destination: /remote.php/dav/files/orc/x.7z"));
        Assert::IsTrue(IsSafeHeaderLine(L"Content-Type: text/plain; charset=utf-8"));
    }

    TEST_METHOD(HeaderWithoutAColonIsRejected)
    {
        Assert::IsFalse(IsSafeHeaderLine(L"NotAHeader"));
    }

    TEST_METHOD(HeaderWithControlCharactersIsRejected)
    {
        Assert::IsFalse(IsSafeHeaderLine(L"X-A: b\r\nX-Evil: 1"));
    }

    TEST_METHOD(HeaderWithAnEmptyNameIsRejected)
    {
        Assert::IsFalse(IsSafeHeaderLine(L": value"));
    }
};

// A server-controlled capture must never be able to smuggle a placeholder that a later pass expands.
// ResolveUploadTemplate is single-pass by construction; this locks it so a refactor cannot reopen it.
TEST_CLASS(RestTemplateReinjectionTest)
{
public:
    TEST_METHOD(ACaptureCarryingAPlaceholderIsNotExpanded)
    {
        const UploadTemplateContext ctx {0, 0, 0, L"archive.7z", L"C:\\out\\archive.7z"};
        const std::map<std::wstring, std::wstring> captures = {{L"evil", L"{upload.local_path}"}, {L"x", L"ok"}};

        Assert::AreEqual(std::wstring(L"/a/{upload.local_path}/ok"),
                         ResolveUploadTemplate(L"/a/{evil}/{x}", ctx, captures));
    }
};

}  // namespace Orc::Test
