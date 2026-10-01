//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
#include "stdafx.h"

#include "Authenticator.h"

#include "AwsV4.h"
#include "HttpClient.h"
#include "Log/Log.h"
#include "Utils/Result.h"

#include <winhttp.h>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Orc {

namespace {

// SigV4 itself allows 15 minutes of skew. A day is generous enough to cover a host whose clock never got
// set, and tight enough that a server answering with an absurd date cannot push a signature far out of any
// plausible window.
constexpr int64_t kMaxClockOffsetSeconds = 24 * 60 * 60;

constexpr int64_t kFileTimeTicksPerSecond = 10000000LL;

// "Sun, 06 Nov 1994 08:49:37 GMT"
constexpr size_t kImfFixdateLength = 29;

// The month number (1-12) of a three-letter RFC 7231 abbreviation, or 0 when it names none.
WORD ParseImfMonth(const std::wstring& name)
{
    static const wchar_t* const kMonths[] = {
        L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};

    for (WORD index = 0; index < 12; ++index)
    {
        if (name == kMonths[index])
        {
            return static_cast<WORD>(index + 1);
        }
    }

    return 0;
}

// 'count' decimal digits of 'text' at 'offset', or nullopt when the range is out of bounds or any character
// in it is not a digit. wcstoul is avoided on purpose: it would accept signs and spaces this format forbids.
std::optional<WORD> ParseImfNumber(const std::wstring& text, size_t offset, size_t count)
{
    if (count > text.size() || offset > text.size() - count)
    {
        return std::nullopt;
    }

    WORD value = 0;
    for (size_t index = 0; index < count; ++index)
    {
        const wchar_t c = text[offset + index];
        if (c < L'0' || c > L'9')
        {
            return std::nullopt;
        }

        value = static_cast<WORD>(value * 10 + (c - L'0'));
    }

    return value;
}

// An RFC 7231 IMF-fixdate ("Sun, 06 Nov 1994 08:49:37 GMT") as a SYSTEMTIME. Only that fixed-width form is
// accepted: the two obsolete formats RFC 7231 still tolerates are not worth a parser here, and a header that
// does not match to the character is refused rather than guessed at.
std::optional<SYSTEMTIME> ParseImfFixdate(const std::wstring& httpDate)
{
    if (httpDate.size() != kImfFixdateLength || httpDate[3] != L',' || httpDate[4] != L' ' || httpDate[7] != L' '
        || httpDate[11] != L' ' || httpDate[16] != L' ' || httpDate[19] != L':' || httpDate[22] != L':'
        || httpDate.compare(25, 4, L" GMT") != 0)
    {
        return std::nullopt;
    }

    const WORD month = ParseImfMonth(httpDate.substr(8, 3));
    const std::optional<WORD> day = ParseImfNumber(httpDate, 5, 2);
    const std::optional<WORD> year = ParseImfNumber(httpDate, 12, 4);
    const std::optional<WORD> hour = ParseImfNumber(httpDate, 17, 2);
    const std::optional<WORD> minute = ParseImfNumber(httpDate, 20, 2);
    const std::optional<WORD> second = ParseImfNumber(httpDate, 23, 2);
    if (month == 0 || !day || !year || !hour || !minute || !second)
    {
        return std::nullopt;
    }

    SYSTEMTIME systemTime = {};
    systemTime.wYear = *year;
    systemTime.wMonth = month;
    systemTime.wDay = *day;
    systemTime.wHour = *hour;
    systemTime.wMinute = *minute;
    systemTime.wSecond = *second;

    // SystemTimeToFileTime validates the whole combination (field ranges, month length, leap years), so a
    // syntactically well-formed nonsense date such as "31 Feb" is refused here instead of becoming an offset.
    FILETIME probe = {};
    if (!SystemTimeToFileTime(&systemTime, &probe))
    {
        return std::nullopt;
    }

    return systemTime;
}

int64_t FileTimeToTicks(const FILETIME& fileTime)
{
    ULARGE_INTEGER value = {};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    return static_cast<int64_t>(value.QuadPart);
}

// Seconds to ADD to this host's clock to reach the server's, read from its Date header. False when the header
// does not parse: fail closed, no retry rather than a retry signed with a guessed timestamp.
bool ParseHttpDateOffset(const std::wstring& httpDate, int64_t& offsetSeconds)
{
    const std::optional<SYSTEMTIME> serverTime = ParseImfFixdate(httpDate);
    if (!serverTime)
    {
        return false;
    }

    FILETIME serverFileTime = {};
    if (!SystemTimeToFileTime(&*serverTime, &serverFileTime))
    {
        Log::Debug(L"SystemTimeToFileTime failed on the server Date [{}]", LastWin32Error());
        return false;
    }

    FILETIME localFileTime = {};
    GetSystemTimeAsFileTime(&localFileTime);

    offsetSeconds = (FileTimeToTicks(serverFileTime) - FileTimeToTicks(localFileTime)) / kFileTimeTicksPerSecond;
    return true;
}

// 'time' moved by 'seconds', through FILETIME so that the calendar arithmetic (month lengths, leap years) is
// the system's rather than ours. 'seconds' is bounded by kMaxClockOffsetSeconds, so the tick product cannot
// overflow.
Result<SYSTEMTIME> ShiftSystemTime(const SYSTEMTIME& time, int64_t seconds)
{
    FILETIME fileTime = {};
    if (!SystemTimeToFileTime(&time, &fileTime))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"SystemTimeToFileTime failed while applying the clock offset [{}]", ec);
        return ec;
    }

    const int64_t shiftedTicks = FileTimeToTicks(fileTime) + seconds * kFileTimeTicksPerSecond;
    if (shiftedTicks < 0)
    {
        Log::Debug(L"The clock offset moves the timestamp before the FILETIME epoch");
        return SystemError(E_INVALIDARG);
    }

    ULARGE_INTEGER ticks = {};
    ticks.QuadPart = static_cast<ULONGLONG>(shiftedTicks);

    FILETIME shiftedFileTime = {};
    shiftedFileTime.dwLowDateTime = ticks.LowPart;
    shiftedFileTime.dwHighDateTime = ticks.HighPart;

    SYSTEMTIME shifted = {};
    if (!FileTimeToSystemTime(&shiftedFileTime, &shifted))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"FileTimeToSystemTime failed while applying the clock offset [{}]", ec);
        return ec;
    }

    return shifted;
}

// The Host header WinHTTP will synthesise for 'serverName'. A SigV4 signature covers that header byte for
// byte, and WinHTTP omits the port when it is the scheme default (measured: "127.0.0.1" on 80,
// "127.0.0.1:8099" on 8099), so a default port written out in the configuration has to be dropped here -
// signing "host:443" while the wire carries "host" is a SignatureDoesNotMatch 403 that names nothing.
std::wstring SignedHostForServer(const std::wstring& serverName, bool secure)
{
    const INTERNET_PORT defaultPort = secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;

    const auto hostPort = SplitHostPort(serverName, defaultPort);
    if (hostPort.has_error())
    {
        // Sign the configured string as it stands: HttpClient::Open keeps a malformed port attached so the
        // connection fails visibly, and the signer must not diverge from the host the transport uses.
        Log::Debug(L"AwsV4Authenticator: could not split the server name; signing it verbatim [{}]", hostPort.error());
        return serverName;
    }

    if (hostPort->port == defaultPort)
    {
        return hostPort->host;
    }

    return hostPort->host + L":" + std::to_wstring(hostPort->port);
}

}  // namespace

Result<void> BasicAuthenticator::ApplyToHandle(HINTERNET hRequest)
{
    if (hRequest == nullptr)
    {
        Log::Debug(L"BasicAuthenticator: no request handle");
        return SystemError(E_INVALIDARG);
    }

    const auto authHeader = MakeBasicAuthHeader(m_user, m_password);
    if (authHeader.has_error())
    {
        // MakeBasicAuthHeader logged the specific crypto failure; forward its error_code.
        return authHeader.error();
    }

    if (!WinHttpAddRequestHeaders(hRequest, authHeader->c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed to add Authorization header [{}]", ec);
        return ec;
    }

    return Orc::Success<void>();
}

Result<void> IntegratedAuthenticator::ApplyToHandle(HINTERNET hRequest)
{
    if (hRequest == nullptr)
    {
        Log::Debug(L"IntegratedAuthenticator: no request handle");
        return SystemError(E_INVALIDARG);
    }

    const DWORD winScheme = WinHttpAuthScheme(m_scheme, /*allowBasic*/ false);
    if (winScheme == 0)
    {
        Log::Debug(L"IntegratedAuthenticator: no WinHTTP scheme for the configured value");
        return Orc::Success<void>();
    }

    if (!WinHttpSetCredentials(
            hRequest, WINHTTP_AUTH_TARGET_SERVER, winScheme, m_user.c_str(), m_password.c_str(), nullptr))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"WinHttpSetCredentials (server) failed [{}]", ec);
        return ec;
    }

    return Orc::Success<void>();
}

Result<void> AwsV4Authenticator::PrepareRequest(HttpRequest& request)
{
    // Split any query off the path: the canonical request treats them separately, and the query is
    // canonicalized (sorted, encoded per parameter) rather than percent-encoded as a whole.
    std::wstring path = request.path;
    std::wstring query;
    const size_t mark = path.find(L'?');
    if (mark != std::wstring::npos)
    {
        query = path.substr(mark);
        path = path.substr(0, mark);
    }

    // Encode once, sign exactly these bytes, and tell HttpClient not to escape them again.
    const std::wstring encodedPath = AwsUriEncode(path, /*keepSlash*/ true);
    const std::wstring canonicalQuery = AwsCanonicalQueryString(query);

    SYSTEMTIME utcNow = {};
    GetSystemTime(&utcNow);
    if (m_clockOffsetSeconds != 0)
    {
        auto shifted = ShiftSystemTime(utcNow, m_clockOffsetSeconds);
        if (shifted.has_error())
        {
            Log::Debug(L"AwsV4Authenticator: could not apply the recorded clock offset [{}]", shifted.error());
            return shifted.error();
        }

        utcNow = *shifted;
    }

    auto headers =
        MakeAwsV4Headers(request.method, encodedPath, canonicalQuery, m_host, request.headers, m_credentials, utcNow);
    if (headers.has_error())
    {
        // Includes the refusal of a step header colliding with a name the signer owns, which ToolEmbed
        // already rejects: report it rather than send an unsigned or doubly-signed request.
        Log::Debug(L"AwsV4Authenticator: could not sign the request [{}]", headers.error());
        return headers.error();
    }

    request.path = encodedPath + (canonicalQuery.empty() ? std::wstring() : (L"?" + canonicalQuery));
    request.pathIsPreEncoded = true;

    // Appended last so the Authorization line wins the ADD|REPLACE rule in Send's header loop against any
    // operator-supplied Authorization header.
    for (std::wstring& header : *headers)
    {
        request.headers.push_back(std::move(header));
    }

    return Orc::Success<void>();
}

Result<void> AwsV4Authenticator::ApplyToHandle(HINTERNET hRequest)
{
    // A signing scheme presents nothing to WinHTTP: its credential is already in the headers.
    UNREFERENCED_PARAMETER(hRequest);
    return Orc::Success<void>();
}

bool AwsV4Authenticator::OnRejected(const HttpResponse& response)
{
    if (response.statusCode != 403 || !m_allowServerClock || m_clockCorrected)
    {
        return false;
    }

    const std::optional<std::wstring> date = GetResponseHeader(response, L"Date");
    if (!date)
    {
        Log::Debug(L"AwsV4Authenticator: the rejection carries no Date header; not retrying");
        return false;
    }

    int64_t offsetSeconds = 0;
    if (!ParseHttpDateOffset(*date, offsetSeconds))
    {
        Log::Debug(L"AwsV4Authenticator: could not parse the server Date; not retrying");
        return false;
    }

    // Bounded: a server value feeds a request we build, so only a plausible skew is accepted and only one
    // retry is ever granted. The worst case is a second failing signature.
    if (offsetSeconds < -kMaxClockOffsetSeconds || offsetSeconds > kMaxClockOffsetSeconds)
    {
        Log::Debug(L"AwsV4Authenticator: the server clock offset is implausible; not retrying");
        return false;
    }

    m_clockOffsetSeconds = offsetSeconds;
    m_clockCorrected = true;
    Log::Warn(L"The host clock is {} seconds from the server's; re-signing once with the server time", offsetSeconds);
    return true;
}

std::unique_ptr<Authenticator> MakeServerAuthenticator(const HttpClientConfig& config)
{
    // Before the 'serverUser' guard on purpose: an AWSv4 credential lives in the aws fields, so the guard
    // would turn a signed upload into an anonymous one.
    if (config.serverScheme == HttpAuthScheme::AWSv4)
    {
        return std::make_unique<AwsV4Authenticator>(
            config.aws, SignedHostForServer(config.serverName, config.secure), config.awsAllowServerClock);
    }

    if (config.serverUser.empty())
    {
        return nullptr;
    }

    if (config.serverScheme == HttpAuthScheme::Basic)
    {
        return std::make_unique<BasicAuthenticator>(config.serverUser, config.serverPassword);
    }

    if (IsIntegratedAuthScheme(config.serverScheme))
    {
        return std::make_unique<IntegratedAuthenticator>(config.serverScheme, config.serverUser, config.serverPassword);
    }

    return nullptr;
}

}  // namespace Orc
