//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "stdafx.h"

#include "HttpClient.h"
#include "Log/Log.h"
#include "Text/Fmt/std_error_code.h"
#include "Utils/BufferSpan.h"
#include "Utils/BufferView.h"
#include "Utils/Guard.h"
#include "Utils/Result.h"
#include "Utils/String.h"
#include "CryptoHashStream.h"
#include "BinaryBuffer.h"
#include "CaseInsensitive.h"
#include "SystemDetails.h"
#include "Text/CryptString.h"

#include <wincrypt.h>
#include <schannel.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>
#include <vector>

#pragma comment(lib, "Winhttp.lib")
#pragma comment(lib, "crypt32.lib")

// The TLS 1.1 and 1.2 flags may be absent from the 7.1A SDK of the v141_xp toolset, the TLS 1.3 flag from any older
// SDK. Values from the Windows 10 SDK winhttp.h.
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1
#    define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 0x00000200
#endif

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#    define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#    define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

// Same for the SChannel values (schannel.h) and the negotiated-protocol query; on a system that does not answer the
// query, https_security="os-default" then cannot prove TLS 1.2 and refuses without 'insecure'.
#ifndef SP_PROT_TLS1_1_CLIENT
#    define SP_PROT_TLS1_1_CLIENT 0x00000200
#endif

#ifndef SP_PROT_TLS1_2_CLIENT
#    define SP_PROT_TLS1_2_CLIENT 0x00000800
#endif

#ifndef SP_PROT_TLS1_3_CLIENT
#    define SP_PROT_TLS1_3_CLIENT 0x00002000
#endif

#ifndef WINHTTP_OPTION_SECURITY_INFO
#    define WINHTTP_OPTION_SECURITY_INFO 151
#endif

using namespace Orc;

// SHA-256-of-SPKI equality against the configured pin set. Pure; unit-tested in http_client_test.cpp.
bool Orc::SpkiMatchesAnyPin(const std::vector<uint8_t>& live, const std::vector<std::vector<uint8_t>>& pins)
{
    for (const auto& pin : pins)
    {
        if (pin == live)
        {
            return true;
        }
    }

    return false;
}

// Maps OS-neutral TLS version bits (HttpTlsVersion::kTls*) to a WINHTTP_FLAG_SECURE_PROTOCOL_*
// bitmask (for the WINHTTP_OPTION_SECURE_PROTOCOLS option).
DWORD Orc::ToWinHttpProtocols(uint8_t versions)
{
    DWORD mask = 0;
    if (versions & HttpTlsVersion::kTls1_0)
    {
        mask |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1;
    }

    if (versions & HttpTlsVersion::kTls1_1)
    {
        mask |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1;
    }

    if (versions & HttpTlsVersion::kTls1_2)
    {
        mask |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    }

    if (versions & HttpTlsVersion::kTls1_3)
    {
        mask |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    }

    return mask;
}

// No enforced version set (tlsVersions == 0) is https_security="os-default": the system policy chooses, and it
// may negotiate below TLS 1.2, so each HTTPS connection must prove it did not unless 'insecure' accepts it.
bool Orc::RequiresNegotiatedTls12(bool secure, uint8_t tlsVersions, bool insecure)
{
    return secure && tlsVersions == 0 && !insecure;
}

bool Orc::ShouldResendOnChallenge(
    uint32_t statusCode,
    HttpAuthScheme serverScheme,
    HttpAuthScheme proxyScheme,
    bool proxyHasCredentials,
    bool proxyInUse)
{
    if (statusCode == 401)
    {
        return IsIntegratedAuthScheme(serverScheme);
    }

    if (statusCode == 407)
    {
        return proxyInUse && proxyHasCredentials && proxyScheme != HttpAuthScheme::Anonymous;
    }

    return false;
}

std::wstring Orc::AuthSchemesText(DWORD schemes)
{
    constexpr std::pair<DWORD, const wchar_t*> kNames[] = {
        {WINHTTP_AUTH_SCHEME_BASIC, L"Basic"},
        {WINHTTP_AUTH_SCHEME_NTLM, L"NTLM"},
        {WINHTTP_AUTH_SCHEME_PASSPORT, L"Passport"},
        {WINHTTP_AUTH_SCHEME_DIGEST, L"Digest"},
        {WINHTTP_AUTH_SCHEME_NEGOTIATE, L"Negotiate"}};

    std::wstring text;
    DWORD known = 0;
    for (const auto& [bit, name] : kNames)
    {
        known |= bit;
        if (schemes & bit)
        {
            text += text.empty() ? L"" : L", ";
            text += name;
        }
    }

    if (const DWORD unknown = schemes & ~known; unknown != 0)
    {
        text += text.empty() ? L"" : L", ";
        text += fmt::format(L"unknown ({:#x})", unknown);
    }

    return text.empty() ? L"none" : text;
}

bool Orc::IsTls12OrNewerProtocol(DWORD protocol)
{
    return protocol == SP_PROT_TLS1_2_CLIENT || protocol == SP_PROT_TLS1_3_CLIENT;
}

namespace {

constexpr wchar_t kUserAgent[] = L"DFIR-ORC/1.0";

// WinHTTP's defaults stop an upload for reasons that are not failures: 30 s without a byte from a server still
// storing or hashing a multi-gigabyte body, 90 s before its response headers. The transfer is then reported as
// failed and sent again to the next <upload>. Its DNS resolution has no limit at all, so an unreachable resolver
// would hold the collection forever.
constexpr int kResolveTimeoutMs = 60 * 1000;
constexpr int kConnectTimeoutMs = 60 * 1000;
constexpr int kSendTimeoutMs = 5 * 60 * 1000;  // per write of at most 64 KB
constexpr int kReceiveTimeoutMs = 10 * 60 * 1000;  // per read, and for the response headers

// At most this much of a response nobody reads is discarded to keep its connection reusable. A longer body closes
// the connection with the request handle instead: a server cannot make ORC read an unbounded response it did not ask
// for, and the receive timeout bounds one that stalls.
constexpr size_t kMaxDrainedResponseBytes = 1024 * 1024;

void SetSessionTimeouts(HINTERNET session)
{
    // Not fatal: the defaults are shorter, not less safe.
    if (!WinHttpSetTimeouts(session, kResolveTimeoutMs, kConnectTimeoutMs, kSendTimeoutMs, kReceiveTimeoutMs))
    {
        Log::Warn(L"Failed to set the WinHTTP timeouts; the shorter defaults apply [{}]", LastWin32Error());
    }

    DWORD responseTimeout = kReceiveTimeoutMs;
    if (!WinHttpSetOption(session, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT, &responseTimeout, sizeof(responseTimeout)))
    {
        Log::Warn(
            L"Failed to set the WinHTTP response header timeout; the shorter default applies [{}]", LastWin32Error());
    }
}

// What a 401/407 response offers, for a log line: scheme names and target only. The header values are never read
// here, they carry NTLM/Negotiate tokens. The offer is only ever described: the scheme answered stays the configured
// one, since choosing it from what the other end offers would let that end pick Basic and read the password.
std::wstring DescribeChallenge(HINTERNET request, uint32_t statusCode)
{
    DWORD supported = 0, first = 0, target = 0;
    if (!WinHttpQueryAuthSchemes(request, &supported, &first, &target))
    {
        return fmt::format(L"HTTP {}, offering no scheme WinHTTP can read [{}]", statusCode, LastWin32Error());
    }

    return fmt::format(
        L"HTTP {} from the {}, offering {}",
        statusCode,
        target == WINHTTP_AUTH_TARGET_PROXY ? L"proxy" : L"server",
        AuthSchemesText(supported));
}

constexpr size_t kDefaultMaxResponseBodyBytes = 16 * 1024 * 1024;

// The enabled TLS versions as the operator wrote them in 'https_security', for a message. A handshake
// that fails against a server which tops out below this floor is indistinguishable from any other
// channel error unless the message says what was demanded.
std::wstring TlsVersionsText(uint8_t versions)
{
    if (versions == 0)
    {
        return L"the system policy (no version enforced)";
    }

    std::wstring text;
    const std::pair<uint8_t, const wchar_t*> known[] = {
        {HttpTlsVersion::kTls1_0, L"tls1.0"},
        {HttpTlsVersion::kTls1_1, L"tls1.1"},
        {HttpTlsVersion::kTls1_2, L"tls1.2"},
        {HttpTlsVersion::kTls1_3, L"tls1.3"}};

    for (const auto& [bit, name] : known)
    {
        if (versions & bit)
        {
            if (!text.empty())
            {
                text += L", ";
            }

            text += name;
        }
    }

    return text;
}

const wchar_t* SchannelProtocolKeyName(uint8_t version)
{
    switch (version)
    {
        case HttpTlsVersion::kTls1_0:
            return L"TLS 1.0";
        case HttpTlsVersion::kTls1_1:
            return L"TLS 1.1";
        case HttpTlsVersion::kTls1_2:
            return L"TLS 1.2";
        case HttpTlsVersion::kTls1_3:
            return L"TLS 1.3";
        default:
            return nullptr;
    }
}

// An administrator disables a protocol for every SChannel client with Protocols\<version>\Client\Enabled=0, and a
// session locked on it would fail every handshake. Only that explicit 0 counts: a missing key or value keeps the OS
// default and any other value enables. An unreadable policy is not "disabled", so the lock stays on the higher
// version and fails closed rather than stepping down. The registry is local: nothing on the network can set it.
bool IsDisabledBySchannelPolicy(uint8_t version)
{
    const wchar_t* name = SchannelProtocolKeyName(version);
    if (name == nullptr)
    {
        return false;
    }

    const std::wstring path =
        std::wstring(L"SYSTEM\\CurrentControlSet\\Control\\SecurityProviders\\SCHANNEL\\Protocols\\") + name
        + L"\\Client";

    Guard::RegistryHandle key;
    LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_QUERY_VALUE, &key.value());
    if (status != ERROR_SUCCESS)
    {
        if (status != ERROR_FILE_NOT_FOUND)
        {
            Log::Debug(L"TLS: cannot open the SChannel policy key of {} [{}]", name, Win32Error(status));
        }

        return false;
    }

    DWORD type = 0;
    DWORD enabled = 1;
    DWORD size = sizeof(enabled);
    status = RegQueryValueExW(key.value(), L"Enabled", nullptr, &type, reinterpret_cast<LPBYTE>(&enabled), &size);
    if (status != ERROR_SUCCESS)
    {
        if (status != ERROR_FILE_NOT_FOUND)
        {
            Log::Debug(L"TLS: cannot read the SChannel 'Enabled' policy of {} [{}]", name, Win32Error(status));
        }

        return false;
    }

    if (type != REG_DWORD || size != sizeof(enabled))
    {
        Log::Debug(L"TLS: ignoring the SChannel 'Enabled' policy of {}: not a DWORD (type {})", name, type);
        return false;
    }

    return enabled == 0;
}

// Why this Windows version may lack TLS 1.2 in WinHTTP, for the log only: it never takes part in the decision.
std::wstring Tls12UnavailableHint()
{
    DWORD major = 0;
    DWORD minor = 0;
    DWORD build = 0;
    if (HRESULT hr = SystemDetails::GetOSVersion(major, minor, build); FAILED(hr))
    {
        Log::Debug(L"TLS: no OS version to explain the missing TLS 1.2 [{}]", SystemError(hr));
        return {};
    }

    if (major == 5)
    {
        return L" (Windows XP / Server 2003 WinHTTP has no TLS 1.2)";
    }

    if (major == 6 && minor == 0)
    {
        return L" (Windows Vista / Server 2008 needs the TLS 1.2 updates of Server 2008 SP2)";
    }

    if (major == 6 && (minor == 1 || minor == 2))
    {
        return L" (Windows 7 / 8 / Server 2008 R2 / 2012 WinHTTP needs update KB3140245 for TLS 1.2)";
    }

    return {};
}

void LogRefusedTlsVersion(uint8_t version, DWORD protocol, const std::error_code& ec)
{
    if (version == HttpTlsVersion::kTls1_3)
    {
        // Only a candidate from Windows 11 / Server 2022 on, which should accept it: worth a look.
        Log::Warn(L"TLS: WinHTTP refused to enable tls1.3 although this Windows build supports it [{}]", ec);
        return;
    }

    if (version == HttpTlsVersion::kTls1_2)
    {
        Log::Warn(L"TLS: WinHTTP cannot enable tls1.2 on this system{} [{}]", Tls12UnavailableHint(), ec);
        return;
    }

    Log::Debug(L"TLS: WinHTTP refused to enable {} (flag {:#x}) [{}]", TlsVersionsText(version), protocol, ec);
}

void LogLockedTlsVersion(uint8_t version, uint8_t candidates)
{
    if (version < HttpTlsVersion::kTls1_2)
    {
        Log::Warn(
            L"TLS: no TLS 1.2 or newer is available on this system; https_security=\"os-max\" locked on {} as "
            L"insecure=\"true\" allows",
            TlsVersionsText(version));
        return;
    }

    if (version == HttpTlsVersion::kTls1_2 && (candidates & HttpTlsVersion::kTls1_3) == 0)
    {
        Log::Info(L"TLS: https_security=\"os-max\" locked on tls1.2 (TLS 1.3 needs Windows 11 or Windows Server 2022)");
        return;
    }

    Log::Info(L"TLS: https_security=\"os-max\" locked on {}", TlsVersionsText(version));
}

// The WINHTTP_CALLBACK_STATUS_FLAG_* bits by name. The raw value alone sends the reader to the SDK
// headers, and the distinction the reader needs is right here: a certificate problem names a
// certificate, while a version or cipher mismatch surfaces only as SECURITY_CHANNEL_ERROR.
std::wstring SecureFailureText(DWORD flags)
{
    std::wstring text;
    const std::pair<DWORD, const wchar_t*> known[] = {
        {WINHTTP_CALLBACK_STATUS_FLAG_CERT_REV_FAILED, L"CERT_REV_FAILED"},
        {WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CERT, L"INVALID_CERT"},
        {WINHTTP_CALLBACK_STATUS_FLAG_CERT_REVOKED, L"CERT_REVOKED"},
        {WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA, L"INVALID_CA"},
        {WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID, L"CERT_CN_INVALID"},
        {WINHTTP_CALLBACK_STATUS_FLAG_CERT_DATE_INVALID, L"CERT_DATE_INVALID"},
        {WINHTTP_CALLBACK_STATUS_FLAG_CERT_WRONG_USAGE, L"CERT_WRONG_USAGE"},
        {WINHTTP_CALLBACK_STATUS_FLAG_SECURITY_CHANNEL_ERROR, L"SECURITY_CHANNEL_ERROR"}};

    for (const auto& [bit, name] : known)
    {
        if (flags & bit)
        {
            if (!text.empty())
            {
                text += L"|";
            }

            text += name;
        }
    }

    return text.empty() ? L"none reported" : text;
}

// SHA-256 of the certificate's SubjectPublicKeyInfo (DER), as raw bytes. Logs and returns an error on failure.
Result<std::vector<uint8_t>> SpkiSha256(PCCERT_CONTEXT pCert)
{
    if (pCert == nullptr || pCert->pCertInfo == nullptr)
    {
        Log::Debug(L"SpkiSha256: null certificate context");
        return SystemError(E_INVALIDARG);
    }

    DWORD cb = 0;
    if (!CryptEncodeObject(
            X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, &pCert->pCertInfo->SubjectPublicKeyInfo, nullptr, &cb)
        || cb == 0)
    {
        auto ec = LastWin32Error();
        Log::Debug(L"SpkiSha256: CryptEncodeObject (size query) failed [{}]", ec);
        return ec;
    }

    CBinaryBuffer der;
    if (!der.SetCount(cb))
    {
        Log::Debug(L"SpkiSha256: could not allocate {} bytes for the encoded SPKI", cb);
        return SystemError(E_OUTOFMEMORY);
    }

    if (!CryptEncodeObject(
            X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, &pCert->pCertInfo->SubjectPublicKeyInfo, der.GetData(), &cb))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"SpkiSha256: CryptEncodeObject (encode) failed [{}]", ec);
        return ec;
    }

    auto hash = std::make_shared<CryptoHashStream>();
    if (HRESULT hr = hash->OpenToWrite(CryptoHashStream::Algorithm::SHA256, nullptr); FAILED(hr))
    {
        Log::Debug(L"SpkiSha256: hash OpenToWrite failed [{}]", SystemError(hr));
        return SystemError(hr);
    }

    ULONGLONG written = 0;
    if (HRESULT hr = hash->Write(der.GetData(), cb, &written); FAILED(hr))
    {
        Log::Debug(L"SpkiSha256: hash Write failed [{}]", SystemError(hr));
        return SystemError(hr);
    }

    CBinaryBuffer digest;
    const HRESULT hr = hash->GetHash(CryptoHashStream::Algorithm::SHA256, digest);
    if (FAILED(hr) || digest.GetCount() == 0)
    {
        auto ec = FAILED(hr) ? SystemError(hr) : SystemError(E_FAIL);
        Log::Debug(L"SpkiSha256: GetHash failed or produced an empty digest [{}]", ec);
        return ec;
    }

    return std::vector<uint8_t>(digest.GetData(), digest.GetData() + digest.GetCount());
}

Result<void> ApplySecurityFlags(HINTERNET request, bool insecure, bool hasPin)
{
    DWORD secFlags = CertificateSecurityFlags(insecure, hasPin);

    // Failing to set these only makes WinHTTP validate MORE strictly (the handshake would just reject the
    // cert), so it cannot leak. Still forward the error for consistency with the other SetOption calls, and
    // so we don't rely on that implicit downstream rejection.
    if (secFlags != 0 && !WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags)))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed to set TLS certificate security flags [{}]", ec);
        return ec;
    }

    return Orc::Success<void>();
}

// Force WinHTTP auto-logon to HIGH so default (machine/SYSTEM) credentials are never auto-sent to a
// server or proxy - the no-SSO guarantee; all auth here is explicit-credential.
Result<void> SetAutoLogonHigh(HINTERNET request)
{
    DWORD policy = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_AUTOLOGON_POLICY, &policy, sizeof(policy)))
    {
        auto ec = LastWin32Error();
        if (!ec)
        {
            ec = SystemError(E_FAIL);
        }

        Log::Debug(L"Failed to enforce no-SSO auto-logon policy; refusing the request [{}]", ec);
        return ec;
    }

    return Orc::Success<void>();
}

// 'context' becomes the handle's callback context and MUST carry the ConnectionCheckContext when pinning is
// configured. Measured on Windows 11 26200: passing 0 here does NOT clear what
// WinHttpSetOption(WINHTTP_OPTION_CONTEXT_VALUE) stored, so the callback still receives it. Passing the
// value explicitly costs nothing and removes the dependency on that undocumented behavior, which was never
// measured on the WinHTTP 5.1 of the v141_xp targets.
Result<void> SendRequestResilient(HINTERNET request, DWORD bodyLength, DWORD_PTR context)
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        if (WinHttpSendRequest(
                request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, bodyLength, context))
        {
            return Orc::Success<void>();
        }

        const DWORD dwError = GetLastError();
        if (dwError != ERROR_WINHTTP_RESEND_REQUEST)
        {
            auto ec = Win32Error(dwError);
            Log::Debug(L"Failed WinHttpSendRequest [{}]", ec);
            return ec;
        }

        Log::Debug(L"WinHTTP requested a resend (attempt {})", attempt + 1);
    }

    Log::Debug(L"WinHttpSendRequest kept requesting a resend; giving up");
    return Win32Error(ERROR_WINHTTP_RESEND_REQUEST);
}

// Stream 'length' bytes from 'bodyFile' at 'offset' as the request body (64-bit seek for >4 GB archives).
Result<void> StreamBody(HINTERNET request, HANDLE bodyFile, uint64_t offset, DWORD length)
{
    LARGE_INTEGER liOffset;
    liOffset.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(bodyFile, liOffset, NULL, FILE_BEGIN))
    {
        const auto ec = LastWin32Error();
        Log::Debug(L"Failed SetFilePointerEx for offset {} [{}]", offset, ec);
        return ec;
    }

    // Heap, not stack: a 64 KB frame is a large fraction of the reserved stack of the worker threads that
    // run an upload, and the span carries the size so the read length cannot outgrow the storage.
    std::vector<uint8_t> storage(65536);  // 64 KB
    const BufferSpan buffer(storage.data(), storage.size());

    DWORD bytesRemaining = length;
    while (bytesRemaining > 0)
    {
        DWORD bytesToRead = (std::min)(static_cast<DWORD>(buffer.size()), bytesRemaining);
        DWORD bytesRead = 0;
        if (!ReadFile(bodyFile, buffer.data(), bytesToRead, &bytesRead, NULL))
        {
            const auto ec = LastWin32Error();
            Log::Debug(L"Failed ReadFile [{}]", ec);
            return ec;
        }

        if (bytesRead == 0)
        {
            Log::Debug(L"Unexpected end of file at offset {}", offset + (length - bytesRemaining));
            return SystemError(E_UNEXPECTED);
        }

        DWORD bytesWritten = 0;
        if (!WinHttpWriteData(request, buffer.data(), bytesRead, &bytesWritten))
        {
            const auto ec = LastWin32Error();
            Log::Debug(L"Failed WinHttpWriteData [{}]", ec);
            return ec;
        }

        if (bytesWritten != bytesRead)
        {
            Log::Debug(L"WinHttpWriteData wrote {} bytes but expected {}", bytesWritten, bytesRead);
            return SystemError(E_FAIL);
        }

        bytesRemaining -= bytesRead;
    }

    return Orc::Success<void>();
}

// Write an already-encoded literal body (e.g. a urlencoded form) as the request body. A single
// WinHttpWriteData call may write fewer bytes than asked, so the write is looped until the whole
// body is out.
Result<void> WriteLiteralBody(HINTERNET request, const std::string& body)
{
    const uint8_t* data = reinterpret_cast<const uint8_t*>(body.data());
    size_t bytesRemaining = body.size();

    while (bytesRemaining > 0)
    {
        const DWORD bytesToWrite = static_cast<DWORD>((std::min)(bytesRemaining, static_cast<size_t>(MAXDWORD)));
        DWORD bytesWritten = 0;
        if (!WinHttpWriteData(request, data, bytesToWrite, &bytesWritten))
        {
            const auto ec = LastWin32Error();
            Log::Debug(L"Failed WinHttpWriteData for the literal request body [{}]", ec);
            return ec;
        }

        if (bytesWritten == 0)
        {
            Log::Debug(L"WinHttpWriteData wrote 0 bytes of the literal request body; aborting");
            return SystemError(E_FAIL);
        }

        data += bytesWritten;
        bytesRemaining -= bytesWritten;
    }

    return Orc::Success<void>();
}

// Receive the response and read the numeric HTTP status.
Result<void> ReceiveStatus(HINTERNET request, uint32_t& statusCode)
{
    if (!WinHttpReceiveResponse(request, NULL))
    {
        const auto ec = LastWin32Error();
        Log::Debug(L"Failed WinHttpReceiveResponse [{}]", ec);
        return ec;
    }

    DWORD code = 0;
    DWORD codeSize = sizeof(code);
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &code,
            &codeSize,
            WINHTTP_NO_HEADER_INDEX))
    {
        const auto ec = LastWin32Error();
        Log::Debug(L"Failed WinHttpQueryHeaders [{}]", ec);
        return ec;
    }

    statusCode = code;
    return Orc::Success<void>();
}

// What ConnectionCheckCallback proved for the request as it was last sent (see HttpConnectionCheck).
struct ConnectionCheckContext
{
    const std::vector<std::vector<uint8_t>>* pinnedSpki = nullptr;
    HttpConnectionCheck pinCheck = HttpConnectionCheck::NotRun;
    bool requireTls12 = false;  // https_security="os-default" without 'insecure': prove TLS 1.2 or newer
    HttpConnectionCheck protocolCheck = HttpConnectionCheck::NotRun;
    uint8_t tlsVersions = 0;  // carried only so the secure-failure message can name what was demanded
};

// The request is about to be sent again, possibly over another connection: what the callback proved so far is
// about the previous one.
void RequireNewProof(ConnectionCheckContext& ctx)
{
    ctx.pinCheck = Orc::RequireNewProof(ctx.pinCheck);
    ctx.protocolCheck = Orc::RequireNewProof(ctx.protocolCheck);
}

std::wstring SchannelProtocolText(DWORD protocol)
{
    switch (protocol)
    {
        case SP_PROT_SSL2_CLIENT:
            return L"SSL 2.0";
        case SP_PROT_SSL3_CLIENT:
            return L"SSL 3.0";
        case SP_PROT_TLS1_CLIENT:
            return L"TLS 1.0";
        case SP_PROT_TLS1_1_CLIENT:
            return L"TLS 1.1";
        case SP_PROT_TLS1_2_CLIENT:
            return L"TLS 1.2";
        case SP_PROT_TLS1_3_CLIENT:
            return L"TLS 1.3";
        default:
            return fmt::format(L"an unknown protocol ({:#x})", protocol);
    }
}

// Read the protocol this connection negotiated and accept only proof of TLS 1.2 or newer. Runs after the
// handshake and before the request is sent, so a refusal sends nothing. The reason is logged here: the callback
// that calls this has no caller to hand an error to.
// The check is per connection because the system policy cannot be read up front: WINHTTP_OPTION_SECURE_PROTOCOLS
// is set-only (WinHttpQueryOption refuses it with ERROR_INVALID_PARAMETER), while WINHTTP_OPTION_SECURITY_INFO
// reports what a connection actually negotiated. That policy may well be below TLS 1.2: WinHTTP on Windows 7 and 8
// enables only SSL 3.0 and TLS 1.0 by default.
bool CheckNegotiatedTls12(HINTERNET request)
{
    DWORD size = 0;
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_SECURITY_INFO, nullptr, &size))
    {
        const DWORD sizeError = GetLastError();
        if (sizeError != ERROR_INSUFFICIENT_BUFFER)
        {
            Log::Error(
                L"TLS: the negotiated protocol cannot be read on this system, so https_security=\"os-default\" "
                L"cannot prove TLS 1.2; aborting before send (insecure=\"true\" accepts the system policy) [{}]",
                Win32Error(sizeError));
            return false;
        }
    }

    std::vector<uint8_t> storage(size);
    if (size < sizeof(SecPkgContext_ConnectionInfo)
        || !WinHttpQueryOption(request, WINHTTP_OPTION_SECURITY_INFO, storage.data(), &size)
        || size < sizeof(SecPkgContext_ConnectionInfo))
    {
        const auto ec = LastWin32Error();
        Log::Error(
            L"TLS: failed to read the negotiated protocol ({} bytes), so https_security=\"os-default\" cannot prove "
            L"TLS 1.2; aborting before send [{}]",
            size,
            ec);
        return false;
    }

    // WinHTTP answers this option as bytes, so the struct is copied out of a view bounded by what the call
    // reported writing rather than by the allocation: the size checks above are what make the copy legal,
    // and the view is what keeps them from being separated from it by a later edit.
    const BufferView written(storage.data(), (std::min)(static_cast<size_t>(size), storage.size()));
    SecPkgContext_ConnectionInfo info {};
    std::copy_n(written.begin(), sizeof(info), reinterpret_cast<uint8_t*>(&info));
    if (!IsTls12OrNewerProtocol(info.dwProtocol))
    {
        Log::Error(
            L"TLS: the connection negotiated {} and https_security=\"os-default\" requires TLS 1.2 or newer unless "
            L"insecure=\"true\"; aborting before send",
            SchannelProtocolText(info.dwProtocol));
        return false;
    }

    Log::Debug(L"TLS: negotiated {} under https_security=\"os-default\"", SchannelProtocolText(info.dwProtocol));
    return true;
}

void CALLBACK ConnectionCheckCallback(HINTERNET hInternet, DWORD_PTR dwContext, DWORD dwStatus, LPVOID info, DWORD)
{
    auto* ctx = reinterpret_cast<ConnectionCheckContext*>(dwContext);

    if (dwStatus == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE)
    {
        const DWORD flags = info ? *static_cast<DWORD*>(info) : 0;

        // Name the enabled versions alongside the failure: SECURITY_CHANNEL_ERROR is all WinHTTP reports
        // when the server cannot reach the configured floor, and no check on this host can see that -
        // the limit is the server's. It is the one failure whose cause lives entirely at the other end.
        Log::Error(
            L"TLS secure failure during handshake [flags={:#x} ({})]; this client requires {}",
            flags,
            SecureFailureText(flags),
            TlsVersionsText(ctx ? ctx->tlsVersions : 0));
        return;
    }

    // A followed redirect sends the request again, possibly to another host over another connection. What was
    // proven belongs to the previous hop: clear it, so the next SENDING_REQUEST must prove it again, and a hop
    // this callback never sees leaves no proof and gets its response refused.
    if (dwStatus == WINHTTP_CALLBACK_STATUS_REDIRECT)
    {
        if (ctx != nullptr)
        {
            RequireNewProof(*ctx);
            Log::Debug(L"TLS connection check: redirected; the next connection must pass the checks again");
        }

        return;
    }

    if (dwStatus != WINHTTP_CALLBACK_STATUS_SENDING_REQUEST)
    {
        return;
    }

    if (ctx == nullptr)
    {
        return;  // no check configured for this request
    }

    if (ctx->requireTls12)
    {
        if (!CheckNegotiatedTls12(hInternet))
        {
            ctx->protocolCheck = HttpConnectionCheck::Rejected;
            WinHttpCloseHandle(hInternet);
            return;
        }

        ctx->protocolCheck = HttpConnectionCheck::Passed;
    }

    if (ctx->pinnedSpki == nullptr || ctx->pinnedSpki->empty())
    {
        return;  // pinning not configured for this request
    }

    PCCERT_CONTEXT pCert = nullptr;
    DWORD cb = sizeof(pCert);
    if (!WinHttpQueryOption(hInternet, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &pCert, &cb) || pCert == nullptr)
    {
        Log::Error(L"Certificate pinning: server certificate unavailable at send; aborting [{}]", LastWin32Error());
        ctx->pinCheck = HttpConnectionCheck::Rejected;
        WinHttpCloseHandle(hInternet);
        return;
    }

    const auto freeCert = Guard::CreateScopeGuard([pCert]() { CertFreeCertificateContext(pCert); });

    const auto live = SpkiSha256(pCert);
    if (live.has_error() || !SpkiMatchesAnyPin(*live, *ctx->pinnedSpki))
    {
        Log::Error(
            L"Certificate pinning: server public key matches no configured 'server_cert_pin'; aborting before send");
        ctx->pinCheck = HttpConnectionCheck::Rejected;
        WinHttpCloseHandle(hInternet);
        return;
    }

    ctx->pinCheck = HttpConnectionCheck::Passed;
    Log::Debug(L"Certificate pinning: server public key matches a configured 'server_cert_pin'");
}

Result<void> SendAttempt(HINTERNET request, const HttpRequest& httpRequest, DWORD_PTR context, uint32_t& statusCode)
{
    if (httpRequest.bodyFile != INVALID_HANDLE_VALUE && !httpRequest.bodyData.empty())
    {
        Log::Debug(L"A request cannot carry both a file body and a literal body; refusing to send");
        return Win32Error(ERROR_INVALID_PARAMETER);
    }

    if (httpRequest.bodyData.size() > MAXDWORD)
    {
        Log::Debug(
            L"Literal request body of {} bytes exceeds the DWORD length WinHTTP declares; refusing to send",
            httpRequest.bodyData.size());
        return Win32Error(ERROR_INVALID_PARAMETER);
    }

    const bool hasFileBody = httpRequest.bodyFile != INVALID_HANDLE_VALUE && httpRequest.bodyLength > 0;
    const bool hasLiteralBody = !httpRequest.bodyData.empty();
    const DWORD length = hasLiteralBody ? static_cast<DWORD>(httpRequest.bodyData.size()) : httpRequest.bodyLength;

    if (auto result = SendRequestResilient(request, length, context); result.has_error())
    {
        return result;
    }

    if (hasFileBody)
    {
        if (auto result = StreamBody(request, httpRequest.bodyFile, httpRequest.bodyOffset, length); result.has_error())
        {
            return result;
        }
    }
    else if (hasLiteralBody)
    {
        if (auto result = WriteLiteralBody(request, httpRequest.bodyData); result.has_error())
        {
            return result;
        }
    }

    return ReceiveStatus(request, statusCode);
}

Result<void> RegisterConnectionCheckCallback(HINTERNET request, ConnectionCheckContext& ctx)
{
    // Seeds the handle context for any callback that fires before the send. WinHttpSendRequest sets it
    // again from its own dwContext argument (see SendRequestResilient), which is what the SENDING_REQUEST
    // callback reads on a version that does honor it.
    ConnectionCheckContext* pCtx = &ctx;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &pCtx, sizeof(pCtx)))
    {
        const auto ec = LastWin32Error();
        Log::Error(L"TLS connection check: failed to set the request context; refusing to send [{}]", ec);
        return ec;
    }

    if (WinHttpSetStatusCallback(
            request,
            ConnectionCheckCallback,
            WINHTTP_CALLBACK_FLAG_SEND_REQUEST | WINHTTP_CALLBACK_FLAG_SECURE_FAILURE | WINHTTP_CALLBACK_FLAG_REDIRECT,
            0)
        == WINHTTP_INVALID_STATUS_CALLBACK)
    {
        const auto ec = LastWin32Error();
        Log::Error(L"TLS connection check: failed to register the status callback; refusing to send [{}]", ec);
        return ec;
    }

    return Orc::Success<void>();
}

// First send attempt plus the reactive resend on an auth challenge: integrated schemes (NTLM/Negotiate)
// and proxy auth answer the 401/407 with the credentials preset by the caller. Bounded to two resends.
//
// The resend re-streams the request body. That is deliberate: NTLM/Negotiate is a multi-leg handshake and
// WinHTTP requires the whole request - body included - to be re-issued on the 401/407 leg. The re-upload
// cost is accepted; the bound of two resends caps it.
// 'checks' is null when no connection check is registered. Before a resend it is cleared, so the response that
// is finally kept carries proof from its own send, not from an earlier one.
Result<void> SendWithChallengeResends(
    HINTERNET request,
    const HttpRequest& httpRequest,
    ConnectionCheckContext* checks,
    HttpAuthScheme serverScheme,
    const ProxySettings& proxy,
    bool proxyInUse,
    uint32_t& statusCode)
{
    const DWORD_PTR context = reinterpret_cast<DWORD_PTR>(checks);
    if (auto result = SendAttempt(request, httpRequest, context, statusCode); result.has_error())
    {
        return result;
    }

    const bool proxyHasCredentials = !proxy.user.empty();
    for (int resend = 0;
         resend < 2 && ShouldResendOnChallenge(statusCode, serverScheme, proxy.scheme, proxyHasCredentials, proxyInUse);
         ++resend)
    {
        Log::Debug(
            L"Authentication challenge ({}); resending with preset credentials",
            DescribeChallenge(request, statusCode));
        if (checks != nullptr)
        {
            RequireNewProof(*checks);
        }

        if (auto result = SendAttempt(request, httpRequest, context, statusCode); result.has_error())
        {
            return result;
        }
    }

    return Orc::Success<void>();
}

Result<void> PerformSend(
    Guard::WinHttpHandle& hRequest,
    const std::vector<std::vector<uint8_t>>& pinnedSpki,
    uint8_t tlsVersions,
    bool requireTls12,
    HttpAuthScheme serverScheme,
    const ProxySettings& proxy,
    bool proxyInUse,
    const HttpRequest& request,
    uint32_t& statusCode)
{
    ConnectionCheckContext checkCtx;
    checkCtx.pinnedSpki = &pinnedSpki;
    checkCtx.requireTls12 = requireTls12;
    checkCtx.tlsVersions = tlsVersions;

    const bool checked = !pinnedSpki.empty() || requireTls12;
    if (checked)
    {
        if (auto result = RegisterConnectionCheckCallback(hRequest.value(), checkCtx); result.has_error())
        {
            return result;
        }
    }

    // Fires at this function's scope exit - before the caller moves the handle out - so the handle is
    // handed over callback-free. The falsy check skips the unregister after a check abort released it.
    auto unregister = Guard::CreateScopeGuard([&]() {
        if (checked && hRequest)
        {
            WinHttpSetStatusCallback(hRequest.value(), nullptr, 0, 0);
        }
    });

    auto result = SendWithChallengeResends(
        hRequest.value(), request, checked ? &checkCtx : nullptr, serverScheme, proxy, proxyInUse, statusCode);

    const HttpSendOutcome outcome = DecideSendOutcome(
        !pinnedSpki.empty(), checkCtx.pinCheck, requireTls12, checkCtx.protocolCheck, result.has_error());

    if (outcome == HttpSendOutcome::Rejected)
    {
        hRequest.release();  // callback already closed it: do not double-close, do not unregister
        return Win32Error(ERROR_ACCESS_DENIED);
    }

    if (outcome == HttpSendOutcome::SendFailed)
    {
        // WinHTTP collapses every handshake refusal into ERROR_WINHTTP_SECURE_FAILURE, which the system
        // message table does not know: the operator is left with "0x80072f8f: unknown error", naming
        // neither TLS nor a cause. Say what was demanded, since the one cause no check on this host can
        // see is the server's own ceiling. Only when no connection check is registered - such a request has
        // the secure-failure callback, and that reports the same failure with its flags.
        if (!checked && result.error().value() == static_cast<int>(HRESULT_FROM_WIN32(ERROR_WINHTTP_SECURE_FAILURE)))
        {
            Log::Error(
                L"TLS handshake refused; this client requires {} and the server offered nothing it accepts, "
                L"or presented a certificate it refused [{}]",
                TlsVersionsText(tlsVersions),
                result.error());
        }

        return result;
    }

    // Positive proof required: a pinned request that completed without the callback comparing the key - on its
    // last hop or its last resend - went out with CA validation relaxed and nothing checking the certificate.
    // Refuse the response rather than let the operator believe the connection was pinned.
    if (outcome == HttpSendOutcome::PinNotProven)
    {
        Log::Error(
            L"Certificate pinning: the pin check never ran for this request as last sent; refusing the response "
            L"(the connection was NOT pinned)");
        return Win32Error(ERROR_ACCESS_DENIED);
    }

    // The same positive proof for the TLS 1.2 requirement of https_security="os-default": a request that went
    // out without the negotiated protocol ever being read proved nothing about it.
    if (outcome == HttpSendOutcome::ProtocolNotProven)
    {
        Log::Error(
            L"TLS: the negotiated protocol was never checked for this request; refusing the response "
            L"(https_security=\"os-default\" requires TLS 1.2 or newer unless insecure=\"true\")");
        return Win32Error(ERROR_ACCESS_DENIED);
    }

    return result;
}

// Read a response header by name (case-insensitive). std::nullopt if the header is absent.
std::optional<std::wstring> QueryResponseHeaderRaw(HINTERNET hRequest, const std::wstring& name)
{
    DWORD cb = 0;
    WinHttpQueryHeaders(
        hRequest, WINHTTP_QUERY_CUSTOM, name.c_str(), WINHTTP_NO_OUTPUT_BUFFER, &cb, WINHTTP_NO_HEADER_INDEX);
    const DWORD sizeErr = GetLastError();
    if (sizeErr == ERROR_WINHTTP_HEADER_NOT_FOUND)
    {
        return std::nullopt;  // benign: the response simply carries no such header (the caller uses its default)
    }

    if (sizeErr != ERROR_INSUFFICIENT_BUFFER || cb < sizeof(wchar_t))
    {
        // A real failure to size the header, not a plain absence: log it (the caller still falls back to default).
        Log::Debug(L"Failed WinHttpQueryHeaders sizing header '{}' [{}]", name, Win32Error(sizeErr));
        return std::nullopt;
    }

    std::wstring value(cb / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CUSTOM, name.c_str(), value.data(), &cb, WINHTTP_NO_HEADER_INDEX))
    {
        // The header existed at the sizing call but the read failed: a genuine error, not a benign absence.
        Log::Debug(L"Failed WinHttpQueryHeaders reading header '{}' [{}]", name, LastWin32Error());
        return std::nullopt;
    }

    value.resize(cb / sizeof(wchar_t));
    return value;
}

// Read the full response body (e.g. a PROPFIND 207 multistatus document). std::nullopt if a WinHTTP
// read fails partway: the caller must not treat a truncated body as complete (a partial PROPFIND
// could yield a wrong captured value). An empty body is a successful empty string, not nullopt.
// 'maxBytes' caps total buffering; an over-limit body is treated as a failed read (nullopt), exactly like
// a truncated read - the caller falls back to its default.
std::optional<std::string> ReadResponseBodyRaw(HINTERNET hRequest, size_t maxBytes = kDefaultMaxResponseBodyBytes)
{
    std::string body;
    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &available))
        {
            Log::Debug(L"Failed WinHttpQueryDataAvailable [{}]", LastWin32Error());
            return std::nullopt;  // API failure: the body is incomplete - signal it rather than truncate
        }

        if (available == 0)
        {
            break;  // clean end of body
        }

        // body.size() never exceeds the cap, so the subtraction cannot underflow.
        if (available > maxBytes - body.size())
        {
            Log::Error(
                L"Response body exceeds the {}-byte cap; treating the response as unavailable (a dependent "
                L"capture will fall back to its default)",
                maxBytes);
            return std::nullopt;
        }

        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, chunk.data(), available, &read))
        {
            Log::Debug(L"Failed WinHttpReadData [{}]", LastWin32Error());
            return std::nullopt;  // partial read: signal incomplete rather than truncate
        }

        if (read == 0)
        {
            // WinHttpQueryDataAvailable reported bytes available but the read returned none: the server is
            // making no forward progress. Stop rather than spin forever - the size cap is keyed on bytes
            // buffered, which would never grow here, so it cannot bound this stall. Treat as an incomplete read.
            Log::Debug(L"Response read returned 0 bytes while data was reported available; aborting read");
            return std::nullopt;
        }

        body.append(chunk.data(), read);
    }

    return body;
}

// Header names whose values are secret and must never reach a log (session tokens, auth challenges).
//
// The ASCII fold, not equalCaseInsensitive/towlower: this decision fails OPEN (an unrecognised name is
// logged with its value), and every name below carries an 'i', the one letter a locale can move -
// towlower() defers to LCMapStringW for non-ASCII and Turkish casing maps 'I' to U+0131, so a header
// written "AUTHORIZATION" would not have matched "authorization" and its token would have been logged.
bool IsSensitiveHeaderName(std::wstring_view name)
{
    constexpr std::wstring_view kSensitive[] = {
        L"set-cookie", L"www-authenticate", L"proxy-authenticate", L"authorization"};

    return std::any_of(std::begin(kSensitive), std::end(kSensitive), [name](std::wstring_view sensitive) {
        return EqualsAsciiCaseInsensitive(name, sensitive);
    });
}

// True for the one header the client presets itself (the preemptive Basic Authorization header added by
// BasicAuthenticator). TrimSpacesAndTabs so a config-written " Authorization: ..." still matches.
bool IsAuthorizationHeaderName(std::wstring_view name)
{
    return EqualsAsciiCaseInsensitive(TrimSpacesAndTabs(name), L"authorization");
}

}  // namespace

// https_security="os-max": offer exactly one TLS version, the highest of 'candidates' available here, so a
// handshake has no lower version to be pushed down to. A version is unavailable only for local reasons nothing on
// the network can provoke: the SChannel policy disables it, or WinHttpSetOption refuses its flag. A failed
// handshake is never retried with a lower version, as such a retry is the downgrade this lock exists to rule out.
// OsMaxTlsCandidates never pairs TLS 1.3 with a lower version, so an unavailable TLS 1.3 refuses the upload.
Result<uint8_t> Orc::LockHighestTlsVersion(uint8_t candidates, const TlsLockProbes& probes)
{
    if (candidates == 0)
    {
        // No candidate (the OS build is unknown): nothing is guessed, and the system policy is not an option either.
        // Reading an unknown build as an old host would lock Windows 11 or Server 2022 on TLS 1.2, the very
        // fallback os-max exists to rule out.
        auto ec = Win32Error(ERROR_INVALID_PARAMETER);
        Log::Error(L"TLS: https_security=\"os-max\" has no TLS version to lock on; refusing the upload [{}]", ec);
        return ec;
    }

    const uint8_t highestFirst[] = {
        HttpTlsVersion::kTls1_3, HttpTlsVersion::kTls1_2, HttpTlsVersion::kTls1_1, HttpTlsVersion::kTls1_0};

    std::error_code lastError = Win32Error(ERROR_INVALID_PARAMETER);  // reported if no version is a candidate
    for (const uint8_t version : highestFirst)
    {
        if ((candidates & version) == 0)
        {
            continue;
        }

        if (probes.isDisabledByPolicy(version))
        {
            lastError = Win32Error(ERROR_NOT_SUPPORTED);
            Log::Warn(
                L"TLS: {} is disabled by this system's SChannel policy (Protocols\\{}\\Client\\Enabled=0); not "
                L"offering it",
                TlsVersionsText(version),
                SchannelProtocolKeyName(version));
            continue;
        }

        if (std::error_code ec = probes.enableOnly(version); ec)
        {
            lastError = ec;
            LogRefusedTlsVersion(version, ToWinHttpProtocols(version), lastError);
            continue;
        }

        LogLockedTlsVersion(version, candidates);
        return version;
    }

    if (candidates == HttpTlsVersion::kTls1_3)
    {
        Log::Error(
            L"TLS: tls1.3 is not available on this system, and https_security=\"os-max\" requires it on Windows 11 "
            L"and Windows Server 2022 or later: it never falls back to TLS 1.2 there, even with insecure=\"true\" [{}]",
            lastError);
        return lastError;
    }

    const bool belowTls12Allowed = (candidates & (HttpTlsVersion::kTls1_0 | HttpTlsVersion::kTls1_1)) != 0;
    Log::Error(
        L"TLS: none of {} is available on this system for https_security=\"os-max\"{} [{}]",
        TlsVersionsText(candidates),
        belowTls12Allowed ? L"" : L"; a version below TLS 1.2 is only used with insecure=\"true\"",
        lastError);
    return lastError;
}

// CryptStringToBinary with CRYPT_STRING_BASE64_ANY takes PEM armor, bare base64 and embedded whitespace alike;
// CertCreateCertificateContext then refuses anything that is not a certificate.
Result<std::vector<uint8_t>> Orc::CertBlobToSpkiSha256(const std::wstring& blob)
{
    DWORD cbDer = 0;
    if (!CryptStringToBinaryW(blob.c_str(), 0, CRYPT_STRING_BASE64_ANY, nullptr, &cbDer, nullptr, nullptr)
        || cbDer == 0)
    {
        auto ec = LastWin32Error();
        Log::Debug(L"CertBlobToSpkiSha256: CryptStringToBinary (size query) failed [{}]", ec);
        return ec;
    }

    CBinaryBuffer der;
    if (!der.SetCount(cbDer))
    {
        Log::Debug(L"CertBlobToSpkiSha256: could not allocate {} bytes for the decoded certificate", cbDer);
        return SystemError(E_OUTOFMEMORY);
    }

    if (!CryptStringToBinaryW(blob.c_str(), 0, CRYPT_STRING_BASE64_ANY, der.GetData(), &cbDer, nullptr, nullptr))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"CertBlobToSpkiSha256: CryptStringToBinary (decode) failed [{}]", ec);
        return ec;
    }

    PCCERT_CONTEXT pCert = CertCreateCertificateContext(X509_ASN_ENCODING, der.GetData(), cbDer);
    if (pCert == nullptr)
    {
        auto ec = LastWin32Error();
        Log::Debug(L"CertBlobToSpkiSha256: CertCreateCertificateContext failed [{}]", ec);
        return ec;
    }

    const auto freeCert = Guard::CreateScopeGuard([pCert]() { CertFreeCertificateContext(pCert); });
    return SpkiSha256(pCert);  // logs its own errors
}

Result<std::vector<std::vector<uint8_t>>> Orc::ParseServerCertPins(const std::vector<std::wstring>& blobs)
{
    std::vector<std::vector<uint8_t>> pins;
    for (size_t i = 0; i < blobs.size(); ++i)
    {
        auto spki = CertBlobToSpkiSha256(blobs[i]);
        if (spki.has_error())
        {
            Log::Debug(
                L"server_cert_pin: entry {} of {} is not a usable certificate [{}]", i + 1, blobs.size(), spki.error());
            return spki.error();
        }

        pins.push_back(std::move(*spki));
    }

    return pins;
}

DWORD Orc::CertificateSecurityFlags(bool insecure, bool pinned)
{
    if (insecure)
    {
        return SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
            | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    }

    if (pinned)
    {
        return SECURITY_FLAG_IGNORE_UNKNOWN_CA;
    }

    return 0;
}

HttpConnectionCheck Orc::RequireNewProof(HttpConnectionCheck check)
{
    return check == HttpConnectionCheck::Rejected ? HttpConnectionCheck::Rejected : HttpConnectionCheck::NotRun;
}

// Order matters: a rejection closed the handle, so it wins over the send error it caused; a failed send reports
// its own error, since the checks legitimately did not run when nothing was sent; only a completed send needs
// positive proof from every check it required.
HttpSendOutcome Orc::DecideSendOutcome(
    bool pinned,
    HttpConnectionCheck pinCheck,
    bool requireTls12,
    HttpConnectionCheck protocolCheck,
    bool sendFailed)
{
    if (pinCheck == HttpConnectionCheck::Rejected || protocolCheck == HttpConnectionCheck::Rejected)
    {
        return HttpSendOutcome::Rejected;
    }

    if (sendFailed)
    {
        return HttpSendOutcome::SendFailed;
    }

    if (pinned && pinCheck != HttpConnectionCheck::Passed)
    {
        return HttpSendOutcome::PinNotProven;
    }

    if (requireTls12 && protocolCheck != HttpConnectionCheck::Passed)
    {
        return HttpSendOutcome::ProtocolNotProven;
    }

    return HttpSendOutcome::Accept;
}

Result<std::wstring> Orc::MakeBasicAuthHeader(const std::wstring& user, const std::wstring& password)
{
    std::wstring cred = user;
    cred.append(L":").append(password);
    // Scrub the cleartext "user:password" from the heap on EVERY exit path (declared first so it also
    // covers the size-query failure below, before the UTF-8 buffer exists).
    const auto wipeCred =
        Guard::CreateScopeGuard([&]() { SecureZeroMemory(cred.data(), cred.size() * sizeof(wchar_t)); });

    const int cb =
        WideCharToMultiByte(CP_UTF8, 0, cred.c_str(), static_cast<int>(cred.size()), nullptr, 0, nullptr, nullptr);
    if (cb <= 0)
    {
        auto ec = LastWin32Error();
        Log::Debug(L"MakeBasicAuthHeader: WideCharToMultiByte (size query) failed [{}]", ec);
        return ec;
    }

    std::vector<BYTE> utf8(static_cast<size_t>(cb));
    // Scrub the UTF-8 copy of the same credential too.
    const auto wipeUtf8 = Guard::CreateScopeGuard([&]() { SecureZeroMemory(utf8.data(), utf8.size()); });

    if (WideCharToMultiByte(
            CP_UTF8,
            0,
            cred.c_str(),
            static_cast<int>(cred.size()),
            reinterpret_cast<LPSTR>(utf8.data()),
            cb,
            nullptr,
            nullptr)
        != cb)
    {
        auto ec = LastWin32Error();
        Log::Debug(L"MakeBasicAuthHeader: WideCharToMultiByte (convert) failed [{}]", ec);
        return ec;
    }

    auto b64 = CryptBinaryToSingleLineString(BufferView(utf8.data(), utf8.size()), CRYPT_STRING_BASE64);
    if (b64.has_error())
    {
        Log::Debug(L"MakeBasicAuthHeader: base64 encoding failed [{}]", b64.error());
        return b64.error();
    }

    return L"Authorization: Basic " + *b64;
}

// Split a "host[:port]" string into its host and port. Input: 'server' is a host, optionally with a port
// ("host", "host:8443", "[::1]", "[::1]:8443") and NEVER a scheme (the caller tracks https/http via
// 'secure'); 'defaultPort' is used when no port is given.
Result<Orc::HostPort> Orc::SplitHostPort(const std::wstring& server, INTERNET_PORT defaultPort)
{
    // The port separator is the last ':' that is not inside an "[...]" IPv6 literal.
    const size_t closeBracket = server.rfind(L']');
    const size_t colon = server.rfind(L':');

    // A bracketless IPv6 literal (e.g. "fe80::1") has multiple colons and no ']'; it carries no port, so its
    // last colon is NOT a port separator (splitting on it would yield a wrong host like "fe80:" + port 1).
    // Attaching a port to an IPv6 address requires brackets ("[fe80::1]:443").
    const bool bracketlessIpv6 =
        (closeBracket == std::wstring::npos) && (colon != std::wstring::npos) && (server.find(L':') != colon);

    const bool hasSeparator = (colon != std::wstring::npos)
        && (closeBracket == std::wstring::npos || colon > closeBracket) && !bracketlessIpv6;

    if (!hasSeparator)
    {
        return Orc::HostPort {server, defaultPort};  // no port given (or a bracketless IPv6): use the default
    }

    const std::wstring portStr = server.substr(colon + 1);
    if (portStr.empty())
    {
        Log::Debug(L"SplitHostPort: '{}' has a ':' but no port", server);
        return SystemError(E_INVALIDARG);
    }

    wchar_t* end = nullptr;
    const unsigned long value = wcstoul(portStr.c_str(), &end, 10);
    if (*end != L'\0' || value == 0 || value > 65535)
    {
        Log::Debug(L"SplitHostPort: invalid port '{}' in '{}' (must be 1..65535)", portStr, server);
        return SystemError(E_INVALIDARG);
    }

    std::wstring host = server.substr(0, colon);
    if (host.empty())
    {
        Log::Debug(L"SplitHostPort: '{}' has a port but no host", server);
        return SystemError(E_INVALIDARG);
    }

    return Orc::HostPort {std::move(host), static_cast<INTERNET_PORT>(value)};
}

std::wstring Orc::RedactResponseHeaders(const std::wstring& raw)
{
    std::wstring out;
    size_t start = 0;
    for (;;)
    {
        const size_t end = raw.find(L"\r\n", start);
        std::wstring line = raw.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);

        if (const size_t colon = line.find(L':'); colon != std::wstring::npos)
        {
            std::wstring name = line.substr(0, colon);
            while (!name.empty() && (name.back() == L' ' || name.back() == L'\t'))
            {
                name.pop_back();
            }

            if (IsSensitiveHeaderName(name))
            {
                line = line.substr(0, colon) + L": [redacted]";
            }
        }

        out += line;
        if (end == std::wstring::npos)
        {
            break;
        }

        out += L"\r\n";
        start = end + 2;
    }

    return out;
}

std::optional<std::wstring> Orc::GetResponseHeader(const HttpResponse& response, const std::wstring& name)
{
    return QueryResponseHeaderRaw(response.request.value(), name);
}

std::optional<std::string> Orc::ReadResponseBody(const HttpResponse& response)
{
    return ReadResponseBodyRaw(response.request.value());
}

std::optional<std::string> Orc::ReadResponseBody(const HttpResponse& response, size_t maxBytes)
{
    return ReadResponseBodyRaw(response.request.value(), maxBytes);
}

std::wstring Orc::DescribeAuthChallenge(const HttpResponse& response)
{
    return DescribeChallenge(response.request.value(), response.statusCode);
}

// WinHTTP returns a connection to its pool only once the response on it has been read to the end. Closing the
// request handle earlier closes the connection too, and the next request pays a new TCP and TLS handshake.
void Orc::DrainResponseBody(const HttpResponse& response)
{
    if (!response.request)
    {
        return;
    }

    const HINTERNET hRequest = response.request.value();

    std::array<uint8_t, 8192> storage {};
    const BufferSpan buffer(storage.data(), storage.size());
    size_t drained = 0;
    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &available) || available == 0)
        {
            return;
        }

        if (available > kMaxDrainedResponseBytes - drained)
        {
            Log::Debug(
                L"Response body over {} bytes left unread; its connection is not reused", kMaxDrainedResponseBytes);
            return;
        }

        DWORD read = 0;
        if (!WinHttpReadData(hRequest, buffer.data(), (std::min)(available, static_cast<DWORD>(buffer.size())), &read)
            || read == 0)
        {
            return;
        }

        drained += read;
    }
}

// On an unexpected status, dump the response headers and a bounded body snippet at Debug level. A
// failing server usually explains itself here (a TUS 412 carries a "Tus-Version" header, and the
// body carries the reason). Debug-only, so a normal run stays quiet.
void Orc::LogResponseDiagnostics(const HttpResponse& response)
{
    HINTERNET hRequest = response.request.value();

    DWORD cb = 0;
    WinHttpQueryHeaders(
        hRequest,
        WINHTTP_QUERY_RAW_HEADERS_CRLF,
        WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER,
        &cb,
        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || cb < sizeof(wchar_t))
    {
        Log::Debug(
            L"Failed WinHttpQueryHeaders: could not size raw response headers for diagnostics [{}]", LastWin32Error());
    }
    else
    {
        std::wstring headers(cb / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(
                hRequest,
                WINHTTP_QUERY_RAW_HEADERS_CRLF,
                WINHTTP_HEADER_NAME_BY_INDEX,
                headers.data(),
                &cb,
                WINHTTP_NO_HEADER_INDEX))
        {
            headers.resize(cb / sizeof(wchar_t));
            Log::Debug(L"Response headers:\n{}", RedactResponseHeaders(headers));
        }
        else
        {
            Log::Debug(L"Failed WinHttpQueryHeaders (raw headers) [{}]", LastWin32Error());
        }
    }

    const std::optional<std::string> body = ReadResponseBodyRaw(hRequest);
    if (body && !body->empty())
    {
        constexpr size_t kMaxBody = 2048;
        const std::string snippet = body->size() > kMaxBody ? body->substr(0, kMaxBody) : *body;
        Log::Debug(L"Response body ({} bytes): {}", body->size(), std::wstring(snippet.begin(), snippet.end()));
    }
}

Result<void> Orc::HttpClient::Open(const HttpClientConfig& config, HttpClient& client)
{
    auto pins = ParseServerCertPins(config.serverCertPins);
    if (pins.has_error())
    {
        Log::Error(
            L"server_cert_pin: a configured certificate could not be parsed; refusing to upload [{}]", pins.error());
        return pins.error();
    }

    client.m_pinnedSpki = std::move(*pins);

    client.m_secure = config.secure;
    client.m_insecure = config.insecure;
    client.m_followRedirects = config.followRedirects;
    client.m_serverScheme = config.serverScheme;
    client.m_serverUser = config.serverUser;
    client.m_serverPassword = config.serverPassword;
    client.m_serverAuth = MakeServerAuthenticator(config);
    client.m_proxy = config.proxy;
    client.m_tlsVersions = config.tlsVersions;

    if (auto result = CheckProxySettings(client.m_proxy); result.has_error())
    {
        return result;  // CheckProxySettings reported why no session may be opened
    }

    client.m_session = OpenProxySession(kUserAgent, client.m_proxy);
    if (!client.m_session)
    {
        const auto ec = LastWin32Error();
        Log::Debug(L"Failed WinHttpOpen [{}]", ec);
        return ec;
    }

    SetSessionTimeouts(client.m_session.value());

    if (config.tlsLockHighest)
    {
        const HINTERNET session = client.m_session.value();
        TlsLockProbes probes;
        probes.isDisabledByPolicy = IsDisabledBySchannelPolicy;
        probes.enableOnly = [session](uint8_t version) -> std::error_code {
            DWORD protocol = ToWinHttpProtocols(version);
            if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocol, sizeof(protocol)))
            {
                return LastWin32Error();  // LockHighestTlsVersion logs the refusal with the version
            }

            return {};
        };

        const auto locked = LockHighestTlsVersion(config.tlsVersions, probes);
        if (locked.has_error())
        {
            return locked.error();  // LockHighestTlsVersion logged the cause
        }

        client.m_tlsVersions = *locked;
    }

    const DWORD tlsProtocols = config.tlsLockHighest ? 0 : ToWinHttpProtocols(config.tlsVersions);
    if (tlsProtocols != 0
        && !WinHttpSetOption(
            client.m_session.value(), WINHTTP_OPTION_SECURE_PROTOCOLS, (LPVOID)&tlsProtocols, sizeof(tlsProtocols)))
    {
        auto ec = LastWin32Error();
        Log::Debug(
            L"Failed WinHttpSetOption to enforce TLS {} (mask {:#x}); refusing to risk a downgrade [{}]",
            TlsVersionsText(config.tlsVersions),
            tlsProtocols,
            ec);
        return ec;
    }

    const INTERNET_PORT defaultPort = config.secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    const auto hostPort = SplitHostPort(config.serverName, defaultPort);
    if (hostPort.has_error())
    {
        return hostPort.error();  // SplitHostPort logged the specific cause
    }

    const auto& [host, port] = *hostPort;

    if (auto result = BindProxyToSession(client.m_session.value(), config.secure, host, port, client.m_proxy);
        result.has_error())
    {
        return result;  // BindProxyToSession logged the cause
    }

    // Proxy credentials go to a proxy or nowhere. A 407 on a connection with no proxy comes from something else in
    // the path, and a host the bypass list may cover counts as direct: withholding the credentials from a real proxy
    // fails the upload on its 407, which the log then explains.
    client.m_proxyInUse = IsProxyInUse(client.m_proxy, host);
    if (!client.m_proxy.user.empty() && !client.m_proxyInUse)
    {
        Log::Warn(
            L"No proxy handles the connection to '{}' (none resolved for it, or the bypass list may cover it); "
            L"'proxy_user' is not sent",
            host);
    }

    client.m_connect = Guard::WinHttpHandle(WinHttpConnect(client.m_session.value(), host.c_str(), port, 0));
    if (!client.m_connect)
    {
        const auto ec = LastWin32Error();
        Log::Debug(L"Failed WinHttpConnect for host '{}' port {} [{}]", host, port, ec);
        return ec;
    }

    Log::Debug(L"HttpClient opened for {}://{}:{}", config.secure ? L"https" : L"http", host, port);
    return Orc::Success<void>();
}

void Orc::HttpClient::Close()
{
    m_connect = Guard::WinHttpHandle();
    m_session = Guard::WinHttpHandle();
}

Result<void> Orc::HttpClient::Send(const HttpRequest& request, HttpResponse& response)
{
    // A scheme may shape the request (headers, path encoding), and the clock-skew retry re-runs that
    // shaping. Working on a copy makes it idempotent by construction: every attempt starts from the
    // caller's pristine request instead of from the previous attempt's output.
    HttpRequest working = request;

    // Shape the request before its handle exists: the handle is built FROM working.path, so a scheme that
    // encodes the path has to run here, not with the credentials below.
    if (m_serverAuth)
    {
        if (auto result = m_serverAuth->PrepareRequest(working); result.has_error())
        {
            return result;
        }
    }

    // WINHTTP_FLAG_REFRESH sends "Pragma: no-cache" and "Cache-Control: no-cache": a caching proxy in front of an
    // http:// server must not answer a HEAD or PROPFIND resume probe with a remote size it stored earlier, which would
    // resume the body at the wrong offset, or a repeat="Once" check, which would skip or redo a set on a stale answer.
    // HTTPS through a proxy is a tunnel it cannot cache, and PUT or POST responses are not cached.
    //
    // A pre-encoded path is already exactly what must go on the wire. Measured on Windows 11 26200: the AWS
    // encoding alphabet (A-Za-z0-9-_.~ % hex /) contains nothing WinHTTP escapes in either mode, so the escape
    // flags are belt-and-braces rather than load-bearing - they guard against an older WinHTTP whose "unsafe"
    // set includes '~', which AWS mandates be left bare.
    DWORD dwFlags = WINHTTP_FLAG_REFRESH | (m_secure ? WINHTTP_FLAG_SECURE : 0);
    if (working.pathIsPreEncoded)
    {
        dwFlags |= WINHTTP_FLAG_ESCAPE_DISABLE | WINHTTP_FLAG_ESCAPE_DISABLE_QUERY;
    }

    Guard::WinHttpHandle hRequest(WinHttpOpenRequest(
        m_connect.value(),
        working.method.c_str(),
        working.path.c_str(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        dwFlags));
    if (!hRequest)
    {
        // The path is not logged: it may carry a <response> capture value substituted into it, and a
        // captured upload URL embeds a session token. HttpClient has no template to fall back to at
        // this layer - it only ever sees the substituted HttpRequest - so log the method and the error.
        const auto ec = LastWin32Error();
        Log::Debug(L"Failed WinHttpOpenRequest for {} [{}]", working.method, ec);
        return ec;
    }

    // Redirect policy. By default (follow_redirects off) the REST dialog models any location change itself
    // (captured Location + UrlPathAndQuery), so transparent redirects are disabled: auto-following a cross-host
    // redirect would re-send the preemptive Basic Authorization header / preset integrated credentials to the
    // redirect target, leaking them to a MITM that certificate pinning does not cover (pinning is optional). When
    // follow_redirects is enabled, allow redirects but keep forbidding the HTTPS->HTTP downgrade. A scheme that
    // signs the request refuses redirects outright, whatever the configuration asks (see ForbidsRedirects).
    const bool forbidRedirects = m_serverAuth && m_serverAuth->ForbidsRedirects();
    DWORD dwRedirectPolicy = (m_followRedirects && !forbidRedirects)
        ? WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP
        : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(
            hRequest.value(), WINHTTP_OPTION_REDIRECT_POLICY, &dwRedirectPolicy, sizeof(dwRedirectPolicy)))
    {
        const auto ec = LastWin32Error();
        Log::Debug(L"Failed to set WinHTTP redirect policy; refusing the request [{}]", ec);
        return ec;
    }

    // Setup, all BEFORE the send. Credentials are pre-set (Basic preemptive; integrated for the resend). The
    // proxy is bound to the session in Open(). The Basic Authorization header (inside the server Authenticator)
    // is added before the caller's headers so an operator header can override it.
    if (auto result = ApplySecurityFlags(hRequest.value(), m_insecure, !m_pinnedSpki.empty()); result.has_error())
    {
        return result;
    }

    if (auto result = SetAutoLogonHigh(hRequest.value()); result.has_error())
    {
        return result;
    }

    // The server credential, when the configuration named one. Null means Anonymous, or a scheme with
    // no user - both of which presented nothing before this seam existed either.
    if (m_serverAuth)
    {
        if (auto result = m_serverAuth->ApplyToHandle(hRequest.value()); result.has_error())
        {
            return result;
        }
    }

    if (m_proxyInUse)
    {
        if (auto result = ApplyProxyCredentials(hRequest.value(), m_proxy); result.has_error())
        {
            return result;
        }
    }

    for (const auto& header : working.headers)
    {
        // Log the header name only (the value may carry a secret, e.g. a token).
        const auto colon = header.find(L':');
        const std::wstring name = colon == std::wstring::npos ? header : header.substr(0, colon);

        // An operator 'Authorization' header must REPLACE the preemptive Basic one, not stack next to it.
        // Measured: with plain ADD, WinHTTP accepts the second header and builds a request carrying BOTH
        // Authorization lines, then refuses to send it - WinHttpSendRequest fails with ERROR_ALREADY_EXISTS
        // (0xB7) and nothing reaches the wire, so the upload dies with an opaque error. Every other header
        // keeps plain ADD so a config may legitimately repeat a header name.
        const DWORD addFlags = IsAuthorizationHeaderName(name) ? (WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)
                                                               : WINHTTP_ADDREQ_FLAG_ADD;
        if (!WinHttpAddRequestHeaders(hRequest.value(), header.c_str(), (DWORD)-1, addFlags))
        {
            // Fail closed: a missing header can corrupt the request (e.g. a chunk's Content-Range) or drop an
            // auth token, so refuse rather than send a malformed/under-authenticated request.
            auto ec = LastWin32Error();
            Log::Debug(L"Failed to add request header '{}'; refusing the request [{}]", name, ec);
            return ec;
        }

        Log::Debug(L"Sent request header '{}'", name);
    }

    // The send phase. PerformSend owns the pin callback's lifetime (when pinning is configured) and
    // unregisters it before returning, so the handle below is callback-free and safe to hand to the caller.
    uint32_t statusCode = 0;
    if (auto result = PerformSend(
            hRequest,
            m_pinnedSpki,
            m_tlsVersions,
            RequiresNegotiatedTls12(m_secure, m_tlsVersions, m_insecure),
            m_serverScheme,
            m_proxy,
            m_proxyInUse,
            working,
            statusCode);
        result.has_error())
    {
        return result;
    }

    response.statusCode = statusCode;
    response.request = std::move(hRequest);

    // One re-signed attempt, and only when the scheme asks for it. Distinct from the 401/407 resend: that one
    // presents the SAME credential again, this one presents a NEW signature, so unlike an auth resend it can
    // actually succeed. The recursion is bounded by the authenticator itself, which grants at most one
    // correction for the life of the object, so the second attempt cannot ask for a third.
    if (m_serverAuth && m_serverAuth->OnRejected(response))
    {
        DrainResponseBody(response);
        return Send(request, response);
    }

    return Orc::Success<void>();
}
