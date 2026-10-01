//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): DFIR-ORC Team
//
#include "stdafx.h"

#include "OutputSpec.h"
#include "Network/Http/Authenticator.h"
#include "Network/Http/HttpClient.h"
#include "Network/Http/HttpProxy.h"
#include "RESTStep.h"
#include "RESTAgent.h"
#include "Configuration/ConfigFile.h"
#include "Configuration/ConfigFile_Common.h"
#include "Configuration/ConfigFileReader.h"
#include "MemoryStream.h"

using namespace std::string_literals;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;

namespace Orc::Test {

TEST_CLASS(UploadProxyValidateTest)
{
private:
    // Minimal REST upload that Validate() otherwise accepts, so each test isolates a proxy rule.
    static OutputSpec::Upload MakeValidRestUpload()
    {
        OutputSpec::Upload u;
        u.Method = OutputSpec::UploadMethod::REST;
        u.bitsMode = OutputSpec::BITSMode::HTTPS;
        u.Mode = OutputSpec::UploadMode::Synchronous;
        u.ServerName = L"host.example";
        u.AuthScheme = OutputSpec::UploadAuthScheme::Anonymous;

        RESTStep body;
        body.method = L"PUT";
        body.path = L"/{upload.filename}";
        body.hasBody = true;
        u.rest.RestSteps.push_back(std::move(body));
        return u;
    }

public:
    TEST_METHOD(BaselineIsOk)
    {
        const auto v = MakeValidRestUpload().Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    TEST_METHOD(MalformedServerPortRefused)
    {
        // A malformed 'server' port is now refused at embed/Validate time, not only at runtime.
        auto u = MakeValidRestUpload();
        u.ServerName = L"host.example:notaport";
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"expected Refuse for malformed port");
    }

    TEST_METHOD(PlainHttpWithoutInsecureRefused)
    {
        // REST over plain HTTP (clear text) is refused unless insecure="true" authorizes it.
        auto u = MakeValidRestUpload();
        u.bitsMode = OutputSpec::BITSMode::HTTP;
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"plain HTTP without insecure should refuse");
    }

    TEST_METHOD(PlainHttpWithInsecureAllowed)
    {
        // insecure="true" authorizes the weak transport: not refused (it warns).
        auto u = MakeValidRestUpload();
        u.bitsMode = OutputSpec::BITSMode::HTTP;
        u.rest.Insecure = true;
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict != OutputSpec::Upload::Verdict::Refuse, L"insecure=true should allow plain HTTP");
    }

    TEST_METHOD(InsecureOverHttpsWarns)
    {
        // insecure="true" over HTTPS disables certificate validation: allowed but warned (never silent).
        auto u = MakeValidRestUpload();  // HTTPS baseline
        u.rest.Insecure = true;
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Warn, L"insecure=true over HTTPS should warn");
    }

    TEST_METHOD(ProxyCredentialsWithoutProxySourceRefused)
    {
        auto u = MakeValidRestUpload();
        u.rest.ProxyUser = L"svc";
        u.rest.ProxyPassword = L"pw";
        // No 'proxy' set: the machine configuration is followed, but the credentials name no destination,
        // so they stay refused - proxy="system" is how an operator opts into sending them there.
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"expected Refuse");
    }

    TEST_METHOD(ProxyCredentialsWithExplicitProxyOk)
    {
        auto u = MakeValidRestUpload();
        u.rest.Proxy = L"proxy.example:8080";
        u.rest.ProxyUser = L"svc";
        u.rest.ProxyAuthScheme = OutputSpec::UploadAuthScheme::Basic;
        u.rest.ProxyPassword = L"pw";
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    TEST_METHOD(ProxyWpadWithCredentialsOk)
    {
        auto u = MakeValidRestUpload();
        u.rest.Proxy = L"wpad";
        u.rest.ProxyUser = L"svc";
        u.rest.ProxyAuthScheme = OutputSpec::UploadAuthScheme::Basic;
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    TEST_METHOD(ProxySystemWithCredentialsOk)
    {
        auto u = MakeValidRestUpload();
        u.rest.Proxy = L"system";  // machine-wide proxy as an explicit, auth-capable source
        u.rest.ProxyUser = L"svc";
        u.rest.ProxyAuthScheme = OutputSpec::UploadAuthScheme::Basic;
        u.rest.ProxyPassword = L"pw";
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    TEST_METHOD(ProxyCredentialsWithoutSchemeRefused)
    {
        auto u = MakeValidRestUpload();
        u.rest.Proxy = L"proxy.example:8080";
        u.rest.ProxyUser = L"svc";
        u.rest.ProxyPassword = L"pw";  // ProxyAuthScheme left Anonymous => must be refused
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"expected Refuse");
    }

    TEST_METHOD(ProxyNtlmSchemeAccepted)
    {
        // NTLM/Negotiate proxy auth IS supported (verified end-to-end against a real squid/Samba NTLM proxy);
        // it requires a domain-qualified proxy_user at runtime, but Validate accepts the config.
        for (auto scheme : {OutputSpec::UploadAuthScheme::NTLM, OutputSpec::UploadAuthScheme::Negotiate,
                            OutputSpec::UploadAuthScheme::Kerberos})
        {
            auto u = MakeValidRestUpload();
            u.rest.Proxy = L"proxy.example:8080";
            u.rest.ProxyUser = L"ORCTEST\\svc";
            u.rest.ProxyPassword = L"pw";
            u.rest.ProxyAuthScheme = scheme;
            const auto v = u.Validate();
            Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
        }
    }

    TEST_METHOD(ProxyPasswordWithoutUserRefused)
    {
        auto u = MakeValidRestUpload();
        u.rest.Proxy = L"proxy.example:8080";
        u.rest.ProxyPassword = L"pw";  // no ProxyUser => can never be sent
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"expected Refuse");
    }

    TEST_METHOD(ServerCertPinOverHttpRefused)
    {
        auto u = MakeValidRestUpload();
        u.bitsMode = OutputSpec::BITSMode::HTTP;  // plain HTTP presents no certificate to pin against
        u.rest.ServerCertPins.push_back(L"MIIB...base64-cert...");
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"expected Refuse");
    }

    // The single 'proxy' value classifies into exactly one mode; host:port vs PAC URL is disambiguated by
    // the ".pac" path suffix (or an explicit "pac:" prefix), never by a fragile heuristic.
    TEST_METHOD(ProxyClassification)
    {
        auto cl = [](const wchar_t* p)
        {
            OutputSpec::Upload u;
            u.rest.Proxy = p;
            return Orc::ClassifyProxy(u);
        };
        // Absent and empty are NOT the same. No 'proxy' attribute means "follow the machine" (System,
        // which connects direct by itself when the machine has no proxy); proxy="" is the operator
        // stating there is no proxy, so no discovery of any kind may happen.
        OutputSpec::Upload unset;
        Assert::IsTrue(Orc::ClassifyProxy(unset).mode == Orc::ProxyMode::System);
        Assert::IsTrue(cl(L"").mode == Orc::ProxyMode::Direct);
        Assert::IsTrue(cl(L"   ").mode == Orc::ProxyMode::Direct);  // whitespace-only is still "no proxy"

        // A reference that survived expansion means the variable is unset on this host. It is neither a
        // host name nor "no proxy": the upload is refused when it runs.
        Assert::IsTrue(cl(L"%ORC_UPLOAD_PROXY%").mode == Orc::ProxyMode::Invalid);
        Assert::IsTrue(cl(L"proxy%.corp:8080").mode == Orc::ProxyMode::Named);  // a lone '%' is not one
        Assert::IsTrue(cl(L"direct").mode == Orc::ProxyMode::Direct);
        Assert::IsTrue(cl(L"none").mode == Orc::ProxyMode::Direct);
        Assert::IsTrue(cl(L"system").mode == Orc::ProxyMode::System);
        Assert::IsTrue(cl(L"WPAD").mode == Orc::ProxyMode::Wpad);
        Assert::IsTrue(cl(L"auto").mode == Orc::ProxyMode::Wpad);  // documented synonym of "wpad"

        const auto named = cl(L"proxy.corp:8080");
        Assert::IsTrue(named.mode == Orc::ProxyMode::Named);
        Assert::AreEqual(std::wstring(L"proxy.corp:8080"), named.server);

        Assert::IsTrue(cl(L"http://proxy.corp:8080").mode == Orc::ProxyMode::Named);  // bare URL => named

        const auto autoPac = cl(L"http://wpad.corp/proxy.pac?id=1");
        Assert::IsTrue(autoPac.mode == Orc::ProxyMode::Pac);
        Assert::AreEqual(std::wstring(L"http://wpad.corp/proxy.pac?id=1"), autoPac.pacUrl);

        const auto explicitPac = cl(L"pac:http://config.corp/getproxy");
        Assert::IsTrue(explicitPac.mode == Orc::ProxyMode::Pac);
        Assert::AreEqual(std::wstring(L"http://config.corp/getproxy"), explicitPac.pacUrl);
    }

    // A variable left unexpanded is refused, but a URI percent-escape reads the same way to
    // ExpandEnvironmentStringsW: "%20orc%" names no variable either. Two escapes are URI text, not a reference.
    TEST_METHOD(UnexpandedEnvReferenceIsNotAPercentEscape)
    {
        Assert::IsTrue(HasUnexpandedEnvReference(L"%ORC_UPLOAD_SERVER%"));
        Assert::IsTrue(HasUnexpandedEnvReference(L"https://%ORC_UPLOAD_HOST%:8443"));
        Assert::IsTrue(HasUnexpandedEnvReference(L"%ADD%"));        // hexadecimal name, closed by the end
        Assert::IsTrue(HasUnexpandedEnvReference(L"%ADD%/drop"));   // ... or by a separator
        Assert::IsTrue(HasUnexpandedEnvReference(L"%20a%ORC_X%"));  // a reference after an escape
        Assert::IsTrue(HasUnexpandedEnvReference(L"https://%ORC_UPLOAD_HOST%/dfir%20orc%20drop/"));

        Assert::IsFalse(HasUnexpandedEnvReference(L""));
        Assert::IsFalse(HasUnexpandedEnvReference(L"https://host/drop/"));
        Assert::IsFalse(HasUnexpandedEnvReference(L"%%"));
        Assert::IsFalse(HasUnexpandedEnvReference(L"proxy%.corp:8080"));
        Assert::IsFalse(HasUnexpandedEnvReference(L"https://host/dfir%20orc%20drop/"));
        Assert::IsFalse(HasUnexpandedEnvReference(L"https://host/a%2Fb%2fc%3Dd"));
        Assert::IsFalse(HasUnexpandedEnvReference(L"http://proxy/my%20pac%20file.pac"));

        // The proxy classification that used to take the escapes for an unset variable.
        OutputSpec::Upload u;
        u.rest.Proxy = L"http://proxy/my%20pac%20file.pac";
        Assert::IsTrue(Orc::ClassifyProxy(u).mode == Orc::ProxyMode::Pac);
    }

    // Regression guard for the schema-registration bug: the proxy attributes must be registered on the
    // <upload> schema, else every proxy setting parses to nothing. Covers Common::upload(); Wolf::upload()
    // lives in OrcCommand (not linked here) but shares the same upload_proxy_attributes() helper.
    TEST_METHOD(UploadSchemaRegistersProxyAttributes)
    {
        ConfigItem root;
        Assert::IsTrue(SUCCEEDED(Orc::Config::Common::upload(root, 0)), L"upload schema build failed");

        const ConfigItem& up = root.SubItems[0];
        Assert::IsTrue(
            up.SubItems.size() > CONFIG_UPLOAD_PROXY_AUTHSCHEME, L"proxy attributes were not registered");
        Assert::AreEqual(std::wstring(L"proxy"), std::wstring(up.SubItems[CONFIG_UPLOAD_PROXY].strName));
        Assert::AreEqual(
            std::wstring(L"proxy_authscheme"), std::wstring(up.SubItems[CONFIG_UPLOAD_PROXY_AUTHSCHEME].strName));
    }

    // follow_redirects defaults to off (the secure default: REDIRECT_POLICY_NEVER is applied in HttpClient::Send).
    TEST_METHOD(FollowRedirectsDefaultsOff)
    {
        Assert::IsFalse(MakeValidRestUpload().rest.FollowRedirects, L"follow_redirects must default to false");
    }

    // follow_redirects is a valid REST opt-in; it must not be refused on a REST upload.
    TEST_METHOD(FollowRedirectsOnRestOk)
    {
        auto u = MakeValidRestUpload();
        u.rest.FollowRedirects = true;
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    // follow_redirects only affects the REST HttpClient transport, so it is refused on a non-REST (BITS) upload
    // rather than silently ignored.
    TEST_METHOD(FollowRedirectsOnNonRestRefused)
    {
        OutputSpec::Upload u;
        u.Method = OutputSpec::UploadMethod::BITS;
        u.bitsMode = OutputSpec::BITSMode::HTTPS;
        u.Mode = OutputSpec::UploadMode::Synchronous;
        u.ServerName = L"host.example";
        u.AuthScheme = OutputSpec::UploadAuthScheme::Negotiate;
        u.rest.FollowRedirects = true;

        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"follow_redirects on BITS must be refused");
        Assert::IsTrue(
            v.message.find(L"follow_redirects") != std::wstring::npos, L"refusal must name follow_redirects");
    }

    // Schema-registration guard: the attribute must be registered at its declared index, else 'follow_redirects'
    // parses to nothing. Shares the upload_proxy_attributes() tail with Wolf::upload() (OrcCommand).
    TEST_METHOD(UploadSchemaRegistersFollowRedirects)
    {
        ConfigItem root;
        Assert::IsTrue(SUCCEEDED(Orc::Config::Common::upload(root, 0)), L"upload schema build failed");

        const ConfigItem& up = root.SubItems[0];
        Assert::IsTrue(
            up.SubItems.size() > CONFIG_UPLOAD_FOLLOW_REDIRECTS, L"follow_redirects attribute was not registered");
        Assert::AreEqual(
            std::wstring(L"follow_redirects"), std::wstring(up.SubItems[CONFIG_UPLOAD_FOLLOW_REDIRECTS].strName));
    }

    // A chunked step that names no offset sends every chunk to the same address: the last one replaces
    // the rest and the stored object is the final fragment, with every request returning 2xx. Measured
    // against MinIO: an 890 byte archive became a 122 byte object and the run reported success.
    TEST_METHOD(ChunkedStepWithoutOffsetRefused)
    {
        auto u = MakeValidRestUpload();
        u.rest.RestSteps[0].chunkSize = 256;
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"a chunked step with no offset must refuse");
        Assert::IsTrue(
            v.message.find(L"upload.range_start") != std::wstring::npos, L"the refusal must name the placeholder");
    }

    // The three shapes every existing chunked fixture uses must keep validating: the offset in the query
    // (vm-https-pin.xml), in a Content-Range header (rest-http-chunked.xml), or in the path (WebDAV).
    TEST_METHOD(ChunkedStepWithOffsetInQueryOk)
    {
        auto u = MakeValidRestUpload();
        u.rest.RestSteps[0].chunkSize = 256;
        u.rest.RestSteps[0].query = L"?start={upload.range_start}";
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    TEST_METHOD(ChunkedStepWithContentRangeHeaderOk)
    {
        auto u = MakeValidRestUpload();
        u.rest.RestSteps[0].chunkSize = 256;
        u.rest.RestSteps[0].headers.push_back(
            L"Content-Range: bytes {upload.range_start}-{upload.range_end}/{upload.total_size}");
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    TEST_METHOD(ChunkedStepWithOffsetInPathOk)
    {
        auto u = MakeValidRestUpload();
        u.rest.RestSteps[0].chunkSize = 256;
        u.rest.RestSteps[0].path = L"/{upload.filename}/{upload.range_start}";
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    // The rule is about chunking only: an unchunked step needs no offset placeholder, which is the
    // single-PUT shape the S3 fixtures use.
    TEST_METHOD(UnchunkedStepWithoutOffsetOk)
    {
        auto u = MakeValidRestUpload();
        u.rest.RestSteps[0].chunkSize = 0;
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    // AWSv4 needs all three of access_key, secret_key and aws_region. aws_region has no default on
    // purpose: an implicit us-east-1 turns a forgotten attribute into a 403 that names nothing.
    TEST_METHOD(AwsV4WithoutAccessKeyRefused)
    {
        auto u = MakeValidRestUpload();
        u.AuthScheme = OutputSpec::UploadAuthScheme::AWSv4;
        u.rest.aws.SecretKey = L"secret";
        u.rest.aws.Region = L"eu-west-3";
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"AWSv4 with no access_key must refuse");
    }

    TEST_METHOD(AwsV4WithoutSecretKeyRefused)
    {
        auto u = MakeValidRestUpload();
        u.AuthScheme = OutputSpec::UploadAuthScheme::AWSv4;
        u.rest.aws.AccessKey = L"AKIAEXAMPLE";
        u.rest.aws.Region = L"eu-west-3";
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"AWSv4 with no secret_key must refuse");
    }

    TEST_METHOD(AwsV4WithoutRegionRefused)
    {
        auto u = MakeValidRestUpload();
        u.AuthScheme = OutputSpec::UploadAuthScheme::AWSv4;
        u.rest.aws.AccessKey = L"AKIAEXAMPLE";
        u.rest.aws.SecretKey = L"secret";
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"AWSv4 with no aws_region must refuse");
        Assert::IsTrue(v.message.find(L"aws_region") != std::wstring::npos, L"the refusal must name aws_region");
    }

    TEST_METHOD(AwsV4FullyConfiguredIsOk)
    {
        auto u = MakeValidRestUpload();
        u.AuthScheme = OutputSpec::UploadAuthScheme::AWSv4;
        u.rest.aws.AccessKey = L"AKIAEXAMPLE";
        u.rest.aws.SecretKey = L"secret";
        u.rest.aws.Region = L"eu-west-3";
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    // A credential set under another scheme is refused, not ignored. A silently dropped credential is
    // the bug class that already cost a day here.
    TEST_METHOD(AwsCredentialsUnderAnotherSchemeRefused)
    {
        auto u = MakeValidRestUpload();
        u.AuthScheme = OutputSpec::UploadAuthScheme::Anonymous;
        u.rest.aws.AccessKey = L"AKIAEXAMPLE";
        const auto v = u.Validate();
        Assert::IsTrue(
            v.verdict == OutputSpec::Upload::Verdict::Refuse, L"an AWS credential without AWSv4 must refuse");
    }

    // The auth refusals must precede the transport Warn block: Validate returns on the first non-Ok
    // verdict, and a Warn placed first would mask them - the masking that once let an insecure-flagged
    // config bypass the auth gate entirely.
    TEST_METHOD(AwsV4RefusalIsNotMaskedByTheInsecureWarn)
    {
        auto u = MakeValidRestUpload();
        u.bitsMode = OutputSpec::BITSMode::HTTP;
        u.rest.Insecure = true;
        u.AuthScheme = OutputSpec::UploadAuthScheme::AWSv4;
        const auto v = u.Validate();
        Assert::IsTrue(
            v.verdict == OutputSpec::Upload::Verdict::Refuse, L"the auth refusal must not be masked by a Warn");
    }

    // A literal Authorization header would compete with the one the signer builds, with the winner
    // decided by header ordering - two sources for one header whose meaning is then accidental.
    TEST_METHOD(AwsV4WithAuthorizationHeaderRefused)
    {
        auto u = MakeValidRestUpload();
        u.AuthScheme = OutputSpec::UploadAuthScheme::AWSv4;
        u.rest.aws.AccessKey = L"AKIAEXAMPLE";
        u.rest.aws.SecretKey = L"secret";
        u.rest.aws.Region = L"eu-west-3";
        u.rest.HttpHeaders.push_back(L"Authorization: Bearer token");
        const auto v = u.Validate();
        Assert::IsTrue(
            v.verdict == OutputSpec::Upload::Verdict::Refuse,
            L"AWSv4 with a literal Authorization header must refuse");
    }

    // Same rule via a per-step <header>, and the name comparison is case-insensitive.
    TEST_METHOD(AwsV4WithAuthorizationHeaderInStepRefused)
    {
        auto u = MakeValidRestUpload();
        u.AuthScheme = OutputSpec::UploadAuthScheme::AWSv4;
        u.rest.aws.AccessKey = L"AKIAEXAMPLE";
        u.rest.aws.SecretKey = L"secret";
        u.rest.aws.Region = L"eu-west-3";
        u.rest.RestSteps[0].headers.push_back(L"AUTHORIZATION: Bearer token");
        const auto v = u.Validate();
        Assert::IsTrue(
            v.verdict == OutputSpec::Upload::Verdict::Refuse,
            L"AWSv4 with a literal Authorization header in a <rest> step must refuse");
    }

    // host/x-amz-date/x-amz-content-sha256/x-amz-security-token are computed by the signer itself
    // (MakeAwsV4Headers already refuses this collision at request time with E_INVALIDARG, since SigV4's
    // comma-joined signed-header list is order sensitive); refusing at embed time names the problem
    // before a deployed collection fails weeks later.
    TEST_METHOD(AwsV4WithSignerOwnedHeaderRefused)
    {
        auto u = MakeValidRestUpload();
        u.AuthScheme = OutputSpec::UploadAuthScheme::AWSv4;
        u.rest.aws.AccessKey = L"AKIAEXAMPLE";
        u.rest.aws.SecretKey = L"secret";
        u.rest.aws.Region = L"eu-west-3";
        u.rest.HttpHeaders.push_back(L"X-Amz-Date: 20260101T000000Z");
        const auto v = u.Validate();
        Assert::IsTrue(
            v.verdict == OutputSpec::Upload::Verdict::Refuse, L"AWSv4 with a literal x-amz-date header must refuse");
        Assert::IsTrue(v.message.find(L"x-amz-date") != std::wstring::npos, L"the refusal must name the header");
    }

    // Schema-registration guard: an attribute registered at the wrong index parses to nothing, and the
    // upload then runs with no credential at all.
    TEST_METHOD(UploadSchemaRegistersAwsAttributes)
    {
        ConfigItem root;
        Assert::IsTrue(SUCCEEDED(Orc::Config::Common::upload(root, 0)), L"upload schema build failed");

        const ConfigItem& up = root.SubItems[0];
        Assert::IsTrue(up.SubItems.size() > CONFIG_UPLOAD_AWS_CLOCK, L"the aws attributes were not registered");
        Assert::AreEqual(std::wstring(L"access_key"), std::wstring(up.SubItems[CONFIG_UPLOAD_ACCESS_KEY].strName));
        Assert::AreEqual(std::wstring(L"secret_key"), std::wstring(up.SubItems[CONFIG_UPLOAD_SECRET_KEY].strName));
        Assert::AreEqual(
            std::wstring(L"session_token"), std::wstring(up.SubItems[CONFIG_UPLOAD_SESSION_TOKEN].strName));
        Assert::AreEqual(std::wstring(L"aws_region"), std::wstring(up.SubItems[CONFIG_UPLOAD_AWS_REGION].strName));
        Assert::AreEqual(std::wstring(L"aws_service"), std::wstring(up.SubItems[CONFIG_UPLOAD_AWS_SERVICE].strName));
        Assert::AreEqual(std::wstring(L"aws_clock"), std::wstring(up.SubItems[CONFIG_UPLOAD_AWS_CLOCK].strName));
    }
};

// TLS version policy is a floor (minimum): ResolveTlsVersions enables the configured version and every
// higher one up to what the running OS supports, so a future TLS version is picked up by an existing config.
TEST_CLASS(HttpsSecurityFloorTest)
{
    using U = OutputSpec::Upload;

    static int Resolve(const U& u, DWORD major, DWORD minor) { return static_cast<int>(u.ResolveTlsVersions(major, minor)); }

public:
    TEST_METHOD(ModernDefaultIsTls13AndUp)
    {
        U u;  // defaults: floor TLS 1.3, XP exception 1.2
        // Today the highest known is 1.3, so a 1.3 floor on a modern OS resolves to {1.3}; a future 1.4 would
        // be added automatically once kHighestKnownTls/ToWinHttpProtocols learn it.
        Assert::AreEqual(static_cast<int>(U::kTls1_3), Resolve(u, 10, 0));
    }

    TEST_METHOD(ModernFloorTls12EnablesTls12And13)
    {
        U u;
        u.rest.TlsVersions = U::kTls1_2;
        Assert::AreEqual(static_cast<int>(U::kTls1_2 | U::kTls1_3), Resolve(u, 10, 0));
    }

    TEST_METHOD(ModernFloorTls10EnablesAll)
    {
        U u;
        u.rest.TlsVersions = U::kTls1_0;
        Assert::AreEqual(
            static_cast<int>(U::kTls1_0 | U::kTls1_1 | U::kTls1_2 | U::kTls1_3), Resolve(u, 10, 0));
    }

    TEST_METHOD(XpUsesExceptionFloor)
    {
        U u;  // default XP exception -> 1.2
        Assert::AreEqual(static_cast<int>(U::kTls1_2), Resolve(u, 5, 1));
    }

    TEST_METHOD(XpCeilingPreventsTls13)
    {
        U u;
        u.rest.TlsOsExceptions.clear();
        u.rest.TlsVersions = U::kTls1_2;  // floor 1.2, no exception, running on XP (ceiling 1.2)
        Assert::AreEqual(static_cast<int>(U::kTls1_2), Resolve(u, 5, 1));  // must NOT enable 1.3 on XP
    }

    TEST_METHOD(FloorAboveOsCeilingStillRequestsFloor)
    {
        U u;
        u.rest.TlsOsExceptions.clear();
        u.rest.TlsVersions = U::kTls1_3;  // floor 1.3 on Windows 7 (ceiling 1.2)
        // Preserve prior behavior: request the configured floor rather than silently downgrade below it.
        Assert::AreEqual(static_cast<int>(U::kTls1_3), Resolve(u, 6, 1));
    }

    TEST_METHOD(EmptyFloorFallsBackToTls12)
    {
        U u;
        u.rest.TlsOsExceptions.clear();
        u.rest.TlsVersions = 0;  // e.g. https_security="tls1.2[xp]": every entry is OS-tagged
        // Must never resolve to 0: HttpClient reads 0 as "leave WINHTTP_OPTION_SECURE_PROTOCOLS unset",
        // handing the connection back to the system policy - which can still negotiate TLS 1.0/1.1.
        Assert::AreEqual(static_cast<int>(U::kTls1_2 | U::kTls1_3), Resolve(u, 10, 0));
    }

    TEST_METHOD(UnknownOsVersionEnforcesTheBaseline)
    {
        U u;  // defaults: floor TLS 1.3, XP exception 1.2
        // (0, 0) is what ToHttpClientConfig passes when SystemDetails::GetOSVersion fails. No OS target
        // matches it, so no exception applies and the baseline floor is enforced - fail closed. On an XP
        // host that means the handshake fails rather than quietly dropping to the XP exception, which is
        // why the caller reports the lookup failure at Error.
        Assert::AreEqual(static_cast<int>(U::kTls1_3), Resolve(u, 0, 0));
    }
};

// Downgrade guard for the whole TLS selection chain: https_security text -> ApplyHttpsSecurity -> Validate
// gate -> ResolveTlsVersions on every OS the selection can see -> ToHttpTlsVersions -> ToWinHttpProtocols.
// The property under test is that nothing the configuration did not ask for, and nothing the host reports
// about itself, can enable a TLS version below the requested floor or hand the connection back to the system
// TLS policy.
TEST_CLASS(TlsDowngradeGuardTest)
{
    using U = OutputSpec::Upload;

    struct OsVersion
    {
        DWORD major;
        DWORD minor;
    };

    // XP, 2003/XP x64, Vista/2008, 7/2008R2, 8, 8.1, 10/11, and the 0.0 BuildHttpClientConfig passes when the
    // OS version lookup fails.
    static constexpr OsVersion kOsVersions[] = {
        {5, 0}, {5, 1}, {5, 2}, {6, 0}, {6, 1}, {6, 2}, {6, 3}, {10, 0}, {0, 0}};

    static constexpr uint8_t kAllKnown = U::kTls1_0 | U::kTls1_1 | U::kTls1_2 | U::kTls1_3;

    static U::RestConfig Apply(const std::wstring& value)
    {
        U::RestConfig rest;
        ApplyHttpsSecurity(value, rest);
        return rest;
    }

    static uint8_t Resolve(const U::RestConfig& rest, const OsVersion& os)
    {
        U u;
        u.rest = rest;
        return u.ResolveTlsVersions(os.major, os.minor);
    }

    static uint8_t LowestBit(uint8_t versions)
    {
        for (uint8_t bit = 1; bit != 0; bit = static_cast<uint8_t>(bit << 1))
        {
            if (versions & bit)
            {
                return bit;
            }
        }

        return 0;
    }

    // True when the set is one unbroken run of bits: "floor and up", never a hole a later version skips over.
    static bool IsContiguous(uint8_t versions)
    {
        const uint8_t low = LowestBit(versions);
        return versions != 0 && ((versions + low) & versions) == 0;
    }

    static std::wstring Describe(const std::wstring& what, const OsVersion& os)
    {
        return what + L" on OS " + std::to_wstring(os.major) + L"." + std::to_wstring(os.minor);
    }

    static bool IsDefault(const U::RestConfig& rest)
    {
        const U::RestConfig defaults;
        return rest.TlsVersions == defaults.TlsVersions && rest.TlsOsExceptions == defaults.TlsOsExceptions
            && rest.TlsOsDefault == defaults.TlsOsDefault && rest.TlsOsMax == defaults.TlsOsMax;
    }

    static U MakeHttpsRestUpload()
    {
        U u;
        u.Method = OutputSpec::UploadMethod::REST;
        u.bitsMode = OutputSpec::BITSMode::HTTPS;
        u.Mode = OutputSpec::UploadMode::Synchronous;
        u.ServerName = L"host.example";
        u.AuthScheme = OutputSpec::UploadAuthScheme::Anonymous;

        RESTStep body;
        body.method = L"PUT";
        body.path = L"/{upload.filename}";
        body.hasBody = true;
        u.rest.RestSteps.push_back(std::move(body));
        return u;
    }

public:
    TEST_METHOD(DefaultIsTls13FloorWithAnXpException)
    {
        const U::RestConfig rest;
        Assert::AreEqual(static_cast<int>(U::kTls1_3), static_cast<int>(rest.TlsVersions));
        Assert::AreEqual(1, static_cast<int>(rest.TlsOsExceptions.size()));
        Assert::AreEqual(static_cast<int>(U::kTls1_2), static_cast<int>(rest.TlsOsExceptions.at(U::OsTarget::Xp)));
        Assert::IsFalse(rest.TlsOsDefault);
    }

    TEST_METHOD(EachVersionTokenSelectsItsOwnBit)
    {
        const std::pair<const wchar_t*, uint8_t> tokens[] = {
            {L"tls1.0", U::kTls1_0}, {L"tls1.1", U::kTls1_1}, {L"tls1.2", U::kTls1_2}, {L"tls1.3", U::kTls1_3}};

        for (const auto& [token, bit] : tokens)
        {
            const auto rest = Apply(token);
            Assert::AreEqual(static_cast<int>(bit), static_cast<int>(rest.TlsVersions), token);
            Assert::IsTrue(rest.TlsOsExceptions.empty(), token);
            Assert::IsFalse(rest.TlsOsDefault, token);
        }
    }

    TEST_METHOD(EverySpellingOfAVersionMeansThatVersion)
    {
        for (const auto* value : {L"tls1.2", L"TLS1.2", L"Tls1.2", L"1.2", L"tls 1.2", L"  tls1.2  "})
        {
            Assert::AreEqual(static_cast<int>(U::kTls1_2), static_cast<int>(Apply(value).TlsVersions), value);
        }
    }

    // A value ORC cannot read must not weaken anything: the defaults stay, the default floor included.
    TEST_METHOD(NothingParseableKeepsTheDefaults)
    {
        for (const auto* value :
             {L"", L",", L" , ", L"garbage", L"tls1.4", L"ssl3", L"tls1.2x", L"os-default-please", L"[xp]", L"tls1.2]"})
        {
            Assert::IsTrue(IsDefault(Apply(value)), value);
        }
    }

    TEST_METHOD(UnknownTokensBesideAValidOneAddNothingBelowIt)
    {
        const auto rest = Apply(L"tls1.3, tls1.0x, garbage, ssl3, tls0.9");
        Assert::AreEqual(static_cast<int>(U::kTls1_3), static_cast<int>(rest.TlsVersions));
        Assert::IsTrue(rest.TlsOsExceptions.empty());
        Assert::IsFalse(rest.TlsOsDefault);
    }

    // A version tagged for an OS target ORC does not know is dropped, never applied to some other OS.
    TEST_METHOD(UnknownOrEmptyOsTargetIsNeverApplied)
    {
        for (const auto* value :
             {L"tls1.3, tls1.0[vista]", L"tls1.3, tls1.0[]", L"tls1.3, tls1.0[win7]", L"tls1.3, tls1.0[ xp x ]"})
        {
            const auto rest = Apply(value);
            Assert::AreEqual(static_cast<int>(U::kTls1_3), static_cast<int>(rest.TlsVersions), value);
            Assert::IsTrue(rest.TlsOsExceptions.empty(), value);
        }
    }

    // Tagged entries name no baseline: the floor already in force must survive, not fall to 0.
    TEST_METHOD(OsTaggedEntriesAloneKeepTheBaselineFloor)
    {
        const auto xpOnly = Apply(L"tls1.2[xp]");
        Assert::AreEqual(static_cast<int>(U::kTls1_3), static_cast<int>(xpOnly.TlsVersions));
        Assert::AreEqual(static_cast<int>(U::kTls1_2), static_cast<int>(xpOnly.TlsOsExceptions.at(U::OsTarget::Xp)));

        U::RestConfig raised;
        raised.TlsVersions = U::kTls1_2;
        ApplyHttpsSecurity(L"tls1.0[seven]", raised);
        Assert::AreEqual(static_cast<int>(U::kTls1_2), static_cast<int>(raised.TlsVersions));
        Assert::AreEqual(static_cast<int>(U::kTls1_0), static_cast<int>(raised.TlsOsExceptions.at(U::OsTarget::Seven)));
    }

    TEST_METHOD(VersionsForTheSameOsAccumulate)
    {
        const auto rest = Apply(L"tls1.3, tls1.2[xp], tls1.1[xp]");
        Assert::AreEqual(
            static_cast<int>(U::kTls1_2 | U::kTls1_1), static_cast<int>(rest.TlsOsExceptions.at(U::OsTarget::Xp)));
    }

    // The only value that turns enforcement off is the whole value "os-default" or "none", in any case.
    TEST_METHOD(OnlyAWholeOsDefaultValueDropsEnforcement)
    {
        for (const auto* value : {L"os-default", L"OS-DEFAULT", L"none", L"None", L" none "})
        {
            const auto rest = Apply(value);
            Assert::IsTrue(rest.TlsOsDefault, value);
            Assert::IsFalse(rest.TlsOsMax, value);
            Assert::AreEqual(0, static_cast<int>(rest.TlsVersions), value);
            Assert::IsTrue(rest.TlsOsExceptions.empty(), value);
        }

        for (const auto* value : {L"os-default, tls1.2", L"tls1.3, none"})
        {
            const auto rest = Apply(value);
            Assert::IsFalse(rest.TlsOsDefault, value);
            Assert::AreNotEqual(0, static_cast<int>(rest.TlsVersions), value);
        }
    }

    // Only the whole value "os-max", in any case, selects the lock on the highest version; it is never mixed with
    // versions or tags, and it is not os-default.
    TEST_METHOD(OnlyAWholeOsMaxValueSelectsTheLock)
    {
        for (const auto* value : {L"os-max", L"OS-MAX", L" Os-Max "})
        {
            const auto rest = Apply(value);
            Assert::IsTrue(rest.TlsOsMax, value);
            Assert::IsFalse(rest.TlsOsDefault, value);
            Assert::AreEqual(0, static_cast<int>(rest.TlsVersions), value);
            Assert::IsTrue(rest.TlsOsExceptions.empty(), value);
        }

        for (const auto* value : {L"os-max, tls1.2", L"tls1.3, os-max", L"tls1.2[xp], os-max"})
        {
            const auto rest = Apply(value);
            Assert::IsFalse(rest.TlsOsMax, value);
            Assert::AreNotEqual(0, static_cast<int>(rest.TlsVersions), value);
        }

        for (const auto* value : {L"os-maximum", L"max", L"os max"})
        {
            Assert::IsTrue(IsDefault(Apply(value)), value);
        }
    }

    // A malformed OS tag is recorded for Validate to refuse, and its entry is never applied: nothing it could
    // have meant lowers the baseline or enables TLS 1.0/1.1 anywhere.
    TEST_METHOD(MalformedOsTagIsRecordedAndNeverApplied)
    {
        const std::pair<const wchar_t*, const wchar_t*> cases[] = {
            {L"tls1.2[xp", L"tls1.2[xp"},
            {L"tls1.3[seven]junk", L"tls1.3[seven]junk"},
            {L"tls1.2[xp]tls1.0", L"tls1.2[xp]tls1.0"},
            {L"tls1.2]", L"tls1.2]"},
            {L"tls1.2[xp][seven]", L"tls1.2[xp][seven]"},
            {L"tls1.3, tls1.0[xp", L"tls1.0[xp"}};

        for (const auto& [value, entry] : cases)
        {
            const auto rest = Apply(value);
            Assert::AreEqual(1, static_cast<int>(rest.HttpsSecurityInvalid.size()), value);
            Assert::AreEqual(std::wstring(entry), rest.HttpsSecurityInvalid.front(), value);
            Assert::AreEqual(static_cast<int>(U::kTls1_3), static_cast<int>(rest.TlsVersions), value);
            Assert::IsFalse(rest.TlsOsDefault, value);
            for (const auto& [target, versions] : rest.TlsOsExceptions)
            {
                Assert::AreEqual(0, versions & (U::kTls1_0 | U::kTls1_1), value);
            }
        }
    }

    // Known versions with known, well-formed tags record nothing; spaces around the tag are allowed.
    TEST_METHOD(ValidEntriesRecordNothingInvalid)
    {
        for (const auto* value :
             {L"tls1.3, tls1.2[xp]", L"tls1.2 [ xp ]", L"tls1.3,tls1.2[seven]", L"os-default", L"os-max", L"", L" , "})
        {
            Assert::IsTrue(Apply(value).HttpsSecurityInvalid.empty(), value);
        }
    }

    // An unknown OS target or an unknown version is recorded like a malformed tag, never applied, and refused by
    // the ToolEmbed gate: the value would not do what was written.
    TEST_METHOD(UnknownTargetsAndVersionsAreRecordedAndRefused)
    {
        const std::pair<const wchar_t*, const wchar_t*> cases[] = {
            {L"garbage", L"garbage"},
            {L"tls1.4", L"tls1.4"},
            {L"ssl3", L"ssl3"},
            {L"tls1.3, tls1.0x", L"tls1.0x"},
            {L"tls1.2[vista]", L"tls1.2[vista]"},
            {L"tls1.3, tls1.0[win7]", L"tls1.0[win7]"},
            {L"tls1.3, tls1.2[ xp x ]", L"tls1.2[ xp x ]"}};

        for (const auto& [value, entry] : cases)
        {
            auto u = MakeHttpsRestUpload();
            ApplyHttpsSecurity(value, u.rest);
            Assert::IsFalse(u.rest.HttpsSecurityInvalid.empty(), value);
            Assert::AreEqual(std::wstring(entry), u.rest.HttpsSecurityInvalid.front(), value);
            Assert::IsTrue(u.rest.TlsOsExceptions.count(U::OsTarget::Seven) == 0, value);

            const auto verdict = u.Validate();
            Assert::IsTrue(verdict.verdict == U::Verdict::Refuse, value);
            Assert::IsTrue(verdict.message.find(entry) != std::wstring::npos, verdict.message.c_str());
        }
    }

    // The gate ToolEmbed runs refuses a malformed OS tag, naming it; 'insecure' does not authorize it.
    TEST_METHOD(ValidateRefusesAMalformedOsTag)
    {
        auto u = MakeHttpsRestUpload();
        ApplyHttpsSecurity(L"tls1.3, tls1.2[xp", u.rest);

        const auto refused = u.Validate();
        Assert::IsTrue(refused.verdict == U::Verdict::Refuse, refused.message.c_str());
        Assert::IsTrue(refused.message.find(L"tls1.2[xp") != std::wstring::npos, refused.message.c_str());

        u.rest.Insecure = true;
        Assert::IsTrue(u.Validate().verdict == U::Verdict::Refuse, L"insecure must not authorize a malformed tag");
    }

    // Exhaustive over every configurable set and every OS: the resolved set starts exactly at the configured
    // floor, is never empty, has no hole, and never exceeds the versions this build knows.
    TEST_METHOD(ResolvedSetStartsExactlyAtTheFloorOnEveryOs)
    {
        for (uint8_t configured = 1; configured <= kAllKnown; ++configured)
        {
            U::RestConfig rest;
            rest.TlsOsExceptions.clear();
            rest.TlsVersions = configured;

            for (const auto& os : kOsVersions)
            {
                const uint8_t resolved = Resolve(rest, os);
                const std::wstring what = Describe(L"configured " + std::to_wstring(configured), os);
                Assert::AreEqual(
                    static_cast<int>(LowestBit(configured)), static_cast<int>(LowestBit(resolved)), what.c_str());
                Assert::IsTrue(IsContiguous(resolved), what.c_str());
                Assert::AreEqual(0, resolved & ~kAllKnown, what.c_str());
            }
        }
    }

    // An OS exception may relax the floor, but only on the OS it names: never on a newer one, never when the
    // OS version is unknown.
    TEST_METHOD(AnExceptionOnlyAppliesOnItsOwnOs)
    {
        const std::pair<U::OsTarget, bool (*)(const OsVersion&)> targets[] = {
            {U::OsTarget::Xp, [](const OsVersion& os) { return os.major == 5; }},
            {U::OsTarget::Seven, [](const OsVersion& os) { return os.major == 6 && os.minor == 1; }}};

        for (const auto& [target, isThatOs] : targets)
        {
            U::RestConfig rest;
            rest.TlsVersions = U::kTls1_3;
            rest.TlsOsExceptions = {{target, U::kTls1_0}};

            for (const auto& os : kOsVersions)
            {
                const uint8_t expectedFloor = isThatOs(os) ? U::kTls1_0 : U::kTls1_3;
                Assert::AreEqual(
                    static_cast<int>(expectedFloor),
                    static_cast<int>(LowestBit(Resolve(rest, os))),
                    Describe(L"exception floor", os).c_str());
            }
        }
    }

    // An empty floor must never reach HttpClient as 0, which would leave WINHTTP_OPTION_SECURE_PROTOCOLS unset.
    TEST_METHOD(AnEmptyFloorNeverFallsBackToTheSystemPolicy)
    {
        U::RestConfig noBaseline;
        noBaseline.TlsVersions = 0;
        noBaseline.TlsOsExceptions.clear();

        U::RestConfig emptyExceptions;
        emptyExceptions.TlsVersions = 0;
        emptyExceptions.TlsOsExceptions = {{U::OsTarget::Xp, 0}, {U::OsTarget::Seven, 0}};

        for (const auto& os : kOsVersions)
        {
            Assert::AreEqual(
                static_cast<int>(U::kTls1_2),
                static_cast<int>(LowestBit(Resolve(noBaseline, os))),
                Describe(L"no baseline", os).c_str());
            Assert::AreEqual(
                static_cast<int>(U::kTls1_2),
                static_cast<int>(LowestBit(Resolve(emptyExceptions, os))),
                Describe(L"zero exceptions", os).c_str());
        }
    }

    // The ceiling of an old OS caps what is added above the floor; it never pulls the floor itself down.
    TEST_METHOD(AnOsCeilingNeverPullsTheFloorDown)
    {
        U::RestConfig rest;
        rest.TlsVersions = U::kTls1_3;
        rest.TlsOsExceptions.clear();

        for (const auto& os : {OsVersion {5, 1}, OsVersion {6, 1}})
        {
            Assert::AreEqual(
                static_cast<int>(U::kTls1_3), static_cast<int>(Resolve(rest, os)), Describe(L"1.3 floor", os).c_str());
        }
    }

    // TLS 1.0/1.1 anywhere - baseline or any OS exception - is refused unless 'insecure' authorizes it.
    TEST_METHOD(Tls10Or11AnywhereRequiresInsecure)
    {
        for (const auto* value :
             {L"tls1.0", L"tls1.1", L"tls1.3, tls1.1[xp]", L"tls1.3, tls1.0[seven]", L"tls1.2, tls1.0"})
        {
            auto u = MakeHttpsRestUpload();
            u.rest.TlsVersions = Apply(value).TlsVersions;
            u.rest.TlsOsExceptions = Apply(value).TlsOsExceptions;
            Assert::IsTrue(u.Validate().verdict == U::Verdict::Refuse, value);

            u.rest.Insecure = true;
            Assert::IsTrue(u.Validate().verdict != U::Verdict::Refuse, value);
        }
    }

    TEST_METHOD(ATls12FloorEverywherePassesTheGate)
    {
        for (const auto* value : {L"tls1.2", L"tls1.3", L"tls1.3, tls1.2[xp]", L"tls1.3, tls1.2[seven]"})
        {
            auto u = MakeHttpsRestUpload();
            u.rest.TlsVersions = Apply(value).TlsVersions;
            u.rest.TlsOsExceptions = Apply(value).TlsOsExceptions;
            Assert::IsTrue(u.Validate().verdict == U::Verdict::Ok, value);
        }
    }

    TEST_METHOD(ConfigToTransportBitsMapOneToOne)
    {
        const std::pair<uint8_t, uint8_t> bits[] = {
            {U::kTls1_0, HttpTlsVersion::kTls1_0},
            {U::kTls1_1, HttpTlsVersion::kTls1_1},
            {U::kTls1_2, HttpTlsVersion::kTls1_2},
            {U::kTls1_3, HttpTlsVersion::kTls1_3}};

        uint8_t all = 0;
        for (const auto& [config, transport] : bits)
        {
            Assert::AreEqual(static_cast<int>(transport), static_cast<int>(ToHttpTlsVersions(config)));
            all |= transport;
        }

        Assert::AreEqual(static_cast<int>(all), static_cast<int>(ToHttpTlsVersions(kAllKnown)));
        Assert::AreEqual(0, static_cast<int>(ToHttpTlsVersions(0)));
        Assert::AreEqual(0, static_cast<int>(ToHttpTlsVersions(static_cast<uint8_t>(~kAllKnown))));
    }

    // The WinHTTP mask actually set, end to end, for representative configurations on each kind of host.
    TEST_METHOD(EndToEndWinHttpMask)
    {
        struct Case
        {
            const wchar_t* value;  // nullptr = the built-in default
            OsVersion os;
            DWORD mask;
        };

        const DWORD tls12 = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        const DWORD tls13 = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;

        const Case cases[] = {
            {nullptr, {10, 0}, tls13},
            {nullptr, {6, 1}, tls13},  // no default exception for 7: the 1.3 floor is kept, not lowered
            {nullptr, {5, 1}, tls12},  // the default XP exception
            {nullptr, {0, 0}, tls13},  // OS lookup failed: no exception applies
            {L"tls1.2", {10, 0}, tls12 | tls13},
            {L"tls1.2", {6, 1}, tls12},
            {L"tls1.3, tls1.2[seven]", {6, 1}, tls12},
            {L"tls1.3, tls1.2[seven]", {5, 1}, tls13},  // an explicit value replaces the default XP exception
            {L"garbage", {5, 1}, tls12},
            {L"garbage", {10, 0}, tls13},
            {L"tls1.3, tls1.0[vista]", {6, 0}, tls13}};

        for (const auto& c : cases)
        {
            const U::RestConfig rest = c.value ? Apply(c.value) : U::RestConfig {};
            const DWORD mask = ToWinHttpProtocols(ToHttpTlsVersions(Resolve(rest, c.os)));
            const std::wstring what = Describe(c.value ? c.value : L"default", c.os);
            Assert::AreEqual(static_cast<int>(c.mask), static_cast<int>(mask), what.c_str());
        }
    }

    // os-default is allowed without 'insecure', but embedding says the TLS 1.2 proof happens per connection.
    TEST_METHOD(OsDefaultWithoutInsecureWarnsThatTls12IsProvenPerConnection)
    {
        auto u = MakeHttpsRestUpload();
        ApplyHttpsSecurity(L"os-default", u.rest);

        const auto warned = u.Validate();
        Assert::IsTrue(warned.verdict == U::Verdict::Warn, warned.message.c_str());
        Assert::IsTrue(warned.message.find(L"TLS 1.2") != std::wstring::npos, warned.message.c_str());

        u.rest.Insecure = true;
        Assert::IsTrue(u.Validate().verdict != U::Verdict::Refuse, L"insecure accepts the system policy");

        auto max = MakeHttpsRestUpload();
        ApplyHttpsSecurity(L"os-max", max.rest);
        const auto locked = max.Validate();
        Assert::IsTrue(locked.verdict == U::Verdict::Ok, locked.message.c_str());
    }

    // Only "os-default" produces the 0 that leaves the system TLS policy in charge. An enforced floor enables its
    // set, and "os-max" hands HttpClient candidates to lock on, never below TLS 1.2 without 'insecure'.
    TEST_METHOD(OnlyOsDefaultLeavesTheSystemPolicy)
    {
        const uint8_t below12 = HttpTlsVersion::kTls1_0 | HttpTlsVersion::kTls1_1;
        auto highest = [](uint8_t versions) {
            uint8_t top = 0;
            for (uint8_t bit = 1; bit != 0; bit = static_cast<uint8_t>(bit << 1))
            {
                if (versions & bit)
                {
                    top = bit;
                }
            }

            return static_cast<int>(top);
        };

        auto enforced = MakeHttpsRestUpload();
        const HttpClientConfig enforcedCfg = BuildHttpClientConfig(enforced);
        Assert::AreNotEqual(0, static_cast<int>(enforcedCfg.tlsVersions));
        Assert::AreEqual(0, enforcedCfg.tlsVersions & below12);
        Assert::IsFalse(enforcedCfg.tlsLockHighest);

        auto osDefault = MakeHttpsRestUpload();
        ApplyHttpsSecurity(L"os-default", osDefault.rest);
        const HttpClientConfig osDefaultCfg = BuildHttpClientConfig(osDefault);
        Assert::AreEqual(0, static_cast<int>(osDefaultCfg.tlsVersions));
        Assert::IsFalse(osDefaultCfg.tlsLockHighest);

        auto osMax = MakeHttpsRestUpload();
        ApplyHttpsSecurity(L"os-max", osMax.rest);
        const HttpClientConfig osMaxCfg = BuildHttpClientConfig(osMax);
        Assert::IsTrue(osMaxCfg.tlsLockHighest);
        Assert::AreNotEqual(0, static_cast<int>(osMaxCfg.tlsVersions));
        Assert::AreEqual(0, osMaxCfg.tlsVersions & below12);

        // 'insecure' may add lower versions below the others, never change the one tried first.
        osMax.rest.Insecure = true;
        const HttpClientConfig insecureCfg = BuildHttpClientConfig(osMax);
        Assert::IsTrue(insecureCfg.tlsLockHighest);
        Assert::AreEqual(highest(osMaxCfg.tlsVersions), highest(insecureCfg.tlsVersions));
        Assert::AreEqual(
            static_cast<int>(osMaxCfg.tlsVersions), static_cast<int>(insecureCfg.tlsVersions & osMaxCfg.tlsVersions));
    }

    // os-max candidates: TLS 1.3 alone from the first build whose SChannel client negotiates it, even with
    // 'insecure', so a modern host never falls back to TLS 1.2. Before it, TLS 1.2, plus TLS 1.1/1.0 only with
    // 'insecure'. TLS 1.3 on an older build would fail every handshake on Windows 10; a lower version next to
    // TLS 1.3, or below TLS 1.2 without 'insecure', would be a downgrade.
    TEST_METHOD(OsMaxCandidatesFollowTheBuildAndInsecure)
    {
        const uint8_t tls12 = U::kTls1_2;
        const uint8_t tls13 = U::kTls1_3;
        const uint8_t below12 = U::kTls1_0 | U::kTls1_1;

        struct Case
        {
            DWORD build;
            uint8_t secure;
            uint8_t insecure;
        };

        // 0: a build number of 0. 2600 XP, 3790 2003, 7601 7, 9600 8.1, 19045 10 22H2, 20347/20348 the Server
        // 2022 boundary, 22000 11 21H2, 26200 11 25H2, 0xFFFFFFFF a build no one has seen yet.
        const Case cases[] = {
            {0, tls12, tls12 | below12},
            {2600, tls12, tls12 | below12},
            {3790, tls12, tls12 | below12},
            {7601, tls12, tls12 | below12},
            {9600, tls12, tls12 | below12},
            {19045, tls12, tls12 | below12},
            {20347, tls12, tls12 | below12},
            {20348, tls13, tls13},
            {22000, tls13, tls13},
            {26200, tls13, tls13},
            {0xFFFFFFFF, tls13, tls13}};

        for (const auto& c : cases)
        {
            const std::wstring what = L"build " + std::to_wstring(c.build);
            Assert::AreEqual(
                static_cast<int>(c.secure), static_cast<int>(OsMaxTlsCandidates(c.build, false)), what.c_str());
            Assert::AreEqual(
                static_cast<int>(c.insecure), static_cast<int>(OsMaxTlsCandidates(c.build, true)), what.c_str());
        }

        // GetOSVersion failed: no candidate, with or without 'insecure'. Taking the build as 0 would lock a
        // TLS 1.3 host on TLS 1.2.
        Assert::AreEqual(0, static_cast<int>(OsMaxTlsCandidates(std::nullopt, false)));
        Assert::AreEqual(0, static_cast<int>(OsMaxTlsCandidates(std::nullopt, true)));
    }
};

// {upload.random:<charset>:<length>[:<id>]} placeholder support.
TEST_CLASS(RestRandomTest)
{
    static bool Valid(const wchar_t* spec) { std::wstring e; return IsValidRestRandomSpec(spec, e); }

public:
    TEST_METHOD(ValidSpecsAccepted)
    {
        Assert::IsTrue(Valid(L"hex:4"));
        Assert::IsTrue(Valid(L"num:1"));
        Assert::IsTrue(Valid(L"alpha:64"));
        Assert::IsTrue(Valid(L"alnum:8"));
        Assert::IsTrue(Valid(L"hex:4:shard1"), L"an id must be accepted");
    }

    TEST_METHOD(InvalidSpecsRejected)
    {
        Assert::IsFalse(Valid(L"hex"), L"missing length");
        Assert::IsFalse(Valid(L"hex:0"), L"length 0");
        Assert::IsFalse(Valid(L"hex:65"), L"length > 64");
        Assert::IsFalse(Valid(L"hex:abc"), L"non-numeric length");
        Assert::IsFalse(Valid(L"b64:4"), L"unknown charset");
        Assert::IsFalse(Valid(L""), L"empty");
        Assert::IsFalse(Valid(L"hex:+4"), L"leading sign rejected");
        Assert::IsFalse(Valid(L"hex: 4"), L"leading space rejected");
    }

    TEST_METHOD(GeneratesRequestedLengthAndCharset)
    {
        const std::wstring hex = MakeRestRandom(L"hex:32");
        Assert::AreEqual<size_t>(32, hex.size());
        Assert::IsTrue(hex.find_first_not_of(L"0123456789abcdef") == std::wstring::npos, L"hex charset");

        const std::wstring alpha = MakeRestRandom(L"alpha:16");
        Assert::AreEqual<size_t>(16, alpha.size());
        Assert::IsTrue(alpha.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz") == std::wstring::npos, L"alpha charset");

        const std::wstring num = MakeRestRandom(L"num:10:withid");  // id ignored for generation
        Assert::AreEqual<size_t>(10, num.size());
        Assert::IsTrue(num.find_first_not_of(L"0123456789") == std::wstring::npos, L"num charset");
    }

    TEST_METHOD(InvalidSpecGeneratesEmpty)
    {
        Assert::IsTrue(MakeRestRandom(L"nope:4").empty());
        Assert::IsTrue(MakeRestRandom(L"hex:0").empty());
    }

    TEST_METHOD(IndependentDrawsDiffer)
    {
        // Two independent draws of a wide spec are overwhelmingly unlikely to collide (16^16).
        Assert::AreNotEqual(MakeRestRandom(L"hex:16"), MakeRestRandom(L"hex:16"));
    }

    // A malformed random token in a template is refused at embed (not silently empty at runtime).
    TEST_METHOD(MalformedTokenInTemplateRefused)
    {
        OutputSpec::Upload u;
        u.Method = OutputSpec::UploadMethod::REST;
        u.bitsMode = OutputSpec::BITSMode::HTTPS;
        u.Mode = OutputSpec::UploadMode::Synchronous;
        u.ServerName = L"host.example";
        u.AuthScheme = OutputSpec::UploadAuthScheme::Anonymous;
        RESTStep body;
        body.method = L"PUT";
        body.path = L"/{upload.random:bad:4}/{upload.filename}";  // bad charset
        body.hasBody = true;
        u.rest.RestSteps.push_back(std::move(body));
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"expected Refuse for bad random spec");
    }

    // body= and offset= are substituted at runtime too, so a malformed random there is refused at embed.
    TEST_METHOD(MalformedTokenInBodyRefused)
    {
        OutputSpec::Upload u;
        u.Method = OutputSpec::UploadMethod::REST;
        u.bitsMode = OutputSpec::BITSMode::HTTPS;
        u.Mode = OutputSpec::UploadMode::Synchronous;
        u.ServerName = L"host.example";
        u.AuthScheme = OutputSpec::UploadAuthScheme::Anonymous;
        RESTStep body;
        body.method = L"PUT";
        body.path = L"/{upload.filename}";
        body.bodySource = L"{upload.random:nope:4}";  // bad charset in body= (now validated)
        body.hasBody = true;
        u.rest.RestSteps.push_back(std::move(body));
        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"expected Refuse for bad random in body=");
    }
};

// body= selects the LOCAL file that is read and streamed to the server, so it must never be driven by a
// server-controlled <response> capture (FS-001: a capture in body= lets a malicious/MITM server redirect
// CreateFileW to an arbitrary local file or UNC path). It may only be a literal path or an 'upload.*' built-in.
TEST_CLASS(RestBodyCaptureValidateTest)
{
    static OutputSpec::Upload MakeUpload()
    {
        OutputSpec::Upload u;
        u.Method = OutputSpec::UploadMethod::REST;
        u.bitsMode = OutputSpec::BITSMode::HTTPS;
        u.Mode = OutputSpec::UploadMode::Synchronous;
        u.ServerName = L"host.example";
        u.AuthScheme = OutputSpec::UploadAuthScheme::Anonymous;
        return u;
    }

    // A capture with a real source, so a test isolates the body= rule rather than the "capture needs a source" rule.
    static RESTResponseCapture MakeCapture(const wchar_t* name)
    {
        RESTResponseCapture cap;
        cap.source = RESTResponseCapture::Source::Header;
        cap.key = L"content-length";
        cap.name = name;
        cap.defaultValue = L"0";
        return cap;
    }

public:
    // The FS-001 pattern: a server-controlled capture substituted into body= must be refused at embed time.
    TEST_METHOD(CaptureInBodyRefused)
    {
        auto u = MakeUpload();
        RESTStep probe;
        probe.method = L"HEAD";
        probe.path = L"/{upload.filename}";
        probe.acceptStatus.insert(404);
        probe.captures.push_back(MakeCapture(L"src"));
        u.rest.RestSteps.push_back(std::move(probe));

        RESTStep body;
        body.method = L"PUT";
        body.path = L"/{upload.filename}";
        body.bodySource = L"{src}";  // references the server-controlled capture -> arbitrary local file
        body.hasBody = true;
        u.rest.RestSteps.push_back(std::move(body));

        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"a <response> capture in body= must be refused");
    }

    // The intended pattern: body= is the trusted {upload.local_path} built-in; a capture may still feed offset=.
    TEST_METHOD(BuiltinBodyWithCaptureInOffsetOk)
    {
        auto u = MakeUpload();
        RESTStep probe;
        probe.method = L"HEAD";
        probe.path = L"/{upload.filename}";
        probe.acceptStatus.insert(404);
        probe.captures.push_back(MakeCapture(L"remote_size"));
        u.rest.RestSteps.push_back(std::move(probe));

        RESTStep body;
        body.method = L"PUT";
        body.path = L"/{upload.filename}";
        body.bodySource = L"{upload.local_path}";  // trusted built-in
        body.offsetExpr = L"{remote_size}";        // a capture in offset= is fine (ResolveOffset clamps it)
        body.hasBody = true;
        u.rest.RestSteps.push_back(std::move(body));

        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    // Regression guard: a literal path containing braces that is NOT a declared capture name (e.g. a volume
    // GUID path) must not be mistaken for a capture reference and refused.
    TEST_METHOD(LiteralBracePathOk)
    {
        auto u = MakeUpload();
        RESTStep body;
        body.method = L"PUT";
        body.path = L"/{upload.filename}";
        body.bodySource = LR"(\\?\Volume{12345678-1234-1234-1234-1234567890ab}\dump.bin)";
        body.hasBody = true;
        u.rest.RestSteps.push_back(std::move(body));

        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    // The reserved 'upload.' namespace can never be a capture name, so a server value can't shadow a built-in.
    TEST_METHOD(CaptureNamedUploadPrefixRefused)
    {
        auto u = MakeUpload();
        RESTStep body;
        body.method = L"PUT";
        body.path = L"/{upload.filename}";
        body.hasBody = true;
        body.captures.push_back(MakeCapture(L"upload.total_size"));  // reserved prefix
        u.rest.RestSteps.push_back(std::move(body));

        const auto v = u.Validate();
        Assert::IsTrue(
            v.verdict == OutputSpec::Upload::Verdict::Refuse, L"a capture under the reserved 'upload.' prefix must be refused");
    }

    // A typo'd {upload.*} built-in in body= is refused at embed; otherwise it is left literal at runtime
    // and silently breaks the request (CreateFileW on a path containing '{upload.locl_path}').
    TEST_METHOD(UnknownUploadBuiltinInBodyRefused)
    {
        auto u = MakeUpload();
        RESTStep body;
        body.method = L"PUT";
        body.path = L"/{upload.filename}";
        body.bodySource = L"{upload.locl_path}";  // typo of upload.local_path
        body.hasBody = true;
        u.rest.RestSteps.push_back(std::move(body));

        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"a typo'd upload.* built-in must be refused");
    }

    // The same check applies to every template, not just body= (here a typo in the request path).
    TEST_METHOD(UnknownUploadBuiltinInPathRefused)
    {
        auto u = MakeUpload();
        RESTStep body;
        body.method = L"PUT";
        body.path = L"/{upload.filenam}";  // typo of upload.filename
        body.bodySource = L"{upload.local_path}";
        body.hasBody = true;
        u.rest.RestSteps.push_back(std::move(body));

        const auto v = u.Validate();
        Assert::IsTrue(
            v.verdict == OutputSpec::Upload::Verdict::Refuse, L"a typo'd upload.* built-in in path must be refused");
    }

    // Every real {upload.*} built-in (across path, query, headers, body) is accepted.
    TEST_METHOD(KnownUploadBuiltinsAccepted)
    {
        auto u = MakeUpload();
        RESTStep body;
        body.method = L"PUT";
        body.path = L"/{upload.filename}";
        body.query = L"?s={upload.range_start}&l={upload.range_length}&e={upload.range_end}&t={upload.total_size}";
        body.headers.push_back(L"X-Name: {upload.filename_base64}");
        body.bodySource = L"{upload.local_path}";
        body.hasBody = true;
        u.rest.RestSteps.push_back(std::move(body));

        const auto v = u.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }
};

#if ORC_REST_ENABLE_DAV_PROPERTY
// ExtractDavProperty parses an attacker-controlled PROPFIND 207 body, so it must be both correct and
// bounded (no quadratic blow-up on hostile input). Compiled only when DAV-property captures are enabled.
TEST_CLASS(RestDavParseTest)
{
public:
    TEST_METHOD(ExtractsValueWithNamespacePrefix)
    {
        Assert::AreEqual(
            std::wstring(L"1234"),
            ExtractDavProperty(L"<D:multistatus><D:getcontentlength>1234</D:getcontentlength></D:multistatus>",
                L"getcontentlength"));
    }

    TEST_METHOD(ExtractsValueWithoutPrefix)
    {
        Assert::AreEqual(std::wstring(L"7"), ExtractDavProperty(L"<getcontentlength>7</getcontentlength>", L"getcontentlength"));
    }

    TEST_METHOD(SelfClosedAndAbsentReturnEmpty)
    {
        Assert::IsTrue(ExtractDavProperty(L"<getcontentlength/>", L"getcontentlength").empty());
        Assert::IsTrue(ExtractDavProperty(L"<D:other>x</D:other>", L"getcontentlength").empty());
        Assert::IsTrue(ExtractDavProperty(L"", L"getcontentlength").empty());
    }

    // Regression for the O(n^2) DoS: a long delimiter-free run of '<' before the real element must be
    // parsed in bounded time. Pre-fix this body would spin for ~10^10 comparisons; post-fix it is linear.
    TEST_METHOD(PathologicalManyAnglesIsBoundedAndCorrect)
    {
        std::wstring body(200000, L'<');
        body += L"<D:getcontentlength>42</D:getcontentlength>";
        Assert::AreEqual(std::wstring(L"42"), ExtractDavProperty(body, L"getcontentlength"));
    }

    // A property value arrives entity-encoded like any other XML text. Before this it was returned raw,
    // so a displayname of "a&amp;b" reached the caller as the literal seven characters.
    TEST_METHOD(EntitiesAreDecoded)
    {
        Assert::AreEqual(
            std::wstring(L"a&b"), ExtractDavProperty(L"<D:displayname>a&amp;b</D:displayname>", L"displayname"));
        Assert::AreEqual(
            std::wstring(L"<x>"), ExtractDavProperty(L"<D:displayname>&lt;x&gt;</D:displayname>", L"displayname"));

        // XML's named apostrophe, which the HTML table did not carry.
        Assert::AreEqual(
            std::wstring(L"it's"), ExtractDavProperty(L"<D:displayname>it&apos;s</D:displayname>", L"displayname"));
    }

    // One pass, the same guarantee the form-input scan has: "&amp;lt;" becomes "&lt;" and must NOT
    // become "<", or a server could smuggle a character past a caller's later checks.
    TEST_METHOD(EntitiesAreDecodedOnce)
    {
        Assert::AreEqual(
            std::wstring(L"&lt;"),
            ExtractDavProperty(L"<D:displayname>&amp;lt;</D:displayname>", L"displayname"));
    }

    // Literal padding around the value is trimmed; an entity the table does not carry is left alone
    // rather than guessed at.
    TEST_METHOD(SurroundingWhitespaceIsTrimmed)
    {
        Assert::AreEqual(
            std::wstring(L"x"), ExtractDavProperty(L"<D:displayname>   x   </D:displayname>", L"displayname"));
        Assert::AreEqual(
            std::wstring(L"&#32;x"), ExtractDavProperty(L"<D:displayname>&#32;x</D:displayname>", L"displayname"));
    }

    // A property spelled inside <!-- --> is not a property. An XML parser ignores it, so neither may we,
    // or a body could carry one value for the server's real clients and another for us.
    TEST_METHOD(APropertyInsideACommentIsIgnored)
    {
        Assert::IsTrue(
            ExtractDavProperty(L"<!--<D:getcontentlength>99</D:getcontentlength>-->", L"getcontentlength").empty());

        // The commented one must not shadow the real one that follows.
        Assert::AreEqual(
            std::wstring(L"42"),
            ExtractDavProperty(
                L"<!--<D:getcontentlength>99</D:getcontentlength>--><D:getcontentlength>42</D:getcontentlength>",
                L"getcontentlength"));
    }

    // Element names are case-SENSITIVE in XML, unlike the HTML tag names the form-input scan folds.
    TEST_METHOD(ElementNamesAreCaseSensitive)
    {
        Assert::IsTrue(ExtractDavProperty(L"<D:GetContentLength>7</D:GetContentLength>", L"getcontentlength").empty());
    }
};
#endif  // ORC_REST_ENABLE_DAV_PROPERTY

#if !ORC_REST_ENABLE_DAV_PROPERTY
// With DAV-property captures compiled out (the default), a config that still asks for one must be
// refused at embed time (fail closed), never silently ignored at runtime.
TEST_CLASS(RestDavPropertyDisabledTest)
{
public:
    TEST_METHOD(DavPropertyCaptureRefusedWhenCompiledOut)
    {
        OutputSpec::Upload u;
        u.Method = OutputSpec::UploadMethod::REST;
        u.bitsMode = OutputSpec::BITSMode::HTTPS;
        u.Mode = OutputSpec::UploadMode::Synchronous;
        u.ServerName = L"host.example";
        u.AuthScheme = OutputSpec::UploadAuthScheme::Anonymous;

        RESTStep probe;
        probe.method = L"PROPFIND";
        probe.path = L"/{upload.filename}";
        RESTResponseCapture cap;
        cap.source = RESTResponseCapture::Source::DavProperty;
        cap.key = L"getcontentlength";
        cap.name = L"remote_size";
        probe.captures.push_back(std::move(cap));
        u.rest.RestSteps.push_back(std::move(probe));

        RESTStep body;
        body.method = L"PUT";
        body.path = L"/{upload.filename}";
        body.hasBody = true;
        u.rest.RestSteps.push_back(std::move(body));

        const auto v = u.Validate();
        Assert::IsTrue(
            v.verdict == OutputSpec::Upload::Verdict::Refuse, L"expected Refuse for dav_property when compiled out");
    }
};
#endif  // !ORC_REST_ENABLE_DAV_PROPERTY

// repeat="Once" remote check: which steps may run before anything is uploaded, which templates it cannot resolve,
// and which sizes it believes.
TEST_CLASS(RestRemoteCheckTest)
{
public:
    // Only methods that read remote state may run: a check must never create, move or upload anything.
    TEST_METHOD(OnlyReadOnlyMethodsRunBeforeAnUpload)
    {
        for (const auto* method : {L"HEAD", L"GET", L"OPTIONS", L"PROPFIND", L"head", L"Propfind"})
        {
            Assert::IsTrue(IsReadOnlyRestMethod(method), method);
        }

        for (const auto* method :
             {L"PUT", L"POST", L"PATCH", L"MKCOL", L"MOVE", L"COPY", L"DELETE", L"LOCK", L"", L"HEADX"})
        {
            Assert::IsFalse(IsReadOnlyRestMethod(method), method);
        }
    }

    // Placeholders tied to the file being uploaded, or drawn per file, cannot be resolved by a check.
    TEST_METHOD(FileBoundPlaceholdersAreRecognized)
    {
        for (const auto* tmpl :
             {L"{upload.local_path}", L"?total={upload.total_size}", L"bytes {upload.range_start}-{upload.range_end}",
              L"/up/{upload.random:hex:8}/{upload.filename}", L"{upload.range_length}"})
        {
            Assert::IsTrue(UsesFileBoundPlaceholder(tmpl), tmpl);
        }

        for (const auto* tmpl :
             {L"", L"/up/{upload.filename}", L"{upload.filename_base64}", L"{remote_size}", L"/up/upload.total_size",
              L"{upload.random"})
        {
            Assert::IsFalse(UsesFileBoundPlaceholder(tmpl), tmpl);
        }
    }

    // The remote file named in messages always has one leading '/' and one '/' before its name, whatever the root.
    TEST_METHOD(RemoteUrlPathJoinsRootAndName)
    {
        const std::tuple<const wchar_t*, const wchar_t*, const wchar_t*> cases[] = {
            {L"", L"SystemInfo.7z", L"/SystemInfo.7z"},
            {L"/", L"SystemInfo.7z", L"/SystemInfo.7z"},
            {L"up", L"SystemInfo.7z", L"/up/SystemInfo.7z"},
            {L"/up", L"SystemInfo.7z", L"/up/SystemInfo.7z"},
            {L"/up/", L"SystemInfo.7z", L"/up/SystemInfo.7z"},
            {L"\\up\\in\\", L"SystemInfo.7z", L"/up/in/SystemInfo.7z"},
            {L"/up", L"/SystemInfo.7z", L"/up/SystemInfo.7z"},
            {L"/up/", L"", L"/up/"}};

        for (const auto& [root, name, expected] : cases)
        {
            const std::wstring what = std::wstring(L"'") + root + L"' + '" + name + L"'";
            Assert::AreEqual(std::wstring(expected), RemoteUrlPath(root, name), what.c_str());
        }
    }

    // A remote size is believed only when it is a plain number: unlike a resume offset, nothing defaults to 0.
    TEST_METHOD(RemoteSizeIsParsedStrictly)
    {
        Assert::AreEqual(uint64_t(0), ParseRemoteSize(L"0").value());
        Assert::AreEqual(uint64_t(891), ParseRemoteSize(L" 891 ").value());
        Assert::AreEqual(uint64_t(18446744073709551615ull), ParseRemoteSize(L"18446744073709551615").value());

        for (const auto* text :
             {L"", L" ", L"-1", L"+5", L"12 34", L"0x10", L"1e3", L"{remote_size}", L"18446744073709551616",
              L"123456789012345678901"})
        {
            Assert::IsFalse(ParseRemoteSize(text).has_value(), text);
        }
    }
};

// The 'uri' spelling of an upload target: scheme, host, PORT and credentials in one value. It is the
// only address attribute that carries a credential, and the only one that is environment-expanded, so
// it is how an embedded configuration keeps a real password out of the file. That only works if every
// part of it survives parsing - the port used to be dropped, silently sending the upload to 443.
TEST_CLASS(UploadUriParsingTest)
{
private:
    // Parse an inline <upload> the way the config engine does, without a fixture on disk.
    static HRESULT ReadInlineUpload(const std::string& xmlUtf8, OutputSpec::Upload& upload)
    {
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

        auto stream = std::make_shared<MemoryStream>();
        hr = stream->OpenForReadOnly(const_cast<char*>(xmlUtf8.data()), xmlUtf8.size());
        if (FAILED(hr))
        {
            return hr;
        }

        ConfigFileReader reader;
        hr = reader.ReadConfig(stream, root);
        if (FAILED(hr))
        {
            return hr;
        }

        return upload.Configure(root.SubItems[0]);
    }

    static std::string MakeUploadXml(const std::string& uri)
    {
        return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
               "<dfir-orc>\n"
               "  <upload method=\"REST\" uri=\"" + uri + "\">\n"
               "    <rest http_method=\"PUT\" path=\"/{upload.filename}\" body=\"{upload.local_path}\"/>\n"
               "  </upload>\n"
               "</dfir-orc>\n";
    }

public:
    // The regression: an explicit port in 'uri' must reach ServerName, as it does with 'server'.
    TEST_METHOD(UriPortReachesServerName)
    {
        OutputSpec::Upload upload;
        Assert::IsTrue(SUCCEEDED(ReadInlineUpload(MakeUploadXml("https://host.example:8448"), upload)));
        Assert::AreEqual(std::wstring(L"host.example:8448"), upload.ServerName);
    }

    TEST_METHOD(UriWithoutPortLeavesServerNameBare)
    {
        OutputSpec::Upload upload;
        Assert::IsTrue(SUCCEEDED(ReadInlineUpload(MakeUploadXml("https://host.example"), upload)));
        Assert::AreEqual(std::wstring(L"host.example"), upload.ServerName);
    }

    // What the whole 'uri' spelling exists for: host, port AND credential out of one value, so the
    // configuration can take it from %ORC_UPLOAD_URI% and carry no password of its own.
    TEST_METHOD(UriCarriesCredentialsAndPortTogether)
    {
        OutputSpec::Upload upload;
        Assert::IsTrue(
            SUCCEEDED(ReadInlineUpload(MakeUploadXml("https://orc:s3cr3t@host.example:8448"), upload)));
        Assert::AreEqual(std::wstring(L"host.example:8448"), upload.ServerName);
        Assert::AreEqual(std::wstring(L"orc"), upload.UserName);
        Assert::AreEqual(std::wstring(L"s3cr3t"), upload.Password);
    }

    // The port now goes through the same embed-time check as 'server', so an out-of-range value is
    // refused rather than reaching WinHTTP.
    TEST_METHOD(UriPortOutOfRangeRefused)
    {
        OutputSpec::Upload upload;
        Assert::IsTrue(SUCCEEDED(ReadInlineUpload(MakeUploadXml("https://host.example:99999"), upload)));
        upload.AuthScheme = OutputSpec::UploadAuthScheme::Anonymous;
        upload.Mode = OutputSpec::UploadMode::Synchronous;
        const auto v = upload.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"an out-of-range uri port must refuse");
    }
};

// retry / retry_delay: the per-<upload> retry policy. Two things must hold beyond "the value parses".
// The defaults must keep the historical single-attempt behaviour, since every configuration written
// before these attributes existed carries them implicitly. And a value that cannot be read must fall
// back to the default rather than to zero: a retry_delay silently read as 0 would turn a typo into a
// tight retry loop against a destination that is already in trouble.
TEST_CLASS(UploadRetryPolicyTest)
{
private:
    // Parse an inline <upload> through the real schema, so the attribute registration is covered too.
    static HRESULT ReadInlineUpload(const std::string& xmlUtf8, OutputSpec::Upload& upload)
    {
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

        auto stream = std::make_shared<MemoryStream>();
        hr = stream->OpenForReadOnly(const_cast<char*>(xmlUtf8.data()), xmlUtf8.size());
        if (FAILED(hr))
        {
            return hr;
        }

        ConfigFileReader reader;
        hr = reader.ReadConfig(stream, root);
        if (FAILED(hr))
        {
            return hr;
        }

        return upload.Configure(root.SubItems[0]);
    }

    // 'attrs' goes on the <upload> element, so each test writes only what it is about.
    static std::string MakeUploadXml(const std::string& attrs)
    {
        return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
               "<dfir-orc>\n"
               "  <upload method=\"REST\" server=\"https://host.example\" " + attrs + ">\n"
               "    <rest http_method=\"PUT\" path=\"/{upload.filename}\" body=\"{upload.local_path}\"/>\n"
               "  </upload>\n"
               "</dfir-orc>\n";
    }

    // BITS, for the async rules: REST refuses mode="async" on its own, so a REST fixture could not tell
    // the retry rule apart from that older refusal.
    static std::string MakeBitsXml(const std::string& attrs)
    {
        return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
               "<dfir-orc>\n"
               "  <upload method=\"BITS\" server=\"https://host.example\" " + attrs + "/>\n"
               "</dfir-orc>\n";
    }

    static OutputSpec::Upload Read(const std::string& attrs)
    {
        OutputSpec::Upload upload;
        Assert::IsTrue(SUCCEEDED(ReadInlineUpload(MakeUploadXml(attrs), upload)), L"the fixture must parse");
        return upload;
    }

    static OutputSpec::Upload ReadBits(const std::string& attrs)
    {
        OutputSpec::Upload upload;
        Assert::IsTrue(SUCCEEDED(ReadInlineUpload(MakeBitsXml(attrs), upload)), L"the fixture must parse");
        return upload;
    }

public:
    // No attributes at all is the shape of every configuration written before this feature existed.
    TEST_METHOD(DefaultsAreNoRetryAndThirtySeconds)
    {
        const auto upload = Read("");
        Assert::AreEqual(0u, upload.Retry, L"retry must default to 0 (one attempt, as before)");
        Assert::AreEqual(30LL, static_cast<long long>(upload.RetryDelay.count()), L"retry_delay defaults to 30s");
    }

    TEST_METHOD(ValuesAreRead)
    {
        const auto upload = Read("retry=\"2\" retry_delay=\"5\"");
        Assert::AreEqual(2u, upload.Retry);
        Assert::AreEqual(5LL, static_cast<long long>(upload.RetryDelay.count()));
    }

    // retry_delay="0" is a legitimate ask (retry at once), and must survive as 0 rather than be
    // mistaken for "unset" and replaced by the default.
    TEST_METHOD(ZeroDelayIsKept)
    {
        const auto upload = Read("retry=\"1\" retry_delay=\"0\"");
        Assert::AreEqual(1u, upload.Retry);
        Assert::AreEqual(0LL, static_cast<long long>(upload.RetryDelay.count()));
    }

    // The PARSER keeps the defaults rather than inventing a value; the refusal is Validate's job (see
    // UnparseableRetryIsRefused). Both halves matter: a value that cannot be read must neither retry on
    // its own terms nor shorten the wait, in case a caller ever reads these before validating.
    TEST_METHOD(UnparseableValuesKeepTheDefaults)
    {
        const auto upload = Read("retry=\"abc\" retry_delay=\"soon\"");
        Assert::AreEqual(0u, upload.Retry, L"an unreadable retry must not retry");
        Assert::AreEqual(30LL, static_cast<long long>(upload.RetryDelay.count()), L"and must not shorten the wait");
    }

    TEST_METHOD(NegativeValuesKeepTheDefaults)
    {
        const auto upload = Read("retry=\"-1\" retry_delay=\"-30\"");
        Assert::AreEqual(0u, upload.Retry);
        Assert::AreEqual(30LL, static_cast<long long>(upload.RetryDelay.count()));
    }

    TEST_METHOD(RetryIsAcceptedByValidate)
    {
        const auto v = Read("retry=\"3\" retry_delay=\"10\"").Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    // A value in milliseconds is the mistake this bound exists to catch, and it must be caught at embed
    // time rather than at the end of a collection.
    TEST_METHOD(DelayAboveTheCapIsRefused)
    {
        const auto v = Read("retry=\"1\" retry_delay=\"30000\"").Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"a delay past the cap must be refused");
    }

    // The cap itself is still allowed: the rule is "above", not "at". One retry at the maximum delay is
    // also exactly the whole budget, which is why the two constants are equal.
    TEST_METHOD(DelayAtTheCapIsAccepted)
    {
        const auto v = Read("retry=\"1\" retry_delay=\"1800\"").Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }


    // The regression this bound exists for: GetIntegerFromArg defaults to radix 0, which auto-detects the
    // base, so a zero-padded value would be read as octal - "010" as 8 seconds rather than 10.
    TEST_METHOD(LeadingZeroIsDecimalNotOctal)
    {
        const auto upload = Read("retry=\"1\" retry_delay=\"010\"");
        Assert::AreEqual(10LL, static_cast<long long>(upload.RetryDelay.count()), L"010 is ten, not eight");
    }

    // Unreadable is refused, not silently defaulted: a mistyped retry would otherwise leave Retry at 0 and
    // the whole policy would vanish without a word.
    TEST_METHOD(UnparseableRetryIsRefused)
    {
        const auto upload = Read("retry=\"two\"");
        const auto v = upload.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"an unreadable retry must be refused");
        Assert::IsTrue(v.message.find(L"retry") != std::wstring::npos, v.message.c_str());
    }

    TEST_METHOD(NegativeRetryIsRefused)
    {
        const auto v = Read("retry=\"-1\"").Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse);
    }

    TEST_METHOD(RetryAboveTheCapIsRefused)
    {
        const auto v = Read("retry=\"11\" retry_delay=\"1\"").Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"retry past kMaxRetry must be refused");
    }

    // The rule that actually bounds how long a run can stand still: each value is individually legal here,
    // only their product is not.
    TEST_METHOD(RetryBudgetIsRefused)
    {
        const auto v = Read("retry=\"10\" retry_delay=\"1000\"").Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"the retry budget must be enforced");
        Assert::IsTrue(v.message.find(L"budget") != std::wstring::npos, v.message.c_str());
    }

    TEST_METHOD(RetryBudgetAtTheLimitIsAccepted)
    {
        const auto v = Read("retry=\"10\" retry_delay=\"180\"").Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Ok, v.message.c_str());
    }

    // ValidateRetryPolicy must never return Warn, and must never be reached in a way that hides a later
    // refusal: Validate() returns the FIRST non-Ok verdict, so a perfectly good retry policy on an upload
    // whose transport is refused must still come back Refuse.
    TEST_METHOD(AGoodRetryPolicyDoesNotMaskATransportRefusal)
    {
        OutputSpec::Upload upload;
        Assert::IsTrue(SUCCEEDED(ReadInlineUpload(
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<dfir-orc>\n"
            "  <upload method=\"REST\" server=\"http://host.example\" retry=\"2\" retry_delay=\"5\">\n"
            "    <rest http_method=\"PUT\" path=\"/{upload.filename}\" body=\"{upload.local_path}\"/>\n"
            "  </upload>\n"
            "</dfir-orc>\n",
            upload)));

        // Plain HTTP without 'insecure' is refused; the retry policy must not shadow that verdict.
        const auto v = upload.Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"an insecure transport must still refuse");
    }

    // An async entry finishes after DFIR-ORC exits and BITSAgent::IsComplete reports "done, no failure"
    // without inspecting the jobs, so no attempt is ever seen to fail and retry cannot do anything. That is
    // a warning, not a refusal: the attribute is harmless, and an operator may well have meant it for
    // another entry in the same list. ToolEmbed prints Warn verdicts with the config path.
    TEST_METHOD(RetryWithAsyncModeWarns)
    {
        const auto v = ReadBits("retry=\"2\" mode=\"async\"").Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Warn, L"retry with mode=async must warn");
        Assert::IsTrue(v.message.find(L"retry") != std::wstring::npos, v.message.c_str());
        Assert::IsTrue(v.message.find(L"async") != std::wstring::npos, v.message.c_str());
    }

    // The warning must not become a refusal by another name: the entry still uploads.
    TEST_METHOD(RetryWithAsyncModeIsNotRefused)
    {
        const auto v = ReadBits("retry=\"2\" mode=\"async\"").Validate();
        Assert::IsTrue(v.verdict != OutputSpec::Upload::Verdict::Refuse, v.message.c_str());
    }

    // A bad value is still refused even on an async entry: the hard rules run before the advisory.
    TEST_METHOD(AsyncDoesNotExcuseAnUnreadableRetry)
    {
        const auto v = ReadBits("retry=\"two\" mode=\"async\"").Validate();
        Assert::IsTrue(v.verdict == OutputSpec::Upload::Verdict::Refuse, L"an unreadable retry still refuses");
    }

    // Without retry, mode="async" is untouched by the new rule
    // Without retry, mode="async" is untouched by the new rule: an existing BITS configuration that has
    // always been allowed must stay allowed.
    TEST_METHOD(AsyncWithoutRetryIsUnaffected)
    {
        const auto v = ReadBits("mode=\"async\"").Validate();
        Assert::IsTrue(v.verdict != OutputSpec::Upload::Verdict::Refuse, v.message.c_str());
    }
};

// MakeServerAuthenticator is the single place a scheme becomes an object. Anonymous must build
// NOTHING: the null pointer is how "this upload presents no server credential" is expressed, so a
// scheme's code is unreachable for an upload that did not ask for it.
TEST_CLASS(ServerAuthenticatorTest)
{
private:
    static HttpClientConfig MakeConfig(HttpAuthScheme scheme, const std::wstring& user)
    {
        HttpClientConfig config;
        config.serverName = L"host.example";
        config.serverScheme = scheme;
        config.serverUser = user;
        config.serverPassword = L"secret";
        return config;
    }

public:
    TEST_METHOD(AnonymousBuildsNoAuthenticator)
    {
        const auto auth = MakeServerAuthenticator(MakeConfig(HttpAuthScheme::Anonymous, L""));
        Assert::IsTrue(auth == nullptr, L"Anonymous must build no authenticator");
    }

    // A credential-less Basic or integrated config also builds nothing: the old ApplyServerCredentials
    // guarded both branches with '!user.empty()', and that behaviour must carry over exactly.
    TEST_METHOD(EmptyUserBuildsNoAuthenticator)
    {
        Assert::IsTrue(
            MakeServerAuthenticator(MakeConfig(HttpAuthScheme::Basic, L"")) == nullptr,
            L"Basic with no user must build no authenticator");
        Assert::IsTrue(
            MakeServerAuthenticator(MakeConfig(HttpAuthScheme::NTLM, L"")) == nullptr,
            L"NTLM with no user must build no authenticator");
    }

    TEST_METHOD(BasicBuildsAnAuthenticator)
    {
        const auto auth = MakeServerAuthenticator(MakeConfig(HttpAuthScheme::Basic, L"orc"));
        Assert::IsTrue(auth != nullptr, L"Basic with a user must build an authenticator");
    }

    TEST_METHOD(EveryIntegratedSchemeBuildsAnAuthenticator)
    {
        for (const HttpAuthScheme scheme : {HttpAuthScheme::NTLM, HttpAuthScheme::Kerberos, HttpAuthScheme::Negotiate})
        {
            const auto auth = MakeServerAuthenticator(MakeConfig(scheme, L"ORCTEST\\orc"));
            Assert::IsTrue(auth != nullptr, L"an integrated scheme with a user must build an authenticator");
        }
    }

    // ApplyToHandle must fail closed rather than crash when handed no request handle: it is the last
    // gate before a request is sent, and a silent success there would send an unauthenticated request.
    TEST_METHOD(ApplyToNullHandleFails)
    {
        const auto auth = MakeServerAuthenticator(MakeConfig(HttpAuthScheme::Basic, L"orc"));
        Assert::IsTrue(auth != nullptr, L"precondition: Basic builds an authenticator");
        Assert::IsTrue(auth->ApplyToHandle(nullptr).has_error(), L"a null request handle must be an error");
    }
};

}  // namespace Orc::Test
