//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): DFIR-ORC Team
//
// The form-login primitives: the bounded <input> scanner, the urlencoder, and the
// request-path / header-line guards. All three read attacker-controlled input, so the tests
// below are mostly hostile cases: the happy path is one test, the bounds are the rest.
//
#include "stdafx.h"

#include "RESTStep.h"
#include "RESTCapture.h"
#include "OutputSpec.h"
#include "MemoryStream.h"
#include "Configuration/ConfigFile.h"
#include "Configuration/ConfigFileReader.h"
#include "Configuration/ConfigFile_Common.h"
#include "Network/Http/HttpClient.h"
#include "Text/Iconv.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;

namespace {

// The shape the fork serves: three hidden inputs in one form.
constexpr auto kForkPage =
    L"<html><body><form method=\"post\">"
    L"<input type=\"hidden\" name=\"requesttoken\" value=\"ABCDEF:XYZ123\">"
    L"<input type=\"hidden\" name=\"sharingToken\" value=\"oeErC634izAMr3n\">"
    L"<input type=\"hidden\" name=\"sharingType\" value=\"3\">"
    L"</form></body></html>";

// The fork login dialog end to end: a session="once" GET that captures a form_input token, a
// session="once" POST that logs in with the captured token plus a literal password, and the
// ordinary per-file PUT that actually sends the collected data. This is the config-validity
// baseline for the whole class: it must VALIDATE (Verdict::Ok), not just parse.
constexpr auto kForkDialogXml =
    L"<dfir-orc><upload method=\"REST\" server=\"https://host\" operation=\"copy\">"
    L"<rest http_method=\"GET\" path=\"/s/tok\" session=\"once\">"
    L"<response form_input=\"requesttoken\" name=\"rt\"/>"
    L"</rest>"
    L"<rest http_method=\"POST\" path=\"/s/tok\" session=\"once\">"
    L"<form field=\"password\" value=\"pw\"/>"
    L"<form field=\"requestToken\" value=\"{rt}\"/>"
    L"</rest>"
    L"<rest http_method=\"PUT\" path=\"/d/{upload.filename}\" body=\"{upload.local_path}\"/>"
    L"</upload></dfir-orc>";

// Registers the <upload> schema (as Common::upload()/Wolf::upload() do) under a fresh 'dfir-orc'
// root, the same registration ReadFixture in webdav_upload_test.cpp relies on. Asserts rather than
// propagating a failure: a schema registration failure means the test fixture itself is broken.
void RegisterUploadRoot(ConfigItem& root)
{
    root.strName = L"dfir-orc";
    root.Flags = ConfigItem::MANDATORY;
    root.dwIndex = 0L;
    root.Status = ConfigItem::MISSING;

    Assert::IsTrue(SUCCEEDED(Orc::Config::Common::upload(root, 0)), L"Common::upload() schema registration failed");
}

// Sets an environment variable for one test and puts it back afterwards. Same pattern as
// webdav_upload_test.cpp's ScopedEnv (anonymous-namespace local to each translation unit, so it is
// redeclared here rather than shared).
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

// The shape stock Nextcloud 35 and the fork both serve, trimmed from the real 26 KB page: no password
// form at all, the CSRF token on a <head> attribute, and "hidden inputs" that are 'id'-keyed
// initial-state blobs of base64 JSON carrying no 'name' - which is exactly why the <input> scan finds
// nothing here. The token is shortened; its <base64>:<base64> shape is kept.
constexpr auto kNextcloudPage =
    L"<!DOCTYPE html>\n"
    L"<html class=\"ng-csp\" data-placeholder-focus=\"false\" lang=\"en\" translate=\"no\">\n"
    L"<head data-requesttoken=\"lTPzSolp:zWKfG6I\">\n"
    L"<title>Nextcloud</title>\n"
    L"<input type=\"hidden\" id=\"initial-state-files_sharing-sharingToken\" value=\"ImE0aVpEeXlraXpZY1JwYSI=\">\n"
    L"<input type=\"hidden\" id=\"initial-state-core-publicShareAuth\" value=\"eyJzaGFyZVR5cGUiOjN9\">\n"
    L"</head><body><div id=\"app\"></div></body></html>";

}  // namespace

namespace Orc::Test {

TEST_CLASS(RestFormInputScannerTest)
{
public:
    TEST_METHOD(ReadsTheForkPage)
    {
        Assert::AreEqual(std::wstring(L"ABCDEF:XYZ123"),
                         ExtractFormInputValue(kForkPage, L"requesttoken").value_or(L"<absent>"));
        Assert::AreEqual(std::wstring(L"oeErC634izAMr3n"),
                         ExtractFormInputValue(kForkPage, L"sharingToken").value_or(L"<absent>"));
        Assert::AreEqual(std::wstring(L"3"),
                         ExtractFormInputValue(kForkPage, L"sharingType").value_or(L"<absent>"));
    }

    TEST_METHOD(AbsentInputIsNullopt)
    {
        Assert::IsFalse(ExtractFormInputValue(kForkPage, L"nosuchfield").has_value());
    }

    TEST_METHOD(EmptyBodyIsNullopt)
    {
        Assert::IsFalse(ExtractFormInputValue(L"", L"requesttoken").has_value());
    }

    TEST_METHOD(ValueBeforeNameIsFound)
    {
        Assert::AreEqual(std::wstring(L"v"),
                         ExtractFormInputValue(L"<input value=\"v\" name=\"n\">", L"n").value_or(L"<absent>"));
    }

    TEST_METHOD(SingleQuotesAreAccepted)
    {
        Assert::AreEqual(std::wstring(L"v"),
                         ExtractFormInputValue(L"<input name='n' value='v'>", L"n").value_or(L"<absent>"));
    }

    // XHTML self-closing form. The '/' must not become part of the value. (Review Focus 2)
    TEST_METHOD(SelfClosingTagIsParsed)
    {
        Assert::AreEqual(std::wstring(L"y"),
                         ExtractFormInputValue(L"<input name=\"x\" value=\"y\" />", L"x").value_or(L"<absent>"));
    }

    TEST_METHOD(EmptyValueIsFoundNotAbsent)
    {
        const std::optional<std::wstring> v = ExtractFormInputValue(L"<input name=\"n\" value=\"\">", L"n");
        Assert::IsTrue(v.has_value());
        Assert::AreEqual(std::wstring(L""), *v);
    }

    // The scan stops as soon as every requested name is resolved, so the FIRST occurrence wins and
    // a later duplicate is never read. An attacker who can only append to the page therefore cannot
    // change what we capture.
    TEST_METHOD(FirstOccurrenceWins)
    {
        const std::wstring html = L"<input name=\"n\" value=\"first\"><input name=\"n\" value=\"second\">";
        Assert::AreEqual(std::wstring(L"first"), ExtractFormInputValue(html, L"n").value_or(L"<absent>"));
    }

    TEST_METHOD(AllThreeNamesComeFromOnePass)
    {
        const std::map<std::wstring, std::wstring> values =
            ExtractFormInputValues(kForkPage, {L"requesttoken", L"sharingToken", L"sharingType"});

        Assert::AreEqual(size_t(3), values.size());
        Assert::AreEqual(std::wstring(L"ABCDEF:XYZ123"), values.at(L"requesttoken"));
        Assert::AreEqual(std::wstring(L"oeErC634izAMr3n"), values.at(L"sharingToken"));
        Assert::AreEqual(std::wstring(L"3"), values.at(L"sharingType"));
    }

    TEST_METHOD(OnlyResolvedNamesAppear)
    {
        const std::map<std::wstring, std::wstring> values =
            ExtractFormInputValues(kForkPage, {L"sharingType", L"nosuchfield"});

        Assert::AreEqual(size_t(1), values.size());
        Assert::AreEqual(std::wstring(L"3"), values.at(L"sharingType"));
        Assert::IsTrue(values.find(L"nosuchfield") == values.end());
    }

    // No names requested means the body is never examined: a step with no form_input capture must
    // not pay for a scan, and must not touch server-controlled bytes it has no use for.
    TEST_METHOD(NoNamesRequestedScansNothing)
    {
        std::wstring huge = L"<input name=\"n\" value=\"v\">";
        huge.append(400000, L'x');
        Assert::IsTrue(ExtractFormInputValues(huge, {}).empty());
    }

    // Once every requested name is resolved the scan stops, so rubbish past that point - including
    // a tag that would otherwise trip a bound - is never reached.
    TEST_METHOD(StopsScanningOnceEveryNameIsResolved)
    {
        std::wstring html = L"<input name=\"n\" value=\"v\">";
        html += L"<input name=\"later\" value=\"";
        html.append(9000, L'a');  // would trip kMaxInputTagChars if it were ever scanned
        html += L"\">";

        const std::map<std::wstring, std::wstring> values = ExtractFormInputValues(html, {L"n"});
        Assert::AreEqual(size_t(1), values.size());
        Assert::AreEqual(std::wstring(L"v"), values.at(L"n"));
    }

    TEST_METHOD(UnquotedValueIsSkipped)
    {
        Assert::IsFalse(ExtractFormInputValue(L"<input name=n value=v>", L"n").has_value());
    }

    TEST_METHOD(InputInsideACommentIsIgnored)
    {
        const std::wstring html = L"<!-- <input name=\"n\" value=\"evil\"> --><input name=\"n\" value=\"good\">";
        Assert::AreEqual(std::wstring(L"good"), ExtractFormInputValue(html, L"n").value_or(L"<absent>"));
    }

    TEST_METHOD(UnterminatedTagIsRejected)
    {
        Assert::IsFalse(ExtractFormInputValue(L"<input name=\"n\" value=\"v\"", L"n").has_value());
    }

    TEST_METHOD(OverlongTagIsRejected)
    {
        std::wstring html = L"<input name=\"n\" value=\"";
        html.append(9000, L'a');
        html.append(L"\">");
        Assert::IsFalse(ExtractFormInputValue(html, L"n").has_value());
    }

    TEST_METHOD(TooManyTagsIsRejected)
    {
        std::wstring html;
        for (int i = 0; i < 2000; ++i)
        {
            html += L"<input name=\"filler\" value=\"x\">";
        }

        html += L"<input name=\"n\" value=\"v\">";
        Assert::IsFalse(ExtractFormInputValue(html, L"n").has_value());
    }

    TEST_METHOD(AttributeNamesAreCaseInsensitiveValuesAreNot)
    {
        Assert::AreEqual(std::wstring(L"v"),
                         ExtractFormInputValue(L"<INPUT NAME=\"n\" VALUE=\"v\">", L"n").value_or(L"<absent>"));
        Assert::IsFalse(ExtractFormInputValue(L"<input name=\"n\" value=\"v\">", L"N").has_value());
    }

    // The reason the attributes are read in ONE sequential walk rather than by finding each name
    // anywhere in the tag: an earlier attribute's quoted value must not be able to impersonate a later
    // attribute. Here 'type' has "name='requesttoken' value='ATTACKER'" INSIDE its own quotes; the tag's
    // real attributes are name="other" / value="ignored".
    TEST_METHOD(AQuotedValueCannotImpersonateAnotherAttribute)
    {
        const std::wstring html = L"<input type=\" name='requesttoken' value='ATTACKER'\" "
                                  L"name=\"other\" value=\"ignored\">";

        Assert::IsFalse(ExtractFormInputValue(html, L"requesttoken").has_value());
        Assert::AreEqual(std::wstring(L"ignored"), ExtractFormInputValue(html, L"other").value_or(L"<absent>"));
    }

    // Within ONE tag, a repeated attribute resolves to the first, which is what a browser does. (Between
    // tags, FirstOccurrenceWins covers the same rule.)
    TEST_METHOD(ARepeatedAttributeResolvesToTheFirst)
    {
        const std::wstring html = L"<input name=\"first\" name=\"second\" value=\"v\">";

        Assert::AreEqual(std::wstring(L"v"), ExtractFormInputValue(html, L"first").value_or(L"<absent>"));
        Assert::IsFalse(ExtractFormInputValue(html, L"second").has_value());
    }

    // Attributes separate on HTML's ASCII whitespace only. A no-break space is not a separator - a
    // browser does not split on it either - so " value" is one attribute name that matches nothing,
    // and the tag carries no usable 'value'. Reading it as a separator would mean parsing the tag into
    // different attributes than the browser parsed it into.
    TEST_METHOD(ANoBreakSpaceIsNotAnAttributeSeparator)
    {
        Assert::IsFalse(ExtractFormInputValue(L"<input name=\"n\" value=\"v\">", L"n").has_value());
        Assert::AreEqual(std::wstring(L"v"),
                         ExtractFormInputValue(L"<input name=\"n\" value=\"v\">", L"n").value_or(L"<absent>"));
    }

    TEST_METHOD(BasicEntitiesAreDecodedOnce)
    {
        Assert::AreEqual(std::wstring(L"a&b"),
                         ExtractFormInputValue(L"<input name=\"n\" value=\"a&amp;b\">", L"n").value_or(L"<absent>"));

        // One pass only: "&amp;lt;" becomes "&lt;" and must NOT become "<".
        Assert::AreEqual(std::wstring(L"&lt;"),
                         ExtractFormInputValue(L"<input name=\"n\" value=\"&amp;lt;\">", L"n").value_or(L"<absent>"));
    }
};

TEST_CLASS(RestFormEncoderTest)
{
public:
    TEST_METHOD(UnreservedCharactersPassThrough)
    {
        Assert::AreEqual(std::string("aZ09-._~"), UrlEncodeFormValue(L"aZ09-._~").value());
    }

    // Without this, a captured "3&password=known" would append a field, and a server-side parser
    // taking the last occurrence would authenticate a password the SERVER chose.
    TEST_METHOD(StructuralCharactersAreEncoded)
    {
        Assert::AreEqual(std::string("3%26password%3Dknown"), UrlEncodeFormValue(L"3&password=known").value());
    }

    TEST_METHOD(SpaceBecomesPercent20)
    {
        Assert::AreEqual(std::string("a%20b"), UrlEncodeFormValue(L"a b").value());
    }

    TEST_METHOD(PercentIsItselfEncoded)
    {
        Assert::AreEqual(std::string("100%25"), UrlEncodeFormValue(L"100%").value());
    }

    TEST_METHOD(ColonInATokenIsEncoded)
    {
        Assert::AreEqual(std::string("ABCDEF%3AXYZ123"), UrlEncodeFormValue(L"ABCDEF:XYZ123").value());
    }

    TEST_METHOD(EmptyValueEncodesToEmpty)
    {
        Assert::AreEqual(std::string(""), UrlEncodeFormValue(L"").value());
    }

    // An operator password with an accent must survive as UTF-8, not be mangled. (Review Focus 5)
    TEST_METHOD(NonAsciiIsUtf8ThenPercentEncoded)
    {
        Assert::AreEqual(std::string("caf%C3%A9"), UrlEncodeFormValue(L"caf\u00e9").value());
    }

    TEST_METHOD(BodyJoinsFieldsWithAmpersand)
    {
        const std::vector<std::pair<std::wstring, std::wstring>> fields = {
            {L"password", L"s3cr3t"}, {L"requestToken", L"ABCDEF:XYZ123"}, {L"sharingType", L"3"}};

        Assert::AreEqual(std::string("password=s3cr3t&requestToken=ABCDEF%3AXYZ123&sharingType=3"),
                         BuildFormBody(fields).value());
    }

    TEST_METHOD(BodyOfNoFieldsIsEmpty)
    {
        Assert::AreEqual(std::string(""), BuildFormBody({}).value());
    }
};

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

    // A captured value is chosen by the server and may carry fmt syntax. It must stay inert data
    // everywhere it lands: never a format string (the Global Constraints forbid a non-literal in a
    // Log:: format position), and structurally encoded in a form body.
    TEST_METHOD(BracesInACaptureStayLiteral)
    {
        const UploadTemplateContext ctx {0, 0, 0, L"archive.7z", L"C:\\out\\archive.7z"};
        const std::map<std::wstring, std::wstring> captures = {{L"t", L"{0:>9999999}"}};

        // Substituted once, verbatim, and not re-parsed.
        Assert::AreEqual(std::wstring(L"/a/{0:>9999999}"), ResolveUploadTemplate(L"/a/{t}", ctx, captures));

        // In a form body it is percent-encoded, so it cannot be structure there either.
        Assert::AreEqual(std::string("%7B0%3A%3E9999999%7D"), UrlEncodeFormValue(L"{0:>9999999}").value());
    }
};

// Parse an <upload> config from an in-memory XML string through the real config engine. No temp
// file: ConfigFileReader's stream overload (ConfigFileReader.h:37) is fed a MemoryStream holding
// the XML as UTF-8 bytes, rewound before the read (MemoryStream pattern per hash_stream_test.cpp:134).
OutputSpec::Upload ParseUploadConfig(const std::wstring& xml)
{
    ConfigItem root;
    RegisterUploadRoot(root);

    std::string utf8 = ToUtf8(xml);

    auto stream = std::make_shared<MemoryStream>();
    Assert::IsTrue(stream->OpenForReadWrite(static_cast<DWORD>(utf8.size())) == S_OK, L"OpenForReadWrite failed");

    ULONGLONG written = 0;
    Assert::IsTrue(stream->Write(utf8.data(), utf8.size(), &written) == S_OK, L"MemoryStream write failed");
    Assert::AreEqual(utf8.size(), static_cast<size_t>(written));

    Assert::IsTrue(stream->SetFilePointer(0, FILE_BEGIN, nullptr) == S_OK, L"MemoryStream rewind failed");

    ConfigFileReader reader;
    Assert::IsTrue(SUCCEEDED(reader.ReadConfig(stream, root)), L"ReadConfig(stream) failed");

    OutputSpec::Upload upload;
    Assert::IsTrue(SUCCEEDED(upload.Configure(root.SubItems[0])), L"upload.Configure() failed");
    return upload;
}

// The filename twin of the helper above, for Task 10's committed fixture under tests/RestAgent/.
// Not used by this task's test; kept as a clean seam so Task 10 does not have to rediscover the shape.
OutputSpec::Upload ParseUploadConfigFile(const std::wstring& path)
{
    ConfigItem root;
    RegisterUploadRoot(root);

    ConfigFileReader reader;
    Assert::IsTrue(SUCCEEDED(reader.ReadConfig(path.c_str(), root)), L"ReadConfig(path) failed");

    OutputSpec::Upload upload;
    Assert::IsTrue(SUCCEEDED(upload.Configure(root.SubItems[0])), L"upload.Configure() failed");
    return upload;
}

// Config parsing for the form-login shape: a session="once" GET capturing a form_input, a
// session="once" POST with two <form> fields (one referencing that capture), and the ordinary
// per-file PUT. Confirms the schema and ParseRestStepNode produce the expected RESTStep shape.
TEST_CLASS(RestFormSchemaTest)
{
public:
    TEST_METHOD(FormLoginConfigParsesToTheExpectedShape)
    {
        const std::wstring xml =
            // NOTE: "https://host", not the brief's literal "host" - ApplyServerScheme() requires a
            // scheme prefix (http/https/file), so a bare hostname fails upload.Configure() before REST
            // steps are ever parsed. Unrelated to the form-login shape under test; see the task report.
            L"<dfir-orc><upload method=\"REST\" server=\"https://host\" operation=\"copy\">"
            L"<rest http_method=\"GET\" path=\"/s/tok\" session=\"once\">"
            L"<response form_input=\"requesttoken\" name=\"rt\"/>"
            L"</rest>"
            L"<rest http_method=\"POST\" path=\"/s/tok\" session=\"once\">"
            L"<form field=\"password\" value=\"pw\"/>"
            L"<form field=\"requestToken\" value=\"{rt}\"/>"
            L"</rest>"
            L"<rest http_method=\"PUT\" path=\"/d/{upload.filename}\" body=\"{upload.local_path}\"/>"
            L"</upload></dfir-orc>";

        const OutputSpec::Upload upload = Orc::Test::ParseUploadConfig(xml);

        Assert::AreEqual(size_t(3), upload.rest.RestSteps.size());

        const RESTStep& get = upload.rest.RestSteps[0];
        Assert::IsTrue(get.runOnce);
        Assert::AreEqual(size_t(1), get.captures.size());
        Assert::IsTrue(get.captures[0].source == RESTResponseCapture::Source::FormInput);
        Assert::AreEqual(std::wstring(L"requesttoken"), get.captures[0].key);
        Assert::AreEqual(std::wstring(L"rt"), get.captures[0].name);

        const RESTStep& post = upload.rest.RestSteps[1];
        Assert::IsTrue(post.runOnce);
        Assert::IsFalse(post.hasBody);  // hasBody still means the FILE body
        Assert::AreEqual(size_t(2), post.formFields.size());
        Assert::AreEqual(std::wstring(L"password"), post.formFields[0].field);
        Assert::AreEqual(std::wstring(L"pw"), post.formFields[0].value);
        Assert::AreEqual(std::wstring(L"requestToken"), post.formFields[1].field);
        Assert::AreEqual(std::wstring(L"{rt}"), post.formFields[1].value);

        const RESTStep& put = upload.rest.RestSteps[2];
        Assert::IsFalse(put.runOnce);
        Assert::IsTrue(put.hasBody);
        Assert::AreEqual(size_t(0), put.formFields.size());
    }

    // The parser half of the session="once" typo rule: 'session' carrying anything but "once" must set
    // hasUnknownSessionToken rather than silently falling through to "per file" (which would replay a
    // login's credential on every upload). The refusal itself (ValidateRestStep) is tested separately in
    // RestFormValidationTest; this test is only about what Configure() records.
    TEST_METHOD(UnrecognizedSessionValueIsRecordedByTheParser)
    {
        const std::wstring xml =
            L"<dfir-orc><upload method=\"REST\" server=\"https://host\" operation=\"copy\">"
            L"<rest http_method=\"GET\" path=\"/s/tok\" session=\"onse\"/>"
            L"<rest http_method=\"PUT\" path=\"/d/{upload.filename}\" body=\"{upload.local_path}\"/>"
            L"</upload></dfir-orc>";

        const OutputSpec::Upload upload = Orc::Test::ParseUploadConfig(xml);

        Assert::AreEqual(size_t(2), upload.rest.RestSteps.size());
        Assert::IsTrue(upload.rest.RestSteps[0].hasUnknownSessionToken);
        Assert::IsFalse(upload.rest.RestSteps[0].runOnce);  // a typo must NOT fall through to "per file"
    }

    // The parser half of the "exactly one <response> source" rule: a <response> naming both 'header' and
    // 'form_input' must set bothSourcesGiven. The refusal itself (ValidateRestStep) is tested separately
    // in RestFormValidationTest, where it sets the flag by hand; this test is only about the sourceCount
    // computation in Configure() that a hand-set flag cannot exercise.
    TEST_METHOD(TwoResponseSourcesAreRecordedByTheParser)
    {
        const std::wstring xml =
            L"<dfir-orc><upload method=\"REST\" server=\"https://host\" operation=\"copy\">"
            L"<rest http_method=\"GET\" path=\"/s/tok\">"
            L"<response header=\"X-Token\" form_input=\"requesttoken\" name=\"rt\"/>"
            L"</rest>"
            L"<rest http_method=\"PUT\" path=\"/d/{upload.filename}\" body=\"{upload.local_path}\"/>"
            L"</upload></dfir-orc>";

        const OutputSpec::Upload upload = Orc::Test::ParseUploadConfig(xml);

        Assert::AreEqual(size_t(1), upload.rest.RestSteps[0].captures.size());
        Assert::IsTrue(upload.rest.RestSteps[0].captures[0].bothSourcesGiven);
    }
};

// The upload a step (or a small set of steps) is validated inside. upload.rest.RestSteps is a
// public, writable member (the same path ValidateRestStep reads), so it is assigned directly
// rather than through an accessor.
//
// ServerName/bitsMode are set the way upload.Configure() would leave them for server="https://host"
// (ApplyServerScheme strips the scheme into bitsMode and leaves the bare host in ServerName): a
// raw "https://host" string in ServerName would make SplitHostPort misparse the "//host" remainder
// as a port and refuse for an unrelated reason, which is exactly the trap this helper exists to avoid.
bool RefusesSteps(std::vector<RESTStep> steps)
{
    OutputSpec::Upload upload;
    upload.Method = OutputSpec::UploadMethod::REST;
    upload.ServerName = L"host";
    upload.bitsMode = OutputSpec::BITSMode::HTTPS;
    upload.rest.RestSteps = std::move(steps);
    return upload.Validate().verdict == OutputSpec::Upload::Verdict::Refuse;
}

// The single-step case, in terms of the primitive above. Because the step under test is the ONLY
// step in RestSteps, ValidateRestSteps' per-step loop calls ValidateRestStep(step) and returns its
// verdict immediately if it is not Ok - before the "needs one body step" check ever runs. So a
// Refuse result here can only come from the step's own validation, never from the list being
// otherwise incomplete. A test that needs to prove ACCEPTANCE of a non-body step (so the "needs one
// body step" rule does not mask what is being tested) uses RefusesSteps directly with a second,
// valid body step alongside it.
bool RefusesStep(const RESTStep& step)
{
    return RefusesSteps({step});
}

// The fork login dialog, which must VALIDATE: every other test in this class is a refusal, so
// without this one a validator that refused everything would pass the whole class.
//
// ParseUploadConfig() already asserts on a parse failure (with a message naming which step failed),
// so a failed parse fails this test loudly rather than being folded into "not refused" - which
// would otherwise make the caller's Assert::IsFalse pass for the wrong reason.
bool RefusesForkDialog()
{
    const OutputSpec::Upload upload = ParseUploadConfig(kForkDialogXml);
    return upload.Validate().verdict == OutputSpec::Upload::Verdict::Refuse;
}

// Embed-time refusal of malformed <form> / form_input / session="once" configurations (Task 5).
// Each ValidateRestStep rule gets one hostile test; TrivialStepIsAccepted and TheForkDialogIsAccepted
// are the non-refusal cases that keep the rest of the class honest (see RefusesStep/RefusesForkDialog
// above): without them a validator that refused every step, valid or not, would still pass every
// other test here.
TEST_CLASS(RestFormValidationTest)
{
public:
    // Baseline for RefusesStep: a step with no <form>, no session="once", and a valid body must NOT
    // be refused. Every other RefusesStep-based test below relies on this being true - otherwise a
    // Refuse verdict could come from a broken baseline instead of the violation under test.
    TEST_METHOD(TrivialStepIsAccepted)
    {
        RESTStep step;
        step.method = L"PUT";
        step.path = L"/d/{upload.filename}";
        step.hasBody = true;
        step.bodySource = L"{upload.local_path}";
        Assert::IsFalse(RefusesStep(step));
    }

    // Review Focus 1: a POST declaring a form body but no fields would send a zero-length
    // urlencoded body, which no server answers usefully. Refuse it at embed time.
    TEST_METHOD(FormStepWithNoFieldsIsRefused)
    {
        RESTStep step;
        step.method = L"POST";
        step.path = L"/s/tok";
        step.formFields.clear();
        // A step with an empty <form> list is indistinguishable from one with no <form> at all, so
        // the refusal must come from the PRESENCE of the element. Set the marker the parser sets
        // (declaredEmptyForm), the same way BothHeaderAndFormInputOnOneResponseIsRefused below sets
        // cap.bothSourcesGiven directly rather than through a helper parameter.
        step.declaredEmptyForm = true;
        Assert::IsTrue(RefusesStep(step));
    }

    TEST_METHOD(FormOnAGetIsRefused)
    {
        RESTStep step;
        step.method = L"GET";
        step.path = L"/s/tok";
        step.formFields.push_back({L"password", L"pw"});
        Assert::IsTrue(RefusesStep(step));
    }

    TEST_METHOD(FormTogetherWithABodyIsRefused)
    {
        RESTStep step;
        step.method = L"POST";
        step.path = L"/s/tok";
        step.hasBody = true;
        step.bodySource = L"{upload.local_path}";
        step.formFields.push_back({L"password", L"pw"});
        Assert::IsTrue(RefusesStep(step));
    }

    TEST_METHOD(FieldNameWithStructuralCharactersIsRefused)
    {
        for (const wchar_t* bad : {L"a&b", L"a=b", L"a b", L"", L"caf\u00e9"})
        {
            RESTStep step;
            step.method = L"POST";
            step.path = L"/s/tok";
            step.formFields.push_back({bad, L"v"});
            Assert::IsTrue(RefusesStep(step));
        }
    }

    // NOTE: {upload.filename} is deliberately NOT used here. UsesFileBoundPlaceholder (RESTStep.cpp)
    // only treats local_path/total_size/range_*/random: as file-bound - filename is available even
    // for the "check if a named remote file already exists" call, so it is not in that set. This test
    // uses {upload.local_path}, which IS in the set and is what a once-step genuinely cannot resolve
    // (no local file is open yet when a login step runs). {upload.filename} in a once-step IS refused
    // too, but through UsesPerFilePlaceholder, not UsesFileBoundPlaceholder - see the two tests below.
    TEST_METHOD(FileBoundPlaceholderInAOnceStepIsRefused)
    {
        RESTStep step;
        step.method = L"POST";
        step.path = L"/s/tok";
        step.runOnce = true;
        step.formFields.push_back({L"name", L"{upload.local_path}"});
        Assert::IsTrue(RefusesStep(step));
    }

    // {upload.filename} is NOT file-bound (UsesFileBoundPlaceholder excludes it - see RESTStep.h), but
    // it IS per-file: a session="once" step runs before any file is chosen, and Task 9's
    // EstablishSession calls SendRequest with an empty remoteFileName for a once-step, so this would
    // silently resolve to an empty string rather than fail loudly. UsesPerFilePlaceholder exists to
    // catch exactly this case in both places a once-step can carry a template: a <form> value...
    TEST_METHOD(FilenamePlaceholderInAOnceStepFormValueIsRefused)
    {
        RESTStep step;
        step.method = L"POST";
        step.path = L"/s/tok";
        step.runOnce = true;
        step.formFields.push_back({L"name", L"{upload.filename}"});
        Assert::IsTrue(RefusesStep(step));
    }

    // ...and the step's own path/query/headers.
    TEST_METHOD(FilenamePlaceholderInAOnceStepPathIsRefused)
    {
        RESTStep step;
        step.method = L"GET";
        step.path = L"/s/{upload.filename}";
        step.runOnce = true;
        Assert::IsTrue(RefusesStep(step));
    }

    // The general (non-once) path must NOT be tightened by UsesPerFilePlaceholder: a per-file PUT step
    // legitimately uses {upload.filename} (see kForkDialogXml's body step), and a runtime remote check
    // can resolve it too. Uses RefusesSteps with a second, valid body step, not RefusesStep: a single
    // form-only step here would be refused for "needs exactly one body step", which would make this
    // test pass without ever exercising the rule under test.
    TEST_METHOD(FilenamePlaceholderOnANonOnceStepIsAccepted)
    {
        RESTStep form;
        form.method = L"POST";
        form.path = L"/s/tok";
        form.formFields.push_back({L"name", L"{upload.filename}"});

        RESTStep body;
        body.method = L"PUT";
        body.path = L"/d/{upload.filename}";
        body.hasBody = true;
        body.bodySource = L"{upload.local_path}";

        Assert::IsFalse(RefusesSteps({form, body}));
    }

    TEST_METHOD(UnknownUploadPlaceholderInAFormValueIsRefused)
    {
        RESTStep step;
        step.method = L"POST";
        step.path = L"/s/tok";
        step.formFields.push_back({L"name", L"{upload.no_such_thing}"});
        Assert::IsTrue(RefusesStep(step));
    }

    TEST_METHOD(BothHeaderAndFormInputOnOneResponseIsRefused)
    {
        RESTStep step;
        step.method = L"GET";
        step.path = L"/s/tok";
        RESTResponseCapture cap;
        cap.source = RESTResponseCapture::Source::FormInput;
        cap.key = L"requesttoken";
        cap.name = L"rt";
        cap.bothSourcesGiven = true;  // set by the parser when the XML carries both attributes
        step.captures.push_back(cap);
        Assert::IsTrue(RefusesStep(step));
    }

    // The validator half of the session="once" typo rule (the parser half - Configure() setting the
    // flag - is tested in RestFormSchemaTest::UnrecognizedSessionValueIsRecordedByTheParser). A typo
    // must not silently fall through to "per file": it would replay a login's credential on every
    // upload instead of refusing at embed time.
    TEST_METHOD(UnknownSessionTokenIsRefused)
    {
        RESTStep step;
        step.method = L"GET";
        step.path = L"/s/tok";
        step.hasUnknownSessionToken = true;
        Assert::IsTrue(RefusesStep(step));
    }

    // I2: session="once" together with 'body=' must be refused. Without this, EstablishSession would
    // send this step through SendRequest with no file handle - a bodiless request to the destination
    // path, creating or truncating the remote object to zero bytes before the real body step ever runs.
    TEST_METHOD(OnceStepWithABodyIsRefused)
    {
        RESTStep step;
        step.method = L"PUT";
        step.path = L"/d/x";
        step.runOnce = true;
        step.hasBody = true;
        step.bodySource = L"{upload.local_path}";
        Assert::IsTrue(RefusesStep(step));
    }

    // Defense in depth for the once-step per-file placeholder scan: 'offsetExpr' must be checked even
    // though a real config can only set it alongside hasBody (itself refused with runOnce above), in
    // case that pairing is ever relaxed. Constructed directly on the struct, bypassing the parser.
    TEST_METHOD(FilenamePlaceholderInAOnceStepOffsetIsRefused)
    {
        RESTStep step;
        step.method = L"GET";
        step.path = L"/s/tok";
        step.runOnce = true;
        step.offsetExpr = L"{upload.filename}";
        Assert::IsTrue(RefusesStep(step));
    }

    // I4: a <form value> naming a capture nothing declares must be refused, the same way a typo'd
    // capture name in 'body=' already is. Without this, value="{rtt}" for a capture actually named
    // "rt" would reach ResolveUploadTemplate, stay verbatim, and post "field=%7Brtt%7D".
    TEST_METHOD(FormValueReferencingAnUndeclaredCaptureIsRefused)
    {
        RESTStep get;
        get.method = L"GET";
        get.path = L"/s/tok";
        RESTResponseCapture cap;
        cap.source = RESTResponseCapture::Source::FormInput;
        cap.key = L"requesttoken";
        cap.name = L"rt";
        get.captures.push_back(cap);

        RESTStep post;
        post.method = L"POST";
        post.path = L"/s/tok";
        post.formFields.push_back({L"requestToken", L"{rtt}"});  // typo: the capture above is named "rt"

        // A valid body step is included so a Refuse here can only come from the rule under test, never
        // from the unrelated "needs exactly one body step" rule.
        RESTStep body;
        body.method = L"PUT";
        body.path = L"/d/{upload.filename}";
        body.hasBody = true;
        body.bodySource = L"{upload.local_path}";

        Assert::IsTrue(RefusesSteps({get, post, body}));
    }

    // The positive twin of the test above: a <form value> naming a capture that IS declared must not be
    // refused, so the rule targets only the typo case.
    TEST_METHOD(FormValueReferencingADeclaredCaptureIsAccepted)
    {
        RESTStep get;
        get.method = L"GET";
        get.path = L"/s/tok";
        RESTResponseCapture cap;
        cap.source = RESTResponseCapture::Source::FormInput;
        cap.key = L"requesttoken";
        cap.name = L"rt";
        get.captures.push_back(cap);

        RESTStep post;
        post.method = L"POST";
        post.path = L"/s/tok";
        post.formFields.push_back({L"requestToken", L"{rt}"});

        RESTStep body;
        body.method = L"PUT";
        body.path = L"/d/{upload.filename}";
        body.hasBody = true;
        body.bodySource = L"{upload.local_path}";

        Assert::IsFalse(RefusesSteps({get, post, body}));
    }

    TEST_METHOD(TheForkDialogIsAccepted)
    {
        Assert::IsFalse(RefusesForkDialog());
    }
};

TEST_CLASS(RestCaptureValueTest)
{
public:
    TEST_METHOD(OrdinaryTokensAreAccepted)
    {
        using Source = RESTResponseCapture::Source;
        Assert::IsTrue(IsAcceptableCaptureValue(L"ABCDEF:XYZ123", Source::FormInput));
        Assert::IsTrue(IsAcceptableCaptureValue(L"oeErC634izAMr3n", Source::FormInput));
        Assert::IsTrue(IsAcceptableCaptureValue(L"3", Source::FormInput));
        Assert::IsTrue(IsAcceptableCaptureValue(L"", Source::FormInput));
    }

    TEST_METHOD(ControlCharactersAreRejected)
    {
        using Source = RESTResponseCapture::Source;
        Assert::IsFalse(IsAcceptableCaptureValue(L"a\r\nb", Source::FormInput));
        Assert::IsFalse(IsAcceptableCaptureValue(L"a\tb", Source::FormInput));
        Assert::IsFalse(IsAcceptableCaptureValue(L"a\r\nb", Source::Header));
    }

    // A token is never non-ASCII. Refusing the whole range closes homoglyph and normalization
    // tricks before the value reaches a path or a body.
    TEST_METHOD(NonAsciiIsRejectedForAFormInput)
    {
        Assert::IsFalse(IsAcceptableCaptureValue(L"caf\u00e9", RESTResponseCapture::Source::FormInput));
    }

    // Header captures keep today's rule so no existing dialog regresses.
    TEST_METHOD(NonAsciiIsStillAcceptedForAHeader)
    {
        Assert::IsTrue(IsAcceptableCaptureValue(L"caf\u00e9", RESTResponseCapture::Source::Header));
    }

    TEST_METHOD(OverlongValueIsRejected)
    {
        const std::wstring big(5000, L'a');
        Assert::IsFalse(IsAcceptableCaptureValue(big, RESTResponseCapture::Source::FormInput));
        Assert::IsTrue(IsAcceptableCaptureValue(std::wstring(4096, L'a'), RESTResponseCapture::Source::FormInput));
    }
};

// ApplyResponseCaptures against a default-constructed HttpResponse - the S5 hard failure, the single
// most important new behaviour on the branch, and reachable without a server: HttpResponse::request
// defaults to a null WinHttpHandle, so GetResponseHeader/ReadResponseBody both fail their first WinHTTP
// call and return std::nullopt/absent, exactly as an ordinary "the server sent nothing" case would.
// statusCode defaults to 0, not 404, so ReadCaptureValue does not take the early "not found" path -
// these tests exercise the SAME path a real absent value would.
TEST_CLASS(RestCaptureApplyTest)
{
public:
    // form_input, no value, no default: HARD FAILURE (E_FAIL). This is the rule S5 exists for - a
    // login step that would otherwise send a structurally valid but meaningless empty token.
    TEST_METHOD(FormInputWithNoValueAndNoDefaultHardFails)
    {
        RESTStep step;
        RESTResponseCapture cap;
        cap.source = RESTResponseCapture::Source::FormInput;
        cap.key = L"requesttoken";
        cap.name = L"rt";
        step.captures.push_back(cap);

        const HttpResponse response;
        std::map<std::wstring, std::wstring> captures;
        Assert::IsTrue(ApplyResponseCaptures(step, response, captures) == E_FAIL);
    }

    // form_input, no value, WITH a default: falls back to the default rather than failing.
    TEST_METHOD(FormInputWithNoValueFallsBackToItsDefault)
    {
        RESTStep step;
        RESTResponseCapture cap;
        cap.source = RESTResponseCapture::Source::FormInput;
        cap.key = L"requesttoken";
        cap.name = L"rt";
        cap.defaultValue = L"fallback";
        step.captures.push_back(cap);

        const HttpResponse response;
        std::map<std::wstring, std::wstring> captures;
        Assert::IsTrue(SUCCEEDED(ApplyResponseCaptures(step, response, captures)));
        Assert::AreEqual(std::wstring(L"fallback"), captures.at(L"rt"));
    }

    // Header, no value, no default: the S5 hard failure is scoped to form_input ONLY, so a Header
    // capture must keep falling back (to "" when no default is given) rather than failing. This is
    // the non-regression the whole scoping decision in RESTCapture.cpp rests on: an existing TUS
    // resume dialog whose size header the server legitimately omits must not start failing.
    TEST_METHOD(HeaderWithNoValueAndNoDefaultFallsBackToEmpty)
    {
        RESTStep step;
        RESTResponseCapture cap;
        cap.source = RESTResponseCapture::Source::Header;
        cap.key = L"X-Remote-Size";
        cap.name = L"sz";
        step.captures.push_back(cap);

        const HttpResponse response;
        std::map<std::wstring, std::wstring> captures;
        Assert::IsTrue(SUCCEEDED(ApplyResponseCaptures(step, response, captures)));
        Assert::AreEqual(std::wstring(L""), captures.at(L"sz"));
    }

    // A capture name under the reserved 'upload.' namespace is skipped outright - never set from a
    // response, even if an unvalidated config somehow reaches this function.
    TEST_METHOD(ReservedUploadNameIsNeverSetFromAResponse)
    {
        RESTStep step;
        RESTResponseCapture cap;
        cap.source = RESTResponseCapture::Source::Header;
        cap.key = L"X-Remote-Size";
        cap.name = L"upload.total_size";
        step.captures.push_back(cap);

        const HttpResponse response;
        std::map<std::wstring, std::wstring> captures;
        Assert::IsTrue(SUCCEEDED(ApplyResponseCaptures(step, response, captures)));
        Assert::IsTrue(captures.find(L"upload.total_size") == captures.end());
    }
};

// A failed session must not be retried per file. The agent needs a server, so the rule is pinned on
// the shape instead: once a dialog declares session="once" steps, a failure in them is terminal for
// every file, and the config that expresses that must validate.
TEST_CLASS(RestSessionShapeTest)
{
public:
    TEST_METHOD(TheForkDialogDeclaresItsLoginStepsOnce)
    {
        const OutputSpec::Upload upload = Orc::Test::ParseUploadConfig(kForkDialogXml);

        Assert::AreEqual(size_t(3), upload.rest.RestSteps.size());
        Assert::IsTrue(upload.rest.RestSteps[0].runOnce);
        Assert::IsTrue(upload.rest.RestSteps[1].runOnce);
        Assert::IsFalse(upload.rest.RestSteps[2].runOnce);

        // The file step, the only one that repeats, must not depend on a once-step's per-file state.
        Assert::IsTrue(upload.rest.RestSteps[2].hasBody);
        Assert::IsTrue(upload.Validate().verdict == OutputSpec::Upload::Verdict::Ok);
    }
};

// The committed fixture (tests/RestAgent/rest-form-login.xml) parsed through the real config engine,
// the same guard webdav_upload_test.cpp keeps for the WebDAV fixtures: a fixture can drift from the
// dialog it is named after without any code change catching it. Uses ParseUploadConfigFile, the
// filename seam Task 4 added to the anonymous namespace above for exactly this.
TEST_CLASS(RestFormFixtureTest)
{
public:
    TEST_METHOD(FormLoginFixtureParsesToTheLoginShape)
    {
        UnitTestHelper helper;
        const std::wstring path = helper.GetDirectoryName(__WFILE__) + L"\\..\\RestAgent\\rest-form-login.xml";

        // The fixture reads both %ORC_UPLOAD_SERVER% (the target) and %ORC_SHARE_TOKEN% (the share
        // path); Configure() refuses either left unexpanded, so both are defined for the read.
        ScopedEnv server(L"ORC_UPLOAD_SERVER", L"https://share.example:8443");
        ScopedEnv shareToken(L"ORC_SHARE_TOKEN", L"abc123token");

        const OutputSpec::Upload upload = ParseUploadConfigFile(path);

        Assert::AreEqual(size_t(3), upload.rest.RestSteps.size());
        Assert::IsTrue(upload.rest.RestSteps[0].runOnce);
        Assert::AreEqual(size_t(3), upload.rest.RestSteps[0].captures.size());
        Assert::AreEqual(size_t(4), upload.rest.RestSteps[1].formFields.size());
        Assert::IsTrue(upload.rest.RestSteps[2].hasBody);
        Assert::IsTrue(upload.Validate().verdict == OutputSpec::Upload::Verdict::Ok);
    }
};

TEST_CLASS(RestAttributeScannerTest)
{
public:
    TEST_METHOD(ReadsTheRequestTokenOffTheHeadTag)
    {
        Assert::AreEqual(std::wstring(L"lTPzSolp:zWKfG6I"),
                         ExtractAttributeValue(kNextcloudPage, L"data-requesttoken").value_or(L"<absent>"));
    }

    TEST_METHOD(AbsentAttributeIsNullopt)
    {
        Assert::IsFalse(ExtractAttributeValue(kNextcloudPage, L"data-nosuchthing").has_value());
    }

    // Why this source exists at all. The same page yields nothing to the <input> scan: its hidden
    // inputs are keyed by 'id', carry no 'name', and the token is not an input in the first place.
    TEST_METHOD(TheInputScanFindsNothingOnThisPage)
    {
        Assert::IsFalse(ExtractFormInputValue(kNextcloudPage, L"requesttoken").has_value());
        Assert::IsFalse(ExtractFormInputValue(kNextcloudPage, L"sharingToken").has_value());
        Assert::AreEqual(size_t(0),
                         ExtractFormInputValues(kNextcloudPage, {L"requesttoken", L"sharingToken"}).size());
    }

    TEST_METHOD(EmptyBodyIsNullopt)
    {
        Assert::IsFalse(ExtractAttributeValue(L"", L"data-requesttoken").has_value());
    }

    TEST_METHOD(EmptyNameIsNullopt)
    {
        Assert::IsFalse(ExtractAttributeValue(kNextcloudPage, L"").has_value());
    }

    // Any element, not only <input>: Nextcloud puts it on <head>, a fork may put it anywhere.
    TEST_METHOD(AnyTagCanCarryTheAttribute)
    {
        Assert::AreEqual(std::wstring(L"v"),
                         ExtractAttributeValue(L"<div data-x=\"v\"></div>", L"data-x").value_or(L"<absent>"));
        Assert::AreEqual(std::wstring(L"v"),
                         ExtractAttributeValue(L"<input data-x=\"v\">", L"data-x").value_or(L"<absent>"));
    }

    TEST_METHOD(SingleQuotesAreAccepted)
    {
        Assert::AreEqual(std::wstring(L"v"),
                         ExtractAttributeValue(L"<head data-x='v'>", L"data-x").value_or(L"<absent>"));
    }

    // HTML says attribute names are ASCII case-insensitive, so the page's casing must not decide
    // whether the capture resolves - and the result is keyed by what the CONFIG asked for, because
    // that is what the caller looks it up by.
    TEST_METHOD(TheAttributeNameMatchesCaseInsensitivelyAndKeysOnTheRequest)
    {
        const std::map<std::wstring, std::wstring> found =
            ExtractAttributeValues(L"<head DATA-RequestToken=\"TOK\">", {L"data-requesttoken"});

        Assert::AreEqual(size_t(1), found.size());
        Assert::AreEqual(std::wstring(L"TOK"), found.at(L"data-requesttoken"));
    }

    // The value keeps its case even though the name does not: a CSRF token is base64, where case is
    // the difference between a valid token and a rejected one.
    TEST_METHOD(TheValueKeepsItsCase)
    {
        Assert::AreEqual(std::wstring(L"AbCd"),
                         ExtractAttributeValue(L"<head data-x=\"AbCd\">", L"data-x").value_or(L"<absent>"));
    }

    // A longer name that merely STARTS with the one asked for is a different attribute. Matching it
    // would capture the wrong value from a page that carries both.
    TEST_METHOD(ALongerAttributeNameIsNotAMatch)
    {
        Assert::IsFalse(ExtractAttributeValue(L"<head data-x-extra=\"v\">", L"data-x").has_value());

        Assert::AreEqual(std::wstring(L"right"),
                         ExtractAttributeValue(L"<head data-x-extra=\"wrong\" data-x=\"right\">", L"data-x")
                             .value_or(L"<absent>"));
    }

    TEST_METHOD(AttributeInsideACommentIsIgnored)
    {
        Assert::IsFalse(ExtractAttributeValue(L"<!-- <head data-x=\"v\"> -->", L"data-x").has_value());
    }

    // An unquoted value stops the tag's attribute walk rather than guessing where the value ends.
    TEST_METHOD(UnquotedValueIsSkipped)
    {
        Assert::IsFalse(ExtractAttributeValue(L"<head data-x=v>", L"data-x").has_value());
    }

    TEST_METHOD(EmptyValueIsFoundNotAbsent)
    {
        const std::optional<std::wstring> v = ExtractAttributeValue(L"<head data-x=\"\">", L"data-x");
        Assert::IsTrue(v.has_value());
        Assert::AreEqual(std::wstring(L""), *v);
    }

    // First occurrence wins, which is what bounds an attacker who can only APPEND to an honest page.
    TEST_METHOD(FirstOccurrenceWins)
    {
        Assert::AreEqual(std::wstring(L"first"),
                         ExtractAttributeValue(L"<head data-x=\"first\"><div data-x=\"second\">", L"data-x")
                             .value_or(L"<absent>"));
    }

    TEST_METHOD(FirstOccurrenceWinsWithinOneTag)
    {
        Assert::AreEqual(std::wstring(L"first"),
                         ExtractAttributeValue(L"<head data-x=\"first\" data-x=\"second\">", L"data-x")
                             .value_or(L"<absent>"));
    }

    // Both tokens of a page can sit on the same element; one tag must be able to resolve both.
    TEST_METHOD(TwoNamesOnOneTagResolveTogether)
    {
        const std::map<std::wstring, std::wstring> found =
            ExtractAttributeValues(L"<head data-a=\"1\" data-b=\"2\">", {L"data-a", L"data-b"});

        Assert::AreEqual(size_t(2), found.size());
        Assert::AreEqual(std::wstring(L"1"), found.at(L"data-a"));
        Assert::AreEqual(std::wstring(L"2"), found.at(L"data-b"));
    }

    TEST_METHOD(OnlyResolvedNamesAppear)
    {
        const std::map<std::wstring, std::wstring> found =
            ExtractAttributeValues(L"<head data-a=\"1\">", {L"data-a", L"data-missing"});

        Assert::AreEqual(size_t(1), found.size());
        Assert::IsTrue(found.find(L"data-missing") == found.end());
    }

    // No name wanted must mean the body is never touched. A hostile body that would trip a bound
    // proves it: if the scan ran at all this would return an empty map for a different reason, so the
    // assertion is paired with the same body resolving normally when a name IS wanted.
    TEST_METHOD(NoNamesRequestedScansNothing)
    {
        const std::wstring unterminated = L"<head data-a=\"1\"";
        Assert::AreEqual(size_t(0), ExtractAttributeValues(unterminated, {}).size());
        Assert::AreEqual(size_t(0), ExtractAttributeValues(unterminated, {L"data-a"}).size());
    }

    TEST_METHOD(UnterminatedTagIsRejected)
    {
        Assert::IsFalse(ExtractAttributeValue(L"<head data-x=\"v\"", L"data-x").has_value());
    }

    // A bound trips on the WHOLE body, never on part of it: an earlier good value must not survive a
    // later refusal, because a caller that got a value would never learn the body was refused.
    TEST_METHOD(ABoundRefusesTheWholeBodyNotJustTheBadTag)
    {
        const std::wstring html = L"<head data-a=\"1\"><div data-b=\"2\"";
        Assert::AreEqual(size_t(0), ExtractAttributeValues(html, {L"data-a", L"data-b"}).size());
    }

    // ... but the early stop means a refusal AFTER everything wanted is resolved is never reached.
    // This is the same property the form-input scan has, and it is what keeps a legitimate page with
    // sloppy markup further down working.
    TEST_METHOD(StopsScanningOnceEveryNameIsResolved)
    {
        const std::wstring html = L"<head data-a=\"1\"><div data-b=\"2\"";
        Assert::AreEqual(std::wstring(L"1"), ExtractAttributeValue(html, L"data-a").value_or(L"<absent>"));
    }

    TEST_METHOD(OverlongTagIsRejected)
    {
        std::wstring html = L"<head data-x=\"v\" pad=\"";
        html.append(9000, L'a');
        html.append(L"\">");
        Assert::IsFalse(ExtractAttributeValue(html, L"data-x").has_value());
    }

    TEST_METHOD(TooManyTagsIsRejected)
    {
        std::wstring html;
        for (int i = 0; i < 1100; ++i)
        {
            html.append(L"<div></div>");
        }
        html.append(L"<head data-x=\"v\">");
        Assert::IsFalse(ExtractAttributeValue(html, L"data-x").has_value());
    }

    // One pass, so "&amp;lt;" stays "&lt;" instead of collapsing to "<". A second decode would let a
    // server smuggle a character the value checks had already cleared.
    TEST_METHOD(EntitiesDecodeExactlyOnce)
    {
        Assert::AreEqual(std::wstring(L"a&b"),
                         ExtractAttributeValue(L"<head data-x=\"a&amp;b\">", L"data-x").value_or(L"<absent>"));
        Assert::AreEqual(std::wstring(L"&lt;"),
                         ExtractAttributeValue(L"<head data-x=\"&amp;lt;\">", L"data-x").value_or(L"<absent>"));
    }

    // A quoted value must not be able to spell out another attribute and be read as one.
    TEST_METHOD(AQuotedValueCannotImpersonateAnotherAttribute)
    {
        const std::wstring html = L"<head title=\" data-x='ATTACKER'\" data-x=\"real\">";
        Assert::AreEqual(std::wstring(L"real"), ExtractAttributeValue(html, L"data-x").value_or(L"<absent>"));
    }

    // The real page: the token is on the 4th tag of a 26 KB body, so the early stop walks a handful of
    // tags rather than the whole page. Asserting the value proves the walk reached it through a
    // doctype, an <html> tag carrying several attributes, and a comment.
    TEST_METHOD(ReadsThroughADoctypeAndAComment)
    {
        const std::wstring html = L"<!DOCTYPE html>\n<!-- a comment -->\n"
                                  L"<html data-placeholder-focus=\"false\" lang=\"en\" translate=\"no\">\n"
                                  L"<head data-requesttoken=\"TOK=:TOK=\">";
        Assert::AreEqual(std::wstring(L"TOK=:TOK="),
                         ExtractAttributeValue(html, L"data-requesttoken").value_or(L"<absent>"));
    }
};

TEST_CLASS(RestAttributeNameTest)
{
public:
    TEST_METHOD(TheRealAttributeNamesAreAccepted)
    {
        for (const wchar_t* good : {L"data-requesttoken", L"content", L"id", L"xml:lang", L"a.b", L"a_b", L"A1"})
        {
            Assert::IsTrue(IsSupportedAttributeName(good), good);
        }
    }

    // Each of these would describe something the scan cannot match, so it can only ever fail at
    // collection time. Refusing it at embed time is the whole point.
    TEST_METHOD(UnmatchableNamesAreRefused)
    {
        for (const wchar_t* bad : {L"", L"data x", L"data=x", L"data\"x", L"data'x", L"<data", L"data>", L"data/x",
                                   L"café", L"data\tx", L"data\r\nx"})
        {
            Assert::IsFalse(IsSupportedAttributeName(bad), bad);
        }
    }

    TEST_METHOD(AnOverlongNameIsRefused)
    {
        Assert::IsTrue(IsSupportedAttributeName(std::wstring(64, L'a')));
        Assert::IsFalse(IsSupportedAttributeName(std::wstring(65, L'a')));
    }
};

// The config surface of the attribute source: it must parse to the right shape, validate as a whole
// dialog, and be refused when it names something the scan could never match.
TEST_CLASS(RestAttributeConfigTest)
{
public:
    // The four-step dialog the fork needs, end to end. Two attribute captures, the login's 303, and
    // the per-file PUT carrying the second token as a header. This must VALIDATE, not merely parse:
    // it is the configuration this feature exists to make possible.
    TEST_METHOD(TheForkDialogParsesAndValidates)
    {
        const std::wstring xml =
            L"<dfir-orc><upload method=\"REST\" server=\"https://host\" operation=\"copy\">"
            L"<rest http_method=\"GET\" path=\"/s/tok/authenticate/showshare\" session=\"once\">"
            L"<response attribute=\"data-requesttoken\" name=\"rt1\"/>"
            L"</rest>"
            L"<rest http_method=\"POST\" path=\"/s/tok/authenticate/showshare\" session=\"once\""
            L" accept_status=\"302,303\">"
            L"<header>requesttoken: {rt1}</header>"
            L"<form field=\"password\" value=\"pw\"/>"
            L"<form field=\"sharingToken\" value=\"tok\"/>"
            L"<form field=\"sharingType\" value=\"3\"/>"
            L"</rest>"
            L"<rest http_method=\"GET\" path=\"/s/tok\" session=\"once\">"
            L"<response attribute=\"data-requesttoken\" name=\"rt2\"/>"
            L"</rest>"
            L"<rest http_method=\"PUT\" path=\"/public.php/dav/files/tok/{upload.filename}\""
            L" body=\"{upload.local_path}\">"
            L"<header>requesttoken: {rt2}</header>"
            L"<header>X-Requested-With: XMLHttpRequest</header>"
            L"</rest>"
            L"</upload></dfir-orc>";

        const OutputSpec::Upload upload = Orc::Test::ParseUploadConfig(xml);

        Assert::AreEqual(size_t(4), upload.rest.RestSteps.size());

        const RESTStep& authPage = upload.rest.RestSteps[0];
        Assert::IsTrue(authPage.runOnce);
        Assert::AreEqual(size_t(1), authPage.captures.size());
        Assert::IsTrue(authPage.captures[0].source == RESTResponseCapture::Source::Attribute);
        Assert::AreEqual(std::wstring(L"data-requesttoken"), authPage.captures[0].key);
        Assert::AreEqual(std::wstring(L"rt1"), authPage.captures[0].name);
        Assert::IsFalse(authPage.captures[0].bothSourcesGiven);

        const RESTStep& sharePage = upload.rest.RestSteps[2];
        Assert::IsTrue(sharePage.runOnce);
        Assert::IsTrue(sharePage.captures[0].source == RESTResponseCapture::Source::Attribute);
        Assert::AreEqual(std::wstring(L"rt2"), sharePage.captures[0].name);

        const RESTStep& put = upload.rest.RestSteps[3];
        Assert::IsFalse(put.runOnce);
        Assert::IsTrue(put.hasBody);
        Assert::AreEqual(size_t(2), put.headers.size());

        Assert::IsFalse(RefusesSteps(upload.rest.RestSteps));
    }

    // An attribute name the scan cannot match could only ever fail at collection time, where the
    // failure costs a collection. Refuse it at embed time instead.
    TEST_METHOD(AnUnmatchableAttributeNameIsRefused)
    {
        for (const wchar_t* bad : {L"", L"data x", L"data=x", L"data\"x", L"café"})
        {
            RESTStep step;
            step.method = L"GET";
            step.path = L"/s/tok";
            RESTResponseCapture cap;
            cap.source = RESTResponseCapture::Source::Attribute;
            cap.key = bad;
            cap.name = L"rt";
            step.captures.push_back(cap);
            Assert::IsTrue(RefusesStep(step), bad);
        }
    }

    // The same refusal the other sources get: which source the operator meant is not guessable, and
    // guessing wrong reads a different value off the server.
    TEST_METHOD(AttributeTogetherWithAnotherSourceIsRefused)
    {
        RESTStep step;
        step.method = L"GET";
        step.path = L"/s/tok";
        RESTResponseCapture cap;
        cap.source = RESTResponseCapture::Source::Attribute;
        cap.key = L"data-requesttoken";
        cap.name = L"rt";
        cap.bothSourcesGiven = true;
        step.captures.push_back(cap);
        Assert::IsTrue(RefusesStep(step));
    }

    // A valid attribute capture on its own must NOT be refused, so the refusals above can only come
    // from the violation each one sets up.
    TEST_METHOD(AValidAttributeCaptureIsNotRefusedOnItsOwn)
    {
        RESTStep get;
        get.method = L"GET";
        get.path = L"/s/tok";
        RESTResponseCapture cap;
        cap.source = RESTResponseCapture::Source::Attribute;
        cap.key = L"data-requesttoken";
        cap.name = L"rt";
        get.captures.push_back(cap);

        RESTStep put;
        put.method = L"PUT";
        put.path = L"/d/{upload.filename}";
        put.hasBody = true;
        put.bodySource = L"{upload.local_path}";

        Assert::IsFalse(RefusesSteps({get, put}));
    }

    // The value rules are shared with form_input, and the reason is the same: a token that carries
    // anything but printable ASCII has no business reaching a header line or a form body.
    TEST_METHOD(AnAttributeValueFollowsTheFormInputValueRules)
    {
        Assert::IsTrue(IsAcceptableCaptureValue(L"AbC=:9+/", RESTResponseCapture::Source::Attribute));
        Assert::IsFalse(IsAcceptableCaptureValue(L"café", RESTResponseCapture::Source::Attribute));
        Assert::IsFalse(IsAcceptableCaptureValue(L"a\r\nX-Evil: 1", RESTResponseCapture::Source::Attribute));
        Assert::IsFalse(
            IsAcceptableCaptureValue(std::wstring(5000, L'a'), RESTResponseCapture::Source::Attribute));
    }
};

}  // namespace Orc::Test
