//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
#include "stdafx.h"

#include "Crypto/HmacSha256.h"
#include "Network/Http/Authenticator.h"
#include "Network/Http/AwsV4.h"
#include "Network/Http/HttpClient.h"

#include <array>
#include <string>
#include <vector>
#include <cwchar>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;

namespace Orc::Test {

TEST_CLASS(HmacSha256Test)
{
private:
    static std::string LowerHex(const std::array<uint8_t, 32>& digest)
    {
        static const char* kHex = "0123456789abcdef";
        std::string out;
        for (uint8_t b : digest)
        {
            out.push_back(kHex[(b >> 4) & 0x0F]);
            out.push_back(kHex[b & 0x0F]);
        }
        return out;
    }

    static BufferView View(const std::vector<uint8_t>& v)
    {
        return BufferView(v.data(), v.size());
    }

public:
    // RFC 4231 test case 1: 20 bytes of 0x0b as the key, "Hi There" as the data.
    TEST_METHOD(Rfc4231Case1)
    {
        const std::vector<uint8_t> key(20, 0x0b);
        const std::string data = "Hi There";
        const std::vector<uint8_t> dataBytes(data.begin(), data.end());

        const auto mac = HmacSha256(View(key), View(dataBytes));
        Assert::IsFalse(mac.has_error(), L"HmacSha256 failed");
        Assert::AreEqual(
            std::string("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"), LowerHex(*mac));
    }

    // RFC 4231 test case 2: a 4 byte key ("Jefe"), shorter than the 64 byte block.
    TEST_METHOD(Rfc4231Case2)
    {
        const std::string key = "Jefe";
        const std::string data = "what do ya want for nothing?";
        const std::vector<uint8_t> keyBytes(key.begin(), key.end());
        const std::vector<uint8_t> dataBytes(data.begin(), data.end());

        const auto mac = HmacSha256(View(keyBytes), View(dataBytes));
        Assert::IsFalse(mac.has_error(), L"HmacSha256 failed");
        Assert::AreEqual(
            std::string("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"), LowerHex(*mac));
    }

    // RFC 4231 test case 3: 20 bytes of 0xaa as the key, 50 bytes of 0xdd as the data.
    TEST_METHOD(Rfc4231Case3)
    {
        const std::vector<uint8_t> key(20, 0xaa);
        const std::vector<uint8_t> data(50, 0xdd);

        const auto mac = HmacSha256(View(key), View(data));
        Assert::IsFalse(mac.has_error(), L"HmacSha256 failed");
        Assert::AreEqual(
            std::string("773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"), LowerHex(*mac));
    }

    // An empty key is not a legitimate SigV4 input and must not silently produce a digest that looks
    // valid: the derivation chain would then be unauthenticated.
    TEST_METHOD(EmptyKeyIsRejected)
    {
        const std::vector<uint8_t> data(8, 0x01);
        Assert::IsTrue(HmacSha256(BufferView(), View(data)).has_error(), L"an empty key must be an error");
    }
};

TEST_CLASS(AwsV4EncodingTest)
{
public:
    // The unreserved set A-Za-z0-9-_.~ is never encoded. '~' is the trap: it is unreserved in RFC 3986
    // but sits in the WinINet-era "unsafe" set, so getting it wrong produces a 403 that names nothing.
    TEST_METHOD(UnreservedCharactersAreNotEncoded)
    {
        Assert::AreEqual(std::wstring(L"abcXYZ019-_.~"), AwsUriEncode(L"abcXYZ019-_.~", false));
    }

    // Everything else is percent-encoded with UPPERCASE hex.
    TEST_METHOD(ReservedCharactersAreEncodedUppercase)
    {
        Assert::AreEqual(std::wstring(L"a%20b"), AwsUriEncode(L"a b", false));
        Assert::AreEqual(std::wstring(L"a%2Bb"), AwsUriEncode(L"a+b", false));
        Assert::AreEqual(std::wstring(L"a%25b"), AwsUriEncode(L"a%b", false));
        Assert::AreEqual(std::wstring(L"a%3Fb"), AwsUriEncode(L"a?b", false));
        Assert::AreEqual(std::wstring(L"a%5Bb%5D"), AwsUriEncode(L"a[b]", false));
    }

    // In a path the separator survives; everywhere else it is encoded. S3 does not double-encode.
    TEST_METHOD(SlashIsKeptOnlyInPathMode)
    {
        Assert::AreEqual(std::wstring(L"/bucket/key"), AwsUriEncode(L"/bucket/key", true));
        Assert::AreEqual(std::wstring(L"%2Fbucket%2Fkey"), AwsUriEncode(L"/bucket/key", false));
    }

    // Non-ASCII is UTF-8 first, then percent-encoded per byte.
    TEST_METHOD(NonAsciiIsUtf8Encoded)
    {
        Assert::AreEqual(std::wstring(L"%C3%A9"), AwsUriEncode(L"\x00e9", false));
    }

    // The canonical query sorts by encoded name, then by encoded value.
    TEST_METHOD(QueryIsSortedAndEncoded)
    {
        Assert::AreEqual(std::wstring(L"a=1&b=2"), AwsCanonicalQueryString(L"?b=2&a=1"));
        Assert::AreEqual(std::wstring(L"a=1&a=2"), AwsCanonicalQueryString(L"?a=2&a=1"));
        Assert::AreEqual(std::wstring(L"key=a%20b"), AwsCanonicalQueryString(L"?key=a b"));
        Assert::AreEqual(std::wstring(L"flag="), AwsCanonicalQueryString(L"?flag"));
        Assert::AreEqual(std::wstring(L""), AwsCanonicalQueryString(L""));
    }
};

TEST_CLASS(AwsV4SignatureTest)
{
private:
    static AwsCredentials MakeCredentials()
    {
        AwsCredentials credentials;
        credentials.accessKey = L"AKIDEXAMPLE";
        credentials.secretKey = L"wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";
        credentials.region = L"us-east-1";
        credentials.service = L"service";
        return credentials;
    }

    static SYSTEMTIME MakeTimestamp()
    {
        SYSTEMTIME utc = {};
        utc.wYear = 2015;
        utc.wMonth = 8;
        utc.wDay = 30;
        utc.wHour = 12;
        utc.wMinute = 36;
        utc.wSecond = 0;
        return utc;
    }

    static std::wstring HeaderValue(const std::vector<std::wstring>& headers, const std::wstring& name)
    {
        for (const std::wstring& header : headers)
        {
            if (header.size() > name.size() && _wcsnicmp(header.c_str(), name.c_str(), name.size()) == 0)
            {
                const size_t colon = header.find(L':');
                std::wstring value = header.substr(colon + 1);
                while (!value.empty() && value.front() == L' ')
                {
                    value.erase(value.begin());
                }
                return value;
            }
        }
        return L"";
    }

public:
    // AWS's published 'get-vanilla' vector: same credentials, same timestamp, same host, same empty path
    // and query. The canonical request and string to sign were checked byte for byte against the
    // aws-sig-v4-test-suite get-vanilla.creq, .sts and .authz files. The only deliberate difference is the
    // x-amz-content-sha256:UNSIGNED-PAYLOAD header this signer always adds, which get-vanilla does not
    // carry: that is why the credential scope and the date match the suite verbatim while the
    // signed-header list carries one more name.
    TEST_METHOD(GetVanillaMatchesThePublishedVector)
    {
        const auto headers =
            MakeAwsV4Headers(L"GET", L"/", L"", L"example.amazonaws.com", {}, MakeCredentials(), MakeTimestamp());
        Assert::IsFalse(headers.has_error(), L"MakeAwsV4Headers failed");

        Assert::AreEqual(std::wstring(L"20150830T123600Z"), HeaderValue(*headers, L"x-amz-date"));
        Assert::AreEqual(std::wstring(L"UNSIGNED-PAYLOAD"), HeaderValue(*headers, L"x-amz-content-sha256"));

        const std::wstring authorization = HeaderValue(*headers, L"Authorization");
        Assert::IsTrue(
            authorization.find(L"AWS4-HMAC-SHA256 Credential=AKIDEXAMPLE/20150830/us-east-1/service/aws4_request")
                == 0,
            L"the credential scope is wrong");
        Assert::IsTrue(
            authorization.find(L"SignedHeaders=host;x-amz-content-sha256;x-amz-date") != std::wstring::npos,
            L"the signed-header list is wrong");

        // Pinned so that a single wrong byte anywhere in the canonicalization is caught here rather than as
        // an opaque 403. Produced by an independent implementation of the same derivation, one that
        // reproduces get-vanilla.authz's own signature (5fa00fa3...) exactly from the suite's inputs.
        Assert::IsTrue(
            authorization.find(L"Signature=9b02fb7b5d0076fa47a0adda28c71e74ba4588334bc0139b8cd6bb87f16afe16")
                != std::wstring::npos,
            L"the signature is wrong");
    }

    // The query arrives ALREADY canonical and is signed verbatim, the same contract 'encodedPath' has:
    // the caller canonicalizes it once, puts those bytes on the wire, and hands the same bytes here.
    // Canonicalizing a second time would re-encode the '%' of every escape -
    // "tag=a%20b" becoming "tag=a%2520b" - so the signature would cover a query the request never
    // carries: SignatureDoesNotMatch, a 403 naming nothing.
    //
    // Pinned from the same independent implementation as the get-vanilla vector above, the one that
    // reproduces get-vanilla.authz's own signature (5fa00fa3...) from the suite's inputs. The pin is
    // what catches a double pass applied consistently to BOTH the wire and the signature: those two
    // would still agree with each other, and only a server would ever notice.
    TEST_METHOD(CanonicalQueryIsSignedVerbatim)
    {
        const auto headers = MakeAwsV4Headers(
            L"PUT", L"/bucket/key", L"tag=a%20b&x=1", L"s3.example", {}, MakeCredentials(), MakeTimestamp());
        Assert::IsFalse(headers.has_error(), L"MakeAwsV4Headers failed");

        Assert::IsTrue(
            HeaderValue(*headers, L"Authorization")
                    .find(L"Signature=177004ba5b719bf091f70ab1dd28dce7a8b8d45d5587f724d1d7b5a32d93bca2")
                != std::wstring::npos,
            L"the canonical query must be signed exactly as given");
    }

    // A session token is emitted AND signed: an unsigned x-amz-* header is what AWS rejects.
    TEST_METHOD(SessionTokenIsEmittedAndSigned)
    {
        AwsCredentials credentials = MakeCredentials();
        credentials.sessionToken = L"FQoDYXdzEExampleToken";

        const auto headers =
            MakeAwsV4Headers(L"PUT", L"/bucket/key", L"", L"s3.example:8450", {}, credentials, MakeTimestamp());
        Assert::IsFalse(headers.has_error(), L"MakeAwsV4Headers failed");

        Assert::AreEqual(std::wstring(L"FQoDYXdzEExampleToken"), HeaderValue(*headers, L"x-amz-security-token"));
        Assert::IsTrue(
            HeaderValue(*headers, L"Authorization").find(L"x-amz-security-token") != std::wstring::npos,
            L"the session token must appear in SignedHeaders");
    }

    // Every header the step emits is signed, so an operator x-amz-* header cannot produce a 403 that
    // names nothing.
    TEST_METHOD(StepHeadersAreSigned)
    {
        const std::vector<std::wstring> stepHeaders = {L"x-amz-server-side-encryption: AES256"};
        const auto headers = MakeAwsV4Headers(
            L"PUT", L"/bucket/key", L"", L"s3.example", stepHeaders, MakeCredentials(), MakeTimestamp());
        Assert::IsFalse(headers.has_error(), L"MakeAwsV4Headers failed");

        Assert::IsTrue(
            HeaderValue(*headers, L"Authorization").find(L"x-amz-server-side-encryption") != std::wstring::npos,
            L"a step header must appear in SignedHeaders");
    }

    // HttpClient::Send adds every header but Authorization with a plain WINHTTP_ADDREQ_FLAG_ADD, so a
    // config that repeats a header name puts BOTH lines on the wire and S3 canonicalises them as one
    // entry whose values are joined with ','. Signing only the last value was SignatureDoesNotMatch, a
    // 403 naming nothing.
    TEST_METHOD(RepeatedStepHeadersAreCommaJoined)
    {
        const std::vector<std::wstring> stepHeaders = {L"x-amz-meta-tag: alpha", L"X-Amz-Meta-Tag: beta"};
        const auto headers = MakeAwsV4Headers(
            L"PUT", L"/bucket/key", L"", L"s3.example", stepHeaders, MakeCredentials(), MakeTimestamp());
        Assert::IsFalse(headers.has_error(), L"MakeAwsV4Headers failed");

        // The repeated name is one entry in the signed-header list, not two.
        const std::wstring authorization = HeaderValue(*headers, L"Authorization");
        Assert::IsTrue(
            authorization.find(L"SignedHeaders=host;x-amz-content-sha256;x-amz-date;x-amz-meta-tag,")
                != std::wstring::npos,
            L"a repeated header name must be listed once");

        // Pinned from the same independent implementation as the get-vanilla vector, over the canonical
        // headers block "...x-amz-meta-tag:alpha,beta\n".
        Assert::IsTrue(
            authorization.find(L"Signature=db2ce39db2b79a73dfa0cd8cefbae840b426b74d517a9328602dc309f8e089f1")
                != std::wstring::npos,
            L"the repeated header must be signed comma-joined, in input order");
    }

    // The signer computes and emits host, x-amz-date, x-amz-content-sha256 and x-amz-security-token
    // itself. A step header repeating one of those cannot be merged: the joined value would depend on
    // where the transport places the signer's line relative to the step's, so the signature would be a
    // guess. Refuse loudly instead of dropping it.
    TEST_METHOD(StepHeaderRepeatingASignerHeaderIsRejected)
    {
        const std::vector<std::wstring> stepHeaders = {L"X-Amz-Date: 20200101T000000Z"};
        const auto headers = MakeAwsV4Headers(
            L"PUT", L"/bucket/key", L"", L"s3.example", stepHeaders, MakeCredentials(), MakeTimestamp());
        Assert::IsTrue(headers.has_error(), L"a step header repeating x-amz-date must be an error");
    }

    // Content-Length is deliberately NOT signed: WinHTTP generates it and its exact rendering would be
    // a guess.
    TEST_METHOD(ContentLengthIsNotSigned)
    {
        const std::vector<std::wstring> stepHeaders = {L"Content-Length: 42"};
        const auto headers = MakeAwsV4Headers(
            L"PUT", L"/bucket/key", L"", L"s3.example", stepHeaders, MakeCredentials(), MakeTimestamp());
        Assert::IsFalse(headers.has_error(), L"MakeAwsV4Headers failed");

        Assert::IsTrue(
            HeaderValue(*headers, L"Authorization").find(L"content-length") == std::wstring::npos,
            L"content-length must not be signed");
    }

    // A missing region is a programming error here (Validate refuses it at config time), but the signer
    // must not invent a scope.
    TEST_METHOD(EmptyRegionIsRejected)
    {
        AwsCredentials credentials = MakeCredentials();
        credentials.region.clear();

        const auto headers =
            MakeAwsV4Headers(L"GET", L"/", L"", L"example.amazonaws.com", {}, credentials, MakeTimestamp());
        Assert::IsTrue(headers.has_error(), L"an empty region must be an error");
    }
};

TEST_CLASS(AwsV4AuthenticatorTest)
{
private:
    static HttpClientConfig MakeConfig()
    {
        HttpClientConfig config;
        config.serverName = L"s3.example:8450";
        config.serverScheme = HttpAuthScheme::AWSv4;
        config.aws.accessKey = L"AKIAEXAMPLE";
        config.aws.secretKey = L"secret";
        config.aws.region = L"eu-west-3";
        config.aws.service = L"s3";
        return config;
    }

    // The host MakeServerAuthenticator actually handed the signer: m_host goes verbatim into
    // MakeAwsV4Headers, so this is what the signature's Host covers.
    static std::wstring SignedHostOf(const std::wstring& serverName, bool secure)
    {
        HttpClientConfig config = MakeConfig();
        config.serverName = serverName;
        config.secure = secure;

        const auto auth = MakeServerAuthenticator(config);
        Assert::IsTrue(auth != nullptr, L"AWSv4 must build an authenticator");

        const AwsV4Authenticator* signer = dynamic_cast<const AwsV4Authenticator*>(auth.get());
        Assert::IsTrue(signer != nullptr, L"AWSv4 must build an AwsV4Authenticator");
        return signer->SignedHost();
    }

    // The timestamp PrepareRequest actually signed at, read back from the x-amz-date it emitted. The
    // signer takes its clock from GetSystemTime and offers no injection point at this level, so
    // reproducing its signature means asking the request which second it used.
    static SYSTEMTIME SignedAt(const HttpRequest& request)
    {
        SYSTEMTIME utc = {};
        for (const std::wstring& header : request.headers)
        {
            if (_wcsnicmp(header.c_str(), L"x-amz-date:", 11) != 0)
            {
                continue;
            }

            const int fields = swscanf_s(
                header.c_str() + 11,
                L" %4hu%2hu%2huT%2hu%2hu%2huZ",
                &utc.wYear,
                &utc.wMonth,
                &utc.wDay,
                &utc.wHour,
                &utc.wMinute,
                &utc.wSecond);
            Assert::AreEqual(6, fields, L"x-amz-date is not an ISO8601 basic timestamp");
            return utc;
        }

        Assert::Fail(L"PrepareRequest emitted no x-amz-date");
        return utc;
    }

    // The "Signature=<hex>" tail of an Authorization line, empty when there is none.
    static std::wstring SignatureOf(const std::vector<std::wstring>& headers)
    {
        for (const std::wstring& header : headers)
        {
            const size_t mark = header.find(L"Signature=");
            if (header.find(L"Authorization:") == 0 && mark != std::wstring::npos)
            {
                return header.substr(mark);
            }
        }

        return std::wstring();
    }

    // The credentials MakeConfig carries, as MakeServerAuthenticator hands them to the signer.
    static AwsCredentials ConfiguredCredentials() { return MakeConfig().aws; }

public:
    // AWSv4 builds an authenticator even though serverUser is empty: its credential lives in the aws
    // fields, not in user/password.
    TEST_METHOD(AwsV4BuildsAnAuthenticator)
    {
        Assert::IsTrue(MakeServerAuthenticator(MakeConfig()) != nullptr, L"AWSv4 must build an authenticator");
    }

    // PrepareRequest owns the path encoding, and marks the path so HttpClient stops WinHTTP from
    // escaping it again. The signed bytes and the wire bytes must be the same object.
    TEST_METHOD(PrepareRequestEncodesThePathAndMarksIt)
    {
        const auto auth = MakeServerAuthenticator(MakeConfig());
        Assert::IsTrue(auth != nullptr, L"precondition");

        HttpRequest request;
        request.method = L"PUT";
        request.path = L"/bucket/ORC SRV01.7z";

        Assert::IsFalse(auth->PrepareRequest(request).has_error(), L"PrepareRequest failed");
        Assert::AreEqual(std::wstring(L"/bucket/ORC%20SRV01.7z"), request.path);
        Assert::IsTrue(request.pathIsPreEncoded, L"the path must be marked as already encoded");
    }

    // '~' is unreserved for AWS and must survive untouched; encoding it would produce a 403 naming
    // nothing.
    TEST_METHOD(PrepareRequestLeavesTildeAlone)
    {
        const auto auth = MakeServerAuthenticator(MakeConfig());
        HttpRequest request;
        request.method = L"PUT";
        request.path = L"/bucket/ORC_SRV01~1.7z";

        Assert::IsFalse(auth->PrepareRequest(request).has_error(), L"PrepareRequest failed");
        Assert::AreEqual(std::wstring(L"/bucket/ORC_SRV01~1.7z"), request.path);
    }

    // The query is canonicalized exactly ONCE, and what is signed is what goes on the wire. The signer
    // consumes the canonical query verbatim (see CanonicalQueryIsSignedVerbatim); a second pass here
    // would leave "tag=a%20b" in request.path and "tag=a%2520b" in the signature, for a
    // SignatureDoesNotMatch 403 that names nothing.
    //
    // Nothing is pinned: the signature PrepareRequest produced is compared against one recomputed over
    // the query as request.path actually carries it, at the x-amz-date PrepareRequest emitted, so the
    // check holds whatever the clock said. What it covers is the COMPOSITION - that PrepareRequest
    // hands the signer exactly the query it puts on the wire. It cannot by itself catch a second pass
    // INSIDE the signer, since the recomputation would inherit it; CanonicalQueryIsSignedVerbatim is
    // what pins that half.
    TEST_METHOD(PrepareRequestSignsTheQueryItPutsOnTheWire)
    {
        const auto auth = MakeServerAuthenticator(MakeConfig());
        Assert::IsTrue(auth != nullptr, L"precondition");

        const AwsV4Authenticator* signer = dynamic_cast<const AwsV4Authenticator*>(auth.get());
        Assert::IsTrue(signer != nullptr, L"precondition");

        HttpRequest request;
        request.method = L"PUT";
        request.path = L"/bucket/ORC SRV01.7z?tag=a b&x=1";

        Assert::IsFalse(auth->PrepareRequest(request).has_error(), L"PrepareRequest failed");

        // Path encoded once, query canonicalized once: '%' is itself reserved, so a second pass would
        // show up right here as "%2520".
        Assert::AreEqual(std::wstring(L"/bucket/ORC%20SRV01.7z?tag=a%20b&x=1"), request.path);

        const size_t mark = request.path.find(L'?');
        Assert::IsTrue(mark != std::wstring::npos, L"the query must survive on the wire");

        const auto recomputed = MakeAwsV4Headers(
            request.method,
            request.path.substr(0, mark),
            request.path.substr(mark + 1),
            signer->SignedHost(),
            {},
            ConfiguredCredentials(),
            SignedAt(request));
        Assert::IsFalse(recomputed.has_error(), L"the recomputation failed");

        const std::wstring produced = SignatureOf(request.headers);
        Assert::IsFalse(produced.empty(), L"PrepareRequest emitted no Authorization signature");
        Assert::AreEqual(
            produced, SignatureOf(*recomputed), L"the signature must cover the query the request carries");
    }

    TEST_METHOD(PrepareRequestAddsTheSignedHeaders)
    {
        const auto auth = MakeServerAuthenticator(MakeConfig());
        HttpRequest request;
        request.method = L"PUT";
        request.path = L"/bucket/key";

        Assert::IsFalse(auth->PrepareRequest(request).has_error(), L"PrepareRequest failed");

        bool hasAuthorization = false;
        bool hasDate = false;
        bool hasPayload = false;
        for (const std::wstring& header : request.headers)
        {
            if (header.find(L"Authorization:") == 0)
                hasAuthorization = true;
            if (header.find(L"x-amz-date:") == 0)
                hasDate = true;
            if (header.find(L"x-amz-content-sha256:") == 0)
                hasPayload = true;
        }

        Assert::IsTrue(hasAuthorization, L"Authorization is missing");
        Assert::IsTrue(hasDate, L"x-amz-date is missing");
        Assert::IsTrue(hasPayload, L"x-amz-content-sha256 is missing");
    }

    // ApplyToHandle must do nothing for a signing scheme: it has no credentials to preset on the
    // handle, and calling WinHttpSetCredentials with an AWS key would be wrong.
    TEST_METHOD(ApplyToHandleIsANoOp)
    {
        const auto auth = MakeServerAuthenticator(MakeConfig());
        Assert::IsFalse(auth->ApplyToHandle(nullptr).has_error(), L"a signing scheme must not touch the handle");
    }

    // A signed request must never follow a redirect: the signature binds the host it was computed for.
    TEST_METHOD(SigningForbidsRedirects)
    {
        const auto auth = MakeServerAuthenticator(MakeConfig());
        Assert::IsTrue(auth->ForbidsRedirects(), L"a signing scheme must forbid redirects");
    }

    // WinHTTP synthesises 'Host' WITHOUT the port when it is the scheme default (measured: "127.0.0.1"
    // on 80, "127.0.0.1:8099" on 8099). Signing an explicitly written ":443" would name a host the wire
    // never carries, for a SignatureDoesNotMatch 403 explaining nothing.
    TEST_METHOD(SignedHostDropsTheDefaultHttpsPort)
    {
        Assert::AreEqual(std::wstring(L"host.example"), SignedHostOf(L"host.example:443", /*secure*/ true));
    }

    TEST_METHOD(SignedHostDropsTheDefaultHttpPort)
    {
        Assert::AreEqual(std::wstring(L"host.example"), SignedHostOf(L"host.example:80", /*secure*/ false));
    }

    // A non-default port is part of the Host header, so it is part of the signature.
    TEST_METHOD(SignedHostKeepsANonDefaultPort)
    {
        Assert::AreEqual(std::wstring(L"host.example:8450"), SignedHostOf(L"host.example:8450", /*secure*/ true));
    }

    TEST_METHOD(SignedHostLeavesABarePortlessNameAlone)
    {
        Assert::AreEqual(std::wstring(L"host.example"), SignedHostOf(L"host.example", /*secure*/ true));
    }

    // 443 is the default for HTTPS only: on http:// it is a port like any other and stays signed.
    TEST_METHOD(SignedHostKeeps443OnPlainHttp)
    {
        Assert::AreEqual(std::wstring(L"host.example:443"), SignedHostOf(L"host.example:443", /*secure*/ false));
    }

    // A server name that does not parse is signed verbatim: the transport keeps it too, so the signature
    // must not name a different host than the one the connection is attempted against.
    TEST_METHOD(SignedHostKeepsAnUnparsableServerNameVerbatim)
    {
        Assert::AreEqual(std::wstring(L"host.example:notaport"), SignedHostOf(L"host.example:notaport", true));
    }

    // aws_clock="local" (the default) never retries: the skew is logged and the upload fails.
    TEST_METHOD(ClockLocalDoesNotRetry)
    {
        const auto auth = MakeServerAuthenticator(MakeConfig());
        HttpResponse response;
        response.statusCode = 403;
        Assert::IsFalse(auth->OnRejected(response), L"the default clock source must not retry");
    }
};

}  // namespace Orc::Test
