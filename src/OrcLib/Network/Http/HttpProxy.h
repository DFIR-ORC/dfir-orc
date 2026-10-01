//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "HttpAuth.h"
#include "Utils/Guard/WinHttp.h"
#include "Utils/Result.h"

#include <winhttp.h>
#include <string>

namespace Orc {

enum class ProxyMode
{
    Direct,
    System,
    Wpad,
    Pac,
    Named,
    // The 'proxy' value names no reachable proxy and no known mode, today only an environment reference
    // that was never expanded. It deliberately has no fallback: every other mode would send the upload
    // somewhere the operator did not ask for, DIRECT included.
    Invalid,
};

struct ProxySettings
{
    ProxyMode mode = ProxyMode::Direct;
    std::wstring server;
    std::wstring pacUrl;
    std::wstring bypass;
    std::wstring user;
    std::wstring password;
    HttpAuthScheme scheme = HttpAuthScheme::Anonymous;
};

// Refuse settings no session may be opened for: today only a 'proxy' value that was never expanded
// (ProxyMode::Invalid). Checked at connection time rather than at embed time on purpose - an unset
// environment variable is a property of the machine running the collection, not of the configuration,
// so a config that reads %ORC_UPLOAD_PROXY% must still embed on a build machine that does not define it.
Result<void> CheckProxySettings(const ProxySettings& settings);

Guard::WinHttpHandle OpenProxySession(LPCWSTR userAgent, const ProxySettings& settings);

// Resolve the proxy for the target host (WPAD/PAC only, a no-op for the other modes) and bind it to the
// SESSION, so the connect handle and every request inherit it. Call before WinHttpConnect. 'settings' is
// updated with what the auto-configuration resolved.
Result<void> BindProxyToSession(
    HINTERNET session,
    bool secure,
    const std::wstring& host,
    INTERNET_PORT port,
    ProxySettings& settings);

// True when an entry of the WinHTTP proxy bypass list 'bypass' may match 'host' (as SplitHostPort returns it). Entries
// are separated by ';' or whitespace, '*' matches any run of characters, "<local>" matches a name without a dot, and a
// scheme or a port on an entry is ignored. Errs towards a match on purpose: a wrong "bypassed" only withholds the proxy
// credentials, and the upload fails on the proxy's 407 with a warning saying why; a wrong "proxied" would hand them
// to whatever answers a direct connection.
bool ProxyBypassMayMatch(const std::wstring& bypass, const std::wstring& host);

// True when requests to 'host' go through 'proxyServer', given its bypass list. Pure.
bool ProxyAppliesToHost(const std::wstring& proxyServer, const std::wstring& bypass, const std::wstring& host);

// True when the session opened for 'settings', resolved by BindProxyToSession, sends requests to 'host' through a
// proxy. "system" reads the machine-wide WinHTTP configuration; a configuration that cannot be read counts as none.
bool IsProxyInUse(const ProxySettings& settings, const std::wstring& host);

Result<void> ApplyProxyCredentials(HINTERNET request, const ProxySettings& settings);

}  // namespace Orc
