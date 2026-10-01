//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
#include "stdafx.h"

#include "AwsV4.h"

#include "BinaryBuffer.h"
#include "Crypto/HmacSha256.h"
#include "CryptoHashStream.h"
#include "Log/Log.h"
#include "Text/Fmt/std_error_code.h"
#include "Text/Iconv.h"
#include "Utils/BufferView.h"

#include <fmt/format.h>

#include <algorithm>
#include <cwctype>
#include <array>
#include <map>
#include <memory>
#include <string_view>
#include <utility>

namespace Orc {

namespace {

// S3 accepts this literal in place of the payload digest, which is what lets the body stay a stream: the
// alternative is hashing the whole archive before sending it.
constexpr std::wstring_view kUnsignedPayload = L"UNSIGNED-PAYLOAD";

constexpr std::wstring_view kTerminator = L"aws4_request";

// SigV4 renders the digests and the signature in LOWERCASE hex. Every shared hex helper in OrcLib is
// uppercase, and a shared table could be changed for unrelated reasons and silently invalidate every
// signature, so this one stays local.
std::string LowerHex(const uint8_t* data, size_t size)
{
    static const char kDigits[] = "0123456789abcdef";

    std::string hex;
    hex.reserve(size * 2);
    for (size_t i = 0; i < size; ++i)
    {
        hex.push_back(kDigits[(data[i] >> 4) & 0x0F]);
        hex.push_back(kDigits[data[i] & 0x0F]);
    }

    return hex;
}

BufferView Utf8View(const std::string& utf8)
{
    return BufferView(reinterpret_cast<const uint8_t*>(utf8.data()), utf8.size());
}

Result<std::array<uint8_t, 32>> Sha256(const std::string& utf8)
{
    auto hash = std::make_shared<CryptoHashStream>();
    if (HRESULT hr = hash->OpenToWrite(CryptoHashStream::Algorithm::SHA256, nullptr); FAILED(hr))
    {
        Log::Debug(L"AwsV4: hash OpenToWrite failed [{}]", SystemError(hr));
        return SystemError(hr);
    }

    ULONGLONG written = 0;
    if (HRESULT hr = hash->Write(const_cast<char*>(utf8.data()), utf8.size(), &written); FAILED(hr))
    {
        Log::Debug(L"AwsV4: hash Write failed [{}]", SystemError(hr));
        return SystemError(hr);
    }

    CBinaryBuffer digest;
    const HRESULT hr = hash->GetHash(CryptoHashStream::Algorithm::SHA256, digest);
    if (FAILED(hr) || digest.GetCount() != 32)
    {
        auto ec = FAILED(hr) ? SystemError(hr) : SystemError(E_FAIL);
        Log::Debug(L"AwsV4: GetHash failed or returned {} bytes [{}]", digest.GetCount(), ec);
        return ec;
    }

    std::array<uint8_t, 32> sha256 = {};
    std::copy(digest.GetData(), digest.GetData() + sha256.size(), sha256.begin());
    return sha256;
}

std::wstring FormatAmzDate(const SYSTEMTIME& utc)
{
    return fmt::format(
        L"{:04}{:02}{:02}T{:02}{:02}{:02}Z", utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
}

std::wstring FormatDateStamp(const SYSTEMTIME& utc)
{
    return fmt::format(L"{:04}{:02}{:02}", utc.wYear, utc.wMonth, utc.wDay);
}

std::wstring ToLower(std::wstring value)
{
    std::transform(
        value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
    return value;
}

// Trim both ends and collapse every internal run of spaces and tabs into a single space, which is what
// SigV4 calls "Trimall" on a header value.
std::wstring TrimAll(std::wstring_view value)
{
    std::wstring trimmed;
    bool pendingSpace = false;
    for (wchar_t c : value)
    {
        if (c == L' ' || c == L'\t')
        {
            pendingSpace = !trimmed.empty();
            continue;
        }

        if (pendingSpace)
        {
            trimmed.push_back(L' ');
            pendingSpace = false;
        }

        trimmed.push_back(c);
    }

    return trimmed;
}

// A "Name: value" line split at the first ':'. The name is returned as written; the caller lowercases it.
// A line without a ':' yields an empty name, which the caller drops.
std::pair<std::wstring, std::wstring> SplitHeader(const std::wstring& line)
{
    const size_t colon = line.find(L':');
    if (colon == std::wstring::npos)
    {
        return {std::wstring(), std::wstring()};
    }

    return {TrimAll(std::wstring_view(line).substr(0, colon)), TrimAll(std::wstring_view(line).substr(colon + 1))};
}

// The names the signer computes and emits itself.
bool IsSignerOwnedHeaderName(const std::wstring& name)
{
    return name == L"host" || name == L"x-amz-date" || name == L"x-amz-content-sha256"
        || name == L"x-amz-security-token";
}

// The headers that take part in the signature, keyed by lowercased name so the map sorts them the way the
// canonical request wants.
Result<std::map<std::wstring, std::wstring>> BuildSignedHeaderMap(
    const std::wstring& host,
    const std::wstring& amzDate,
    const std::vector<std::wstring>& headers,
    const AwsCredentials& credentials)
{
    std::map<std::wstring, std::wstring> canonical;
    canonical[L"host"] = TrimAll(host);
    canonical[L"x-amz-content-sha256"] = std::wstring(kUnsignedPayload);
    canonical[L"x-amz-date"] = amzDate;

    if (!credentials.sessionToken.empty())
    {
        canonical[L"x-amz-security-token"] = TrimAll(credentials.sessionToken);
    }

    for (const std::wstring& line : headers)
    {
        const std::pair<std::wstring, std::wstring> header = SplitHeader(line);
        if (header.first.empty())
        {
            continue;
        }

        // Content-Length is generated by WinHTTP, so its exact rendering would be a guess; Authorization is
        // what we are about to produce.
        const std::wstring name = ToLower(header.first);
        if (name == L"content-length" || name == L"authorization")
        {
            continue;
        }

        // A step header repeating one of the signer's own names is refused rather than merged: the joined
        // value would depend on where the transport places the signer's line relative to the step's, which
        // the signer does not control, so the signature would be a guess. It can never be a legitimate
        // configuration either - three of these names are computed here and 'host' comes from the upload
        // address.
        if (IsSignerOwnedHeaderName(name))
        {
            Log::Debug(L"AwsV4: refusing a step header that repeats the signer's own '{}'", name);
            return SystemError(E_INVALIDARG);
        }

        // SigV4 canonicalises a repeated header name as ONE entry whose values are joined with ',' in the
        // order they appear in the request, and HttpClient::Send adds every header but Authorization with a
        // plain ADD from this same vector, so both lines reach the wire in this order. Keeping only the last
        // value signed something other than what is sent: SignatureDoesNotMatch, a 403 naming nothing.
        const std::map<std::wstring, std::wstring>::iterator existing = canonical.find(name);
        if (existing == canonical.end())
        {
            canonical[name] = header.second;
        }
        else
        {
            existing->second.append(L",").append(header.second);
        }
    }

    return canonical;
}

std::wstring CanonicalHeaders(const std::map<std::wstring, std::wstring>& headers)
{
    std::wstring canonical;
    for (const std::pair<const std::wstring, std::wstring>& header : headers)
    {
        canonical.append(header.first).append(L":").append(header.second).append(L"\n");
    }

    return canonical;
}

std::wstring SignedHeaderList(const std::map<std::wstring, std::wstring>& headers)
{
    std::wstring list;
    for (const std::pair<const std::wstring, std::wstring>& header : headers)
    {
        if (!list.empty())
        {
            list.push_back(L';');
        }

        list.append(header.first);
    }

    return list;
}

// kSigning = HMAC(HMAC(HMAC(HMAC("AWS4" + secret, dateStamp), region), service), "aws4_request").
Result<std::array<uint8_t, 32>> DeriveSigningKey(const AwsCredentials& credentials, const std::wstring& dateStamp)
{
    const std::string secret = "AWS4" + ToUtf8(credentials.secretKey);
    const std::string date = ToUtf8(dateStamp);
    const std::string region = ToUtf8(credentials.region);
    const std::string service = ToUtf8(credentials.service);
    const std::string terminator = ToUtf8(kTerminator);

    const Result<std::array<uint8_t, 32>> dateKey = HmacSha256(Utf8View(secret), Utf8View(date));
    if (dateKey.has_error())
    {
        Log::Debug(L"AwsV4: the date key derivation failed [{}]", dateKey.error());
        return dateKey.error();
    }

    const Result<std::array<uint8_t, 32>> regionKey =
        HmacSha256(BufferView(dateKey->data(), dateKey->size()), Utf8View(region));
    if (regionKey.has_error())
    {
        Log::Debug(L"AwsV4: the region key derivation failed [{}]", regionKey.error());
        return regionKey.error();
    }

    const Result<std::array<uint8_t, 32>> serviceKey =
        HmacSha256(BufferView(regionKey->data(), regionKey->size()), Utf8View(service));
    if (serviceKey.has_error())
    {
        Log::Debug(L"AwsV4: the service key derivation failed [{}]", serviceKey.error());
        return serviceKey.error();
    }

    Result<std::array<uint8_t, 32>> signingKey =
        HmacSha256(BufferView(serviceKey->data(), serviceKey->size()), Utf8View(terminator));
    if (signingKey.has_error())
    {
        Log::Debug(L"AwsV4: the signing key derivation failed [{}]", signingKey.error());
        return signingKey.error();
    }

    return *signingKey;
}

bool IsUnreserved(uint8_t byte)
{
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '-'
        || byte == '_' || byte == '.' || byte == '~';
}

std::vector<std::wstring_view> Split(std::wstring_view text, wchar_t separator)
{
    std::vector<std::wstring_view> items;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i)
    {
        if (i == text.size() || text[i] == separator)
        {
            items.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }

    return items;
}

}  // namespace

std::wstring AwsUriEncode(const std::wstring& value, bool keepSlash)
{
    static const char kDigits[] = "0123456789ABCDEF";

    std::error_code ec;
    const std::string utf8 = ToUtf8(value, ec);
    if (ec)
    {
        // Returning the empty string cannot be mistaken for the input: it yields a signature that does not
        // match the request, which the server rejects, instead of signing something other than what is sent.
        Log::Debug(L"AwsV4: could not convert a value to UTF-8 [{}]", ec);
        return std::wstring();
    }

    std::wstring encoded;
    encoded.reserve(utf8.size());
    for (char c : utf8)
    {
        const uint8_t byte = static_cast<uint8_t>(c);
        if (IsUnreserved(byte) || (keepSlash && byte == '/'))
        {
            encoded.push_back(static_cast<wchar_t>(byte));
            continue;
        }

        encoded.push_back(L'%');
        encoded.push_back(static_cast<wchar_t>(kDigits[(byte >> 4) & 0x0F]));
        encoded.push_back(static_cast<wchar_t>(kDigits[byte & 0x0F]));
    }

    return encoded;
}

std::wstring AwsCanonicalQueryString(const std::wstring& query)
{
    std::wstring_view parameters(query);
    if (!parameters.empty() && parameters.front() == L'?')
    {
        parameters.remove_prefix(1);
    }

    if (parameters.empty())
    {
        return std::wstring();
    }

    std::vector<std::pair<std::wstring, std::wstring>> encoded;
    for (std::wstring_view parameter : Split(parameters, L'&'))
    {
        if (parameter.empty())
        {
            continue;
        }

        const size_t equals = parameter.find(L'=');
        const std::wstring_view name = equals == std::wstring_view::npos ? parameter : parameter.substr(0, equals);
        const std::wstring_view value =
            equals == std::wstring_view::npos ? std::wstring_view() : parameter.substr(equals + 1);

        encoded.emplace_back(AwsUriEncode(std::wstring(name), false), AwsUriEncode(std::wstring(value), false));
    }

    std::sort(encoded.begin(), encoded.end());

    std::wstring canonical;
    for (const std::pair<std::wstring, std::wstring>& parameter : encoded)
    {
        if (!canonical.empty())
        {
            canonical.push_back(L'&');
        }

        canonical.append(parameter.first).append(L"=").append(parameter.second);
    }

    return canonical;
}

Result<std::vector<std::wstring>> MakeAwsV4Headers(
    const std::wstring& method,
    const std::wstring& encodedPath,
    const std::wstring& canonicalQuery,
    const std::wstring& host,
    const std::vector<std::wstring>& headers,
    const AwsCredentials& credentials,
    const SYSTEMTIME& utcNow)
{
    if (credentials.accessKey.empty())
    {
        Log::Debug(L"AwsV4: the access key is empty");
        return SystemError(E_INVALIDARG);
    }

    if (credentials.secretKey.empty())
    {
        Log::Debug(L"AwsV4: the secret key is empty");
        return SystemError(E_INVALIDARG);
    }

    // The scope is part of what is signed: inventing a region or a service would produce a signature that
    // no server can reproduce, so refuse instead.
    if (credentials.region.empty())
    {
        Log::Debug(L"AwsV4: the region is empty");
        return SystemError(E_INVALIDARG);
    }

    if (credentials.service.empty())
    {
        Log::Debug(L"AwsV4: the service is empty");
        return SystemError(E_INVALIDARG);
    }

    const std::wstring amzDate = FormatAmzDate(utcNow);
    const std::wstring dateStamp = FormatDateStamp(utcNow);
    const Result<std::map<std::wstring, std::wstring>> signedHeaderMap =
        BuildSignedHeaderMap(host, amzDate, headers, credentials);
    if (signedHeaderMap.has_error())
    {
        return signedHeaderMap.error();  // BuildSignedHeaderMap logged which header it refused
    }

    const std::wstring signedHeaders = SignedHeaderList(*signedHeaderMap);

    // CanonicalHeaders() already ends every entry with '\n', so the '\n' before the signed header list is
    // the empty line SigV4 requires between the two blocks.
    const std::wstring canonicalRequest = fmt::format(
        L"{}\n{}\n{}\n{}\n{}\n{}",
        method,
        encodedPath,
        canonicalQuery,
        CanonicalHeaders(*signedHeaderMap),
        signedHeaders,
        kUnsignedPayload);

    // Hash the UTF-8 bytes: the digest is over what goes on the wire, not over UTF-16.
    const std::string canonicalRequestUtf8 = ToUtf8(canonicalRequest);
    const Result<std::array<uint8_t, 32>> canonicalRequestHash = Sha256(canonicalRequestUtf8);
    if (canonicalRequestHash.has_error())
    {
        Log::Debug(L"AwsV4: could not hash the canonical request [{}]", canonicalRequestHash.error());
        return canonicalRequestHash.error();
    }

    const std::wstring credentialScope =
        fmt::format(L"{}/{}/{}/{}", dateStamp, credentials.region, credentials.service, kTerminator);
    const std::wstring stringToSign = fmt::format(
        L"AWS4-HMAC-SHA256\n{}\n{}\n{}",
        amzDate,
        credentialScope,
        ToUtf16(LowerHex(canonicalRequestHash->data(), canonicalRequestHash->size())));

    const Result<std::array<uint8_t, 32>> signingKey = DeriveSigningKey(credentials, dateStamp);
    if (signingKey.has_error())
    {
        return signingKey.error();  // DeriveSigningKey logged which step failed
    }

    const std::string stringToSignUtf8 = ToUtf8(stringToSign);
    const Result<std::array<uint8_t, 32>> signature =
        HmacSha256(BufferView(signingKey->data(), signingKey->size()), Utf8View(stringToSignUtf8));
    if (signature.has_error())
    {
        Log::Debug(L"AwsV4: could not sign the string to sign [{}]", signature.error());
        return signature.error();
    }

    std::vector<std::wstring> signedRequestHeaders;
    signedRequestHeaders.push_back(fmt::format(L"x-amz-date: {}", amzDate));
    signedRequestHeaders.push_back(fmt::format(L"x-amz-content-sha256: {}", kUnsignedPayload));
    if (!credentials.sessionToken.empty())
    {
        signedRequestHeaders.push_back(fmt::format(L"x-amz-security-token: {}", credentials.sessionToken));
    }

    signedRequestHeaders.push_back(
        fmt::format(
            L"Authorization: AWS4-HMAC-SHA256 Credential={}/{}, SignedHeaders={}, Signature={}",
            credentials.accessKey,
            credentialScope,
            signedHeaders,
            ToUtf16(LowerHex(signature->data(), signature->size()))));

    return signedRequestHeaders;
}

}  // namespace Orc
