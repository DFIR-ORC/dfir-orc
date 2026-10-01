//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): DFIR-ORC Team
//
// The WebDAV upload shapes: the plain PUT and the chunked (offset-named chunks assembled by a MOVE).
// Three things are checked here, none of which needs a server: that the shapes pass Validate(), that
// the DAV paths their templates describe resolve as intended, and that the committed fixtures still
// parse to those shapes.
//
// The live half lives in tests/RestAgent. The mechanisms are exercised against the python sink
// (-Tag webdav, webdav-chunked, webdav-chunked-lex); the vm-nextcloud*.xml family and its tags only
// confirm that a real Nextcloud accepts the same dialog. What neither can catch cheaply is a fixture
// that stops describing the dialog it is named after, which is what the parsing tests below are for.
//
#include "stdafx.h"

#include "OutputSpec.h"
#include "RESTStep.h"
#include "Configuration/ConfigFile.h"
#include "Configuration/ConfigFileReader.h"
#include "Configuration/ConfigFile_Common.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;
using namespace Orc::Test;

namespace {

// The account and collection vm/setup_nextcloud.sh provisions, which the fixtures address.
constexpr auto kFilesRoot = L"/remote.php/dav/files/orc/orc-upload/";
constexpr auto kUploadsRoot = L"/remote.php/dav/uploads/orc/";

// The smallest artefact the collection rig can produce (tests/RestAgent/README.md measured a 7z
// header at 891 bytes). A chunk size at or above it would make every upload a single chunk, which
// is exactly the gap vm-nextcloud-chunked-many.xml exists to close.
constexpr uint64_t kSmallestArchiveSize = 891;

// Sets an environment variable for one test and puts it back afterwards. The fixtures take their
// address from %ORC_UPLOAD_SERVER% and Configure() refuses an unexpanded reference, so a test that
// reads one has to define it - and must not leak it into the next test.
struct ScopedEnv
{
    explicit ScopedEnv(const wchar_t* name, const wchar_t* value)
        : m_name(name)
    {
        const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
        if (size > 0)
        {
            m_previous.resize(size);
            const DWORD written = GetEnvironmentVariableW(name, m_previous.data(), size);
            m_previous.resize(written);
            m_hadPrevious = true;
        }

        SetEnvironmentVariableW(name, value);
    }

    ~ScopedEnv() { SetEnvironmentVariableW(m_name, m_hadPrevious ? m_previous.c_str() : nullptr); }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    const wchar_t* m_name;
    std::wstring m_previous;
    bool m_hadPrevious = false;
};

// The plain-PUT shape of vm-nextcloud.xml, built in code: MKCOL the collection (405 once it
// exists), then PUT the body into it.
OutputSpec::Upload MakeNextcloudPutUpload()
{
    OutputSpec::Upload upload;
    upload.Method = OutputSpec::UploadMethod::REST;
    upload.bitsMode = OutputSpec::BITSMode::HTTPS;
    upload.Mode = OutputSpec::UploadMode::Synchronous;
    upload.ServerName = L"nextcloud.example:8448";
    upload.AuthScheme = OutputSpec::UploadAuthScheme::Basic;
    upload.UserName = L"orc";
    upload.Password = L"orc-test-not-a-secret";

    RESTStep mkcol;
    mkcol.method = L"MKCOL";
    mkcol.path = kFilesRoot;
    mkcol.acceptStatus = ParseAcceptStatus(L"405");
    upload.rest.RestSteps.push_back(std::move(mkcol));

    RESTStep put;
    put.method = L"PUT";
    put.path = std::wstring(kFilesRoot) + L"{upload.filename}";
    put.hasBody = true;
    put.bodySource = L"{upload.local_path}";
    upload.rest.RestSteps.push_back(std::move(put));
    return upload;
}

// The chunked (v2) shape of vm-nextcloud-chunked.xml: MKCOL the collection, MKCOL a per-file upload
// directory, PUT one chunk per request named by its byte offset, MOVE the assembled ".file" onto the
// destination.
OutputSpec::Upload MakeNextcloudChunkedUpload()
{
    OutputSpec::Upload upload = MakeNextcloudPutUpload();
    upload.rest.RestSteps.resize(1);  // keep the collection MKCOL, replace the body step

    const std::wstring uploadDir = std::wstring(kUploadsRoot) + L"{upload.random:hex:16}";

    RESTStep mkcol;
    mkcol.method = L"MKCOL";
    mkcol.path = uploadDir;
    upload.rest.RestSteps.push_back(std::move(mkcol));

    RESTStep put;
    put.method = L"PUT";
    put.path = uploadDir + L"/{upload.range_start}";
    put.hasBody = true;
    put.bodySource = L"{upload.local_path}";
    put.chunkSize = 10 * 1024 * 1024;
    upload.rest.RestSteps.push_back(std::move(put));

    RESTStep move;
    move.method = L"MOVE";
    move.path = uploadDir + L"/.file";
    move.headers.push_back(std::wstring(L"Destination: ") + kFilesRoot + L"{upload.filename}");
    upload.rest.RestSteps.push_back(std::move(move));
    return upload;
}

UploadTemplateContext MakeContext(uint64_t offset, uint32_t length, uint64_t total)
{
    UploadTemplateContext ctx;
    ctx.offset = offset;
    ctx.length = length;
    ctx.total = total;
    ctx.filename = L"SystemInfo.7z";
    ctx.inputPath = LR"(C:\temp\SystemInfo.7z)";
    return ctx;
}

// Parse a committed fixture the way the config engine does: the <upload> schema under a 'dfir-orc'
// root, which is the element the /local= fixtures use. Common::upload() is the same registration
// Wolf::upload() extends, so the attributes the fixtures rely on are all here.
//
// Every fixture takes its address from %ORC_UPLOAD_SERVER%, which Configure() refuses unexpanded, so
// the variable is defined for the duration of the read.
HRESULT ReadFixture(const std::wstring& name, OutputSpec::Upload& upload)
{
    UnitTestHelper helper;
    const std::wstring path = helper.GetDirectoryName(__WFILE__) + L"\\..\\RestAgent\\" + name;
    ScopedEnv server(L"ORC_UPLOAD_SERVER", L"https://dav.example:8443");

    ConfigItem root;
    root.strName = L"dfir-orc";
    root.Flags = ConfigItem::MANDATORY;
    root.dwIndex = 0L;
    root.Status = ConfigItem::MISSING;

    HRESULT hr = Orc::Config::Common::upload(root, 0);
    if (FAILED(hr))
    {
        return hr;
    }

    ConfigFileReader reader;
    hr = reader.ReadConfig(path.c_str(), root);
    if (FAILED(hr))
    {
        return hr;
    }

    return upload.Configure(root.SubItems[0]);
}

const RESTStep& BodyStep(const OutputSpec::Upload& upload)
{
    const auto body = std::find_if(
        upload.rest.RestSteps.begin(), upload.rest.RestSteps.end(), [](const RESTStep& s) { return s.hasBody; });
    Assert::IsTrue(body != upload.rest.RestSteps.end(), L"the upload has no body step");
    return *body;
}

// The step list of a fixture, reduced to what makes it the dialog it is: everything except the
// comment and the connection attributes. Two fixtures that differ only in the server they talk to
// must produce the same list.
std::vector<std::wstring> StepSignature(const OutputSpec::Upload& upload)
{
    std::vector<std::wstring> signature;
    for (const RESTStep& step : upload.rest.RestSteps)
    {
        std::wstring line = step.method + L" " + step.path + L" chunk=" + std::to_wstring(step.chunkSize);
        for (const std::wstring& header : step.headers)
        {
            line += L" [" + header + L"]";
        }

        signature.push_back(line);
    }

    return signature;
}

}  // namespace

namespace Orc::Test {

TEST_CLASS(NextcloudUploadTest)
{
public:
    // ---------------------------------------------------------------------------------------
    // The shapes pass Validate()
    // ---------------------------------------------------------------------------------------

    TEST_METHOD(PlainPutShapeIsValid)
    {
        const auto v = MakeNextcloudPutUpload().Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    TEST_METHOD(ChunkedShapeIsValid)
    {
        const auto v = MakeNextcloudChunkedUpload().Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    // The shape carries a Basic credential on every request, so plain HTTP would put the Nextcloud
    // password on the wire in clear. That stays refused unless insecure="true" authorizes it.
    TEST_METHOD(PlainHttpWithCredentialRefused)
    {
        auto upload = MakeNextcloudPutUpload();
        upload.bitsMode = OutputSpec::BITSMode::HTTP;
        const auto v = upload.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"a Basic credential over http must refuse");
    }

    // Nextcloud has no anonymous DAV endpoint: Basic with no account is a configuration that can
    // only ever answer 401, and it is refused rather than attempted.
    TEST_METHOD(BasicWithoutUserRefused)
    {
        auto upload = MakeNextcloudPutUpload();
        upload.UserName.clear();
        const auto v = upload.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"Basic without a user must refuse");
    }

    // ---------------------------------------------------------------------------------------
    // The templates resolve to the paths Nextcloud expects
    // ---------------------------------------------------------------------------------------

    TEST_METHOD(PlainPutPathsResolve)
    {
        const auto upload = MakeNextcloudPutUpload();
        const auto ctx = MakeContext(0, 890, 890);
        const std::map<std::wstring, std::wstring> captures;

        Assert::AreEqual(
            std::wstring(L"/remote.php/dav/files/orc/orc-upload/"),
            ResolveUploadTemplate(upload.rest.RestSteps[0].path, ctx, captures));
        Assert::AreEqual(
            std::wstring(L"/remote.php/dav/files/orc/orc-upload/SystemInfo.7z"),
            ResolveUploadTemplate(upload.rest.RestSteps[1].path, ctx, captures));
    }

    // The property the chunked shape rests on: the per-file upload id is the SAME in every step, so
    // the MKCOL, the chunk PUTs and the MOVE all address one upload directory, while the chunk name
    // advances with the offset. A fresh id per step would scatter the chunks and the MOVE would find
    // an empty directory.
    TEST_METHOD(ChunkPathAdvancesWhileUploadIdStaysStable)
    {
        const auto upload = MakeNextcloudChunkedUpload();
        std::map<std::wstring, std::wstring> captures;
        captures[L"upload.random:hex:16"] = L"4c1cf42099ea7e63";

        const std::wstring dir = L"/remote.php/dav/uploads/orc/4c1cf42099ea7e63";
        const auto& mkcol = upload.rest.RestSteps[1];
        const auto& put = upload.rest.RestSteps[2];
        const auto& move = upload.rest.RestSteps[3];

        Assert::AreEqual(dir, ResolveUploadTemplate(mkcol.path, MakeContext(0, 0, 2048), captures));
        Assert::AreEqual(dir + L"/0", ResolveUploadTemplate(put.path, MakeContext(0, 1024, 2048), captures));
        Assert::AreEqual(dir + L"/1024", ResolveUploadTemplate(put.path, MakeContext(1024, 1024, 2048), captures));
        Assert::AreEqual(dir + L"/.file", ResolveUploadTemplate(move.path, MakeContext(1024, 1024, 2048), captures));
    }

    // The MOVE names its target in a Destination header, and deliberately as a path rather than an
    // absolute URL: an absolute one would have to carry the host, which these configurations take
    // from the environment and never hardcode. Verified against Nextcloud 35, which accepts it.
    TEST_METHOD(MoveDestinationHeaderResolves)
    {
        const auto upload = MakeNextcloudChunkedUpload();
        const auto& move = upload.rest.RestSteps[3];
        Assert::AreEqual(size_t(1), move.headers.size());

        const std::wstring resolved =
            ResolveUploadTemplate(move.headers[0], MakeContext(0, 890, 890), std::map<std::wstring, std::wstring>());
        Assert::AreEqual(
            std::wstring(L"Destination: /remote.php/dav/files/orc/orc-upload/SystemInfo.7z"), resolved);
    }

    // ---------------------------------------------------------------------------------------
    // The committed fixtures still describe those shapes
    // ---------------------------------------------------------------------------------------

    TEST_METHOD(PlainFixtureParsesToThePutShape)
    {
        OutputSpec::Upload upload;
        Assert::IsTrue(SUCCEEDED(ReadFixture(L"vm-nextcloud.xml", upload)), L"fixture did not parse");

        Assert::IsTrue(upload.Method == OutputSpec::UploadMethod::REST);
        Assert::IsTrue(upload.bitsMode == OutputSpec::BITSMode::HTTPS, L"the fixture must stay on https");
        Assert::IsTrue(upload.AuthScheme == OutputSpec::UploadAuthScheme::Basic);
        Assert::IsFalse(upload.rest.Insecure, L"a pinned fixture must not need insecure");
        Assert::IsFalse(upload.rest.ServerCertPins.empty(), L"the fixture must pin the server certificate");

        Assert::AreEqual(size_t(2), upload.rest.RestSteps.size());
        Assert::AreEqual(std::wstring(L"MKCOL"), upload.rest.RestSteps[0].method);
        Assert::AreEqual(std::wstring(kFilesRoot), upload.rest.RestSteps[0].path);
        Assert::IsTrue(upload.rest.RestSteps[0].acceptStatus.count(405) == 1, L"MKCOL must accept 405");

        const RESTStep& body = BodyStep(upload);
        Assert::AreEqual(std::wstring(L"PUT"), body.method);
        Assert::AreEqual(std::wstring(kFilesRoot) + L"{upload.filename}", body.path);
        Assert::AreEqual(uint64_t(0), body.chunkSize, L"the plain shape sends the whole file in one request");

        const auto v = upload.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    TEST_METHOD(ChunkedFixtureParsesToTheChunkedShape)
    {
        OutputSpec::Upload upload;
        Assert::IsTrue(
            SUCCEEDED(ReadFixture(L"vm-nextcloud-chunked.xml", upload)), L"fixture did not parse");

        Assert::AreEqual(size_t(4), upload.rest.RestSteps.size());
        Assert::AreEqual(std::wstring(L"MKCOL"), upload.rest.RestSteps[0].method);
        Assert::AreEqual(std::wstring(L"MKCOL"), upload.rest.RestSteps[1].method);
        Assert::AreEqual(std::wstring(L"PUT"), upload.rest.RestSteps[2].method);
        Assert::AreEqual(std::wstring(L"MOVE"), upload.rest.RestSteps[3].method);

        // The chunk name must be the offset: that is what makes several requests reassemble into one
        // file, and a path without it would overwrite the same chunk on every request.
        const RESTStep& body = BodyStep(upload);
        Assert::IsTrue(
            body.path.find(L"{upload.range_start}") != std::wstring::npos,
            L"the chunk path must be named by the chunk offset");
        Assert::IsTrue(body.chunkSize > 0, L"the chunked shape must set chunk_size");

        // All three upload-directory steps must name the SAME random token, or they address three
        // different directories once it is resolved.
        for (const size_t i : {size_t(1), size_t(2), size_t(3)})
        {
            Assert::IsTrue(
                upload.rest.RestSteps[i].path.find(L"{upload.random:hex:16}") != std::wstring::npos,
                L"every upload-directory step must use the same random token");
        }

        Assert::AreEqual(size_t(1), upload.rest.RestSteps[3].headers.size(), L"MOVE needs a Destination header");
        Assert::IsTrue(
            upload.rest.RestSteps[3].headers[0].find(L"Destination: /remote.php/dav/files/") == 0,
            L"MOVE must target the account's files root by path");

        const auto v = upload.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    // vm-nextcloud-chunked-many.xml exists only to make the rig's small archives span several
    // chunks, so that the live scenario's byte-exact comparison actually proves Nextcloud reassembled
    // them in numeric order. A chunk size raised past the smallest archive would silently turn it back
    // into the single-chunk test it was written to replace.
    TEST_METHOD(ManyChunkFixtureStillSplitsTheSmallestArchive)
    {
        OutputSpec::Upload upload;
        Assert::IsTrue(
            SUCCEEDED(ReadFixture(L"vm-nextcloud-chunked-many.xml", upload)), L"fixture did not parse");

        const RESTStep& body = BodyStep(upload);
        Assert::IsTrue(body.chunkSize > 0, L"the fixture must set chunk_size");

        // Past ten chunks the offsets no longer sort the same way as text ("128" precedes "64"), which
        // is the distinction the live scenario relies on.
        const uint64_t chunks = (kSmallestArchiveSize + body.chunkSize - 1) / body.chunkSize;
        Assert::IsTrue(chunks > 10, L"chunk_size is too large to exercise the reassembly order");
    }
};

// The vm-webdav*.xml family, which exercises the same mechanisms against the python sink rather than
// against Nextcloud. Those scenarios are where MKCOL/405, a control step after the body step, MOVE,
// per-chunk paths and multi-step {upload.random} are actually tested; the Nextcloud ones only confirm
// a real server accepts the dialog.
TEST_CLASS(WebDavFixtureTest)
{
public:
    // A control step after the body step, which no other sink fixture has: the body is staged under a
    // .part name and only gets its final name once it is complete.
    TEST_METHOD(WebDavFixtureMovesTheBodyIntoPlaceAfterwards)
    {
        OutputSpec::Upload upload;
        Assert::IsTrue(SUCCEEDED(ReadFixture(L"vm-webdav.xml", upload)), L"fixture did not parse");

        Assert::AreEqual(size_t(3), upload.rest.RestSteps.size());
        Assert::AreEqual(std::wstring(L"MKCOL"), upload.rest.RestSteps[0].method);
        Assert::IsTrue(upload.rest.RestSteps[0].acceptStatus.count(405) == 1, L"MKCOL must accept 405");
        Assert::IsTrue(upload.rest.RestSteps[1].hasBody, L"the second step must be the body step");
        Assert::AreEqual(std::wstring(L"MOVE"), upload.rest.RestSteps[2].method);
        Assert::AreEqual(size_t(1), upload.rest.RestSteps[2].headers.size(), L"MOVE needs a Destination header");
    }

    TEST_METHOD(WebDavChunkedFixtureNamesChunksByOffset)
    {
        OutputSpec::Upload upload;
        Assert::IsTrue(SUCCEEDED(ReadFixture(L"vm-webdav-chunked.xml", upload)), L"fixture did not parse");

        const RESTStep& body = BodyStep(upload);
        Assert::IsTrue(
            body.path.find(L"{upload.range_start}") != std::wstring::npos,
            L"each chunk must go to its own offset-named path");

        const uint64_t chunks = (kSmallestArchiveSize + body.chunkSize - 1) / body.chunkSize;
        Assert::IsTrue(chunks > 10, L"chunk_size is too large to exercise the reassembly order");
    }

    // The negative scenario is a control: it differs from the positive one ONLY in the sink it is
    // pointed at, which the runner supplies through %ORC_UPLOAD_SERVER%. If an edit to one were not
    // made to the other, the pair would stop comparing the same upload and the corruption it detects
    // could come from the dialog rather than from the assembly order.
    TEST_METHOD(LexicographicFixtureDiffersOnlyInTheSinkItTalksTo)
    {
        OutputSpec::Upload numeric;
        OutputSpec::Upload lexicographic;
        Assert::IsTrue(SUCCEEDED(ReadFixture(L"vm-webdav-chunked.xml", numeric)), L"fixture did not parse");
        Assert::IsTrue(SUCCEEDED(ReadFixture(L"vm-webdav-chunked-lex.xml", lexicographic)), L"fixture did not parse");

        const std::vector<std::wstring> a = StepSignature(numeric);
        const std::vector<std::wstring> b = StepSignature(lexicographic);
        Assert::AreEqual(a.size(), b.size(), L"the two fixtures no longer have the same number of steps");
        for (size_t i = 0; i < a.size(); ++i)
        {
            Assert::AreEqual(a[i], b[i], L"the two fixtures no longer describe the same upload");
        }
    }
};

}  // namespace Orc::Test
