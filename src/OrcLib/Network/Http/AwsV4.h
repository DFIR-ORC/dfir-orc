//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
#pragma once

#include "Utils/Result.h"

#include <windows.h>
#include <string>
#include <vector>

namespace Orc {

// What a SigV4 signature is computed from, beside the request itself. 'sessionToken' is empty unless
// the deployment uses STS temporary credentials.
struct AwsCredentials
{
    std::wstring accessKey;
    std::wstring secretKey;
    std::wstring sessionToken;
    std::wstring region;
    std::wstring service = L"s3";
};

// Percent-encode per RFC 3986 with the AWS unreserved set (A-Za-z0-9-_.~), UPPERCASE hex, non-ASCII as
// UTF-8 bytes. 'keepSlash' leaves '/' alone, which is what a canonical URI wants: S3, unlike every other
// AWS service, does not double-encode the path.
std::wstring AwsUriEncode(const std::wstring& value, bool keepSlash);

// A query string ("?a=1&b=2", leading '?' optional) as a canonical query: every name and value encoded,
// sorted by encoded name then encoded value, a valueless parameter rendered as "name=".
std::wstring AwsCanonicalQueryString(const std::wstring& query);

// The headers a signed request must carry: x-amz-date, x-amz-content-sha256, x-amz-security-token when
// the credentials have one, and Authorization. Each is a full "Name: value" line, ready for
// HttpRequest::headers.
//
// 'encodedPath' must ALREADY be AwsUriEncode(path, true): the caller encodes it once and sends exactly
// that, so the signed bytes and the wire bytes are the same object.
// 'canonicalQuery' is the same bargain for the query: ALREADY AwsCanonicalQueryString(query), with no
// leading '?', and signed verbatim. Canonicalising it a second time here would re-encode the '%' of
// every escape ("tag=a%20b" -> "tag=a%2520b") and sign a query the request never carries.
// 'host' is the Host header WinHTTP will synthesise, including the port when it is not the scheme
// default (measured: "127.0.0.1:8099" on 8099, "127.0.0.1" on 80).
// 'headers' are the step's own header lines; all of them are signed except Content-Length, which
// WinHTTP generates.
// 'utcNow' is injected rather than read from the clock, so the computation stays pure and testable.
Result<std::vector<std::wstring>> MakeAwsV4Headers(
    const std::wstring& method,
    const std::wstring& encodedPath,
    const std::wstring& canonicalQuery,
    const std::wstring& host,
    const std::vector<std::wstring>& headers,
    const AwsCredentials& credentials,
    const SYSTEMTIME& utcNow);

}  // namespace Orc
