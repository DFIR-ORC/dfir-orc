//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "stdafx.h"

#include "HttpProxy.h"

#include "Log/Log.h"
#include "Text/Fmt/std_error_code.h"
#include "Utils/Guard.h"
#include "Utils/Result.h"

#include <algorithm>
#include <cwctype>
#include <string_view>

#pragma comment(lib, "Winhttp.lib")

using namespace Orc;

namespace {

std::wstring ToLower(std::wstring_view value)
{
    std::wstring lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return towlower(c); });
    return lower;
}

// '*' matches any run of characters, every other character itself. Both sides are already lowercase.
bool WildcardMatch(std::wstring_view pattern, std::wstring_view text)
{
    size_t p = 0, t = 0;
    size_t starPattern = std::wstring_view::npos, starText = 0;
    while (t < text.size())
    {
        if (p < pattern.size() && pattern[p] == L'*')
        {
            starPattern = p++;
            starText = t;
        }
        else if (p < pattern.size() && pattern[p] == text[t])
        {
            ++p;
            ++t;
        }
        else if (starPattern != std::wstring_view::npos)
        {
            p = starPattern + 1;
            t = ++starText;
        }
        else
        {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == L'*')
    {
        ++p;
    }

    return p == pattern.size();
}

// The URL an auto-configuration script is asked about. Only its scheme, host and port matter: WinHTTP
// resolves a proxy per origin, and the request path is not part of that decision.
std::wstring ProxyTargetUrl(bool secure, const std::wstring& host, INTERNET_PORT port)
{
    return (secure ? L"https://" : L"http://") + host + L":" + std::to_wstring(port) + L"/";
}

// The host part of one bypass entry: no scheme, no port, no IPv6 brackets.
std::wstring_view BypassEntryHost(std::wstring_view entry)
{
    if (const size_t scheme = entry.find(L"://"); scheme != std::wstring_view::npos)
    {
        entry.remove_prefix(scheme + 3);
    }

    if (!entry.empty() && entry.front() == L'[')
    {
        const size_t close = entry.find(L']');
        return close == std::wstring_view::npos ? entry.substr(1) : entry.substr(1, close - 1);
    }

    // A single ':' separates a port; several are a bare IPv6 address.
    const size_t colon = entry.find(L':');
    if (colon != std::wstring_view::npos && entry.find(L':', colon + 1) == std::wstring_view::npos)
    {
        entry = entry.substr(0, colon);
    }

    return entry;
}

}  // namespace

bool Orc::ProxyBypassMayMatch(const std::wstring& bypass, const std::wstring& host)
{
    const std::wstring lowerHost = ToLower(host);
    std::wstring_view bareHost = lowerHost;
    if (bareHost.size() >= 2 && bareHost.front() == L'[' && bareHost.back() == L']')
    {
        bareHost = bareHost.substr(1, bareHost.size() - 2);
    }

    const std::wstring lowerBypass = ToLower(bypass);
    constexpr std::wstring_view kSeparators = L"; \t\r\n";
    size_t start = 0;
    while (start < lowerBypass.size())
    {
        size_t end = lowerBypass.find_first_of(kSeparators.data(), start, kSeparators.size());
        if (end == std::wstring::npos)
        {
            end = lowerBypass.size();
        }

        const std::wstring_view entry = std::wstring_view(lowerBypass).substr(start, end - start);
        start = end + 1;

        if (entry.empty())
        {
            continue;
        }

        if (entry == L"<local>")
        {
            if (bareHost.find(L'.') == std::wstring_view::npos)
            {
                return true;
            }

            continue;
        }

        if (entry.front() == L'<')
        {
            continue;  // another directive, such as <-loopback>: it names no host
        }

        if (WildcardMatch(BypassEntryHost(entry), bareHost))
        {
            return true;
        }
    }

    return false;
}

bool Orc::ProxyAppliesToHost(const std::wstring& proxyServer, const std::wstring& bypass, const std::wstring& host)
{
    return !proxyServer.empty() && !ProxyBypassMayMatch(bypass, host);
}

bool Orc::IsProxyInUse(const ProxySettings& settings, const std::wstring& host)
{
    switch (settings.mode)
    {
        case ProxyMode::Named:
        case ProxyMode::Wpad:
        case ProxyMode::Pac:
            // ResolveProxyForUrl leaves 'server' empty when the script chose DIRECT.
            return ProxyAppliesToHost(settings.server, settings.bypass, host);
        case ProxyMode::System: {
            WINHTTP_PROXY_INFO info = {};
            if (!WinHttpGetDefaultProxyConfiguration(&info))
            {
                Log::Debug(
                    L"Failed WinHttpGetDefaultProxyConfiguration; counting the machine-wide proxy as none [{}]",
                    LastWin32Error());
                return false;
            }

            const Guard::HGlobal<wchar_t> proxy(info.lpszProxy);
            const Guard::HGlobal<wchar_t> proxyBypass(info.lpszProxyBypass);
            if (info.dwAccessType != WINHTTP_ACCESS_TYPE_NAMED_PROXY || !proxy)
            {
                return false;
            }

            return ProxyAppliesToHost(proxy.get(), proxyBypass ? proxyBypass.get() : L"", host);
        }
        case ProxyMode::Direct:
        case ProxyMode::Invalid:
        default:
            return false;
    }
}

Result<void> Orc::CheckProxySettings(const ProxySettings& settings)
{
    // Fail closed: every other mode would send the upload somewhere the operator did not ask for, DIRECT
    // included. Only the run that has to reach the proxy can tell that the variable is missing.
    if (settings.mode == ProxyMode::Invalid)
    {
        Log::Error(
            L"proxy=\"{}\" was never expanded: that environment variable is not set on this host, so no "
            L"proxy can be resolved and the upload is refused. Set the variable, or name the proxy in the "
            L"configuration: \"system\" (machine-wide), \"direct\"/\"none\" (no proxy), \"wpad\"/\"auto\" "
            L"(auto-detect), \"pac:<url>\", or \"host:port\". Leaving 'proxy' out entirely also follows the "
            L"machine-wide configuration.",
            settings.server);
        return SystemError(E_INVALIDARG);
    }

    return Orc::Success<void>();
}

Guard::WinHttpHandle Orc::OpenProxySession(LPCWSTR userAgent, const ProxySettings& settings)
{
    switch (settings.mode)
    {
        case ProxyMode::Named:
            return Guard::WinHttpHandle(WinHttpOpen(
                userAgent,
                WINHTTP_ACCESS_TYPE_NAMED_PROXY,
                settings.server.c_str(),
                settings.bypass.empty() ? WINHTTP_NO_PROXY_BYPASS : settings.bypass.c_str(),
                0));
        case ProxyMode::System:
            return Guard::WinHttpHandle(WinHttpOpen(
                userAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        case ProxyMode::Invalid:
            // Unreachable in practice: a caller checks CheckProxySettings before opening a session. Kept
            // explicit so that a future caller which forgets that guard gets no session at all, instead of
            // the silent DIRECT the default branch would hand it.
            Log::Debug(L"Refusing to open a session for a 'proxy' value that could not be interpreted");
            return Guard::WinHttpHandle();
        case ProxyMode::Wpad:
        case ProxyMode::Pac:
        case ProxyMode::Direct:
        default:
            return Guard::WinHttpHandle(WinHttpOpen(
                userAgent, WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    }
}

namespace {

// Ask the WPAD/PAC configuration which proxy serves 'targetUrl' and record it in 'settings'. A no-op for
// every other mode, which names its proxy (or none) up front.
Result<void> ResolveProxyForUrl(HINTERNET session, const std::wstring& targetUrl, ProxySettings& settings)
{
    if (settings.mode != ProxyMode::Wpad && settings.mode != ProxyMode::Pac)
    {
        return Orc::Success<void>();
    }

    WINHTTP_AUTOPROXY_OPTIONS options = {};
    if (settings.mode == ProxyMode::Pac)
    {
        options.dwFlags |= WINHTTP_AUTOPROXY_CONFIG_URL;
        options.lpszAutoConfigUrl = settings.pacUrl.c_str();
    }
    else
    {
        options.dwFlags |= WINHTTP_AUTOPROXY_AUTO_DETECT;
        options.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
    }

    // Never relay SYSTEM credentials while fetching the PAC itself.
    options.fAutoLogonIfChallenged = FALSE;

    // Synchronous: WPAD does DHCP/DNS discovery and PAC downloads+runs a script. On a network where the
    // source is unreachable this can block for the WinHTTP autoproxy timeout (up to ~tens of seconds)
    // before falling through to DIRECT.
    Log::Info(
        L"Resolving proxy via {} for '{}' (may pause up to ~30s if the source is unreachable)",
        settings.mode == ProxyMode::Pac ? L"PAC" : L"WPAD",
        targetUrl);

    const LPCWSTR kind = settings.mode == ProxyMode::Pac ? L"PAC" : L"WPAD";

    WINHTTP_PROXY_INFO info = {};
    if (!WinHttpGetProxyForUrl(session, targetUrl.c_str(), &options, &info))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed WinHttpGetProxyForUrl: {} for '{}'; refusing to connect DIRECT [{}]", kind, targetUrl, ec);
        return ec;
    }

    const Guard::HGlobal<wchar_t> proxy(info.lpszProxy);
    const Guard::HGlobal<wchar_t> proxyBypass(info.lpszProxyBypass);

    if (proxy)
    {
        settings.server = proxy.get();
    }

    if (proxyBypass)
    {
        settings.bypass = proxyBypass.get();
    }

    if (settings.server.empty())
    {
        // The PAC/WPAD script ran and explicitly resolved to DIRECT (no proxy). That is a successful result -
        // the operator's own script chose it - so proceed, but warn since wpad/pac usually implies a proxy.
        Log::Warn(
            L"Proxy auto-config ({}) resolved no proxy for '{}'; the upload will connect DIRECT", kind, targetUrl);
        return Orc::Success<void>();
    }

    Log::Debug(L"Resolved proxy '{}' for '{}'", settings.server, targetUrl);
    return Orc::Success<void>();
}

// Bind the proxy a WPAD/PAC run resolved to the session. A no-op for every other mode: their proxy, when
// there is one, is already set on the session by OpenProxySession.
Result<void> ApplyProxy(HINTERNET session, const ProxySettings& settings)
{
    if ((settings.mode != ProxyMode::Wpad && settings.mode != ProxyMode::Pac) || settings.server.empty())
    {
        return Orc::Success<void>();
    }

    WINHTTP_PROXY_INFO info = {};
    info.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
    info.lpszProxy = const_cast<LPWSTR>(settings.server.c_str());
    info.lpszProxyBypass =
        settings.bypass.empty() ? WINHTTP_NO_PROXY_BYPASS : const_cast<LPWSTR>(settings.bypass.c_str());

    // Set on the session (before WinHttpConnect) so the connect handle and every request inherit it. This
    // sidesteps the documented ambiguity of WINHTTP_OPTION_PROXY on a request handle. Fail closed: if the
    // resolved proxy cannot be bound the request would connect DIRECT and bypass egress, so refuse instead.
    if (!WinHttpSetOption(session, WINHTTP_OPTION_PROXY, &info, sizeof(info)))
    {
        auto ec = LastWin32Error();
        Log::Debug(L"Failed to bind the resolved proxy to the session; refusing to connect DIRECT [{}]", ec);
        return ec;
    }

    return Orc::Success<void>();
}

}  // namespace

Result<void> Orc::BindProxyToSession(
    HINTERNET session,
    bool secure,
    const std::wstring& host,
    INTERNET_PORT port,
    ProxySettings& settings)
{
    const std::wstring targetUrl = ProxyTargetUrl(secure, host, port);
    if (auto result = ResolveProxyForUrl(session, targetUrl, settings); result.has_error())
    {
        return result;  // ResolveProxyForUrl logged the cause
    }

    return ApplyProxy(session, settings);  // ApplyProxy logged the cause of a failure
}

Result<void> Orc::ApplyProxyCredentials(HINTERNET request, const ProxySettings& settings)
{
    if (settings.user.empty())
    {
        return Orc::Success<void>();
    }

    const DWORD scheme = WinHttpAuthScheme(settings.scheme, /*allowBasic*/ true);
    if (scheme == 0)
    {
        Log::Debug(L"Proxy credentials set but 'proxy_authscheme' is not Basic/NTLM/Negotiate");
        return SystemError(E_FAIL);
    }

    if (!WinHttpSetCredentials(
            request, WINHTTP_AUTH_TARGET_PROXY, scheme, settings.user.c_str(), settings.password.c_str(), nullptr))
    {
        const auto ec = LastWin32Error();
        Log::Debug(L"Failed WinHttpSetCredentials (proxy) [{}]", ec);
        return ec;
    }

    Log::Debug(L"Proxy credentials set preemptively (scheme=0x{:x})", scheme);
    return Orc::Success<void>();
}
