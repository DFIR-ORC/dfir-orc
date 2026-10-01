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
#include "RESTStep.h"

#include <winhttp.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Orc;

namespace Orc::Test {

namespace {

// Throwaway self-signed certificates generated for these tests; their private keys were never kept. The
// expected hashes were computed independently with openssl, not with the code under test:
//   SPKI:  openssl x509 -pubkey -noout | openssl pkey -pubin -outform DER | openssl dgst -sha256
//   whole: openssl x509 -outform DER | openssl dgst -sha256

// RSA 2048, CN=orc-pin-test-a.
const wchar_t kCertA[] =
    L"MIIDFTCCAf2gAwIBAgIUNazM5gVEYZrz7DWFe9Kf5bvaweswDQYJKoZIhvcNAQELBQAwGTEXMBUGA1UEAwwOb3JjLXBpbi10"
    L"ZXN0LWEwIBcNMjYwOTE0MTgwOTMwWhgPMjEyNjA4MjExODA5MzBaMBkxFzAVBgNVBAMMDm9yYy1waW4tdGVzdC1hMIIBIjAN"
    L"BgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAlJBKZFawgVbcTYuib+96joYQtCIeQPbpb6dBwuABcDMcD8jxMCZ4kzmmjBNH"
    L"bAd6jy3yAQvPcDFQp+8st4rkOLQGgAF2wLyZOOnqrYko3Kq8KOQc6KodSAU8YJD9ZaBp04S0hK+YopgDTWc2EvWkICxvQ4or"
    L"IVazJnoVcwsFoVT5Hy917huV/PdE4odH+RavQVx9GcgitsdCb27sip/rx/opzk4WdqzCpVR+uqfCxWYAiIlX2lUdlmnVKVkU"
    L"6p8tznPN3ZYYRcgCbN6DbOEXcX9Tf6rG1wGwv8ZA5UvXCy3aB+WUBKQmYfJ1tgy/MKQ2lhi92qlRiL2zcw4T3RtsQQIDAQAB"
    L"o1MwUTAdBgNVHQ4EFgQURbFHTTA5y0kS4F4wD5W18tp/iAcwHwYDVR0jBBgwFoAURbFHTTA5y0kS4F4wD5W18tp/iAcwDwYD"
    L"VR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAbHGOasq9v1CZ2s+fE+HOpVvJNkxQxdqqLmPweNaFEc0vuvyU7fFE"
    L"JBHFrT7uVW4Dfv+p/ZtU6hCbmAlPprtfj3jt6B+a434okf4RdvbNgpu/7UzSiERVMF3nroMCpkIT540dnCzF/3aof+k9zhD5"
    L"yYdLekPB8ykVxNCuF+hoHan5kGxSyxC7KWl8QeZdwKYN9rg+m+764Szn9hrjKNu5/t1tWsaLyeDJ6+faSrt7fR6fOfdo2i2j"
    L"0BpOIXFoPuT2NjrYIfpZwFERlBnw8xoKAyoMOqgEQgreZa8LZf+uV2qI55NYPuE7MbRjxoaweJ8kxhCNAiYZPBA8vdP1Ilwo"
    L"nQ==";

// The same key as kCertA, re-issued as CN=orc-pin-test-a-renewed: a renewal that keeps the key.
const wchar_t kCertARenewed[] =
    L"MIIDJTCCAg2gAwIBAgIUGIbDOH74ZgiZlTuhKMoxE3J72sgwDQYJKoZIhvcNAQELBQAwITEfMB0GA1UEAwwWb3JjLXBpbi10"
    L"ZXN0LWEtcmVuZXdlZDAgFw0yNjA5MTQxODA5MzBaGA8yMTI2MDgyMTE4MDkzMFowITEfMB0GA1UEAwwWb3JjLXBpbi10ZXN0"
    L"LWEtcmVuZXdlZDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAJSQSmRWsIFW3E2Lom/veo6GELQiHkD26W+nQcLg"
    L"AXAzHA/I8TAmeJM5powTR2wHeo8t8gELz3AxUKfvLLeK5Di0BoABdsC8mTjp6q2JKNyqvCjkHOiqHUgFPGCQ/WWgadOEtISv"
    L"mKKYA01nNhL1pCAsb0OKKyFWsyZ6FXMLBaFU+R8vde4blfz3ROKHR/kWr0FcfRnIIrbHQm9u7Iqf68f6Kc5OFnaswqVUfrqn"
    L"wsVmAIiJV9pVHZZp1SlZFOqfLc5zzd2WGEXIAmzeg2zhF3F/U3+qxtcBsL/GQOVL1wst2gfllASkJmHydbYMvzCkNpYYvdqp"
    L"UYi9s3MOE90bbEECAwEAAaNTMFEwHQYDVR0OBBYEFEWxR00wOctJEuBeMA+VtfLaf4gHMB8GA1UdIwQYMBaAFEWxR00wOctJ"
    L"EuBeMA+VtfLaf4gHMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQELBQADggEBAFH8NFUFVdvgNSXKnC6BPhtwc9CBTZox"
    L"oPTZ6W/ULI58nik73GQAmzPGDGgMQ0IC8gIUtjwlh2qf0FIPM79wiEkagrXHoWz7C1yjhRksVm5A8IJqMikIiUBUFQLfvqsA"
    L"MgxiOUS9Gu63/bDbazYTNWI6NdLHxYn4yG/HydwCUPBEH0/FHd9AiJRPwC/2RVg7x7ZOluwyFDw/Kd0x/25gs6ZEHeDFHpDi"
    L"Jd5rl/3Ovlh2+Mxq2QCtDy06hBUIA/vta94tMySCR174RHyr+Wbvfem7aKbdsIuINM1ZKa43RMwPy8s8J0zt5LWQ+L7AdHM+"
    L"3uN+xlV1YYpYuje3z3KfLTU=";

// EC P-256, CN=orc-pin-test-ec.
const wchar_t kCertEc[] =
    L"MIIBizCCATGgAwIBAgIUBp65Ls9fA/rsIoaXwHXh5u2WvV4wCgYIKoZIzj0EAwIwGjEYMBYGA1UEAwwPb3JjLXBpbi10ZXN0"
    L"LWVjMCAXDTI2MDkxNDE4MDkzMFoYDzIxMjYwODIxMTgwOTMwWjAaMRgwFgYDVQQDDA9vcmMtcGluLXRlc3QtZWMwWTATBgcq"
    L"hkjOPQIBBggqhkjOPQMBBwNCAASbsxeevfXhWr2UMuoPJ/nuwbat9oE2rV0cyVNHFrt5/WKxrhLTriM7Jx7kcKTPadpZzUZn"
    L"Qw6CEY3N3LMiX5DVo1MwUTAdBgNVHQ4EFgQU+89Dsg/Q4J9rNRheba39R1WHJJIwHwYDVR0jBBgwFoAU+89Dsg/Q4J9rNRhe"
    L"ba39R1WHJJIwDwYDVR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAgNIADBFAiA+E56BoGvX9MIUM0DW+rjG68daDMNL4imZRSm2"
    L"plzT6wIhAOQt+1R+ecvoigUSyvH875bCzGfpsFmtup/uRP247Lut";

const wchar_t kCertAPem[] =
    L"-----BEGIN CERTIFICATE-----\n"
    L"MIIDFTCCAf2gAwIBAgIUNazM5gVEYZrz7DWFe9Kf5bvaweswDQYJKoZIhvcNAQEL\n"
    L"BQAwGTEXMBUGA1UEAwwOb3JjLXBpbi10ZXN0LWEwIBcNMjYwOTE0MTgwOTMwWhgP\n"
    L"MjEyNjA4MjExODA5MzBaMBkxFzAVBgNVBAMMDm9yYy1waW4tdGVzdC1hMIIBIjAN\n"
    L"BgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAlJBKZFawgVbcTYuib+96joYQtCIe\n"
    L"QPbpb6dBwuABcDMcD8jxMCZ4kzmmjBNHbAd6jy3yAQvPcDFQp+8st4rkOLQGgAF2\n"
    L"wLyZOOnqrYko3Kq8KOQc6KodSAU8YJD9ZaBp04S0hK+YopgDTWc2EvWkICxvQ4or\n"
    L"IVazJnoVcwsFoVT5Hy917huV/PdE4odH+RavQVx9GcgitsdCb27sip/rx/opzk4W\n"
    L"dqzCpVR+uqfCxWYAiIlX2lUdlmnVKVkU6p8tznPN3ZYYRcgCbN6DbOEXcX9Tf6rG\n"
    L"1wGwv8ZA5UvXCy3aB+WUBKQmYfJ1tgy/MKQ2lhi92qlRiL2zcw4T3RtsQQIDAQAB\n"
    L"o1MwUTAdBgNVHQ4EFgQURbFHTTA5y0kS4F4wD5W18tp/iAcwHwYDVR0jBBgwFoAU\n"
    L"RbFHTTA5y0kS4F4wD5W18tp/iAcwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0B\n"
    L"AQsFAAOCAQEAbHGOasq9v1CZ2s+fE+HOpVvJNkxQxdqqLmPweNaFEc0vuvyU7fFE\n"
    L"JBHFrT7uVW4Dfv+p/ZtU6hCbmAlPprtfj3jt6B+a434okf4RdvbNgpu/7UzSiERV\n"
    L"MF3nroMCpkIT540dnCzF/3aof+k9zhD5yYdLekPB8ykVxNCuF+hoHan5kGxSyxC7\n"
    L"KWl8QeZdwKYN9rg+m+764Szn9hrjKNu5/t1tWsaLyeDJ6+faSrt7fR6fOfdo2i2j\n"
    L"0BpOIXFoPuT2NjrYIfpZwFERlBnw8xoKAyoMOqgEQgreZa8LZf+uV2qI55NYPuE7\n"
    L"MbRjxoaweJ8kxhCNAiYZPBA8vdP1IlwonQ==\n"
    L"-----END CERTIFICATE-----\n";

// The public key of kCertA alone: the right key, but not a certificate.
const wchar_t kPublicKeyAPem[] =
    L"-----BEGIN PUBLIC KEY-----\n"
    L"MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAlJBKZFawgVbcTYuib+96\n"
    L"joYQtCIeQPbpb6dBwuABcDMcD8jxMCZ4kzmmjBNHbAd6jy3yAQvPcDFQp+8st4rk\n"
    L"OLQGgAF2wLyZOOnqrYko3Kq8KOQc6KodSAU8YJD9ZaBp04S0hK+YopgDTWc2EvWk\n"
    L"ICxvQ4orIVazJnoVcwsFoVT5Hy917huV/PdE4odH+RavQVx9GcgitsdCb27sip/r\n"
    L"x/opzk4WdqzCpVR+uqfCxWYAiIlX2lUdlmnVKVkU6p8tznPN3ZYYRcgCbN6DbOEX\n"
    L"cX9Tf6rG1wGwv8ZA5UvXCy3aB+WUBKQmYfJ1tgy/MKQ2lhi92qlRiL2zcw4T3Rts\n"
    L"QQIDAQAB\n"
    L"-----END PUBLIC KEY-----\n";

const char kSpkiA[] = "041101131061e8dc6e8e51d76aab1f33f7f2632b1fa1332c4f92615a30948740";
const char kSpkiEc[] = "7321137023171881cc72d3f926ead018b16eed1920eeb65a356305d39460c47a";
const char kWholeCertA[] = "44bfa5064f825501f5f818f7edd863ca08984f78f5a327769ce7960b51effd14";
const char kWholeCertARenewed[] = "9f5449b5dc6b9fedceb65740a1344059ad3f145bd0d209433dd51cea8bd74a1e";

std::vector<uint8_t> FromHex(const char* hex)
{
    std::vector<uint8_t> bytes;
    for (size_t i = 0; hex[i] != '\0' && hex[i + 1] != '\0'; i += 2)
    {
        const std::string pair {hex[i], hex[i + 1]};
        bytes.push_back(static_cast<uint8_t>(std::stoul(pair, nullptr, 16)));
    }

    return bytes;
}

std::vector<uint8_t> SpkiOf(const std::wstring& blob)
{
    const auto spki = CertBlobToSpkiSha256(blob);
    Assert::IsFalse(spki.has_error(), L"a valid test certificate must parse");
    return *spki;
}

// The send outcome rule, written out independently of DecideSendOutcome.
HttpSendOutcome ExpectedOutcome(
    bool pinned,
    HttpConnectionCheck pin,
    bool requireTls12,
    HttpConnectionCheck protocol,
    bool sendFailed)
{
    if (pin == HttpConnectionCheck::Rejected || protocol == HttpConnectionCheck::Rejected)
    {
        return HttpSendOutcome::Rejected;
    }

    if (sendFailed)
    {
        return HttpSendOutcome::SendFailed;
    }

    const bool pinProven = !pinned || pin == HttpConnectionCheck::Passed;
    if (!pinProven)
    {
        return HttpSendOutcome::PinNotProven;
    }

    const bool protocolProven = !requireTls12 || protocol == HttpConnectionCheck::Passed;
    if (!protocolProven)
    {
        return HttpSendOutcome::ProtocolNotProven;
    }

    return HttpSendOutcome::Accept;
}

OutputSpec::Upload MakePinnedRestUpload(std::vector<std::wstring> pins)
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
    u.rest.ServerCertPins = std::move(pins);
    return u;
}

}  // namespace

TEST_CLASS(CertificatePinningTest)
{
public:
    // The pin is the SHA-256 of the SubjectPublicKeyInfo, byte for byte what openssl computes, for RSA and for EC.
    TEST_METHOD(SpkiHashMatchesOpensslForRsaAndEc)
    {
        Assert::IsTrue(FromHex(kSpkiA) == SpkiOf(kCertA), L"RSA");
        Assert::IsTrue(FromHex(kSpkiEc) == SpkiOf(kCertEc), L"EC P-256");
    }

    // The public key is what is pinned, not the certificate: a renewal that keeps the key still matches, and the
    // hash is never the hash of the whole certificate.
    TEST_METHOD(ThePinIsTheKeyNotTheCertificate)
    {
        Assert::IsTrue(SpkiOf(kCertA) == SpkiOf(kCertARenewed), L"same key, new certificate");
        Assert::IsTrue(FromHex(kWholeCertA) != FromHex(kWholeCertARenewed), L"the certificates do differ");
        Assert::IsTrue(FromHex(kWholeCertA) != SpkiOf(kCertA), L"not the hash of the whole certificate");
        Assert::IsTrue(SpkiOf(kCertA) != SpkiOf(kCertEc), L"another key never matches");
    }

    // PEM armor, CRLF line ends and whitespace inside base64 are accepted and change nothing.
    TEST_METHOD(PemAndWhitespaceVariantsGiveTheSameHash)
    {
        const std::vector<uint8_t> expected = FromHex(kSpkiA);

        std::wstring crlf = kCertAPem;
        for (size_t at = crlf.find(L'\n'); at != std::wstring::npos; at = crlf.find(L'\n', at + 2))
        {
            crlf.insert(at, 1, L'\r');
        }

        std::wstring spaced = kCertA;
        for (size_t at = 64; at < spaced.size(); at += 65)
        {
            spaced.insert(at, 1, L'\n');
        }

        Assert::IsTrue(expected == SpkiOf(kCertAPem), L"PEM");
        Assert::IsTrue(expected == SpkiOf(crlf), L"PEM with CRLF");
        Assert::IsTrue(expected == SpkiOf(spaced), L"base64 with line breaks");
    }

    // Anything that is not a whole certificate is an error, never a hash: a bare public key (even the right one),
    // damaged or truncated base64, or nothing at all.
    TEST_METHOD(NonCertificatesAreRefused)
    {
        const std::wstring full = kCertA;
        const std::wstring refused[] = {
            L"",
            L"   ",
            L"not base64 at all!",
            L"AAAA",
            kPublicKeyAPem,
            full.substr(0, full.size() / 2),
            full.substr(0, full.size() - 8)};

        for (const auto& blob : refused)
        {
            Assert::IsTrue(CertBlobToSpkiSha256(blob).has_error(), blob.substr(0, 40).c_str());
        }
    }

    // The configured set is all or nothing: one unreadable entry refuses the whole set, so a mistyped rotation pin
    // cannot hide behind a valid one. Order and count are kept.
    TEST_METHOD(OneUnreadablePinRefusesTheWholeSet)
    {
        Assert::IsTrue(ParseServerCertPins({kCertA, L"garbage"}).has_error());
        Assert::IsTrue(ParseServerCertPins({L"garbage", kCertA}).has_error());

        const auto pins = ParseServerCertPins({kCertA, kCertEc});
        Assert::IsFalse(pins.has_error());
        Assert::AreEqual(size_t(2), pins->size());
        Assert::IsTrue(FromHex(kSpkiA) == (*pins)[0]);
        Assert::IsTrue(FromHex(kSpkiEc) == (*pins)[1]);

        const auto none = ParseServerCertPins({});
        Assert::IsFalse(none.has_error());
        Assert::IsTrue(none->empty());
    }

    // Matching is exact: a hash that is a prefix or an extension of a pin, or differs in its last byte, is not it.
    TEST_METHOD(OnlyAnExactKeyHashMatches)
    {
        const std::vector<uint8_t> a = FromHex(kSpkiA);
        const std::vector<std::vector<uint8_t>> pins = {a};

        Assert::IsTrue(SpkiMatchesAnyPin(a, pins));
        Assert::IsFalse(SpkiMatchesAnyPin(FromHex(kSpkiEc), pins));
        Assert::IsTrue(SpkiMatchesAnyPin(FromHex(kSpkiEc), {a, FromHex(kSpkiEc)}), L"a rotation pin matches");

        std::vector<uint8_t> prefix(a.begin(), a.end() - 1);
        std::vector<uint8_t> extended = a;
        extended.push_back(0);
        std::vector<uint8_t> lastByte = a;
        lastByte.back() ^= 1;

        Assert::IsFalse(SpkiMatchesAnyPin(prefix, pins), L"prefix");
        Assert::IsFalse(SpkiMatchesAnyPin(extended, pins), L"extension");
        Assert::IsFalse(SpkiMatchesAnyPin(lastByte, pins), L"last byte");
        Assert::IsFalse(SpkiMatchesAnyPin({}, pins), L"empty");
    }

    // A pin relaxes the unknown-CA check and nothing else: the name, the dates and the usage are still validated.
    // Only 'insecure' drops them.
    TEST_METHOD(APinOnlyRelaxesTheUnknownCaCheck)
    {
        const DWORD stillValidatedWithAPin =
            SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
            | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        const DWORD insecureFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | stillValidatedWithAPin;

        Assert::AreEqual(0, static_cast<int>(CertificateSecurityFlags(false, false)));
        Assert::AreEqual(
            static_cast<int>(SECURITY_FLAG_IGNORE_UNKNOWN_CA), static_cast<int>(CertificateSecurityFlags(false, true)));
        Assert::AreEqual(static_cast<int>(insecureFlags), static_cast<int>(CertificateSecurityFlags(true, false)));
        Assert::AreEqual(static_cast<int>(insecureFlags), static_cast<int>(CertificateSecurityFlags(true, true)));

        // No combination sets a flag beyond these four.
        for (const bool insecure : {false, true})
        {
            for (const bool pinned : {false, true})
            {
                Assert::AreEqual(0, static_cast<int>(CertificateSecurityFlags(insecure, pinned) & ~insecureFlags));
            }
        }
    }

    // A request sent again needs new proof; a rejection is never forgotten.
    TEST_METHOD(ASentAgainRequestNeedsNewProof)
    {
        Assert::IsTrue(RequireNewProof(HttpConnectionCheck::Passed) == HttpConnectionCheck::NotRun);
        Assert::IsTrue(RequireNewProof(HttpConnectionCheck::NotRun) == HttpConnectionCheck::NotRun);
        Assert::IsTrue(RequireNewProof(HttpConnectionCheck::Rejected) == HttpConnectionCheck::Rejected);

        // A pinned request proven on its first hop, redirected, and never checked again: its response is refused.
        HttpConnectionCheck pin = HttpConnectionCheck::Passed;
        pin = RequireNewProof(pin);
        Assert::IsTrue(
            DecideSendOutcome(true, pin, false, HttpConnectionCheck::NotRun, false) == HttpSendOutcome::PinNotProven);
    }

    // Every combination, against the rule stated independently: a rejection always wins; otherwise a failed send
    // reports its own error; otherwise the response is accepted only with positive proof from each required check.
    TEST_METHOD(SendOutcomeRequiresPositiveProofInEveryCombination)
    {
        const HttpConnectionCheck states[] = {
            HttpConnectionCheck::NotRun, HttpConnectionCheck::Passed, HttpConnectionCheck::Rejected};

        int combinations = 0;
        for (const bool pinned : {false, true})
        {
            for (const bool requireTls12 : {false, true})
            {
                for (const bool sendFailed : {false, true})
                {
                    for (const HttpConnectionCheck pin : states)
                    {
                        for (const HttpConnectionCheck protocol : states)
                        {
                            const HttpSendOutcome outcome =
                                DecideSendOutcome(pinned, pin, requireTls12, protocol, sendFailed);
                            Assert::IsTrue(ExpectedOutcome(pinned, pin, requireTls12, protocol, sendFailed) == outcome);
                            ++combinations;
                        }
                    }
                }
            }
        }

        Assert::AreEqual(72, combinations);
    }

    // The attribute splits on ',' and ';' into trimmed, non-empty entries; a multi-line PEM stays one entry.
    TEST_METHOD(PinAttributeSplitsIntoTrimmedEntries)
    {
        const std::vector<std::wstring> expected = {L"a", L"b", L"c"};
        Assert::IsTrue(expected == SplitServerCertPins(L"a,b; c ;;"));
        Assert::IsTrue(SplitServerCertPins(L"").empty());
        Assert::IsTrue(SplitServerCertPins(L" , ; ").empty());

        const std::wstring pem = kCertAPem;
        const auto pems = SplitServerCertPins(pem + L";" + kCertEc);
        Assert::AreEqual(size_t(2), pems.size());
        Assert::IsTrue(FromHex(kSpkiA) == SpkiOf(pems[0]), L"the PEM entry is intact");
        Assert::IsTrue(FromHex(kSpkiEc) == SpkiOf(pems[1]));
    }

    // ToolEmbed and the runtime gate refuse any pin that is not a certificate, naming its position, and
    // 'insecure' does not authorize it.
    TEST_METHOD(ValidateRefusesAnUnreadablePin)
    {
        Assert::IsTrue(MakePinnedRestUpload({kCertA}).Validate().verdict == OutputSpec::Upload::Verdict::Ok);
        Assert::IsTrue(MakePinnedRestUpload({kCertA, kCertEc}).Validate().verdict == OutputSpec::Upload::Verdict::Ok);

        const auto alone = MakePinnedRestUpload({L"garbage"}).Validate();
        Assert::IsTrue(alone.verdict == OutputSpec::Upload::Verdict::Refuse, alone.message.c_str());
        Assert::IsTrue(alone.message.find(L"entry 1 of 1") != std::wstring::npos, alone.message.c_str());

        auto rotation = MakePinnedRestUpload({kCertA, kPublicKeyAPem});
        const auto refused = rotation.Validate();
        Assert::IsTrue(refused.verdict == OutputSpec::Upload::Verdict::Refuse, refused.message.c_str());
        Assert::IsTrue(refused.message.find(L"entry 2 of 2") != std::wstring::npos, refused.message.c_str());

        rotation.rest.Insecure = true;
        Assert::IsTrue(rotation.Validate().verdict == OutputSpec::Upload::Verdict::Refuse, L"insecure does not help");
    }
};

}  // namespace Orc::Test
