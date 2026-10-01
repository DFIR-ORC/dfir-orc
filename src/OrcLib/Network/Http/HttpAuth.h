//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <windows.h>
#include <winhttp.h>

namespace Orc {

// How a request authenticates to a target. The same set applies to a server and to a proxy, which are
// two independent targets of one request (WINHTTP_AUTH_TARGET_SERVER / _PROXY).
enum class HttpAuthScheme : char
{
    Anonymous = 0,
    Basic,
    NTLM,
    Kerberos,
    Negotiate,
    AWSv4,
};

// True for a Windows-integrated (challenge-response) scheme: it only completes when the request is sent
// again to answer a challenge, unlike Basic which travels with the first request.
bool IsIntegratedAuthScheme(HttpAuthScheme scheme);

// The WINHTTP_AUTH_SCHEME_* value to hand WinHttpSetCredentials, or 0 when the scheme has none to offer.
// 'allowBasic' is false where Basic must travel as a preset Authorization header instead (see
// MakeBasicAuthHeader): WinHTTP would otherwise hold those credentials back until a 401.
DWORD WinHttpAuthScheme(HttpAuthScheme scheme, bool allowBasic);

}  // namespace Orc
