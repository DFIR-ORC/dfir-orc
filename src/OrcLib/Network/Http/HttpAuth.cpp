//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "stdafx.h"

#include "HttpAuth.h"

// No Winhttp.lib here on purpose: only the WINHTTP_AUTH_SCHEME_* constants are used, no API is called.

using namespace Orc;

bool Orc::IsIntegratedAuthScheme(HttpAuthScheme scheme)
{
    return scheme == HttpAuthScheme::NTLM || scheme == HttpAuthScheme::Kerberos || scheme == HttpAuthScheme::Negotiate;
}

DWORD Orc::WinHttpAuthScheme(HttpAuthScheme scheme, bool allowBasic)
{
    switch (scheme)
    {
        case HttpAuthScheme::Basic:
            return allowBasic ? WINHTTP_AUTH_SCHEME_BASIC : 0;
        case HttpAuthScheme::NTLM:
            return WINHTTP_AUTH_SCHEME_NTLM;
        case HttpAuthScheme::Kerberos:
        case HttpAuthScheme::Negotiate:
            return WINHTTP_AUTH_SCHEME_NEGOTIATE;
        case HttpAuthScheme::AWSv4:
            // AWSv4 signs the request with an explicit Authorization header (see AwsV4.h); it is never
            // handed to WinHttpSetCredentials, so it has no WINHTTP_AUTH_SCHEME_* value.
            return 0;
        default:
            return 0;
    }
}
