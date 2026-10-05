//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "OutputSpec.h"

#include <algorithm>
#include <filesystem>
#include <regex>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/tokenizer.hpp>
#include <fmt/format.h>
#include <fmt/xchar.h>

#include "Configuration/ConfigItem.h"
#include "ParameterCheck.h"
#include "CaseInsensitive.h"
#include "SystemDetails.h"
#include "Archive.h"
#include "Utils/WinApi.h"
#include "Utils/String.h"
#include "Utils/Uri.h"
#include "Network/Http/HttpProxy.h"
#include "Network/Http/HttpClient.h"  // SplitHostPort, for embed-time server-port validation

using namespace std;
namespace fs = std::filesystem;

using namespace Orc;

namespace {

// True if 'v' is a URL whose path ends in ".pac" (case-insensitive) - the conventional PAC marker.
// Narrow on purpose: a named proxy is never "*.pac", so a host:port is never misread as a PAC; anything
// ambiguous falls through to a named proxy.
bool IsPacUrl(const std::wstring& v)
{
    const size_t scheme = v.find(L"://");
    if (scheme == std::wstring::npos)
    {
        return false;
    }

    const size_t pathStart = v.find(L'/', scheme + 3);
    if (pathStart == std::wstring::npos)
    {
        return false;  // authority-only URL => a named proxy, not a PAC
    }

    const size_t pathEnd = v.find_first_of(L"?#", pathStart);
    const std::wstring path =
        v.substr(pathStart, pathEnd == std::wstring::npos ? std::wstring::npos : pathEnd - pathStart);
    return boost::iends_with(path, L".pac");
}

// Map the config auth scheme onto the HTTP-layer scheme (the boundary that keeps the transport free of
// OutputSpec). An unknown/Anonymous value yields HttpAuthScheme::Anonymous (no credentials sent).
HttpAuthScheme ToHttpAuthScheme(OutputSpec::UploadAuthScheme scheme)
{
    switch (scheme)
    {
        case OutputSpec::UploadAuthScheme::Basic:
            return HttpAuthScheme::Basic;
        case OutputSpec::UploadAuthScheme::NTLM:
            return HttpAuthScheme::NTLM;
        case OutputSpec::UploadAuthScheme::Kerberos:
            return HttpAuthScheme::Kerberos;
        case OutputSpec::UploadAuthScheme::Negotiate:
            return HttpAuthScheme::Negotiate;
        case OutputSpec::UploadAuthScheme::AWSv4:
            return HttpAuthScheme::AWSv4;
        case OutputSpec::UploadAuthScheme::Anonymous:
        default:
            return HttpAuthScheme::Anonymous;
    }
}

// The header names MakeAwsV4Headers computes itself. SigV4's comma-joined signed-header list is order
// sensitive, so the signer already refuses (E_INVALIDARG) a request that also carries one of these as a
// literal header. Refusing here, at embed time, turns that runtime failure into a build-time one the
// operator can act on.
constexpr std::wstring_view kAwsSignerOwnedHeaders[] =
    {L"host"sv, L"x-amz-date"sv, L"x-amz-content-sha256"sv, L"x-amz-security-token"sv};

// A header line's name: the text before the first ':', trimmed. Template lines are always "Name: Value".
std::wstring HeaderName(const std::wstring& header)
{
    std::wstring name = header.substr(0, header.find(L':'));
    boost::trim(name);
    return name;
}

// Every header line configured for a REST upload: the upload-level headers plus every step's own. Used
// only by the AWSv4 header-collision checks below, where a match can come from either source.
std::vector<std::wstring> AllRestHeaders(const OutputSpec::Upload::RestConfig& rest)
{
    std::vector<std::wstring> all = rest.HttpHeaders;
    for (const auto& step : rest.RestSteps)
    {
        all.insert(all.end(), step.headers.begin(), step.headers.end());
    }

    return all;
}

bool HasValue(const ConfigItem& item, DWORD dwIndex)
{
    if (item.SubItems.size() <= dwIndex)
    {
        return false;
    }

    if (!item.SubItems[dwIndex])
    {
        return false;
    }

    return true;
}

// Expand %ENV% references in a config value, falling back to the raw value when expansion fails (e.g.
// the variable is unset). Mirrors how 'server'/'uri'/'path' are already handled. Applied only to the
// ADDRESS-bearing attributes (server/proxy/proxy_bypass) so they can come from %ORC_UPLOAD_SERVER% /
// %ORC_UPLOAD_PROXY%. Credentials (user/password/proxy_user/proxy_password) are deliberately NOT
// expanded, so a value that legitimately contains '%' stays literal.
std::wstring ExpandConfigValue(const std::wstring& value)
{
    std::error_code ec;
    std::wstring expanded = ExpandEnvironmentStringsApi(value.c_str(), ec);
    if (ec)
    {
        Log::Warn(L"Failed to expand environment variables in '{}' [{}]", value, ec);
        return value;
    }

    return expanded;
}

// Whether the '%' at @p pos starts a URI percent-escape: two hexadecimal digits follow it.
bool IsPercentEscapeAt(const std::wstring& value, size_t pos)
{
    const auto isHex = [](wchar_t c) {
        return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
    };

    return pos + 2 < value.size() && isHex(value[pos + 1]) && isHex(value[pos + 2]);
}

// Maps a TLS version token ("1.0".."1.3", optionally "tls"-prefixed) to its version bit; 0 if unknown.
uint8_t TlsVersionBit(std::wstring token)
{
    boost::trim(token);
    if (token.size() >= 3 && equalCaseInsensitive(token.substr(0, 3), L"tls"sv))
    {
        token.erase(0, 3);
        boost::trim(token);
    }

    if (token == L"1.0")
    {
        return OutputSpec::Upload::kTls1_0;
    }

    if (token == L"1.1")
    {
        return OutputSpec::Upload::kTls1_1;
    }

    if (token == L"1.2")
    {
        return OutputSpec::Upload::kTls1_2;
    }

    if (token == L"1.3")
    {
        return OutputSpec::Upload::kTls1_3;
    }

    return 0;
}

// Classify the running OS into a configurable TLS exception target, if any.
std::optional<OutputSpec::Upload::OsTarget> DetectOsTarget(DWORD major, DWORD minor)
{
    if (major == 5)  // Windows XP / Server 2003 era (5.1 / 5.2)
    {
        return OutputSpec::Upload::OsTarget::Xp;
    }

    if (major == 6 && minor == 1)  // Windows 7 / Server 2008 R2
    {
        return OutputSpec::Upload::OsTarget::Seven;
    }

    return std::nullopt;
}

// Highest TLS version the given OS can negotiate. XP/2003 and 7/2008R2 top out at TLS 1.2 (no 1.3);
// any other (newer) OS is allowed up to the highest version this build knows.
uint8_t OsCeilingTls(std::optional<OutputSpec::Upload::OsTarget> target)
{
    if (target == OutputSpec::Upload::OsTarget::Xp || target == OutputSpec::Upload::OsTarget::Seven)
    {
        return OutputSpec::Upload::kTls1_2;
    }

    return OutputSpec::Upload::kHighestKnownTls;
}

// Expand a configured set to "floor and up": take the lowest requested version as the floor, then enable
// every known version from the floor up to max(floor, osCeiling). Using max(floor, osCeiling) preserves the
// prior behavior when a floor exceeds the OS ceiling (the requested version is still set, e.g. a 1.3 floor on
// Windows 7) rather than silently downgrading below the requested floor. A future version is included for free
// once kHighestKnownTls (and the OS ceiling) reach it - embedded configs need no change.
uint8_t FloorAndUp(uint8_t configured, uint8_t osCeiling)
{
    if (configured == 0)
    {
        return 0;
    }

    uint8_t floor = 0;
    for (uint8_t bit = 1; bit != 0; bit = static_cast<uint8_t>(bit << 1))
    {
        if (configured & bit)
        {
            floor = bit;
            break;
        }
    }

    const uint8_t ceiling = (osCeiling > floor) ? osCeiling : floor;

    uint8_t result = 0;
    for (uint8_t bit = floor; bit != 0 && bit <= ceiling && bit <= OutputSpec::Upload::kHighestKnownTls;
         bit = static_cast<uint8_t>(bit << 1))
    {
        result |= bit;
    }

    return result;
}

// If `entry` carries a trailing "[modifier]", strips it from `entry` and returns the lowercased
// modifier; returns std::nullopt when there is no bracket.
std::optional<std::wstring> TakeBracketModifier(std::wstring& entry)
{
    const auto open = entry.find(L'[');
    if (open == std::wstring::npos)
    {
        return std::nullopt;
    }

    const auto close = entry.find(L']', open);
    const auto count = (close == std::wstring::npos) ? std::wstring::npos : close - open - 1;

    std::wstring modifier = entry.substr(open + 1, count);
    boost::trim(modifier);

    entry.erase(open);
    boost::trim(entry);
    return modifier;
}

// Maps an OS-target tag ("xp", "seven") to its enum; std::nullopt if unrecognized.
std::optional<OutputSpec::Upload::OsTarget> ParseOsTarget(const std::wstring& name)
{
    if (equalCaseInsensitive(name, L"xp"sv))
    {
        return OutputSpec::Upload::OsTarget::Xp;
    }

    if (equalCaseInsensitive(name, L"seven"sv))
    {
        return OutputSpec::Upload::OsTarget::Seven;
    }

    return std::nullopt;
}

// True when an https_security entry's OS tag is well formed: no bracket at all, or exactly one "[...]" that
// ends the entry. Anything else - "tls1.2[xp", "tls1.3[seven]junk", "tls1.2]", "tls1.2[xp][seven]" - has no
// reliable meaning: reading it leniently would guess an OS tag and drop the rest, giving the entry a meaning
// its author did not write.
bool IsWellFormedOsTag(const std::wstring& entry)
{
    const size_t open = entry.find(L'[');
    const size_t close = entry.find(L']');
    if (open == std::wstring::npos && close == std::wstring::npos)
    {
        return true;
    }

    if (open == std::wstring::npos || close == std::wstring::npos || close < open)
    {
        return false;
    }

    const bool single =
        entry.find(L'[', open + 1) == std::wstring::npos && entry.find(L']', close + 1) == std::wstring::npos;
    return single && close == entry.size() - 1;
}

// What an https_security value parses to. Field for field what RestConfig stores, so ApplyHttpsSecurity
// is left with the one decision that is not parsing: what to keep when the value names no baseline.
struct HttpsSecurityPolicy
{
    // False when nothing usable was parsed: the caller then keeps its defaults. 'invalid' is still
    // filled in that case, so a value that only holds unreadable entries is still refused by Validate.
    bool parsedAny = false;

    uint8_t versions = 0;
    std::map<OutputSpec::Upload::OsTarget, uint8_t> osExceptions;
    bool osDefault = false;
    bool osMax = false;
    std::vector<std::wstring> invalid;
};

// Parses an https_security value. Grammar: comma list of "tlsX.Y" tokens, each optionally tagged
// with an OS target ("[xp]"/"[seven]") meaning "applies only on that OS". Untagged versions go to
// `versions`; tagged ones to `osExceptions[target]`. "os-default"/"none" => osDefault, "os-max" => osMax. An entry
// that cannot be read - a malformed tag, an unknown OS target, an unknown version - is never applied and is listed
// in `invalid`, which Validate refuses: whatever it meant, the value would not do it.
HttpsSecurityPolicy ParseHttpsSecurity(const std::wstring& value)
{
    HttpsSecurityPolicy policy;

    if (equalCaseInsensitive(value, L"os-default"sv) || equalCaseInsensitive(value, L"none"sv))
    {
        policy.osDefault = true;
        policy.parsedAny = true;
        return policy;
    }

    if (equalCaseInsensitive(value, L"os-max"sv))
    {
        policy.osMax = true;
        policy.parsedAny = true;
        return policy;
    }

    std::vector<std::wstring> entries;
    boost::split(entries, value, boost::is_any_of(L","));

    for (auto& entry : entries)
    {
        boost::trim(entry);
        if (entry.empty())
        {
            continue;
        }

        if (!IsWellFormedOsTag(entry))
        {
            Log::Warn(L"Ignoring https_security entry '{}': malformed OS tag", entry);
            policy.invalid.push_back(entry);
            continue;  // fail-closed
        }

        const std::wstring original = entry;  // TakeBracketModifier strips the tag from 'entry'
        std::optional<OutputSpec::Upload::OsTarget> target;
        if (const auto modifier = TakeBracketModifier(entry); modifier.has_value())
        {
            target = ParseOsTarget(*modifier);
            if (!target.has_value())
            {
                Log::Warn(L"Ignoring https_security entry with unknown OS target '[{}]'", *modifier);
                policy.invalid.push_back(original);
                continue;  // fail-closed
            }
        }

        const uint8_t version = TlsVersionBit(entry);
        if (version == 0)
        {
            Log::Warn(L"Ignoring unknown https_security version '{}'", entry);
            policy.invalid.push_back(original);
            continue;
        }

        if (target.has_value())
        {
            policy.osExceptions[*target] |= version;
        }
        else
        {
            policy.versions |= version;
        }

        policy.parsedAny = true;
    }

    return policy;
}

// Build one RESTStep from a <rest> node: method/path/query/accept_status, body/offset/chunk_size,
// the nested <header> list, the nested <response> capture list, the session="once" attribute, and
// the nested <form> field list.
RESTStep ParseRestStepNode(const ConfigItem& stepNode)
{
    RESTStep step;
    step.method = stepNode.SubItems[CONFIG_REST_HTTP_METHOD];
    step.path = stepNode.SubItems[CONFIG_REST_PATH];
    if (stepNode.SubItems[CONFIG_REST_HTTP_QUERY])
    {
        step.query = stepNode.SubItems[CONFIG_REST_HTTP_QUERY];
    }

    if (stepNode.SubItems[CONFIG_REST_ACCEPT_STATUS])
    {
        step.acceptStatus = ParseAcceptStatus(stepNode.SubItems[CONFIG_REST_ACCEPT_STATUS].c_str());
    }

    if (stepNode.SubItems[CONFIG_REST_BODY])
    {
        step.hasBody = true;
        step.bodySource = stepNode.SubItems[CONFIG_REST_BODY];
    }

    if (stepNode.SubItems[CONFIG_REST_OFFSET])
    {
        step.offsetExpr = stepNode.SubItems[CONFIG_REST_OFFSET];
    }

    if (stepNode.SubItems[CONFIG_REST_CHUNK_SIZE])
    {
        step.chunkSize = wcstoull(stepNode.SubItems[CONFIG_REST_CHUNK_SIZE].c_str(), nullptr, 10);
    }

    if (::HasValue(stepNode, CONFIG_REST_HEADER_LIST))
    {
        for (const auto& h : stepNode.SubItems[CONFIG_REST_HEADER_LIST].NodeList)
        {
            if (h.Status == ConfigItem::PRESENT && !h.strData.empty())
            {
                std::wstring header = h.strData;
                boost::trim(header);
                if (!header.empty())
                {
                    step.headers.push_back(header);
                }
            }
        }
    }

    if (::HasValue(stepNode, CONFIG_REST_RESPONSE_LIST))
    {
        for (const auto& r : stepNode.SubItems[CONFIG_REST_RESPONSE_LIST].NodeList)
        {
            RESTResponseCapture cap;
            if (r.SubItems[CONFIG_RESTRESP_FORM_INPUT])
            {
                cap.source = RESTResponseCapture::Source::FormInput;
                cap.key = r.SubItems[CONFIG_RESTRESP_FORM_INPUT];
            }
            else if (r.SubItems[CONFIG_RESTRESP_ATTRIBUTE])
            {
                cap.source = RESTResponseCapture::Source::Attribute;
                cap.key = r.SubItems[CONFIG_RESTRESP_ATTRIBUTE];
            }
            else if (r.SubItems[CONFIG_RESTRESP_DAV_PROPERTY])
            {
                cap.source = RESTResponseCapture::Source::DavProperty;
                cap.key = r.SubItems[CONFIG_RESTRESP_DAV_PROPERTY];
            }
            else
            {
                cap.source = RESTResponseCapture::Source::Header;
                cap.key = r.SubItems[CONFIG_RESTRESP_HEADER];
            }

            // More than one source attribute on the same <response>: the parser picked one by
            // precedence above, but which one the operator meant is not guessable.
            const int sourceCount = (r.SubItems[CONFIG_RESTRESP_FORM_INPUT] ? 1 : 0)
                + (r.SubItems[CONFIG_RESTRESP_ATTRIBUTE] ? 1 : 0) + (r.SubItems[CONFIG_RESTRESP_DAV_PROPERTY] ? 1 : 0)
                + (r.SubItems[CONFIG_RESTRESP_HEADER] ? 1 : 0);
            cap.bothSourcesGiven = sourceCount > 1;

            cap.name = r.SubItems[CONFIG_RESTRESP_NAME];
            if (r.SubItems[CONFIG_RESTRESP_DEFAULT])
            {
                cap.defaultValue = r.SubItems[CONFIG_RESTRESP_DEFAULT];
            }
            step.captures.push_back(std::move(cap));
        }
    }

    // session="once" marks a step as session establishment rather than per-file work. Any other
    // value is refused by ValidateRestStep; parse only the exact token here, and record an
    // unrecognized one rather than silently letting it fall through to "per file".
    if (stepNode.SubItems[CONFIG_REST_SESSION])
    {
        std::wstring session = stepNode.SubItems[CONFIG_REST_SESSION];
        boost::trim(session);
        if (EqualsAsciiCaseInsensitive(session, L"once"))
        {
            step.runOnce = true;
        }
        else
        {
            step.hasUnknownSessionToken = true;
        }
    }

    if (::HasValue(stepNode, CONFIG_REST_FORM_LIST))
    {
        for (const auto& f : stepNode.SubItems[CONFIG_REST_FORM_LIST].NodeList)
        {
            RESTFormField field;
            field.field = f.SubItems[CONFIG_RESTFORM_FIELD];
            field.value = f.SubItems[CONFIG_RESTFORM_VALUE];
            boost::trim(field.field);
            step.formFields.push_back(std::move(field));
        }

        // <form> present but produced no field (an empty list): distinct from no <form> at all, so
        // the validator can refuse the presence of the element rather than guess from an empty vector.
        step.declaredEmptyForm = step.formFields.empty();
    }

    return step;
}

// Every placeholder in a template must be well-formed at embed time rather than silently misbehaving
// at runtime: a {upload.random:...} token must parse (a bad charset/length is refused), and any
// {upload.*} token must be a real built-in (a typo like {upload.locl_path} would otherwise be left
// literal and break the request). Non-'upload.' tokens are operator captures (validated elsewhere) or
// literal braces (e.g. a volume-GUID path), so they are left untouched here.
bool CheckTemplatePlaceholders(const std::wstring& tmpl, std::wstring& error)
{
    std::vector<std::wstring> specs;
    if (!ScanRestRandomSpecs(tmpl, specs, error))  // shared scanner (RESTStep) - same one used at runtime
    {
        return false;
    }

    for (const auto& spec : specs)
    {
        if (!IsValidRestRandomSpec(spec, error))
        {
            return false;
        }
    }

    for (const auto& token : ScanPlaceholderTokens(tmpl))
    {
        if (IsReservedUploadName(token) && !IsKnownUploadPlaceholder(token))
        {
            error = L"unknown reserved placeholder '{" + token + L"}'";
            return false;
        }
    }

    return true;
}

// The per-chunk placeholders. Any one of them tells the server where the chunk it is reading belongs.
const std::wstring kChunkOffsetTokens[] = {L"{upload.range_start}", L"{upload.range_length}", L"{upload.range_end}"};

bool MentionsChunkOffset(const std::wstring& text)
{
    for (const std::wstring& token : kChunkOffsetTokens)
    {
        if (text.find(token) != std::wstring::npos)
        {
            return true;
        }
    }

    return false;
}

// True if a chunked step tells the server where each chunk belongs. The engine sends one request per
// chunk, so unless the offset travels in the path, the query or a header, every chunk is addressed
// identically: the server cannot tell them apart, the last one replaces the rest, and what remains is
// the final fragment in place of the file - with every request returning 2xx and the run reporting
// success. A step that does none of the three is refused by ValidateRestStep.
bool CarriesChunkOffset(const RESTStep& step)
{
    if (MentionsChunkOffset(step.path) || MentionsChunkOffset(step.query))
    {
        return true;
    }

    for (const std::wstring& header : step.headers)
    {
        if (MentionsChunkOffset(header))
        {
            return true;
        }
    }

    return false;
}

// The methods whose request may carry a body. A GET/HEAD/DELETE body is ignored or refused by real
// servers, so a <form> on one is a configuration mistake rather than a dialect.
//
// The comparison is case-insensitive only to accept a config written "post"; the method itself reaches
// the wire exactly as the config spells it (RESTAgent copies step.method verbatim), and an HTTP method
// is case-SENSITIVE, so a lowercase spelling still earns whatever the server makes of it.
bool MethodCarriesRequestBody(std::wstring_view method)
{
    constexpr std::wstring_view kCarriesBody[] = {L"POST", L"PUT", L"PATCH"};

    const auto matches = [method](std::wstring_view candidate) {
        return EqualsAsciiCaseInsensitive(method, candidate);
    };
    return std::any_of(std::begin(kCarriesBody), std::end(kCarriesBody), matches);
}

// A form field NAME is written into the body verbatim, so it must carry no structure of its own:
// non-empty, printable ASCII, and none of '&', '=' or whitespace. '%' and '+' are excluded too: the
// name is never percent-decoded on our side, but a urlencoded parser on the receiving end decodes
// "a%41b" to "aAb" and "a+b" to "a b", so a name carrying either does not arrive as written.
bool IsInertFormFieldName(const std::wstring& name)
{
    if (name.empty())
    {
        return false;
    }

    return std::all_of(name.begin(), name.end(), [](wchar_t c) {
        return c > 0x20 && c < 0x7F && c != L'&' && c != L'=' && c != L'%' && c != L'+';
    });
}

// True if some step declares a <response name="name"> capture. Used to refuse a <form value> that
// names a capture nothing declares: unlike a path (where a literal '{...}' can be a legitimate
// volume GUID), a <form> field has no legitimate literal-brace use, so a typo'd capture name must be
// caught here rather than posted verbatim as "field=%7Bname%7D".
bool IsDeclaredCaptureName(const std::wstring& name, const std::vector<RESTStep>& steps)
{
    for (const auto& step : steps)
    {
        for (const auto& cap : step.captures)
        {
            if (cap.name == name)
            {
                return true;
            }
        }
    }

    return false;
}

// Read a boolean opt-in attribute: "true"/"yes" engages it, any other value does not. An absent
// attribute leaves `value` at the default the field was declared with.
void ReadBoolAttribute(const ConfigItem& item, DWORD dwIndex, bool& value)
{
    if (!::HasValue(item, dwIndex))
    {
        return;
    }

    const std::wstring& text = item.SubItems[dwIndex];
    value = boost::iequals(text, L"true") || boost::iequals(text, L"yes");
}

// Read one non-negative integer attribute in base 10. Returns false when the attribute is absent, and
// records it in 'invalid' when it is present but unreadable, so Validate() can refuse by name: a mistyped
// retry policy must be reported, not silently replaced by a default the operator never wrote. Same
// contract as rest.HttpsSecurityInvalid.
//
// The radix is passed explicitly because GetIntegerFromArg defaults to 0, which means wcstoll auto-detects
// the base: retry_delay="010" would otherwise be 8 seconds and "0x1e" would be 30.
bool ReadCountAttribute(
    const ConfigItem& item,
    DWORD dwIndex,
    const wchar_t* name,
    uint32_t& value,
    std::wstring& invalid)
{
    if (!::HasValue(item, dwIndex))
    {
        return false;
    }

    const std::wstring& text = item.SubItems[dwIndex];

    auto reject = [&](const std::wstring& why) {
        Log::Debug(L"Cannot read '{}': {}", name, why);

        if (!invalid.empty())
        {
            invalid += L", ";
        }

        invalid += fmt::format(L"{}=\"{}\"", name, text);
    };

    LARGE_INTEGER parsed = {0};
    if (HRESULT hr = GetIntegerFromArg(text.c_str(), parsed, 10); FAILED(hr))
    {
        reject(fmt::format(L"'{}' is not a number [{}]", text, SystemError(hr)));
        return false;
    }

    // GetIntegerFromArg parses a signed value, so a negative or oversized one arrives here rather than
    // wrapping into a huge unsigned count.
    if (parsed.QuadPart < 0 || parsed.QuadPart > MAXDWORD)
    {
        reject(fmt::format(L"{} is out of range", parsed.QuadPart));
        return false;
    }

    value = static_cast<uint32_t>(parsed.QuadPart);
    return true;
}

// retry / retry_delay, the per-entry retry policy (see OutputSpec::Upload). Bounds and readability are
// Validate() rules rather than parser rules, so an embedded configuration is refused at embed time with a
// message naming the attribute, instead of quietly running as something the operator did not write.
void ParseRetryPolicy(const ConfigItem& item, uint32_t& retry, std::chrono::seconds& retryDelay, std::wstring& invalid)
{
    ReadCountAttribute(item, CONFIG_UPLOAD_RETRY, L"retry", retry, invalid);

    uint32_t seconds = 0;
    if (ReadCountAttribute(item, CONFIG_UPLOAD_RETRY_DELAY, L"retry_delay", seconds, invalid))
    {
        retryDelay = std::chrono::seconds(seconds);
    }
}

}  // namespace

namespace Orc {

bool Orc::OutputSpec::IsPattern(const std::wstring& strPattern)
{
    if (strPattern.find(L"{ComputerName}") != wstring::npos)
        return true;
    if (strPattern.find(L"{FullComputerName}") != wstring::npos)
        return true;
    if (strPattern.find(L"{SystemType}") != wstring::npos)
        return true;
    if (strPattern.find(L"{TimeStamp}") != wstring::npos)
        return true;
    if (strPattern.find(L"{Name}") != wstring::npos)
        return true;
    if (strPattern.find(L"{FileName}") != wstring::npos)
        return true;
    if (strPattern.find(L"{DirectoryName}") != wstring::npos)
        return true;
    return false;
}

HRESULT
OutputSpec::ApplyPattern(const std::wstring& strPattern, const std::wstring& strName, std::wstring& strFileName)
{
    wstring strComputerName;
    SystemDetails::GetOrcComputerName(strComputerName);

    wstring strFullComputerName;
    SystemDetails::GetOrcFullComputerName(strFullComputerName);

    wstring strTimeStamp;
    SystemDetails::GetTimeStamp(strTimeStamp);

    wstring strSystemType;
    SystemDetails::GetSystemType(strSystemType);

    using namespace fmt::literals;

    strFileName = fmt::format(
        fmt::runtime(strPattern),
        L"Name"_a = strName,
        L"FileName"_a = strName,
        L"DirectoryName"_a = strName,
        L"ComputerName"_a = strComputerName,
        L"FullComputerName"_a = strFullComputerName,
        L"TimeStamp"_a = strTimeStamp,
        L"SystemType"_a = strSystemType);

    return S_OK;
}

bool OutputSpec::IsDirectory() const
{
    return HasFlag(Type, Kind::Directory);
};

bool OutputSpec::IsFile() const
{
    return HasAnyFlag(
        Type,
        Kind::File | Kind::TableFile | Kind::StructuredFile | Kind::Archive | Kind::CSV | Kind::TSV | Kind::Parquet
            | Kind::ORC | Kind::XML | Kind::JSON);
}

// the same but without archive
bool OutputSpec::IsRegularFile() const
{
    return HasAnyFlag(
        Type,
        Kind::File | Kind::TableFile | Kind::StructuredFile | Kind::CSV | Kind::TSV | Kind::Parquet | Kind::ORC
            | Kind::XML | Kind::JSON);
}

bool OutputSpec::IsTableFile() const
{
    return HasAnyFlag(Type, Kind::TableFile | Kind::CSV | Kind::TSV | Kind::Parquet | Kind::ORC);
}

bool OutputSpec::IsStructuredFile() const
{
    return HasAnyFlag(Type, Kind::StructuredFile | Kind::XML | Kind::JSON);
}

bool OutputSpec::IsArchive() const
{
    return HasFlag(Type, Kind::Archive);
}

std::filesystem::path OutputSpec::Resolve(const std::wstring& path)
{
    fs::path output;

    if (IsPattern(path))
    {
        wstring patternApplied;
        if (auto hr = ApplyPattern(path, L""s, patternApplied); SUCCEEDED(hr))
        {
            output = patternApplied;
        }
        else
        {
            output = path;
        }
    }
    else
    {
        output = path;
    }

    WCHAR szExpanded[ORC_MAX_PATH] = {0};
    ExpandEnvironmentStrings(output.c_str(), szExpanded, ORC_MAX_PATH);
    if (wcscmp(output.c_str(), szExpanded) != 0)
    {
        output.assign(szExpanded);
    }

    return output;
}

HRESULT OutputSpec::Configure(
    OutputSpec::Kind supported,
    const std::wstring& strInputString,
    std::optional<std::filesystem::path> parent)
{
    HRESULT hr = E_FAIL;

    Type = OutputSpec::Kind::None;

    if (IsPattern(strInputString))
    {
        Pattern = strInputString;
    }

    // Now on with regular file paths
    fs::path outPath = Resolve(strInputString);

    if (parent.has_value())
    {
        outPath = parent.value() / outPath.filename();
        Path = outPath;
    }
    else
    {
        Path = outPath;
    }

    FileName = outPath.filename().wstring();

    auto extension = outPath.extension();

    if (HasFlag(supported, OutputSpec::Kind::TableFile))
    {
        if (equalCaseInsensitive(extension.c_str(), L".csv"sv))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::CSV);
            szSeparator = L",";
            szQuote = L"\"";
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
        else if (equalCaseInsensitive(extension.c_str(), L".tsv"sv))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::TSV);
            szSeparator = L"\t";
            szQuote = L"";
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
        else if (equalCaseInsensitive(extension.c_str(), L".parquet"sv))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::Parquet);
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
        else if (equalCaseInsensitive(extension.c_str(), L".orc"sv))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::ORC);
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
    }
    if (HasFlag(supported, OutputSpec::Kind::StructuredFile))
    {
        if (equalCaseInsensitive(extension.c_str(), L".xml"sv))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::StructuredFile | OutputSpec::Kind::XML);
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
    }
    if (HasFlag(supported, OutputSpec::Kind::StructuredFile))
    {
        if (equalCaseInsensitive(extension.c_str(), L".json"sv))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::StructuredFile | OutputSpec::Kind::JSON);
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
    }
    if (HasFlag(supported, OutputSpec::Kind::Archive))
    {
        auto fmt = OrcArchive::GetArchiveFormat(extension.c_str());
        if (fmt != ArchiveFormat::Unknown)
        {
            Type = OutputSpec::Kind::Archive;
            ArchiveFormat = fmt;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
    }

    if (HasFlag(supported, OutputSpec::Kind::Directory) && !outPath.empty())
    {
        // Guess this is a directory path if either there is no extension or no file output is supported
        if (wcslen(extension.c_str()) == 0
            || (!HasFlag(supported, OutputSpec::Kind::File) && !HasFlag(supported, OutputSpec::Kind::StructuredFile)
                && !HasFlag(supported, OutputSpec::Kind::TableFile)))
        {
            // Output without extension could very well be a dir
            if (SUCCEEDED(VerifyDirectoryExists(outPath.c_str())))
            {
                Type = OutputSpec::Kind::Directory;
                Path = outPath.wstring();
                creationStatus = Status::Existing;
                return S_OK;
            }
            else
            {
                Type = OutputSpec::Kind::Directory;
                creationStatus = Status::CreatedNew;
                return Orc::GetOutputDir(outPath.c_str(), Path, true);
            }
        }
    }

    if (HasFlag(supported, OutputSpec::Kind::File))
    {
        Type = OutputSpec::Kind::File;
        return Orc::GetOutputFile(outPath.c_str(), Path, true);
    }
    return S_FALSE;
}

HRESULT
OutputSpec::Configure(OutputSpec::Kind supported, const ConfigItem& item, std::optional<std::filesystem::path> parent)
{
    HRESULT hr = E_FAIL;

    if (!item)
        return S_OK;

    Type = OutputSpec::Kind::None;

    ArchiveFormat = ArchiveFormat::Unknown;
    if (!item.empty())
    {
        if (FAILED(hr = Configure(supported, item.c_str())))
        {
            Log::Error(L"An error occured when evaluating output item {}", item.c_str());
            return hr;
        }
        if (::HasValue(item, CONFIG_OUTPUT_FORMAT))
        {
            ArchiveFormat = OrcArchive::GetArchiveFormat(item.SubItems[CONFIG_OUTPUT_FORMAT]);
        }
    }

    OutputEncoding = OutputSpec::Encoding::UTF8;
    if (::HasValue(item, CONFIG_OUTPUT_ENCODING))
    {
        if (equalCaseInsensitive(item.SubItems[CONFIG_OUTPUT_ENCODING].c_str(), L"utf8"sv))
        {
            OutputEncoding = OutputSpec::Encoding::UTF8;
        }
        else if (equalCaseInsensitive(item.SubItems[CONFIG_OUTPUT_ENCODING].c_str(), L"utf16"sv))
        {
            OutputEncoding = OutputSpec::Encoding::UTF16;
        }
        else
        {
            Log::Error(
                L"Invalid encoding for outputdir in config file: {}", item.SubItems[CONFIG_OUTPUT_ENCODING].c_str());
            return E_INVALIDARG;
        }
    }

    if (::HasValue(item, CONFIG_OUTPUT_COMPRESSION))
    {
        Compression = item.SubItems[CONFIG_OUTPUT_COMPRESSION];
    }

    if (::HasValue(item, CONFIG_OUTPUT_PASSWORD))
    {
        Password = item.SubItems[CONFIG_OUTPUT_PASSWORD];
    }

    if (::HasValue(item, CONFIG_OUTPUT_DISPOSITION))
    {
        if (equalCaseInsensitive(item.SubItems[CONFIG_OUTPUT_DISPOSITION].c_str(), L"Truncate"sv))
        {
            disposition = OutputSpec::Disposition::Truncate;
        }
        else if (equalCaseInsensitive(item.SubItems[CONFIG_OUTPUT_DISPOSITION].c_str(), L"Append"sv))
        {
            disposition = OutputSpec::Disposition::Append;
        }
        else if (equalCaseInsensitive(item.SubItems[CONFIG_OUTPUT_DISPOSITION].c_str(), L"CreateNew"sv))
        {
            disposition = OutputSpec::Disposition::CreateNew;
        }
        else
        {
            Log::Error(
                L"Invalid disposition for output in config file: {}", item.SubItems[CONFIG_OUTPUT_DISPOSITION].c_str());
            return E_INVALIDARG;
        }
    }

    return S_OK;
}

HRESULT OutputSpec::Upload::Configure(const ConfigItem& item)
{
    if (!::HasValue(item, CONFIG_UPLOAD_METHOD))
    {
        return S_OK;
    }

    if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_METHOD], L"BITS"sv))
    {
        Method = OutputSpec::UploadMethod::BITS;
    }
    else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_METHOD], L"filecopy"sv))
    {
        Method = OutputSpec::UploadMethod::FileCopy;
    }
    else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_METHOD], L"rest"sv))
    {
        Method = OutputSpec::UploadMethod::REST;
    }
    else
        Method = OutputSpec::UploadMethod::NoUpload;

    if (HRESULT hr = ParseUploadTarget(item); FAILED(hr))
    {
        return hr;
    }

    ParseUploadCommon(item);
    ParseHttpAttributes(item);
    ParseRestSteps(item);
    ParseProxyAndSecurity(item);
    EmitProxyAdvisories();
    return S_OK;
}

HRESULT OutputSpec::Upload::ParseUploadTarget(const ConfigItem& item)
{
    std::error_code ec;

    if (::HasValue(item, CONFIG_UPLOAD_JOBNAME))
    {
        JobName = item.SubItems[CONFIG_UPLOAD_JOBNAME];
    }

    std::optional<std::wstring> configUploadServer;
    if (item.SubItems[CONFIG_UPLOAD_SERVER])
    {
        configUploadServer = item.SubItems[CONFIG_UPLOAD_SERVER];
    }

    std::optional<std::wstring> configUploadRootPath;
    if (item.SubItems[CONFIG_UPLOAD_ROOTPATH])
    {
        configUploadRootPath = item.SubItems[CONFIG_UPLOAD_ROOTPATH];
    }

    std::optional<std::wstring> configUploadUserName;
    if (item.SubItems[CONFIG_UPLOAD_USER])
    {
        configUploadUserName = item.SubItems[CONFIG_UPLOAD_USER];
    }

    std::optional<std::wstring> configUploadPassword;
    if (item.SubItems[CONFIG_UPLOAD_PASSWORD])
    {
        configUploadPassword = item.SubItems[CONFIG_UPLOAD_PASSWORD];
    }

    if (::HasValue(item, CONFIG_UPLOAD_URI))
    {
        if (configUploadServer || configUploadRootPath || configUploadUserName || configUploadPassword)
        {
            Log::Error(
                "Invalid configuration: 'uri' cannot be used along with 'server','path','username','password' "
                "attributes");
            return E_INVALIDARG;
        }

        std::wstring configUploadUri = ExpandEnvironmentStringsApi(item[CONFIG_UPLOAD_URI].c_str(), ec);
        if (ec)
        {
            Log::Warn("Failed to expand environment variables in uri [{}]", ec);

            // There could be no environment variable and this is still better than nothing...
            configUploadUri = item.SubItems[CONFIG_UPLOAD_URI];
        }

        // Same as 'server' below: an unset variable leaves its reference in the string, and "failed to
        // parse uri '%ORC_UPLOAD_URI%'" does not tell the operator which machine is missing what.
        if (HasUnexpandedEnvReference(configUploadUri))
        {
            Log::Error(
                L"'uri' still reads \"{}\" after expansion: that environment variable is not set here, so no "
                L"address can be resolved. Set it, or write the address in the configuration.",
                configUploadUri);
            return E_INVALIDARG;
        }

        Uri uri(configUploadUri, ec);
        if (ec)
        {
            Log::Error(L"Failed to parse uri '{}' [{}]", item.SubItems[CONFIG_UPLOAD_URI].c_str(), ec);
            return E_INVALIDARG;
        }

        if (uri.Host())
        {
            // Carry the port: Uri parses it separately from the host, so building the server from the host
            // alone silently sent every 'uri' upload to the scheme default. 'server' has always honored an
            // explicit port, and the two spellings must reach the same endpoint. Appending it here also puts
            // it through the same Validate() check, which refuses an out-of-range value at embed time.
            configUploadServer = uri.Scheme().value_or(L"file") + L"://" + *uri.Host();
            if (uri.Port())
            {
                configUploadServer->append(L":").append(*uri.Port());
            }
        }

        if (uri.Path())
        {
            configUploadRootPath = *uri.Path();
        }

        if (uri.UserName())
        {
            configUploadUserName = *uri.UserName();
        }

        if (uri.Password())
        {
            configUploadPassword = *uri.Password();
        }
    }
    if (configUploadServer)
    {
        std::wstring server = ExpandEnvironmentStringsApi(configUploadServer->c_str(), ec);

        // An undefined variable is not an error for ExpandEnvironmentStringsW: the reference survives as
        // literal text. Say so, because the diagnosis the operator gets otherwise is ApplyServerScheme
        // complaining that a string they never wrote is not a URL. Unlike 'proxy', this stays a hard
        // failure rather than being deferred to the run: the scheme decides whether the embed-time
        // transport gate sees plain HTTP, so a server nobody can resolve until runtime would take that
        // check with it: server="%VAR%", set to http://... on the host, would pass the refusal ToolEmbed
        // enforces. Consequence: a config whose 'server' comes from the environment cannot be embedded on a
        // machine that does not define that variable.
        if (HasUnexpandedEnvReference(server))
        {
            Log::Error(
                L"'server' still reads \"{}\" after expansion: that environment variable is not set here, so "
                L"no address can be resolved. Set it, or write the address in the configuration: "
                L"\"https://host[:port]\", or \"http://host[:port]\" with insecure=\"true\".",
                server);
            return E_INVALIDARG;
        }

        if (HRESULT hr = ApplyServerScheme(server); FAILED(hr))
        {
            return hr;
        }
    }
    if (configUploadRootPath)
    {
        std::wstring root = ExpandEnvironmentStringsApi(configUploadRootPath->c_str(), ec);
        if (ec)
        {
            Log::Warn("Failed to expand environment variables [{}]", ec);

            // There could be no environment variable and this is still better than nothing...
            root = item.SubItems[CONFIG_UPLOAD_ROOTPATH];
        }

        NormalizeRootPath(root);
    }

    if (configUploadUserName)
    {
        UserName = *configUploadUserName;
    }

    if (configUploadPassword)
    {
        Password = *configUploadPassword;
    }

    return S_OK;
}

// Map a server URL's scheme (http/https/file) to bitsMode and set ServerName. The string passed in is
// already environment-expanded. Returns E_INVALIDARG on a non-matching or unknown scheme.
HRESULT OutputSpec::Upload::ApplyServerScheme(const std::wstring& server)
{
    static std::wregex r(L"(http|https|file):(//|\\\\)(.*)", std::regex_constants::icase);
    std::wsmatch s;

    if (std::regex_match(server, s, r))
    {
        if (equalCaseInsensitive(s[1].str().c_str(), L"http"sv))
        {
            bitsMode = BITSMode::HTTP;
        }
        else if (equalCaseInsensitive(s[1].str(), L"https"sv))
        {
            bitsMode = BITSMode::HTTPS;
        }
        else if (equalCaseInsensitive(s[1].str(), L"file"sv))
        {
            bitsMode = BITSMode::SMB;
        }
        else
        {
            Log::Debug(L"ApplyServerScheme: unsupported scheme in server '{}'", server);
            return E_INVALIDARG;
        }
        ServerName = s[3].str();

        // WinHttpConnect takes a bare host[:port]. A path (or a trailing slash) left in ServerName passes
        // SplitHostPort - it has no colon - so the embed-time check accepts the config, and the failure only
        // shows up on the endpoint as an opaque connect error. file:// is exempt: its remainder is a UNC path
        // that BITSAgent and CopyFileAgent join with RootPath.
        //
        // Refused only for REST, like the other new hard rules (see the TODO in Validate). BITS and FileCopy
        // have always accepted a path here: refusing one would drop the upload of a config that works today
        // - the config reader skips it, and refuses to run when it was the only upload - and would
        // hard-abort ToolEmbed on it. Warn instead, and expand the refusal in a NEXT MAJOR VERSION.
        if (bitsMode != BITSMode::SMB && ServerName.find_first_of(L"/\\") != std::wstring::npos)
        {
            if (Method == OutputSpec::UploadMethod::REST)
            {
                Log::Debug(L"ApplyServerScheme: server '{}' must be a bare host[:port], with no path", server);
                return E_INVALIDARG;
            }

            Log::Warn(L"Upload 'server' should be a bare host[:port], with no path: '{}'", server);
        }
    }
    else
    {
        Log::Debug(L"ApplyServerScheme: server '{}' is not an http(s):// or file:// URL", server);
        return E_INVALIDARG;
    }

    return S_OK;
}

// Normalize the configured root path into RootPath: swap slashes for the active bitsMode, then ensure a
// non-empty, leading-separator path. The string passed in is already environment-expanded.
void OutputSpec::Upload::NormalizeRootPath(const std::wstring& root)
{
    std::replace_copy(
        std::begin(root),
        std::end(root),
        back_inserter(RootPath),
        bitsMode == BITSMode::SMB ? L'/' : L'\\',
        bitsMode == BITSMode::SMB ? L'\\' : L'/');

    if (RootPath.empty())
    {
        RootPath = bitsMode == BITSMode::SMB ? L"\\" : L"/";
    }
    else if (RootPath.front() != L'\\' && RootPath.front() != L'/')
    {
        RootPath.insert(0, 1, bitsMode == BITSMode::SMB ? L'\\' : L'/');
    }
}

std::vector<std::wstring> Orc::SplitFilterPatterns(const std::wstring& value)
{
    std::vector<std::wstring> raw;
    boost::split(raw, value, boost::is_any_of(L",;"));

    std::vector<std::wstring> patterns;
    for (auto& pattern : raw)
    {
        boost::trim(pattern);
        if (!pattern.empty())
        {
            patterns.push_back(pattern);
        }
    }

    return patterns;
}

void OutputSpec::Upload::ParseUploadCommon(const ConfigItem& item)
{
    Operation = OutputSpec::UploadOperation::Copy;
    if (::HasValue(item, CONFIG_UPLOAD_OPERATION))
    {
        if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_OPERATION], L"Copy"sv))
        {
            Operation = OutputSpec::UploadOperation::Copy;
        }
        else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_OPERATION], L"Move"sv))
        {
            Operation = OutputSpec::UploadOperation::Move;
        }
    }

    Mode = OutputSpec::UploadMode::Synchronous;
    if (::HasValue(item, CONFIG_UPLOAD_MODE))
    {
        if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_MODE], L"sync"sv))
        {
            Mode = OutputSpec::UploadMode::Synchronous;
        }
        else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_MODE], L"async"sv))
        {
            Mode = OutputSpec::UploadMode::Asynchronous;
        }
    }

    // REST never performs Windows-integrated auth, so default it to Anonymous: that makes an
    // explicit NTLM/Kerberos/Negotiate distinguishable from the absence of an authscheme, so
    // Validate() can fail it closed. Other methods (BITS) keep their long-standing Negotiate default.
    AuthScheme = (Method == OutputSpec::UploadMethod::REST) ? OutputSpec::UploadAuthScheme::Anonymous
                                                            : OutputSpec::UploadAuthScheme::Negotiate;
    if (::HasValue(item, CONFIG_UPLOAD_AUTHSCHEME))
    {
        if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_AUTHSCHEME], L"Anonymous"sv))
        {
            AuthScheme = OutputSpec::UploadAuthScheme::Anonymous;
        }
        else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_AUTHSCHEME], L"Basic"sv))
        {
            AuthScheme = OutputSpec::UploadAuthScheme::Basic;
        }
        else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_AUTHSCHEME], L"NTLM"sv))
        {
            AuthScheme = OutputSpec::UploadAuthScheme::NTLM;
        }
        else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_AUTHSCHEME], L"Kerberos"sv))
        {
            AuthScheme = OutputSpec::UploadAuthScheme::Kerberos;
        }
        else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_AUTHSCHEME], L"Negotiate"sv))
        {
            AuthScheme = OutputSpec::UploadAuthScheme::Negotiate;
        }
        else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_AUTHSCHEME], L"AWSv4"sv))
        {
            AuthScheme = OutputSpec::UploadAuthScheme::AWSv4;
        }
    }

    if (::HasValue(item, CONFIG_UPLOAD_BITS_SMB_DELETE_SHARE))
    {
        if (bitsMode != BITSMode::SMB)
        {
            Log::Warn(L"Option 'delete_smb_share' is only supported with BITS SMB mode");
        }
        else
        {
            const auto str = item.SubItems[CONFIG_UPLOAD_BITS_SMB_DELETE_SHARE].c_str();
            bitsDeleteSmbShare = boost::iequals(str, L"true") || boost::iequals(str, L"yes");
        }
    }

    if (::HasValue(item, CONFIG_UPLOAD_FILTER_INC))
    {
        FilterInclude = SplitFilterPatterns((const std::wstring&)item.SubItems[CONFIG_UPLOAD_FILTER_INC]);
    }

    if (::HasValue(item, CONFIG_UPLOAD_FILTER_EXC))
    {
        FilterExclude = SplitFilterPatterns((const std::wstring&)item.SubItems[CONFIG_UPLOAD_FILTER_EXC]);
    }

    ParseRetryPolicy(item, Retry, RetryDelay, RetryInvalid);
}

void OutputSpec::Upload::ParseHttpAttributes(const ConfigItem& item)
{
    // 'http_header' attribute is a single complete header line, used verbatim.
    // Header values legitimately contain both ',' and ';' (e.g. "Content-Type: text/html;
    // charset=utf-8"), so the value is never split on a delimiter. Use repeated <header>
    // child elements to send several headers.
    if (::HasValue(item, CONFIG_UPLOAD_HTTP_HEADER))
    {
        std::wstring header = item.SubItems[CONFIG_UPLOAD_HTTP_HEADER];
        boost::trim(header);
        if (!header.empty())
        {
            rest.HttpHeaders.push_back(header);
        }
    }

    // TLS policy. A comma-separated list of "tlsX.Y" tokens giving the MINIMUM (floor) version; a
    // trailing OS-target tag ("[xp]"/"[seven]") sets a different floor on that OS. At runtime the agent
    // enables the floor and every higher version the OS supports (see ResolveTlsVersions), so a future
    // TLS version is adopted by an existing config after a rebuild - no config change. e.g.
    // "tls1.3, tls1.2[xp]" => floor TLS 1.3 (and up) everywhere, floor TLS 1.2 on XP (its ceiling).
    // "os-default" (or "none") leaves the system TLS policy in charge, and HttpClient proves TLS 1.2 on every
    // connection unless 'insecure'. "os-max" locks each connection on the single highest version the host
    // supports, never below TLS 1.2 without 'insecure' (see OsMaxTlsCandidates).
    if (::HasValue(item, CONFIG_UPLOAD_HTTPS_SECURITY))
    {
        ApplyHttpsSecurity(item.SubItems[CONFIG_UPLOAD_HTTPS_SECURITY], rest);
    }

    // Parse repeated <header> child elements (NODELIST). Each element is one complete
    // header line, used verbatim - no delimiter splitting (see note above).
    if (::HasValue(item, CONFIG_UPLOAD_HEADER_LIST))
    {
        for (const auto& headerNode : item.SubItems[CONFIG_UPLOAD_HEADER_LIST].NodeList)
        {
            if (headerNode.Status == ConfigItem::PRESENT && !headerNode.strData.empty())
            {
                std::wstring header = headerNode.strData;
                boost::trim(header);
                if (!header.empty())
                {
                    rest.HttpHeaders.push_back(header);
                }
            }
        }
    }
}

void OutputSpec::Upload::ParseRestSteps(const ConfigItem& item)
{
    // REST multi-step upload: flag upload-level per-request attributes that <rest> supersedes
    // (Option A - REST request paths come only from <rest>), then parse the ordered <rest> list.
    if (Method == UploadMethod::REST)
    {
        const std::pair<DWORD, const wchar_t*> legacy[] = {
            {CONFIG_UPLOAD_ROOTPATH, L"path"},
            {CONFIG_UPLOAD_HTTP_METHOD, L"http_method"},
            {CONFIG_UPLOAD_HTTP_QUERY, L"http_query"},
            {CONFIG_UPLOAD_CHUNK_SIZE, L"chunk_size"}};
        for (const auto& [idx, name] : legacy)
        {
            if (::HasValue(item, idx))
            {
                if (!rest.RestSupersededAttrs.empty())
                {
                    rest.RestSupersededAttrs += L", ";
                }

                rest.RestSupersededAttrs += name;
            }
        }
    }

    if (::HasValue(item, CONFIG_UPLOAD_REST_LIST))
    {
        for (const auto& stepNode : item.SubItems[CONFIG_UPLOAD_REST_LIST].NodeList)
        {
            rest.RestSteps.push_back(ParseRestStepNode(stepNode));
        }
    }
}

void OutputSpec::Upload::ParseProxyAndSecurity(const ConfigItem& item)
{
    // Security opt-in flags (validated by Validate()).
    ReadBoolAttribute(item, CONFIG_UPLOAD_INSECURE, rest.Insecure);
    ReadBoolAttribute(item, CONFIG_UPLOAD_FOLLOW_REDIRECTS, rest.FollowRedirects);

    // Pinned server certificates (X.509 PEM or base64), comma/semicolon-separated (see SplitServerCertPins).
    if (::HasValue(item, CONFIG_UPLOAD_SERVER_CERT_PIN))
    {
        rest.ServerCertPins = SplitServerCertPins((const std::wstring&)item.SubItems[CONFIG_UPLOAD_SERVER_CERT_PIN]);
    }

    // Proxy. The single 'proxy' attribute carries the discovery mode (host:port / system / wpad /
    // pac:url / *.pac); ClassifyProxy interprets it. Env-expanded like 'server' so the host can come
    // from %ORC_UPLOAD_PROXY%. HasValue is presence, not content: the reader marks an attribute PRESENT
    // before reading its value, so proxy="" arrives here as an engaged but empty string and stays
    // distinguishable from an absent attribute.
    if (::HasValue(item, CONFIG_UPLOAD_PROXY))
    {
        std::wstring proxy = ExpandConfigValue(item.SubItems[CONFIG_UPLOAD_PROXY]);
        boost::trim(proxy);
        rest.Proxy = std::move(proxy);
    }

    if (::HasValue(item, CONFIG_UPLOAD_PROXY_BYPASS))
    {
        rest.ProxyBypass = ExpandConfigValue(item.SubItems[CONFIG_UPLOAD_PROXY_BYPASS]);
        boost::trim(rest.ProxyBypass);
    }

    if (::HasValue(item, CONFIG_UPLOAD_PROXY_USER))
    {
        rest.ProxyUser = item.SubItems[CONFIG_UPLOAD_PROXY_USER];
    }

    if (::HasValue(item, CONFIG_UPLOAD_PROXY_PASSWORD))
    {
        rest.ProxyPassword = item.SubItems[CONFIG_UPLOAD_PROXY_PASSWORD];
    }

    // Proxy auth scheme: Basic/NTLM/Negotiate (Kerberos accepted as a synonym for Negotiate, as for
    // server auth). Unset stays Anonymous (= no proxy auth); Validate refuses Anonymous with proxy creds
    // (auth is preemptive - the scheme is never learned from a 407).
    if (::HasValue(item, CONFIG_UPLOAD_PROXY_AUTHSCHEME))
    {
        const auto& v = item.SubItems[CONFIG_UPLOAD_PROXY_AUTHSCHEME];
        if (equalCaseInsensitive(v, L"Basic"sv))
        {
            rest.ProxyAuthScheme = OutputSpec::UploadAuthScheme::Basic;
        }
        else if (equalCaseInsensitive(v, L"NTLM"sv))
        {
            rest.ProxyAuthScheme = OutputSpec::UploadAuthScheme::NTLM;
        }
        else if (equalCaseInsensitive(v, L"Kerberos"sv) || equalCaseInsensitive(v, L"Negotiate"sv))
        {
            rest.ProxyAuthScheme = OutputSpec::UploadAuthScheme::Negotiate;
        }
    }

    // Credentials are NOT environment-expanded, like user/password/proxy_user/proxy_password: a value
    // that legitimately contains '%' must stay literal. Region and service ARE expanded, being neither
    // addresses nor secrets but values a deployment may vary per site.
    if (::HasValue(item, CONFIG_UPLOAD_ACCESS_KEY))
    {
        rest.aws.AccessKey = item.SubItems[CONFIG_UPLOAD_ACCESS_KEY];
    }

    if (::HasValue(item, CONFIG_UPLOAD_SECRET_KEY))
    {
        rest.aws.SecretKey = item.SubItems[CONFIG_UPLOAD_SECRET_KEY];
    }

    if (::HasValue(item, CONFIG_UPLOAD_SESSION_TOKEN))
    {
        rest.aws.SessionToken = item.SubItems[CONFIG_UPLOAD_SESSION_TOKEN];
    }

    if (::HasValue(item, CONFIG_UPLOAD_AWS_REGION))
    {
        rest.aws.Region = ExpandConfigValue(item.SubItems[CONFIG_UPLOAD_AWS_REGION]);
        boost::trim(rest.aws.Region);
    }

    if (::HasValue(item, CONFIG_UPLOAD_AWS_SERVICE))
    {
        rest.aws.Service = ExpandConfigValue(item.SubItems[CONFIG_UPLOAD_AWS_SERVICE]);
        boost::trim(rest.aws.Service);
    }

    // Unknown values fail closed to the strict default rather than being silently accepted.
    if (::HasValue(item, CONFIG_UPLOAD_AWS_CLOCK))
    {
        const auto& v = item.SubItems[CONFIG_UPLOAD_AWS_CLOCK];
        if (equalCaseInsensitive(v, L"server"sv))
        {
            rest.aws.ClockSource = OutputSpec::Upload::AwsClockSource::Server;
        }
        else if (!equalCaseInsensitive(v, L"local"sv))
        {
            Log::Warn(L"Unknown aws_clock value '{}'; using \"local\" (log the skew and fail)", (const std::wstring&)v);
        }
    }
}

ProxySettings Orc::ClassifyProxy(const OutputSpec::Upload& upload)
{
    ProxySettings s;
    s.bypass = upload.rest.ProxyBypass;
    s.user = upload.rest.ProxyUser;
    s.password = upload.rest.ProxyPassword;
    s.scheme = ToHttpAuthScheme(upload.rest.ProxyAuthScheme);

    // No 'proxy' attribute at all: follow the machine-wide WinHTTP configuration, which connects DIRECT
    // by itself when the machine has no proxy set. That is the zero-config case, and it is NOT the same
    // as proxy="", which is the operator stating there is no proxy and asking for no discovery.
    if (!upload.rest.Proxy.has_value())
    {
        s.mode = ProxyMode::System;
        return s;
    }

    std::wstring v = *upload.rest.Proxy;
    boost::trim(v);

    // An environment reference that survived expansion means the variable is not set on THIS host. Keep
    // the text for the message and refuse later: treating it as a host name would dial "%ORC_UPLOAD_PROXY%"
    // and treating it as "no proxy" would quietly bypass the egress path the operator asked for.
    // HttpClient::Open refuses ProxyMode::Invalid at run time; Validate does not, because an unset variable is a
    // property of the machine running the collection, and a config reading %ORC_UPLOAD_PROXY% must still embed on
    // a build machine that does not define it. ('server' is different, see ParseUploadTarget.)
    if (HasUnexpandedEnvReference(v))
    {
        s.mode = ProxyMode::Invalid;
        s.server = v;
        return s;
    }

    if (v.empty() || boost::iequals(v, L"direct") || boost::iequals(v, L"none"))
    {
        s.mode = ProxyMode::Direct;  // empty/direct/none: no proxy, no discovery
    }
    else if (boost::iequals(v, L"system"))
    {
        s.mode = ProxyMode::System;
    }
    else if (boost::iequals(v, L"wpad") || boost::iequals(v, L"auto"))
    {
        // "auto" must be matched here: falling through to a named proxy "auto" could resolve auto.<dns suffix>
        // and send the archive, with preemptive Basic credentials, to an unintended host.
        s.mode = ProxyMode::Wpad;
    }
    else if (boost::istarts_with(v, L"pac:"))
    {
        s.mode = ProxyMode::Pac;
        s.pacUrl = v.substr(4);
    }
    else if (IsPacUrl(v))
    {
        s.mode = ProxyMode::Pac;
        s.pacUrl = v;
    }
    else
    {
        s.mode = ProxyMode::Named;
        s.server = v;
    }

    return s;
}

// An UNDEFINED variable is not an error for ExpandEnvironmentStringsW: it leaves the reference in place and
// reports success, so an unset %ORC_UPLOAD_PROXY% survives as that literal text - which names no host anyone
// can reach. Only the caller knows whether that is fatal; here it is just recognised.
//
// The same text is also how a URI escapes a character: "https://host/dfir%20orc%20drop/" holds "%20orc%",
// which no variable defines either. The two cannot be told apart by expanding, so the shape decides. Every
// two consecutive '%' are a candidate, and one whose both ends are percent-escapes is taken for URI text.
// A real reference has at least one end that is not: "%ORC_UPLOAD_SERVER%" starts with 'O', and a name made
// of hexadecimal digits alone ("%ADD%") closes on the end of the value or on a separator. "%%" is an escaped
// percent sign, not a reference to a variable with an empty name. Checking names cannot replace the shape test
// either: a Windows variable name may start with a digit. Known miss: a hexadecimal name immediately followed
// by two more hexadecimal digits ("%ADD%20") is taken for URI text.
bool Orc::HasUnexpandedEnvReference(const std::wstring& value)
{
    size_t open = value.find(L'%');
    while (open != std::wstring::npos)
    {
        const size_t close = value.find(L'%', open + 1);
        if (close == std::wstring::npos)
        {
            return false;
        }

        if (close > open + 1 && !(IsPercentEscapeAt(value, open) && IsPercentEscapeAt(value, close)))
        {
            return true;
        }

        open = close;
    }

    return false;
}

// PEM and base64 contain neither ',' nor ';', so they safely separate several certificates (key rotation).
std::vector<std::wstring> Orc::SplitServerCertPins(const std::wstring& value)
{
    std::vector<std::wstring> certs;
    boost::split(certs, value, boost::is_any_of(L",;"));

    std::vector<std::wstring> pins;
    for (auto& cert : certs)
    {
        boost::trim(cert);
        if (!cert.empty())
        {
            pins.push_back(cert);
        }
    }

    return pins;
}

// A value made only of OS-tagged entries ("tls1.2[xp]") names no baseline. Taking the parsed 0 would leave
// WINHTTP_OPTION_SECURE_PROTOCOLS unset on every OTHER OS - back to the system policy, which may still
// negotiate TLS 1.0/1.1 - with nothing in the log to say so. The baseline already in 'rest' is kept there, and
// the exceptions the operator did write are applied. A value that parses to nothing changes nothing.
void Orc::ApplyHttpsSecurity(const std::wstring& rawValue, OutputSpec::Upload::RestConfig& rest)
{
    std::wstring value = rawValue;
    boost::trim(value);

    HttpsSecurityPolicy policy = ParseHttpsSecurity(value);

    // Recorded whether or not anything else parsed: Validate refuses an entry it could not read, so ToolEmbed
    // rejects the configuration instead of embedding a value that does not do what was written.
    rest.HttpsSecurityInvalid = std::move(policy.invalid);

    if (!policy.parsedAny)
    {
        Log::Warn(L"No valid https_security value parsed from '{}'; keeping default", value);
        return;
    }

    if (!policy.osDefault && !policy.osMax && policy.versions == 0)
    {
        Log::Warn(
            L"https_security '{}' sets no baseline TLS version (every entry is OS-tagged); keeping the default floor "
            L"for other systems and applying the OS exceptions",
            value);
    }
    else
    {
        rest.TlsVersions = policy.versions;
    }

    rest.TlsOsExceptions = std::move(policy.osExceptions);
    rest.TlsOsDefault = policy.osDefault;
    rest.TlsOsMax = policy.osMax;
}

// Map the config TLS version bits (OutputSpec::Upload::kTls*) onto the HTTP-layer bits. Done explicitly, bit by
// bit, so the two layouts can diverge without silently misencoding the requested versions.
uint8_t Orc::ToHttpTlsVersions(uint8_t versions)
{
    uint8_t out = 0;
    if (versions & OutputSpec::Upload::kTls1_0)
    {
        out |= HttpTlsVersion::kTls1_0;
    }

    if (versions & OutputSpec::Upload::kTls1_1)
    {
        out |= HttpTlsVersion::kTls1_1;
    }

    if (versions & OutputSpec::Upload::kTls1_2)
    {
        out |= HttpTlsVersion::kTls1_2;
    }

    if (versions & OutputSpec::Upload::kTls1_3)
    {
        out |= HttpTlsVersion::kTls1_3;
    }

    return out;
}

// TLS 1.3 is enabled by default in the SChannel client from Windows Server 2022 (build 20348) and Windows 11
// (build 22000). Earlier Windows 10 builds know the WinHTTP flag without being able to negotiate the version,
// so the flag being accepted proves nothing there and the build decides instead.
constexpr DWORD kFirstTls13ClientBuild = 20348;

// A build that has TLS 1.3 gets TLS 1.3 alone, even with 'insecure': a fallback to TLS 1.2 there would quietly keep
// TLS 1.2 in use on modern hosts long after it is deprecated. Older builds have TLS 1.2 as their highest version.
uint8_t Orc::OsMaxTlsCandidates(std::optional<DWORD> osBuild, bool insecure)
{
    if (!osBuild.has_value())
    {
        return 0;
    }

    if (*osBuild >= kFirstTls13ClientBuild)
    {
        return OutputSpec::Upload::kTls1_3;
    }

    if (insecure)
    {
        return OutputSpec::Upload::kTls1_2 | OutputSpec::Upload::kTls1_1 | OutputSpec::Upload::kTls1_0;
    }

    return OutputSpec::Upload::kTls1_2;
}

HttpClientConfig Orc::BuildHttpClientConfig(const OutputSpec::Upload& upload)
{
    HttpClientConfig cfg;
    cfg.serverName = upload.ServerName;
    cfg.secure = (upload.bitsMode == OutputSpec::BITSMode::HTTPS);
    cfg.insecure = upload.rest.Insecure;
    cfg.followRedirects = upload.rest.FollowRedirects;
    cfg.serverScheme = ToHttpAuthScheme(upload.AuthScheme);
    cfg.serverUser = upload.UserName;
    cfg.serverPassword = upload.Password;
    cfg.serverCertPins = upload.rest.ServerCertPins;
    cfg.proxy = ClassifyProxy(upload);

    // Copied whatever the scheme: Validate already refuses an AWS credential set under another scheme, and
    // only an AWSv4 authenticator ever reads these.
    cfg.aws.accessKey = upload.rest.aws.AccessKey;
    cfg.aws.secretKey = upload.rest.aws.SecretKey;
    cfg.aws.sessionToken = upload.rest.aws.SessionToken;
    cfg.aws.region = upload.rest.aws.Region;
    cfg.aws.service = upload.rest.aws.Service;
    cfg.awsAllowServerClock = (upload.rest.aws.ClockSource == OutputSpec::Upload::AwsClockSource::Server);

    // Restrict accepted TLS versions for HTTPS, resolved against the running OS. An "[xp]"/"[seven]" exception
    // replaces the baseline floor on that OS, and HttpClient enables the resolved set. With TlsOsMax the
    // candidates go to HttpClient instead, which locks on the single highest one this system accepts. With
    // TlsOsDefault the system policy is untouched (tlsVersions stays 0) and HttpClient proves TLS 1.2 per connection.
    if (upload.rest.TlsOsDefault)
    {
        return cfg;
    }

    DWORD osMajor = 0;
    DWORD osMinor = 0;
    DWORD osBuild = 0;
    std::optional<DWORD> knownBuild;
    if (HRESULT hr = SystemDetails::GetOSVersion(osMajor, osMinor, osBuild); FAILED(hr))
    {
        // Fail closed: with no OS version no "[xp]"/"[seven]" exception can match, and os-max has no version to
        // lock on (build 0 would pass for an old host and lock a TLS 1.3 one on TLS 1.2). Report it, because on
        // XP the baseline floor is unreachable and every upload would otherwise fail with nothing to explain why.
        Log::Error(
            L"Failed to resolve the OS version: OS-specific 'https_security' exceptions cannot be applied and "
            L"https_security=\"os-max\" refuses the upload [{}]",
            SystemError(hr));

        osMajor = 0;  // GetOSVersion may have written garbage before failing
        osMinor = 0;
    }
    else
    {
        knownBuild = osBuild;
    }

    if (upload.rest.TlsOsMax)
    {
        cfg.tlsVersions = ToHttpTlsVersions(OsMaxTlsCandidates(knownBuild, upload.rest.Insecure));
        cfg.tlsLockHighest = true;
        return cfg;
    }

    cfg.tlsVersions = ToHttpTlsVersions(upload.ResolveTlsVersions(osMajor, osMinor));
    return cfg;
}

void OutputSpec::Upload::EmitProxyAdvisories()
{
    // Advisory warnings (hard rules live in Validate). proxy_* settings are honored ONLY by the REST
    // uploader, so the REST-specific advice (WPAD/PAC/Basic/NTLM) is emitted only for REST; other methods
    // that carry proxy_* settings get a single "ignored here" notice instead of contradictory advice.
    const ProxySettings proxyCfg = ClassifyProxy(*this);
    // Presence, not content: proxy="" is an explicit setting too, and it is just as ignored outside REST.
    const bool hasProxyConfig = rest.Proxy.has_value() || !rest.ProxyBypass.empty() || !rest.ProxyUser.empty()
        || !rest.ProxyPassword.empty() || rest.ProxyAuthScheme != OutputSpec::UploadAuthScheme::Anonymous;

    if (Method == OutputSpec::UploadMethod::REST)
    {
        if (proxyCfg.mode == ProxyMode::Wpad)
        {
            // Quote what the operator actually wrote ("wpad" or its "auto" synonym) so the warning points
            // back at their config line.
            Log::Warn(
                L"proxy=\"{}\" (WPAD auto-detect) is spoofable on a hostile LAN; prefer an explicit 'proxy' or a "
                L"PAC URL, and pin the server with 'server_cert_pin'",
                rest.Proxy.value_or(L""));
        }
        else if (proxyCfg.mode == ProxyMode::Pac)
        {
            Log::Warn(
                L"proxy uses a PAC script ('{}'): it is downloaded and executed to choose the proxy; prefer an "
                L"https PAC URL and pin the server with 'server_cert_pin'",
                proxyCfg.pacUrl);
        }

        if (rest.ProxyAuthScheme == OutputSpec::UploadAuthScheme::Basic)
        {
            Log::Warn(L"proxy_authscheme=\"Basic\" may expose the proxy credentials on a plaintext proxy connection");
        }

        // NTLM/Negotiate require a domain-qualified 'proxy_user' (WinHTTP sends an empty NTLM domain for a
        // bare name, which most proxies reject - verified against squid/Samba). Warn rather than guess it.
        if ((rest.ProxyAuthScheme == OutputSpec::UploadAuthScheme::NTLM
             || rest.ProxyAuthScheme == OutputSpec::UploadAuthScheme::Kerberos
             || rest.ProxyAuthScheme == OutputSpec::UploadAuthScheme::Negotiate)
            && !rest.ProxyUser.empty() && rest.ProxyUser.find(L'\\') == std::wstring::npos
            && rest.ProxyUser.find(L'@') == std::wstring::npos)
        {
            Log::Warn(
                L"proxy_authscheme=\"NTLM\"/\"Negotiate\" usually needs a domain-qualified 'proxy_user' "
                L"(DOMAIN\\user or user@domain.fqdn); a bare username typically fails the proxy handshake");
        }
    }
    else if (hasProxyConfig)
    {
        Log::Warn(L"proxy settings ('proxy', 'proxy_user', ...) are only applied to REST uploads; ignored here");
    }
}

uint8_t Orc::OutputSpec::Upload::ResolveTlsVersions(DWORD osMajor, DWORD osMinor) const
{
    const auto target = DetectOsTarget(osMajor, osMinor);

    uint8_t configured = rest.TlsVersions;  // baseline floor on any OS without an exception
    if (target.has_value())
    {
        const auto it = rest.TlsOsExceptions.find(*target);
        if (it != rest.TlsOsExceptions.end())
        {
            configured = it->second;
        }
    }

    // Fail closed on an empty floor. The caller only resolves versions for neither os-default nor os-max - the
    // operator asked for an enforced floor - so returning 0 would hand the connection back to the system
    // TLS policy, which may still negotiate TLS 1.0/1.1. Enforce the safe universal minimum instead. Reachable
    // when the config names no baseline and no exception matches, including when GetOSVersion failed and the
    // 0.0 sentinel made DetectOsTarget return none.
    if (configured == 0)
    {
        Log::Warn(L"No TLS version resolved from 'https_security'; enforcing TLS 1.2 as the floor");
        configured = kTls1_2;
    }

    return FloorAndUp(configured, OsCeilingTls(target));
}

// Upload::Validate's checks, one per aspect. Free functions over a const Upload: each returns
// Verdict::Ok to mean "passed, keep checking", and Validate below returns the first non-Ok verdict.
namespace {

using Upload = OutputSpec::Upload;
using Validation = Upload::Validation;
using Verdict = Upload::Verdict;
using BITSMode = OutputSpec::BITSMode;
using UploadAuthScheme = OutputSpec::UploadAuthScheme;
using UploadMethod = OutputSpec::UploadMethod;
using UploadMode = OutputSpec::UploadMode;

Validation ValidateRestStep(const RESTStep& step, const std::vector<RESTStep>& allSteps)
{
    // A typo like session="onse" must not silently fall through to "per file": it would run a
    // login for every file and replay the credential each time.
    if (step.hasUnknownSessionToken)
    {
        return {Verdict::Refuse, L"the only supported 'session' value is \"once\""};
    }

    std::wstring tmplError;
    if (!CheckTemplatePlaceholders(step.path, tmplError) || !CheckTemplatePlaceholders(step.query, tmplError))
    {
        return {Verdict::Refuse, L"<rest> path/query: " + tmplError};
    }

    for (const auto& header : step.headers)
    {
        if (!CheckTemplatePlaceholders(header, tmplError))
        {
            return {Verdict::Refuse, L"<rest> <header>: " + tmplError};
        }
    }

    // body= and offset= are also placeholder-substituted at runtime, so validate them too.
    if (!CheckTemplatePlaceholders(step.bodySource, tmplError)
        || !CheckTemplatePlaceholders(step.offsetExpr, tmplError))
    {
        return {Verdict::Refuse, L"<rest> body/offset: " + tmplError};
    }

    // A <response> capture must read from exactly one source. Configure leaves 'key' empty when none
    // was given - which is also what an OLDER binary does with a source it has never heard of, since
    // an unrecognized XML attribute is dropped with a warning rather than refused. Naming every source
    // this build supports is what turns that into a usable message: an operator whose config carries
    // 'attribute' and whose binary predates it sees a list without it and knows to update the binary,
    // rather than being told to use 'header'.
    for (const auto& cap : step.captures)
    {
        if (cap.key.empty())
        {
            return {
                Verdict::Refuse, L"a <response> capture requires one source: 'header', 'form_input' or 'attribute'"};
        }

#if !ORC_REST_ENABLE_DAV_PROPERTY
        // dav_property parses an attacker-controlled PROPFIND 207 body; it is compiled out by default.
        // Fail closed at embed time rather than silently ignoring the capture at runtime.
        if (cap.source == RESTResponseCapture::Source::DavProperty)
        {
            return {
                Verdict::Refuse,
                L"<response dav_property> is disabled in this build (rebuild with ORC_REST_ENABLE_DAV_PROPERTY=1)"};
        }
#endif

        // The 'upload.' prefix is reserved for engine-provided placeholders; a capture using it
        // would be silently shadowed by a built-in, so refuse it explicitly.
        if (IsReservedUploadName(cap.name))
        {
            return {Verdict::Refuse, L"a <response> 'name' must not start with the reserved 'upload.' prefix"};
        }

        // More than one source attribute on one <response>: which one the operator meant is not
        // guessable, and guessing wrong reads a different value from the server.
        if (cap.bothSourcesGiven)
        {
            return {
                Verdict::Refuse, L"a <response> capture must give exactly one of 'header', 'form_input', 'attribute'"};
        }

        // An attribute name the scan cannot match would never be found at collection time, and a
        // capture that is never found stops the dialog (see the fail-closed rule in RESTCapture).
        // Refuse it here, where the operator can still fix it.
        if (cap.source == RESTResponseCapture::Source::Attribute && !IsSupportedAttributeName(cap.key))
        {
            return {
                Verdict::Refuse,
                L"a <response> 'attribute' must be 1 to 64 characters of letters, digits, '-', '_', ':' or '.'"};
        }
    }

    // A <form> step sends an application/x-www-form-urlencoded body.
    if (!step.formFields.empty() || step.declaredEmptyForm)
    {
        // A GET/HEAD/DELETE with a body is not answered usefully by any server and is refused by
        // some outright. Only a method that carries a body may declare fields.
        if (!MethodCarriesRequestBody(step.method))
        {
            return {Verdict::Refuse, L"<form> requires an http_method that carries a body (POST, PUT or PATCH)"};
        }

        // hasBody streams the local file; formFields builds the body from literals. One request has
        // one body, so a step asking for both is a configuration mistake, not a merge.
        if (step.hasBody)
        {
            return {Verdict::Refuse, L"a <rest> step cannot have both 'body=' and <form> children"};
        }

        if (step.formFields.empty())
        {
            return {Verdict::Refuse, L"a <form> element must declare a 'field' and a 'value'"};
        }

        for (const auto& field : step.formFields)
        {
            // The field NAME is emitted verbatim into the body (only values are encoded), so it has
            // to be structurally inert on its own.
            if (!IsInertFormFieldName(field.field))
            {
                return {
                    Verdict::Refuse,
                    L"a <form> 'field' must be non-empty printable ASCII without '&', '=' or whitespace"};
            }

            if (!CheckTemplatePlaceholders(field.value, tmplError))
            {
                return {Verdict::Refuse, L"<rest> <form> value: " + tmplError};
            }

            // Unlike a path, a <form> field value has no legitimate literal-brace use, so a token that
            // is not an 'upload.*' built-in must name a capture some step actually declares. Without
            // this, a typo (value="{rtt}" for a capture named "rt") reaches ResolveUploadTemplate, is
            // kept verbatim (an unknown non-'upload.' token is assumed to be an operator capture
            // "validated elsewhere"), and is posted as "field=%7Brtt%7D" - the exact failure S5 exists
            // to prevent.
            for (const auto& token : ScanPlaceholderTokens(field.value))
            {
                if (!IsReservedUploadName(token) && !IsDeclaredCaptureName(token, allSteps))
                {
                    return {
                        Verdict::Refuse,
                        L"<rest> <form> value references '{" + token
                            + L"}', which no step declares as a <response> capture"};
                }
            }

            // A session="once" step runs before any file exists, so a per-file placeholder there
            // would resolve to nothing and post an empty field. UsesPerFilePlaceholder, not
            // UsesFileBoundPlaceholder: this step has no filename either (EstablishSession calls
            // SendRequest with an empty remoteFileName), unlike the remote-check caller that helper
            // was written for.
            if (step.runOnce && UsesPerFilePlaceholder(field.value))
            {
                return {
                    Verdict::Refuse, L"a session=\"once\" <form> value cannot use a per-file {upload.*} placeholder"};
            }
        }
    }

    // A session="once" step runs once per agent, before any file is chosen, through EstablishSession -
    // never through the per-file body path. A once-step with 'body=' would still pass "exactly one body
    // step" (it counts as the file's body), so EstablishSession would send it with no file handle: a
    // bodiless request to the destination path, creating or truncating the remote object to zero bytes
    // before the real body step ever runs.
    if (step.runOnce)
    {
        if (step.hasBody)
        {
            return {
                Verdict::Refuse,
                L"a session=\"once\" step cannot also carry 'body=' (the real body step must not run once)"};
        }

        // Same rule for the rest of a once-step: it runs before any file exists, so no template of it
        // may name a per-file value (UsesPerFilePlaceholder; see the comment above).
        std::vector<std::wstring> templates = {step.path, step.query, step.bodySource, step.offsetExpr};
        templates.insert(templates.end(), step.headers.begin(), step.headers.end());

        if (std::any_of(templates.begin(), templates.end(), [](const std::wstring& tmpl) {
                return UsesPerFilePlaceholder(tmpl);
            }))
        {
            return {Verdict::Refuse, L"a session=\"once\" step cannot use a per-file {upload.*} placeholder"};
        }
    }

    // body= selects which LOCAL file is read and streamed to the server. It must be a literal path
    // or an engine 'upload.*' built-in (e.g. {upload.local_path}) - never a server-controlled
    // <response> capture, whose value could redirect CreateFileW to an arbitrary local file or UNC
    // path (arbitrary file read / outbound SMB & NTLM relay). Refuse a body= that references any
    // declared capture name (checked across all steps; a capture in body= is never legitimate).
    if (step.hasBody)
    {
        for (const auto& otherStep : allSteps)
        {
            for (const auto& cap : otherStep.captures)
            {
                if (step.bodySource.find(L"{" + cap.name + L"}") != std::wstring::npos)
                {
                    Log::Error(
                        L"REST body='{}' references the <response> capture '{}': body= must be a literal "
                        L"path or an 'upload.*' built-in, never a server-controlled capture",
                        step.bodySource,
                        cap.name);
                    return {
                        Verdict::Refuse,
                        L"<rest> body= must not reference a <response> capture; use a literal path or an "
                        L"'upload.*' built-in"};
                }
            }
        }
    }

    // A chunk is one HTTP request whose body length is a DWORD; a chunk_size above 4 GB cannot
    // be honored and would truncate. Refuse it here rather than truncating silently at runtime.
    if (step.hasBody && step.chunkSize > 0xFFFFFFFFull)
    {
        return {Verdict::Refuse, L"'chunk_size' exceeds the 4 GB per-request limit"};
    }

    // Every chunk of a chunked step must tell the server where it belongs. Measured against MinIO with
    // chunk_size="256": an 890 byte archive arrived as a 122 byte object, all four PUTs returned 200 and
    // the run reported success - and with operation="move" the local copy is then deleted, so the
    // collection is lost with nothing anywhere saying so. Refused rather than warned because the failure
    // is silent and total: a sink that blindly appends on every PUT to one URL would work, but no config
    // can be distinguished from the corrupting one, and an upload that reports success while destroying
    // the evidence is not a trade worth keeping that case for.
    if (step.hasBody && step.chunkSize > 0 && !CarriesChunkOffset(step))
    {
        return {
            Verdict::Refuse,
            L"a chunked <rest> step must place '{upload.range_start}' (or range_end/range_length) in its "
            L"path, query or a header, so each chunk names its offset; otherwise every chunk overwrites "
            L"the previous one and only the last survives"};
    }

    return {Verdict::Ok, {}};
}

Validation ValidateRestSteps(const Upload& upload)
{
    if (upload.ServerName.empty())
    {
        return {Verdict::Refuse, L"REST upload requires a 'server' (e.g. https://host)"};
    }

    if (upload.bitsMode == BITSMode::SMB)
    {
        return {Verdict::Refuse, L"REST upload requires an http(s) 'server', not a file:// share"};
    }

    // Validate the 'server' port at embed time with the same parse the runtime uses, so a malformed port is
    // refused before deployment rather than failing after collection. A host with no ':' has no port to
    // validate (an error is returned only for a present-but-malformed port or an empty host). The default
    // port value is irrelevant here - only success/failure matters.
    if (SplitHostPort(upload.ServerName, INTERNET_DEFAULT_HTTPS_PORT).has_error())
    {
        return {
            Verdict::Refuse, L"REST 'server' has an invalid port (the value after ':' must be a number in 1..65535)"};
    }

    // REST uploads run inline (the agent does the transfer itself); there is no async/background
    // mode like BITS. Refuse mode="async" instead of silently doing sync.
    if (upload.Mode == UploadMode::Asynchronous)
    {
        return {Verdict::Refuse, L"REST upload is synchronous; mode=\"async\" is not supported"};
    }

    // Option A: REST request paths come only from <rest>. Upload-level per-request attributes
    // were recorded by Configure; their presence is a config error (critical at embed), not a
    // silent override.
    if (!upload.rest.RestSupersededAttrs.empty())
    {
        return {
            Verdict::Refuse,
            L"REST <upload> attribute(s) [" + upload.rest.RestSupersededAttrs
                + L"] are superseded by <rest> steps; move them into a <rest> step and remove them from <upload>"};
    }

    if (upload.rest.RestSteps.empty())
    {
        return {Verdict::Refuse, L"REST upload requires at least one <rest> step"};
    }

    std::wstring tmplError;
    for (const auto& header : upload.rest.HttpHeaders)
    {
        if (!CheckTemplatePlaceholders(header, tmplError))
        {
            return {Verdict::Refuse, L"<header>: " + tmplError};
        }
    }

    size_t bodyCount = 0;
    for (const auto& step : upload.rest.RestSteps)
    {
        if (auto v = ValidateRestStep(step, upload.rest.RestSteps); v.verdict != Verdict::Ok)
        {
            return v;
        }

        if (step.hasBody)
        {
            ++bodyCount;
        }
    }

    if (bodyCount == 0)
    {
        return {Verdict::Refuse, L"REST upload needs one <rest> step with a 'body' (the file to send)"};
    }

    if (bodyCount > 1)
    {
        return {Verdict::Refuse, L"REST upload must have exactly one <rest> step with a 'body'"};
    }

    // Permitted (a lab with a self-signed certificate needs it), but say what it costs: a <form>
    // body routinely carries the share password in clear, and 'insecure' turns off the certificate
    // validation that is the only thing keeping a MITM from reading it.
    const bool anyForm = std::any_of(upload.rest.RestSteps.begin(), upload.rest.RestSteps.end(), [](const RESTStep& s) {
        return !s.formFields.empty();
    });
    if (anyForm && upload.rest.Insecure)
    {
        Log::Warn(
            L"REST: 'insecure' is set and a <form> step sends its fields in clear; anyone able to "
            L"intercept the connection reads them and gains the same access");
    }

    return {Verdict::Ok, {}};
}

Validation ValidateRestAuth(const Upload& upload)
{
    const bool isRest = (upload.Method == UploadMethod::REST);

    // AWSv4 signs every request from these three. aws_region has no default: an implicit us-east-1
    // would turn a forgotten attribute into a 403 that names nothing, which is the hardest SigV4
    // failure to diagnose.
    if (isRest && upload.AuthScheme == UploadAuthScheme::AWSv4)
    {
        // Collected once: AllRestHeaders copies every upload-level and per-step header line, and both
        // checks below read the same list.
        const std::vector<std::wstring> restHeaders = ::AllRestHeaders(upload.rest);

        // A literal Authorization header would compete with the one the signer computes, with the
        // outcome depending on header ordering - a configuration whose meaning is accidental.
        for (const auto& header : restHeaders)
        {
            if (equalCaseInsensitive(::HeaderName(header), L"authorization"sv))
            {
                return {
                    Verdict::Refuse, L"authscheme=\"AWSv4\" cannot be combined with a literal 'Authorization' header"};
            }
        }

        // host/x-amz-date/x-amz-content-sha256/x-amz-security-token are owned by the signer (see
        // MakeAwsV4Headers, which already refuses this collision at request time).
        for (const auto& header : restHeaders)
        {
            const std::wstring name = ::HeaderName(header);
            for (const auto& reserved : kAwsSignerOwnedHeaders)
            {
                if (equalCaseInsensitive(name, reserved))
                {
                    return {
                        Verdict::Refuse,
                        L"authscheme=\"AWSv4\" cannot be combined with a literal '" + std::wstring(reserved)
                            + L"' header (the signer sets it)"};
                }
            }
        }

        if (upload.rest.aws.AccessKey.empty())
        {
            return {Verdict::Refuse, L"authscheme=\"AWSv4\" requires an 'access_key'"};
        }

        if (upload.rest.aws.SecretKey.empty())
        {
            return {Verdict::Refuse, L"authscheme=\"AWSv4\" requires a 'secret_key'"};
        }

        if (upload.rest.aws.Region.empty())
        {
            return {Verdict::Refuse, L"authscheme=\"AWSv4\" requires an 'aws_region' (there is no default)"};
        }

        if (upload.rest.aws.Service.empty())
        {
            return {Verdict::Refuse, L"'aws_service' is empty; leave it unset for the \"s3\" default"};
        }
    }

    // Refused, not ignored: a silently dropped credential leaves an upload running anonymously while
    // the configuration says otherwise.
    if (upload.AuthScheme != UploadAuthScheme::AWSv4
        && (!upload.rest.aws.AccessKey.empty() || !upload.rest.aws.SecretKey.empty()
            || !upload.rest.aws.SessionToken.empty()))
    {
        return {Verdict::Refuse, L"'access_key'/'secret_key'/'session_token' require authscheme=\"AWSv4\""};
    }

    // REST auth (fail-closed). REST does standard header-based HTTP auth: authscheme="Basic" (the agent
    // sends a preemptive Authorization: Basic header built from user/password), a token in a literal
    // <header>, or the Windows-integrated schemes (NTLM/Kerberos/Negotiate). The integrated schemes are
    // explicit-credential only - the agent forces WinHTTP auto-logon to HIGH, so the logged-on
    // machine/SYSTEM account is never used and cannot be relayed; they need a 'user'/'password' just
    // like Basic. Refuse a credential-less integrated config here, at embed time, rather than letting
    // it fail closed at runtime.
    if (isRest && upload.UserName.empty()
        && (upload.AuthScheme == UploadAuthScheme::NTLM || upload.AuthScheme == UploadAuthScheme::Kerberos
            || upload.AuthScheme == UploadAuthScheme::Negotiate))
    {
        return {Verdict::Refuse, L"Windows-integrated auth (NTLM/Kerberos/Negotiate) requires a 'user' and 'password'"};
    }

    if (isRest && upload.AuthScheme == UploadAuthScheme::Basic && upload.UserName.empty())
    {
        return {Verdict::Refuse, L"authscheme=\"Basic\" requires a 'user' (and usually a 'password')"};
    }

    return {Verdict::Ok, {}};
}

Validation ValidateProxy(const Upload& upload)
{
    // Every rule here is REST-only: proxy_* settings reach no other uploader, and EmitProxyAdvisories
    // already says so once for the other methods.
    if (upload.Method != UploadMethod::REST)
    {
        return {Verdict::Ok, {}};
    }

    // Proxy (REST). Explicit proxy credentials are only usable if a proxy is actually configured.
    // Refuse a credentials-without-proxy config rather than silently ignoring the credentials. The
    // no-SSO rule is enforced at runtime (AUTOLOGON HIGH); this is the embed-time functional check.
    // Advisory warnings (WPAD spoofable, PAC executed, Basic-over-plaintext) are emitted in Configure().
    // An absent 'proxy' resolves to the machine configuration, but credentials still require the operator
    // to NAME their destination: proxy="system" says out loud that these secrets may be sent to whatever
    // the machine points at. Nothing here is learned implicitly, as with the auth scheme never being taken
    // from a 407. So the credential check demands an explicit, non-DIRECT value, not just any live source.
    const ProxySettings proxyCfg = ClassifyProxy(upload);
    const bool hasProxySource = upload.rest.Proxy.has_value() && proxyCfg.mode != ProxyMode::Direct;
    if ((!upload.rest.ProxyUser.empty() || !upload.rest.ProxyPassword.empty()) && !hasProxySource)
    {
        return {
            Verdict::Refuse,
            L"'proxy_user'/'proxy_password' require a proxy (proxy=\"host:port\", \"system\", \"wpad\", or a "
            L"PAC URL)"};
    }

    // An authenticating proxy must name its scheme (we never learn it from a 407).
    if ((!upload.rest.ProxyUser.empty() || !upload.rest.ProxyPassword.empty())
        && upload.rest.ProxyAuthScheme == UploadAuthScheme::Anonymous)
    {
        return {
            Verdict::Refuse, L"an authenticating proxy requires 'proxy_authscheme' set to Basic, NTLM, or Negotiate"};
    }

    // A proxy password with no user can never be presented (ApplyProxyCredentials no-ops on an empty user,
    // silently dropping the password); refuse it at embed instead of uploading unauthenticated by surprise.
    if (!upload.rest.ProxyPassword.empty() && upload.rest.ProxyUser.empty())
    {
        return {Verdict::Refuse, L"'proxy_password' requires a 'proxy_user'"};
    }

    // server_cert_pin only has meaning over TLS: the pin is matched against the server certificate, which a
    // plain-HTTP connection never presents. Refuse at embed rather than failing every request at runtime
    // (on HTTP the cert query yields no certificate, so the pin can neither pass nor protect the body).
    if (!upload.rest.ServerCertPins.empty() && upload.bitsMode != BITSMode::HTTPS)
    {
        return {
            Verdict::Refuse, L"'server_cert_pin' requires an https 'server' (pinning is meaningless over plain HTTP)"};
    }

    // Every pin must be a certificate the agent can read. One that is not could only ever be skipped, and a
    // mistyped rotation pin would then go unnoticed until the server key rotates onto it. 'insecure' does not
    // authorize it: an unreadable pin is a broken configuration, not a weaker transport.
    for (size_t i = 0; i < upload.rest.ServerCertPins.size(); ++i)
    {
        if (CertBlobToSpkiSha256(upload.rest.ServerCertPins[i]).has_error())
        {
            return {
                Verdict::Refuse,
                L"'server_cert_pin' entry " + std::to_wstring(i + 1) + L" of "
                    + std::to_wstring(upload.rest.ServerCertPins.size())
                    + L" is not an X.509 certificate (PEM or base64 DER), so it could never be matched"};
        }
    }

    return {Verdict::Ok, {}};
}

// retry / retry_delay. Every rule is Refuse, never Warn, for two reasons: ToolEmbed then catches a bad
// policy when the configuration is embedded instead of leaving a surprise at the end of a collection, and
// Validate() returns the FIRST non-Ok verdict, so a Warn here would mask an insecure-transport Refuse
// below. None of this can break a deployed config: the attributes are new, so an existing one carries the
// defaults and stops at the early return.
Validation ValidateRetryPolicy(const Upload& upload)
{
    // Before the Retry == 0 return: an unreadable retry leaves Retry at 0 and would escape through it.
    if (!upload.RetryInvalid.empty())
    {
        return {
            Verdict::Refuse,
            L"upload attribute(s) [" + upload.RetryInvalid
                + L"] must be a whole number, zero or more (attempts for 'retry', seconds for 'retry_delay')"};
    }

    if (upload.Retry == 0)
    {
        return {Verdict::Ok, {}};
    }

    if (upload.Retry > Upload::kMaxRetry)
    {
        return {
            Verdict::Refuse,
            L"'retry' is " + std::to_wstring(upload.Retry) + L", above the " + std::to_wstring(Upload::kMaxRetry)
                + L" extra-attempt maximum"};
    }

    if (upload.RetryDelay.count() > Upload::kMaxRetryDelay)
    {
        return {
            Verdict::Refuse,
            L"'retry_delay' is " + std::to_wstring(upload.RetryDelay.count()) + L" seconds, above the "
                + std::to_wstring(Upload::kMaxRetryDelay) + L" second maximum (the value is in SECONDS)"};
    }

    // The one that matters: the two ceilings above only make the message name the obviously wrong
    // attribute when there is one, but it is their product that the run pays for.
    const uint64_t budget = static_cast<uint64_t>(upload.Retry) * static_cast<uint64_t>(upload.RetryDelay.count());
    if (budget > Upload::kMaxRetryBudgetSeconds)
    {
        return {
            Verdict::Refuse,
            L"'retry' x 'retry_delay' is " + std::to_wstring(budget)
                + L" seconds of waiting at the end of the run for this entry alone, above the "
                + std::to_wstring(Upload::kMaxRetryBudgetSeconds) + L" second budget"};
    }

    // Last, and only if nothing above refused: this is the one WARNING this function can return, and a
    // warning must not pre-empt a refusal. An asynchronous entry hands the transfer to BITS and the process
    // exits before it finishes, so BITSAgent::IsComplete answers S_OK with no failure the moment the agent
    // is ready to exit, without looking at the jobs at all. Nothing ever reports a failure, so there is
    // never anything to retry: the attribute is accepted and ignored rather than refused, because it is
    // harmless and an operator may well have meant it for a fallback entry lower down the same list.
    if (upload.Mode == UploadMode::Asynchronous)
    {
        return {
            Verdict::Warn,
            L"'retry' has no effect with mode=\"async\": an asynchronous upload finishes after DFIR-ORC "
            L"exits, so its outcome is never observed and no attempt can be retried"};
    }

    return {Verdict::Ok, {}};
}

Validation ValidateTransportSecurity(const Upload& upload)
{
    const bool isRest = (upload.Method == UploadMethod::REST);

    // An entry that could not be read has no reliable meaning - "tls1.2[xp" may be an XP exception or a mistyped
    // baseline, "tls1.2[win10]" names no OS ORC knows, "tls1.4" no version it can enable - so it is refused rather
    // than guessed or dropped, by ToolEmbed at embed time and by the same gate at runtime. 'insecure' does not
    // authorize it: it is a configuration error, not a transport choice.
    if (isRest && !upload.rest.HttpsSecurityInvalid.empty())
    {
        return {
            Verdict::Refuse,
            L"https_security entry '" + upload.rest.HttpsSecurityInvalid.front()
                + L"' is not valid (expected \"tlsX.Y\" with X.Y from 1.0 to 1.3, optionally tagged \"[xp]\" or "
                  L"\"[seven]\", or the whole value \"os-default\" or \"os-max\")"};
    }

    // Inherently-insecure transport.
    std::wstring aspect;
    if (upload.bitsMode == BITSMode::HTTP)
    {
        aspect = L"upload over plain HTTP sends collected data in clear text (use HTTPS)";
    }
    else if (isRest)
    {
        uint8_t allowed = upload.rest.TlsVersions;
        for (const auto& [target, versions] : upload.rest.TlsOsExceptions)
        {
            allowed |= versions;
        }

        if (allowed & (Upload::kTls1_0 | Upload::kTls1_1))
        {
            aspect = L"https_security enables TLS 1.0/1.1 (raise the minimum to TLS 1.2)";
        }
    }

    // REST hard-refuses a weak-transport aspect unless 'insecure' authorizes it; other methods (BITS)
    // only warn so their existing behavior is unchanged.
    if (!aspect.empty() && isRest && !upload.rest.Insecure)
    {
        return {Verdict::Refuse, aspect + L"; set insecure=\"true\" to override"};
    }

    // REST over HTTPS: 'insecure' also disables TLS certificate validation outright, in ALL builds
    // (this replaces the former DEBUG-only 'insecure_for_test_only'). Never a silent downgrade - warn
    // whenever it is set, naming the certificate effect plus any weak-transport aspect it covers.
    if (isRest && upload.rest.Insecure && upload.bitsMode == BITSMode::HTTPS)
    {
        std::wstring reason = L"insecure=\"true\" disables TLS certificate validation (prefer 'server_cert_pin')";
        if (!aspect.empty())
        {
            reason += L"; also: " + aspect;
        }

        return {Verdict::Warn, reason};
    }

    if (!aspect.empty())
    {
        return {Verdict::Warn, aspect};
    }

    // os-default hands the protocol to the system policy of whatever host runs the collection, which embedding
    // cannot see. TLS 1.2 is then proven per connection by HttpClient; say so where the choice is made.
    if (isRest && upload.bitsMode == BITSMode::HTTPS && upload.rest.TlsOsDefault && !upload.rest.Insecure)
    {
        return {
            Verdict::Warn,
            L"https_security=\"os-default\" follows the system TLS policy: each connection must negotiate TLS 1.2 or "
            L"newer, or the upload is refused (insecure=\"true\" accepts whatever the system negotiates)"};
    }

    return {Verdict::Ok, {}};
}

}  // namespace

Orc::OutputSpec::Upload::Validation Orc::OutputSpec::Upload::Validate() const
{
    // TODO: These checks are intentionally scoped to REST for now: BITS/FileCopy keep their long-standing
    // behavior (Warn-only, never Refuse). Expand to all upload methods - drop this 'isRest' gating -
    // in a NEXT MAJOR VERSION, since refusing insecure BITS configs that work today would be a
    // breaking change.
    const bool isRest = (Method == UploadMethod::REST);

    // NOTE: no FileCopy/mode="async" rule here. CopyFileAgent never reads Mode (only BITSAgent and
    // WolfLauncher do), so method="filecopy" mode="async" copies fine today; refusing it would break
    // deployed configs - CreateUploadAgent would return nullptr and the collection would upload nothing.

    if (rest.FollowRedirects && !isRest)
    {
        return {Verdict::Refuse, L"'follow_redirects' only applies to REST uploads (method=\"REST\")"};
    }

    // Functional validity (REST): reject a config that cannot work at runtime so the operator is
    // warned at embed time, not after deployment.
    if (isRest)
    {
        if (auto v = ValidateRestSteps(*this); v.verdict != Verdict::Ok)
        {
            return v;
        }
    }

    if (auto v = ValidateRestAuth(*this); v.verdict != Verdict::Ok)
    {
        return v;
    }

    if (auto v = ValidateProxy(*this); v.verdict != Verdict::Ok)
    {
        return v;
    }

    // Held rather than returned: Validate reports the FIRST non-Ok verdict, so surfacing a retry WARNING
    // here would hide the transport REFUSAL below it. A refusal still short-circuits; a warning waits.
    const Validation retryCheck = ValidateRetryPolicy(*this);
    if (retryCheck.verdict == Verdict::Refuse)
    {
        return retryCheck;
    }

    if (const Validation transport = ValidateTransportSecurity(*this); transport.verdict != Verdict::Ok)
    {
        return transport;
    }

    return retryCheck;
}

bool Orc::OutputSpec::Upload::IsFileUploaded(const std::wstring& file_name) const
{
    if (!FilterInclude.empty())
    {
        auto bMatched = false;
        for (const auto& filter : FilterInclude)
        {
            if (PathMatchSpecW(file_name.c_str(), filter.c_str()))
            {
                bMatched = true;
                break;
            }
        }
        if (!bMatched)
            return false;
    }

    for (const auto& filter : FilterExclude)
    {
        if (PathMatchSpecW(file_name.c_str(), filter.c_str()))
            return false;
    }

    return true;
}

OutputSpec::Disposition OutputSpec::ToDisposition(Orc::FileDisposition disposition)
{
    switch (disposition)
    {
        case FileDisposition::Append:
            return OutputSpec::Disposition::Append;
        case FileDisposition::CreateNew:
            return OutputSpec::Disposition::CreateNew;
        case FileDisposition::Truncate:
            return OutputSpec::Disposition::Truncate;
        default:
            assert(nullptr);
            return OutputSpec::Disposition::Append;
    }
}

OutputSpec::Encoding OutputSpec::ToEncoding(Text::Encoding encoding)
{
    switch (encoding)
    {
        case Text::Encoding::Utf8:
            return OutputSpec::Encoding::UTF8;
        case Text::Encoding::Utf16:
            return OutputSpec::Encoding::UTF16;
        default:
            assert(nullptr);
            return OutputSpec::Encoding::kUnknown;
    }
}

Orc::FileDisposition ToFileDisposition(OutputSpec::Disposition disposition)
{
    switch (disposition)
    {
        case OutputSpec::Disposition::Append:
            return FileDisposition::Append;
        case OutputSpec::Disposition::CreateNew:
            return FileDisposition::CreateNew;
        case OutputSpec::Disposition::Truncate:
            return FileDisposition::Truncate;
        default: {
            assert(nullptr);
            return FileDisposition::Unknown;
        }
    }
}

Text::Encoding ToEncoding(OutputSpec::Encoding encoding)
{
    switch (encoding)
    {
        case OutputSpec::Encoding::UTF8:
            return Text::Encoding::Utf8;
        case OutputSpec::Encoding::UTF16:
            return Text::Encoding::Utf16;
        default: {
            assert(nullptr);
            return Text::Encoding::Unknown;
        }
    }
}

}  // namespace Orc
