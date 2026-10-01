//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
#pragma once

#include "AwsV4.h"
#include "HttpAuth.h"
#include "Utils/Result.h"

#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>

namespace Orc {

struct HttpClientConfig;
struct HttpRequest;
struct HttpResponse;

// One HTTP authentication scheme for the server leg of a request. An instance exists only because the
// configuration named that scheme, so a scheme's code is unreachable for an upload that did not ask
// for it - Anonymous is represented by no object at all.
class Authenticator
{
public:
    virtual ~Authenticator() = default;

    Authenticator(const Authenticator&) = delete;
    Authenticator& operator=(const Authenticator&) = delete;

    // Shape the request before its WinHTTP handle exists. The handle is constructed FROM the path, so a
    // scheme that must encode the path cannot do it in ApplyToHandle. Default: leave the request alone.
    virtual Result<void> PrepareRequest(HttpRequest& request)
    {
        UNREFERENCED_PARAMETER(request);
        return Orc::Success<void>();
    }

    // Applied to the opened request handle on every send attempt, resends included.
    virtual Result<void> ApplyToHandle(HINTERNET hRequest) = 0;

    // Whether a refused response is worth one more attempt with this scheme, after updating whatever
    // state that implies. Default: no.
    virtual bool OnRejected(const HttpResponse& response)
    {
        UNREFERENCED_PARAMETER(response);
        return false;
    }

    // A signature binds host, path and date, and WinHTTP offers no hook to re-sign in flight, so a
    // followed redirect would arrive signed for the previous host. A scheme that signs forbids them.
    virtual bool ForbidsRedirects() const { return false; }

protected:
    Authenticator() = default;
};

// Basic travels with the FIRST request as a preset Authorization header. WinHttpSetCredentials would
// hold it back until a 401, costing a round trip and, for a body request, a re-upload.
class BasicAuthenticator final : public Authenticator
{
public:
    BasicAuthenticator(std::wstring user, std::wstring password)
        : m_user(std::move(user))
        , m_password(std::move(password))
    {
    }

    Result<void> ApplyToHandle(HINTERNET hRequest) override;

private:
    std::wstring m_user;
    std::wstring m_password;
};

// NTLM / Kerberos / Negotiate are challenge-response: presetting the credentials does NOT make WinHTTP
// answer a 401 by itself, the request must be sent again, and only then does WinHTTP run the SSPI legs
// with what was preset here. That resend lives in HttpClient and is not this object's concern.
class IntegratedAuthenticator final : public Authenticator
{
public:
    IntegratedAuthenticator(HttpAuthScheme scheme, std::wstring user, std::wstring password)
        : m_scheme(scheme)
        , m_user(std::move(user))
        , m_password(std::move(password))
    {
    }

    Result<void> ApplyToHandle(HINTERNET hRequest) override;

private:
    HttpAuthScheme m_scheme;
    std::wstring m_user;
    std::wstring m_password;
};

// AWS SigV4. The credential and the clock offset live here; the computation itself is the pure
// Network/Http/AwsV4 functions. ApplyToHandle is deliberately a no-op: a signing scheme presents no
// credential to WinHTTP, it writes its own headers.
class AwsV4Authenticator final : public Authenticator
{
public:
    AwsV4Authenticator(AwsCredentials credentials, std::wstring host, bool allowServerClock)
        : m_credentials(std::move(credentials))
        , m_host(std::move(host))
        , m_allowServerClock(allowServerClock)
    {
    }

    Result<void> PrepareRequest(HttpRequest& request) override;
    Result<void> ApplyToHandle(HINTERNET hRequest) override;
    bool OnRejected(const HttpResponse& response) override;
    bool ForbidsRedirects() const override { return true; }

    // The exact bytes the signature's Host covers, normalised from the configured server name. Exposed so
    // a test can assert that normalisation without signing and re-deriving a timestamped signature.
    const std::wstring& SignedHost() const { return m_host; }

private:
    AwsCredentials m_credentials;
    std::wstring m_host;  // the Host header WinHTTP will synthesise, port included when not the default
    bool m_allowServerClock = false;
    int64_t m_clockOffsetSeconds = 0;
    bool m_clockCorrected = false;  // one retry only, for the whole session
};

// The authenticator for a configuration's server leg, or nullptr when it presents no credential:
// Anonymous, or a scheme whose 'serverUser' is empty. Returning nullptr for an empty user preserves
// the '!user.empty()' guard both branches of the former ApplyServerCredentials carried.
std::unique_ptr<Authenticator> MakeServerAuthenticator(const HttpClientConfig& config);

}  // namespace Orc
