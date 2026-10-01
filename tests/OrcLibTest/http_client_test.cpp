//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): DFIR-ORC Team
//
#include "stdafx.h"

#include "Network/Http/HttpClient.h"
#include "OutputSpec.h"

#include <winhttp.h>
#include <schannel.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;

namespace Orc::Test {

TEST_CLASS(HttpClientHelpersTest)
{
public:
    TEST_METHOD(SplitHostPortPlainHost)
    {
        const auto hp = SplitHostPort(L"host.example", INTERNET_DEFAULT_HTTPS_PORT);
        Assert::IsTrue(hp.has_value());
        Assert::AreEqual(std::wstring(L"host.example"), hp->host);
        Assert::AreEqual((int)INTERNET_DEFAULT_HTTPS_PORT, (int)hp->port);
    }

    TEST_METHOD(SplitHostPortExplicitPort)
    {
        const auto hp = SplitHostPort(L"host.example:8443", INTERNET_DEFAULT_HTTPS_PORT);
        Assert::IsTrue(hp.has_value());
        Assert::AreEqual(std::wstring(L"host.example"), hp->host);
        Assert::AreEqual(8443, (int)hp->port);
    }

    TEST_METHOD(SplitHostPortIpv6WithPort)
    {
        const auto hp = SplitHostPort(L"[2001:db8::1]:9000", INTERNET_DEFAULT_HTTPS_PORT);
        Assert::IsTrue(hp.has_value());
        Assert::AreEqual(std::wstring(L"[2001:db8::1]"), hp->host);
        Assert::AreEqual(9000, (int)hp->port);
    }

    TEST_METHOD(SplitHostPortMalformedPortRefused)
    {
        // A separator is present but the port is invalid: a configuration error, refused (std::nullopt)
        // rather than a silent fall-back to the default port (which would connect to the wrong port).
        Assert::IsFalse(SplitHostPort(L"host.example:notaport", INTERNET_DEFAULT_HTTPS_PORT).has_value());
        Assert::IsFalse(SplitHostPort(L"host.example:99999", INTERNET_DEFAULT_HTTPS_PORT).has_value());
        Assert::IsFalse(SplitHostPort(L"host.example:0", INTERNET_DEFAULT_HTTPS_PORT).has_value());
        Assert::IsFalse(SplitHostPort(L"host.example:", INTERNET_DEFAULT_HTTPS_PORT).has_value());
        Assert::IsFalse(SplitHostPort(L":8443", INTERNET_DEFAULT_HTTPS_PORT).has_value());  // no host
    }

    TEST_METHOD(SplitHostPortBracketlessIpv6IsHostOnly)
    {
        // A bracketless IPv6 literal has multiple colons and no port; the last colon must NOT be taken as a
        // port separator (that yielded host "fe80:" + port 1). The whole string is the host, default port.
        const auto hp = SplitHostPort(L"fe80::1", INTERNET_DEFAULT_HTTPS_PORT);
        Assert::IsTrue(hp.has_value());
        Assert::AreEqual(std::wstring(L"fe80::1"), hp->host);
        Assert::AreEqual((int)INTERNET_DEFAULT_HTTPS_PORT, (int)hp->port);
    }

    TEST_METHOD(MakeBasicAuthHeaderEncodesRfc7617)
    {
        // base64("user:pass") == "dXNlcjpwYXNz"
        const auto h = MakeBasicAuthHeader(L"user", L"pass");
        Assert::IsTrue(h.has_value());
        Assert::AreEqual(std::wstring(L"Authorization: Basic dXNlcjpwYXNz"), *h);
    }

    TEST_METHOD(RedactResponseHeadersDropsSecretValuesKeepsNames)
    {
        const std::wstring raw = L"Content-Type: text/plain\r\nSet-Cookie: secret=abc\r\nAuthorization: Bearer t\r\n";
        const std::wstring out = RedactResponseHeaders(raw);
        Assert::IsTrue(out.find(L"Content-Type: text/plain") != std::wstring::npos, L"non-secret header kept");
        Assert::IsTrue(out.find(L"Set-Cookie: [redacted]") != std::wstring::npos, L"Set-Cookie redacted");
        Assert::IsTrue(out.find(L"Authorization: [redacted]") != std::wstring::npos, L"Authorization redacted");
        Assert::IsTrue(out.find(L"secret=abc") == std::wstring::npos, L"cookie value gone");
        Assert::IsTrue(out.find(L"Bearer t") == std::wstring::npos, L"auth value gone");
    }

    TEST_METHOD(SpkiMatchesAnyPinMatchesPresent)
    {
        const std::vector<uint8_t> live = {0x01, 0x02, 0x03};
        const std::vector<std::vector<uint8_t>> pins = {{0xAA, 0xBB}, {0x01, 0x02, 0x03}};
        Assert::IsTrue(SpkiMatchesAnyPin(live, pins));
    }

    TEST_METHOD(SpkiMatchesAnyPinNoMatch)
    {
        const std::vector<uint8_t> live = {0x01, 0x02, 0x03};
        const std::vector<std::vector<uint8_t>> pins = {{0xAA, 0xBB}, {0x01, 0x02}};
        Assert::IsFalse(SpkiMatchesAnyPin(live, pins));
    }

    TEST_METHOD(SpkiMatchesAnyPinEmptyPinsIsFalse)
    {
        const std::vector<uint8_t> live = {0x01};
        const std::vector<std::vector<uint8_t>> pins;
        Assert::IsFalse(SpkiMatchesAnyPin(live, pins));
    }

    // Each HTTP-layer TLS bit sets exactly its WinHTTP protocol flag. A wrong or missing flag here would
    // silently narrow or drop what the configuration demanded, and a 0 mask would leave the system policy
    // in charge.
    TEST_METHOD(ToWinHttpProtocolsMapsEachVersionToItsOwnFlag)
    {
        const std::pair<uint8_t, DWORD> flags[] = {
            {HttpTlsVersion::kTls1_0, WINHTTP_FLAG_SECURE_PROTOCOL_TLS1},
            {HttpTlsVersion::kTls1_1, WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1},
            {HttpTlsVersion::kTls1_2, WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2},
            {HttpTlsVersion::kTls1_3, WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3}};

        for (const auto& [bit, flag] : flags)
        {
            Assert::AreEqual(static_cast<int>(flag), static_cast<int>(ToWinHttpProtocols(bit)));
        }

        Assert::AreEqual(
            static_cast<int>(
                WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3),
            static_cast<int>(ToWinHttpProtocols(HttpTlsVersion::kTls1_2 | HttpTlsVersion::kTls1_3)));
    }

    // No input, known or not, ever enables SSL 2 or SSL 3, and bits above TLS 1.3 enable nothing.
    TEST_METHOD(ToWinHttpProtocolsNeverEnablesSslOrUnknownBits)
    {
        const DWORD ssl = WINHTTP_FLAG_SECURE_PROTOCOL_SSL2 | WINHTTP_FLAG_SECURE_PROTOCOL_SSL3;
        for (int versions = 0; versions <= 0xFF; ++versions)
        {
            Assert::AreEqual(0, static_cast<int>(ToWinHttpProtocols(static_cast<uint8_t>(versions)) & ssl));
        }

        Assert::AreEqual(0, static_cast<int>(ToWinHttpProtocols(0)));
        Assert::AreEqual(0, static_cast<int>(ToWinHttpProtocols(0xF0)));
    }

    // Only a negotiated TLS 1.2 or TLS 1.3 client value proves the os-default requirement.
    TEST_METHOD(IsTls12OrNewerProtocolAcceptsOnlyTls12And13)
    {
        Assert::IsTrue(IsTls12OrNewerProtocol(SP_PROT_TLS1_2_CLIENT));
        Assert::IsTrue(IsTls12OrNewerProtocol(0x00002000));  // SP_PROT_TLS1_3_CLIENT, absent from older SDKs

        const DWORD refused[] = {
            0,
            SP_PROT_SSL2_CLIENT,
            SP_PROT_SSL3_CLIENT,
            SP_PROT_TLS1_CLIENT,
            SP_PROT_TLS1_1_CLIENT,
            SP_PROT_TLS1_2_SERVER,  // a server-side value is not what a client connection negotiates
            SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_1_CLIENT,  // a combination is not one negotiated protocol
            0x00008000};  // a value this build does not know

        for (const DWORD protocol : refused)
        {
            Assert::IsFalse(IsTls12OrNewerProtocol(protocol), std::to_wstring(protocol).c_str());
        }
    }

    // The per-connection proof applies to HTTPS with no enforced version set (os-default) and no 'insecure'.
    TEST_METHOD(RequiresNegotiatedTls12OnlyForHttpsOsDefaultWithoutInsecure)
    {
        Assert::IsTrue(RequiresNegotiatedTls12(true, 0, false));

        Assert::IsFalse(RequiresNegotiatedTls12(true, 0, true));  // insecure accepts the system policy
        Assert::IsFalse(RequiresNegotiatedTls12(false, 0, false));  // plain HTTP has no TLS to prove
        Assert::IsFalse(RequiresNegotiatedTls12(true, HttpTlsVersion::kTls1_2, false));  // the mask enforces it
        Assert::IsFalse(RequiresNegotiatedTls12(true, HttpTlsVersion::kTls1_0, true));
    }

    // A challenge is answered by sending the body again only when a resend can complete it.
    TEST_METHOD(ChallengeResendOnlyForSchemesThatAnswerIt)
    {
        const HttpAuthScheme all[] = {
            HttpAuthScheme::Anonymous,
            HttpAuthScheme::Basic,
            HttpAuthScheme::NTLM,
            HttpAuthScheme::Kerberos,
            HttpAuthScheme::Negotiate};

        for (const HttpAuthScheme server : all)
        {
            const bool integrated = server == HttpAuthScheme::NTLM || server == HttpAuthScheme::Kerberos
                || server == HttpAuthScheme::Negotiate;
            Assert::AreEqual(integrated, ShouldResendOnChallenge(401, server, HttpAuthScheme::Anonymous, false, false));
            Assert::AreEqual(integrated, ShouldResendOnChallenge(401, server, HttpAuthScheme::Basic, true, true));
        }

        for (const HttpAuthScheme proxy : all)
        {
            const bool answered = proxy != HttpAuthScheme::Anonymous;
            Assert::AreEqual(answered, ShouldResendOnChallenge(407, HttpAuthScheme::Anonymous, proxy, true, true));
            Assert::IsFalse(ShouldResendOnChallenge(407, HttpAuthScheme::NTLM, proxy, false, true));
        }

        for (const uint32_t status : {200u, 301u, 403u, 404u, 500u})
        {
            Assert::IsFalse(ShouldResendOnChallenge(status, HttpAuthScheme::NTLM, HttpAuthScheme::NTLM, true, true));
        }
    }

    // A 407 on a connection that goes through no proxy did not come from a proxy the configuration named: the proxy
    // credentials are never offered to it, whatever the scheme.
    TEST_METHOD(ChallengeResendNeverAnswersA407WithoutAProxyInUse)
    {
        for (const HttpAuthScheme proxy :
             {HttpAuthScheme::Basic, HttpAuthScheme::NTLM, HttpAuthScheme::Kerberos, HttpAuthScheme::Negotiate})
        {
            Assert::IsFalse(ShouldResendOnChallenge(407, HttpAuthScheme::Anonymous, proxy, true, false));
            Assert::IsFalse(ShouldResendOnChallenge(407, HttpAuthScheme::NTLM, proxy, true, false));
        }

        // The server side does not depend on the proxy.
        Assert::IsTrue(ShouldResendOnChallenge(401, HttpAuthScheme::NTLM, HttpAuthScheme::Basic, true, false));
    }

    TEST_METHOD(AuthSchemesTextNamesEveryBit)
    {
        Assert::AreEqual(std::wstring(L"none"), AuthSchemesText(0));
        Assert::AreEqual(std::wstring(L"Basic"), AuthSchemesText(WINHTTP_AUTH_SCHEME_BASIC));
        Assert::AreEqual(
            std::wstring(L"Basic, NTLM, Passport, Digest, Negotiate"),
            AuthSchemesText(
                WINHTTP_AUTH_SCHEME_BASIC | WINHTTP_AUTH_SCHEME_NTLM | WINHTTP_AUTH_SCHEME_PASSPORT
                | WINHTTP_AUTH_SCHEME_DIGEST | WINHTTP_AUTH_SCHEME_NEGOTIATE));
        Assert::AreEqual(std::wstring(L"NTLM, unknown (0x100)"), AuthSchemesText(WINHTTP_AUTH_SCHEME_NTLM | 0x100));
    }
};

// Whether a proxy handles the connection to the upload host decides whether proxy credentials may be offered at all.
// Matching errs towards "bypassed": withholding the credentials fails the upload on a 407, sending them where no
// proxy stands gives them away.
TEST_CLASS(ProxyInUseTest)
{
public:
    TEST_METHOD(NoProxyServerMeansNoProxy)
    {
        Assert::IsFalse(ProxyAppliesToHost(L"", L"", L"upload.example"));
        Assert::IsTrue(ProxyAppliesToHost(L"proxy.example:3128", L"", L"upload.example"));
    }

    TEST_METHOD(BypassMatchesExactAndWildcardHostsCaseInsensitively)
    {
        Assert::IsTrue(ProxyBypassMayMatch(L"upload.example", L"upload.example"));
        Assert::IsTrue(ProxyBypassMayMatch(L"UPLOAD.Example", L"upload.example"));
        Assert::IsTrue(ProxyBypassMayMatch(L"*.example", L"upload.example"));
        Assert::IsTrue(ProxyBypassMayMatch(L"upload.*", L"upload.example"));
        Assert::IsTrue(ProxyBypassMayMatch(L"10.*", L"10.1.2.3"));
        Assert::IsTrue(ProxyBypassMayMatch(L"*", L"upload.example"));

        Assert::IsFalse(ProxyBypassMayMatch(L"*.corp", L"upload.example"));
        Assert::IsFalse(ProxyBypassMayMatch(L"upload.example.org", L"upload.example"));
        Assert::IsFalse(ProxyBypassMayMatch(L"", L"upload.example"));

        Assert::IsFalse(ProxyAppliesToHost(L"proxy.example:3128", L"*.example", L"upload.example"));
    }

    TEST_METHOD(BypassSplitsOnSemicolonsAndWhitespace)
    {
        Assert::IsTrue(ProxyBypassMayMatch(L"*.corp;upload.example", L"upload.example"));
        Assert::IsTrue(ProxyBypassMayMatch(L"*.corp upload.example", L"upload.example"));
        Assert::IsTrue(ProxyBypassMayMatch(L" *.corp ;\tupload.example; ", L"upload.example"));
        Assert::IsFalse(ProxyBypassMayMatch(L";; ;", L"upload.example"));
    }

    TEST_METHOD(BypassLocalMatchesDotlessNamesOnly)
    {
        Assert::IsTrue(ProxyBypassMayMatch(L"<local>", L"collector"));
        Assert::IsTrue(ProxyBypassMayMatch(L"*.corp;<LOCAL>", L"collector"));
        Assert::IsFalse(ProxyBypassMayMatch(L"<local>", L"collector.corp"));
        Assert::IsFalse(ProxyBypassMayMatch(L"<-loopback>", L"collector"));
    }

    // An entry may carry a scheme or a port. Both are ignored, so the entry covers at least what WinHTTP bypasses.
    TEST_METHOD(BypassIgnoresSchemeAndPort)
    {
        Assert::IsTrue(ProxyBypassMayMatch(L"https://upload.example", L"upload.example"));
        Assert::IsTrue(ProxyBypassMayMatch(L"upload.example:8443", L"upload.example"));
        Assert::IsTrue(ProxyBypassMayMatch(L"http://*.example:80", L"upload.example"));
        Assert::IsTrue(ProxyBypassMayMatch(L"[2001:db8::1]:443", L"[2001:db8::1]"));
        Assert::IsTrue(ProxyBypassMayMatch(L"2001:db8::1", L"[2001:db8::1]"));
    }
};

// https_security="os-max" downgrade guard: whatever the local probes report, the lock enables one version at a time,
// only a candidate, highest first, and never one below an unavailable candidate that OsMaxTlsCandidates left out.
TEST_CLASS(OsMaxTlsLockTest)
{
    // Probes answering from bit masks, recording every call in order.
    struct FakeHost
    {
        uint8_t disabled = 0;  // SChannel policy Enabled=0
        uint8_t refused = 0;  // WinHttpSetOption fails
        std::vector<uint8_t> policyChecks;
        std::vector<uint8_t> enabled;

        TlsLockProbes Probes()
        {
            TlsLockProbes probes;
            probes.isDisabledByPolicy = [this](uint8_t version) {
                policyChecks.push_back(version);
                return (disabled & version) != 0;
            };
            probes.enableOnly = [this](uint8_t version) -> std::error_code {
                enabled.push_back(version);
                if ((refused & version) != 0)
                {
                    return std::error_code(ERROR_INVALID_PARAMETER, std::system_category());
                }

                return {};
            };
            return probes;
        }
    };

    static bool IsSingleVersion(uint8_t bits) { return bits != 0 && (bits & (bits - 1)) == 0; }

    static uint8_t Highest(uint8_t bits)
    {
        uint8_t top = 0;
        for (uint8_t bit = HttpTlsVersion::kTls1_0; bit <= HttpTlsVersion::kTls1_3; bit = static_cast<uint8_t>(bit << 1))
        {
            if (bits & bit)
            {
                top = bit;
            }
        }

        return top;
    }

public:
    // Every combination of candidates and of versions the policy disables or WinHTTP refuses. The lock is the highest
    // available candidate, or a failure when there is none; each enable call is a single candidate the policy allows,
    // strictly below the previous call, and the call that succeeds is the last one.
    TEST_METHOD(LocksOnTheHighestAvailableCandidateOnly)
    {
        const uint8_t all = HttpTlsVersion::kTls1_0 | HttpTlsVersion::kTls1_1 | HttpTlsVersion::kTls1_2
            | HttpTlsVersion::kTls1_3;

        for (int candidates = 0; candidates <= all; ++candidates)
        {
            for (int disabled = 0; disabled <= all; ++disabled)
            {
                for (int refused = 0; refused <= all; ++refused)
                {
                    const std::wstring what = L"candidates " + std::to_wstring(candidates) + L", disabled "
                        + std::to_wstring(disabled) + L", refused " + std::to_wstring(refused);

                    FakeHost host;
                    host.disabled = static_cast<uint8_t>(disabled);
                    host.refused = static_cast<uint8_t>(refused);
                    const Result<uint8_t> locked = LockHighestTlsVersion(static_cast<uint8_t>(candidates), host.Probes());

                    const uint8_t available = static_cast<uint8_t>(candidates & ~disabled & ~refused);
                    if (available == 0)
                    {
                        Assert::IsTrue(locked.has_error(), what.c_str());
                    }
                    else
                    {
                        Assert::IsFalse(locked.has_error(), what.c_str());
                        Assert::AreEqual(static_cast<int>(Highest(available)), static_cast<int>(*locked), what.c_str());
                        Assert::AreEqual(static_cast<int>(*locked), static_cast<int>(host.enabled.back()), what.c_str());
                    }

                    uint8_t previous = 0;
                    for (const uint8_t version : host.enabled)
                    {
                        Assert::IsTrue(IsSingleVersion(version), what.c_str());
                        Assert::AreNotEqual(0, candidates & version, what.c_str());
                        Assert::AreEqual(0, disabled & version, what.c_str());
                        Assert::IsTrue(previous == 0 || version < previous, what.c_str());
                        previous = version;
                    }

                    for (const uint8_t version : host.policyChecks)
                    {
                        Assert::AreNotEqual(0, candidates & version, what.c_str());
                    }
                }
            }
        }
    }

    // A modern build offers TLS 1.3 alone, even with 'insecure': unavailable, it refuses rather than enable TLS 1.2.
    TEST_METHOD(ModernBuildNeverFallsBackToTls12)
    {
        for (const bool insecure : {false, true})
        {
            const uint8_t candidates = OsMaxTlsCandidates(26200, insecure);

            FakeHost disabledHost;
            disabledHost.disabled = HttpTlsVersion::kTls1_3;
            Assert::IsTrue(LockHighestTlsVersion(candidates, disabledHost.Probes()).has_error());
            Assert::IsTrue(disabledHost.enabled.empty());

            FakeHost refusedHost;
            refusedHost.refused = HttpTlsVersion::kTls1_3;
            Assert::IsTrue(LockHighestTlsVersion(candidates, refusedHost.Probes()).has_error());
            Assert::AreEqual(1, static_cast<int>(refusedHost.enabled.size()));
            Assert::AreEqual(static_cast<int>(HttpTlsVersion::kTls1_3), static_cast<int>(refusedHost.enabled[0]));
        }
    }

    // An older build steps below an unavailable TLS 1.2 only with 'insecure', and only to TLS 1.1 then TLS 1.0.
    TEST_METHOD(OlderBuildGoesBelowTls12OnlyWithInsecure)
    {
        const uint8_t below12 = HttpTlsVersion::kTls1_0 | HttpTlsVersion::kTls1_1;

        FakeHost secureHost;
        secureHost.refused = HttpTlsVersion::kTls1_2;
        Assert::IsTrue(LockHighestTlsVersion(OsMaxTlsCandidates(19045, false), secureHost.Probes()).has_error());
        for (const uint8_t version : secureHost.enabled)
        {
            Assert::AreEqual(0, version & below12);
        }

        FakeHost insecureHost;
        insecureHost.refused = HttpTlsVersion::kTls1_2;
        insecureHost.disabled = HttpTlsVersion::kTls1_1;
        const Result<uint8_t> locked = LockHighestTlsVersion(OsMaxTlsCandidates(19045, true), insecureHost.Probes());
        Assert::IsFalse(locked.has_error());
        Assert::AreEqual(static_cast<int>(HttpTlsVersion::kTls1_0), static_cast<int>(*locked));

        const std::vector<uint8_t> expected = {HttpTlsVersion::kTls1_2, HttpTlsVersion::kTls1_0};
        Assert::IsTrue(expected == insecureHost.enabled);
    }

    // No candidate: nothing is probed or enabled, and the lock fails instead of leaving the system policy in charge.
    TEST_METHOD(NoCandidateEnablesNothing)
    {
        FakeHost host;
        Assert::IsTrue(LockHighestTlsVersion(0, host.Probes()).has_error());
        Assert::IsTrue(host.policyChecks.empty());
        Assert::IsTrue(host.enabled.empty());
    }
};

}  // namespace Orc::Test
