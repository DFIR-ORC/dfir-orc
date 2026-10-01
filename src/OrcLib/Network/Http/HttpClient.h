//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "Authenticator.h"
#include "AwsV4.h"
#include "HttpAuth.h"
#include "HttpProxy.h"
#include "Utils/Guard/WinHttp.h"
#include "Utils/Result.h"

#include <winhttp.h>
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <functional>
#include <memory>
#include <system_error>

namespace Orc {

namespace HttpTlsVersion {
constexpr uint8_t kTls1_0 = 1 << 0;
constexpr uint8_t kTls1_1 = 1 << 1;
constexpr uint8_t kTls1_2 = 1 << 2;
constexpr uint8_t kTls1_3 = 1 << 3;
}  // namespace HttpTlsVersion

// The response-body cap for a <response> capture read. Far below the generic 16 MiB default on
// purpose: an HTML auth page is tens of kilobytes, and the body is attacker-controlled input that a
// scanner is about to walk. Over the cap the read fails closed and the capture falls back.
constexpr size_t kMaxCaptureBodyBytes = 1 * 1024 * 1024;

struct HostPort
{
    std::wstring host;
    INTERNET_PORT port = 0;
};

struct HttpClientConfig
{
    std::wstring serverName;  // host or host:port (scheme lives in `secure`)
    bool secure = true;  // https vs http
    bool insecure = false;  // disable cert validation (server-scoped)
    bool followRedirects = false;  // follow HTTP redirects (default off; see HttpClient::Send)

    HttpAuthScheme serverScheme = HttpAuthScheme::Anonymous;
    std::wstring serverUser;
    std::wstring serverPassword;

    AwsCredentials aws;  // populated only when serverScheme == HttpAuthScheme::AWSv4
    bool awsAllowServerClock = false;

    std::vector<std::wstring> serverCertPins;
    uint8_t tlsVersions = 0;
    // Lock on the single highest of tlsVersions the system accepts instead of enabling them all
    // (https_security="os-max", see HttpClient::Open).
    bool tlsLockHighest = false;

    ProxySettings proxy;
};

struct HttpRequest
{
    std::wstring method;
    std::wstring path;
    std::vector<std::wstring> headers;

    // The path is already percent-encoded by the caller and WinHTTP must not touch it. Set by a signing
    // scheme, whose signature covers the exact bytes in 'path'.
    bool pathIsPreEncoded = false;

    HANDLE bodyFile = INVALID_HANDLE_VALUE;
    uint64_t bodyOffset = 0;
    DWORD bodyLength = 0;

    // A literal request body (already-encoded bytes, e.g. an application/x-www-form-urlencoded form).
    // Used only when bodyFile == INVALID_HANDLE_VALUE; a request never carries both, and the caller
    // is refused at embed time if it configures both.
    std::string bodyData;
};

struct HttpResponse
{
    uint32_t statusCode = 0;
    Guard::WinHttpHandle request;
};

class HttpClient
{
public:
    HttpClient() = default;

    HttpClient(HttpClient&&) noexcept = default;
    HttpClient& operator=(HttpClient&&) noexcept = default;
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    static Result<void> Open(const HttpClientConfig& config, HttpClient& client);
    Result<void> Send(const HttpRequest& request, HttpResponse& response);
    void Close();

    // Whether a proxy handles the connection to the upload host (see IsProxyInUse). Proxy credentials are only ever
    // offered when it does.
    bool ProxyInUse() const { return m_proxyInUse; }

private:
    Guard::WinHttpHandle m_session;
    Guard::WinHttpHandle m_connect;
    bool m_secure = true;
    bool m_insecure = false;
    bool m_followRedirects = false;
    HttpAuthScheme m_serverScheme = HttpAuthScheme::Anonymous;
    std::wstring m_serverUser;
    std::wstring m_serverPassword;
    // The server-leg authentication scheme as an object, or null when the configuration presents no
    // server credential. Built once in Open() and applied to every request handle in Send().
    std::unique_ptr<Authenticator> m_serverAuth;
    std::vector<std::vector<uint8_t>> m_pinnedSpki;
    ProxySettings m_proxy;
    bool m_proxyInUse = false;

    // Kept after Open() only to name the enforced versions (the locked one under tlsLockHighest) when a
    // handshake fails: WinHTTP reports a server that cannot reach them as an opaque channel error.
    uint8_t m_tlsVersions = 0;
};

// Response accessors for <response> captures and error diagnostics.
std::optional<std::wstring> GetResponseHeader(const HttpResponse& response, const std::wstring& name);

// The response body, read bounded. The no-argument form keeps the generic default; a capture read
// passes kMaxCaptureBodyBytes. std::nullopt means the read failed or the cap tripped: the body is
// incomplete and must not be parsed.
std::optional<std::string> ReadResponseBody(const HttpResponse& response);
std::optional<std::string> ReadResponseBody(const HttpResponse& response, size_t maxBytes);

void LogResponseDiagnostics(const HttpResponse& response);

// A 401/407 response described for a log line: status, who challenged (server or proxy) and the schemes offered.
// No header value, no credential.
std::wstring DescribeAuthChallenge(const HttpResponse& response);

// Read and discard what is left of the response body, up to a bound, so its connection can serve the next request.
void DrainResponseBody(const HttpResponse& response);

// Exposed pure helpers (no I/O)
Result<HostPort> SplitHostPort(const std::wstring& server, INTERNET_PORT defaultPort);
std::wstring RedactResponseHeaders(const std::wstring& raw);
Result<std::wstring> MakeBasicAuthHeader(const std::wstring& user, const std::wstring& password);

bool SpkiMatchesAnyPin(const std::vector<uint8_t>& live, const std::vector<std::vector<uint8_t>>& pins);

// SHA-256 of the public key (SubjectPublicKeyInfo) of one 'server_cert_pin' entry: an X.509 certificate as PEM
// or base64 DER. Anything else - a bare public key, another PEM type, damaged base64 - is an error.
Result<std::vector<uint8_t>> CertBlobToSpkiSha256(const std::wstring& blob);

// Every 'server_cert_pin' entry as a public-key hash, or an error if ANY entry does not parse. Skipping one
// would hide a mistyped rotation pin until the day the server key rotates onto it.
Result<std::vector<std::vector<uint8_t>>> ParseServerCertPins(const std::vector<std::wstring>& blobs);

// The WINHTTP_OPTION_SECURITY_FLAGS for a request. 'insecure' drops certificate validation. A pin only relaxes the
// unknown-CA check, so that a self-signed pinned server connects; name, dates and usage are still validated and
// the public key is compared once the handshake is done.
DWORD CertificateSecurityFlags(bool insecure, bool pinned);

// A check run on the connection after the handshake and before the request is sent. "Never ran" and "passed"
// are distinct on purpose: a pinned request relaxes CA validation, so a send the check never saw accepted any
// certificate, and only positive proof may let its response through.
enum class HttpConnectionCheck
{
    NotRun,  // no proof the check ran for the request as it was last sent
    Passed,  // the check ran and the connection satisfied it
    Rejected,  // the check ran, refused the connection, and closed the handle
};

// A request that is sent again - redirected, or resent after an auth challenge - may reach another connection,
// so a passed check proves nothing about it and must run again. A rejection stays: its handle is closed.
HttpConnectionCheck RequireNewProof(HttpConnectionCheck check);

enum class HttpSendOutcome
{
    Accept,
    Rejected,  // a check refused the connection and closed the handle
    SendFailed,  // the send itself failed; its own error is reported
    PinNotProven,  // pinned, but the key comparison never ran for the request as last sent
    ProtocolNotProven,  // TLS 1.2 required per connection, but the negotiated protocol was never read
};

HttpSendOutcome DecideSendOutcome(
    bool pinned,
    HttpConnectionCheck pinCheck,
    bool requireTls12,
    HttpConnectionCheck protocolCheck,
    bool sendFailed);

// HttpTlsVersion::kTls* bits to the WINHTTP_FLAG_SECURE_PROTOCOL_* mask set as WINHTTP_OPTION_SECURE_PROTOCOLS.
// A 0 result means "leave the system TLS policy alone", which only an explicit https_security="os-default"
// may lead to.
DWORD ToWinHttpProtocols(uint8_t versions);

// True when a connection must prove the protocol it negotiated: HTTPS with no enforced version set - the
// https_security="os-default" case, where the system policy may go below TLS 1.2 - and no 'insecure'.
bool RequiresNegotiatedTls12(bool secure, uint8_t tlsVersions, bool insecure);

// Whether a 401/407 challenge is worth sending the request, body included, again. A 401 completes on the resend only
// with a challenge-response server scheme (NTLM, Kerberos, Negotiate) whose credentials are preset; Basic is already
// sent up front, so a 401 there is a refusal, and Anonymous has nothing to answer with. A 407 is resent when proxy
// credentials are set, Basic included: WinHTTP answers the proxy on that leg. Resending on any challenge would
// upload the whole body three times only to meet the same refusal. A 407 on a connection that goes through no proxy
// ('proxyInUse' false) is never answered: it did not come from a proxy the configuration named.
bool ShouldResendOnChallenge(
    uint32_t statusCode,
    HttpAuthScheme serverScheme,
    HttpAuthScheme proxyScheme,
    bool proxyHasCredentials,
    bool proxyInUse);

// The WINHTTP_AUTH_SCHEME_* bits of a challenge as names, for a log line ("Basic, NTLM", "none").
std::wstring AuthSchemesText(DWORD schemes);

// The local checks https_security="os-max" runs on a candidate TLS version (one HttpTlsVersion::kTls* bit).
// HttpClient::Open reads the SChannel registry policy and sets WINHTTP_OPTION_SECURE_PROTOCOLS on its session;
// unit tests substitute both.
struct TlsLockProbes
{
    std::function<bool(uint8_t version)> isDisabledByPolicy;
    std::function<std::error_code(uint8_t version)> enableOnly;  // enables that version alone
};

// Lock on the highest of 'candidates' the probes find available, enabling no other version, and fail when none
// is. Tries each candidate once, highest first.
Result<uint8_t> LockHighestTlsVersion(uint8_t candidates, const TlsLockProbes& probes);

// True only for the SChannel client values of TLS 1.2 and TLS 1.3 (SecPkgContext_ConnectionInfo::dwProtocol).
// An older protocol, a server-side value, a combination or a value this build does not know proves nothing.
bool IsTls12OrNewerProtocol(DWORD protocol);

}  // namespace Orc
