//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
#include "stdafx.h"

#include "RESTStep.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <wincrypt.h>

#include <boost/algorithm/string.hpp>

#include "Log/Log.h"
#include "Text/CryptString.h"
#include "Text/Fmt/std_error_code.h"
#include "Text/Iconv.h"
#include "Utils/BufferSpan.h"
#include "Utils/Guard.h"
#include "Utils/Result.h"
#include "Utils/String.h"

#pragma comment(lib, "advapi32.lib")

namespace Orc {

namespace {

constexpr unsigned int kMaxRandomLength = 64;

// Character sets for {upload.random:<charset>:...}. Lowercase only (path/host friendly). An empty view
// means the name is unknown: no charset is ever empty.
std::wstring_view RandomCharset(const std::wstring& name)
{
    if (name == L"hex")
    {
        return L"0123456789abcdef";
    }
    if (name == L"num")
    {
        return L"0123456789";
    }
    if (name == L"alpha")
    {
        return L"abcdefghijklmnopqrstuvwxyz";
    }
    if (name == L"alnum")
    {
        return L"abcdefghijklmnopqrstuvwxyz0123456789";
    }

    return {};
}

// The charset and length of a valid "<charset>:<length>[:<id>]" spec.
struct RandomSpec
{
    std::wstring_view charset;
    unsigned int length = 0;
};

// Parse "<charset>:<length>[:<id>]". The id (anything after the second ':') is free-form and ignored
// here - it only differentiates otherwise-identical tokens for caching. std::nullopt on failure, with
// the operator-facing reason in `error`.
std::optional<RandomSpec> ParseRandomSpec(const std::wstring& spec, std::wstring& error)
{
    const size_t firstColon = spec.find(L':');
    if (firstColon == std::wstring::npos)
    {
        error = L"expected {upload.random:<charset>:<length>[:<id>]}";
        return std::nullopt;
    }

    const std::wstring name = spec.substr(0, firstColon);
    const std::wstring_view charset = RandomCharset(name);
    if (charset.empty())
    {
        error = L"unknown charset '" + name + L"' (use hex, num, alpha, or alnum)";
        return std::nullopt;
    }

    const size_t secondColon = spec.find(L':', firstColon + 1);
    const std::wstring lengthText = spec.substr(
        firstColon + 1, (secondColon == std::wstring::npos) ? std::wstring::npos : secondColon - firstColon - 1);

    // Digits only and in range: a sign, a space, or a digit run too long for 64 bits is a rejected spec,
    // never a silently saturated length.
    const std::optional<uint64_t> length = ParseDecimalDigits(lengthText);
    if (!length.has_value() || *length < 1 || *length > kMaxRandomLength)
    {
        error = L"length must be an integer in 1.." + std::to_wstring(kMaxRandomLength);
        return std::nullopt;
    }

    return RandomSpec {charset, static_cast<unsigned int>(*length)};
}

// Fill `out` with `length` characters drawn uniformly (rejection sampling, no modulo bias) from `charset`
// using the system CSPRNG (CryptGenRandom - available down to Windows XP). Empty on RNG failure.
std::wstring GenerateRandom(std::wstring_view charset, unsigned int length)
{
    const unsigned int n = static_cast<unsigned int>(charset.size());
    if (n == 0 || length == 0)
    {
        return {};
    }

    HCRYPTPROV provider = 0;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
    {
        Log::Debug(L"GenerateRandom: CryptAcquireContext failed [{}]", LastWin32Error());
        return {};
    }

    const auto release = Guard::CreateScopeGuard([provider]() { CryptReleaseContext(provider, 0); });

    const unsigned int limit = (256u / n) * n;  // reject bytes >= limit so charset[byte % n] is unbiased

    std::wstring out;
    out.reserve(length);

    // The span carries the size, so the refill length and the byte count can no longer drift apart, and
    // indexing it is checked rather than trusted.
    std::array<uint8_t, 128> storage {};
    const BufferSpan buffer(storage.data(), storage.size());

    size_t available = 0;
    size_t pos = 0;
    while (out.size() < length)
    {
        if (pos >= available)
        {
            if (!CryptGenRandom(provider, static_cast<DWORD>(buffer.size()), buffer.data()))
            {
                Log::Debug(L"GenerateRandom: CryptGenRandom failed [{}]", LastWin32Error());
                return {};
            }
            available = buffer.size();
            pos = 0;
        }

        const uint8_t b = buffer[pos++];
        if (b >= limit)
        {
            continue;  // rejection
        }

        out.push_back(charset[b % n]);
    }

    return out;
}

// True if 's' holds a control character (< 0x20 or DEL). CR/LF in a path splits the request line;
// in a header value it injects a header. Neither has a legitimate place in either.
bool HasControlCharacter(const std::wstring& s)
{
    return std::any_of(s.begin(), s.end(), [](wchar_t c) { return c < 0x20 || c == 0x7F; });
}

// Scan bounds. An auth page carries a handful of inputs in tags of a few hundred bytes; these caps
// are far above anything legitimate and exist only so a hostile body cannot make the scan expensive.
constexpr size_t kMaxInputTagChars = 8192;
constexpr size_t kMaxInputTagsScanned = 1024;

// Decode the basic markup entities in ONE pass. Single-pass on purpose: a repeated decode would
// let "&amp;lt;" collapse to "<", so a server could smuggle a character the caller's later checks
// (control characters, path separators) had already cleared the value of.
//
// Serves both the HTML form-input scan and the DAV property scan, so the table is the union of what
// each needs: XML's five predefined entities (&amp; &lt; &gt; &quot; &apos;) plus "&#39;", HTML's
// usual numeric spelling of the apostrophe. HTML5 defines &apos; too, so the union is correct for
// both rather than a concession by either.
std::wstring DecodeBasicMarkupEntities(std::wstring_view s)
{
    struct Entity
    {
        std::wstring_view text;
        wchar_t ch;
    };

    // Views, so each entity's length is known at compile time rather than measured with wcslen() on
    // every '&' of a server-controlled body.
    constexpr Entity kEntities[] = {
        {L"&amp;", L'&'},
        {L"&lt;", L'<'},
        {L"&gt;", L'>'},
        {L"&quot;", L'"'},
        {L"&apos;", L'\''},
        {L"&#39;", L'\''}};

    std::wstring out;
    out.reserve(s.size());

    while (!s.empty())
    {
        // substr() clamps, so an entity longer than what is left simply does not compare equal.
        const auto matches = [s](const Entity& e) { return s.substr(0, e.text.size()) == e.text; };
        const auto* const hit = s.front() != L'&'
            ? std::end(kEntities)
            : std::find_if(std::begin(kEntities), std::end(kEntities), matches);

        if (hit == std::end(kEntities))
        {
            out.push_back(s.front());
            s.remove_prefix(1);
            continue;
        }

        out.push_back(hit->ch);

        // Past the whole entity, never back into what it decoded to: this is the single pass that
        // keeps "&amp;lt;" at "&lt;".
        s.remove_prefix(hit->text.size());
    }

    return out;
}

// One attribute of a tag. Both halves are views into the tag text the walk was handed: nothing is
// copied, and no name is folded into a lowercased key. They stay valid only as long as that text does.
struct QuotedAttribute
{
    std::wstring_view name;
    std::wstring_view value;
};

// ASCII whitespace as HTML defines it: TAB, LF, FF, CR and SPACE. Not iswspace(), which is
// locale-dependent and in a non-C locale separates on characters a browser does not (NBSP and friends) -
// the tag would then be parsed into different attributes than the browser parsed it into, which is the
// same "honour what a browser ignores" hazard StripMarkupComments exists to close. Vertical tab is left out
// for that same reason: HTML does not count it as whitespace, and iswspace() does.
constexpr bool IsHtmlSpace(wchar_t c) noexcept
{
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\f' || c == L'\r';
}

// Every quoted attribute of one tag's text, in a SINGLE sequential walk that tracks whether the
// current position is inside a quoted value. In tag order, with names and values both kept exactly as
// written (see FindAttribute for the case-insensitive lookup).
//
// Walking sequentially - rather than independently `find`-ing each attribute's name anywhere in the
// tag - is what keeps one attribute's quoted value from impersonating another attribute: a tag such
// as `<input type=" name='requesttoken' value='ATTACKER'" name="other" value="ignored">` must yield
// name=="other" / value=="ignored", not the strings hiding inside `type`'s own quotes. Once inside a
// quoted value the walk only looks for that value's closing quote, so a "name=" or "value=" spelled
// out in there is never seen as a new attribute.
//
// An unquoted or unterminated value stops the walk rather than guessing where it ends: everything
// parsed before it is kept, nothing after it is attempted.
std::vector<QuotedAttribute> ReadQuotedAttributes(std::wstring_view tag)
{
    std::vector<QuotedAttribute> attrs;

    size_t pos = 0;
    while (pos < tag.size())
    {
        while (pos < tag.size() && IsHtmlSpace(tag[pos]))
        {
            ++pos;
        }

        if (pos >= tag.size() || tag[pos] == L'>' || tag[pos] == L'/')
        {
            break;  // end of the tag, or the '/' of a self-closing "/>"
        }

        const size_t nameStart = pos;
        while (pos < tag.size() && !IsHtmlSpace(tag[pos]) && tag[pos] != L'=' && tag[pos] != L'>' && tag[pos] != L'/')
        {
            ++pos;
        }

        const std::wstring_view name = tag.substr(nameStart, pos - nameStart);

        while (pos < tag.size() && IsHtmlSpace(tag[pos]))
        {
            ++pos;
        }

        if (pos >= tag.size() || tag[pos] != L'=')
        {
            continue;  // a bare attribute with no value (e.g. "hidden", "disabled"): nothing to record
        }

        ++pos;  // past '='
        while (pos < tag.size() && IsHtmlSpace(tag[pos]))
        {
            ++pos;
        }

        if (pos >= tag.size())
        {
            break;  // '=' at the very end of the tag
        }

        const wchar_t quote = tag[pos];
        if (quote != L'"' && quote != L'\'')
        {
            break;  // unquoted value: refuse to guess its end, and stop rather than misparse the rest
        }

        const size_t closing = tag.find(quote, pos + 1);
        if (closing == std::wstring_view::npos)
        {
            break;  // unterminated quoted value
        }

        attrs.push_back({name, tag.substr(pos + 1, closing - pos - 1)});
        pos = closing + 1;
    }

    return attrs;
}

// The FIRST attribute of that name, matched with an ASCII fold - HTML attribute names are ASCII
// case-insensitive, and folding any wider would let a name the page never wrote match the one we look
// for. FIRST, not last: that is what the std::map this replaced recorded (emplace keeps the earlier of
// two equal keys), and what a browser does with a duplicated attribute.
std::optional<std::wstring_view> FindAttribute(const std::vector<QuotedAttribute>& attrs, std::wstring_view name)
{
    const auto matches = [name](const QuotedAttribute& attr) {
        return EqualsAsciiCaseInsensitive(attr.name, name);
    };

    const auto it = std::find_if(attrs.begin(), attrs.end(), matches);
    if (it == attrs.end())
    {
        return std::nullopt;
    }

    return it->value;
}

// The body with every <!-- ... --> removed, so an element a real parser ignores can never be the one
// we honour. An unterminated comment swallows the remainder, which is what a browser does too.
//
// Serves both the HTML form-input scan and the DAV property scan: an XML comment is the same
// <!-- --> syntax, and an XML parser also treats an unterminated one as running to the end.
//
// Conservative in the safe direction where it differs from a browser: a browser also closes a comment
// on "--!>", this only closes on "-->", so it strips MORE than the browser did. The cost of that is a
// capture that is not found and an upload that stops, never an attacker-chosen input being honoured.
std::wstring StripMarkupComments(std::wstring_view markup)
{
    std::wstring out;
    out.reserve(markup.size());

    while (!markup.empty())
    {
        const size_t open = markup.find(L"<!--");
        if (open == std::wstring_view::npos)
        {
            out.append(markup);
            break;
        }

        out.append(markup.substr(0, open));
        markup.remove_prefix(open + 4);

        const size_t close = markup.find(L"-->");
        if (close == std::wstring_view::npos)
        {
            break;  // unterminated comment: the rest of the body is comment
        }

        markup.remove_prefix(close + 3);
    }

    return out;
}

// A capture value's length cap. A real token is tens of characters; this is far above anything
// legitimate and exists so a body-derived value cannot grow a request without bound.
constexpr size_t kMaxCaptureValueChars = 4096;

// A sequential walk over the tags of a markup body, shared by the form-input scan and the attribute
// scan so the bounds and the '<'-to-'>' arithmetic exist once rather than twice.
struct TagWalk
{
    std::wstring_view scan;   // the body, comments already stripped
    size_t pos = 0;           // where the next search starts
    size_t tagsExamined = 0;  // tags handed back so far, against kMaxInputTagsScanned
    bool refused = false;     // a bound was crossed: discard the body whole, never a partial result
};

// The next tag the caller cares about, as the text from '<' to '>' inclusive, leaving walk.pos past
// it. 'tagName' selects one element (L"input"); empty selects every tag, which is what an attribute
// capture needs because any element can carry the attribute.
//
// nullopt ends the walk, and walk.refused says why: true means a bound was crossed and the caller
// must discard the whole body rather than trust the part it already has, false means the body simply
// ran out. 'label' names the scan in the log and is always a literal at the call site, so no
// server-controlled text ever reaches a format string.
//
// Only the tags actually RETURNED count against the cap, so each scan's budget is its own tags: a
// form-input scan is bounded by the page's <input> tags exactly as it was before this was factored
// out, and a page thick with other markup is not refused for a scan that would have walked four tags.
std::optional<std::wstring_view> NextTag(TagWalk& walk, std::wstring_view tagName, std::wstring_view label)
{
    while (walk.pos < walk.scan.size())
    {
        const size_t open = walk.scan.find(L'<', walk.pos);
        if (open == std::wstring_view::npos)
        {
            walk.pos = walk.scan.size();
            return std::nullopt;
        }

        if (!tagName.empty())
        {
            // Tag NAMES are case-insensitive in HTML - over ASCII, which is the whole of its grammar.
            // Matching them with an ASCII fold, rather than lowercasing a copy of the body and
            // searching that, keeps the scan on one string (no second copy of a body that reaches
            // kMaxCaptureBodyBytes) and keeps a locale out of a decision taken on server-controlled
            // bytes: towlower() defers to LCMapStringW for non-ASCII, and under a Turkish LC_CTYPE
            // that folds U+0130 to 'i', so "<INPUT ...>" spelled with a dotted capital I - an unknown
            // element a browser ignores - would have become the "<input" this looks for. VALUES are
            // always taken from this same string, so they keep their case either way.
            //
            // substr() clamps, so a '<' in the last few characters simply does not compare equal.
            if (!EqualsAsciiCaseInsensitive(walk.scan.substr(open + 1, tagName.size()), tagName))
            {
                walk.pos = open + 1;
                continue;
            }

            // Require a delimiter right after the tag name, so "<inputx ...>" or "<input_hidden ...>" -
            // which a browser treats as an unknown element and ignores - is not read as "<input ...>".
            const size_t afterName = open + 1 + tagName.size();
            const bool hasDelimiter = afterName < walk.scan.size()
                && (IsHtmlSpace(walk.scan[afterName]) || walk.scan[afterName] == L'>' || walk.scan[afterName] == L'/');
            if (!hasDelimiter)
            {
                walk.pos = open + 1;
                continue;
            }
        }

        if (++walk.tagsExamined > kMaxInputTagsScanned)
        {
            Log::Debug(L"{}: over {} tags; refusing the body", label, kMaxInputTagsScanned);
            walk.refused = true;
            return std::nullopt;
        }

        const size_t close = walk.scan.find(L'>', open);
        if (close == std::wstring_view::npos)
        {
            Log::Debug(L"{}: unterminated tag; refusing the body", label);
            walk.refused = true;
            return std::nullopt;
        }

        // close >= open, so the subtraction cannot underflow.
        const size_t tagLength = close - open + 1;
        if (tagLength > kMaxInputTagChars)
        {
            Log::Debug(L"{}: a tag exceeds {} characters; refusing the body", label, kMaxInputTagChars);
            walk.refused = true;
            return std::nullopt;
        }

        walk.pos = close + 1;

        // A view into walk.scan, which the caller owns for the whole walk: the attributes read out of
        // it are views in turn and must not outlive it.
        return walk.scan.substr(open, tagLength);
    }

    return std::nullopt;
}

}  // namespace

std::set<uint32_t> ParseAcceptStatus(const std::wstring& csv)
{
    std::set<uint32_t> out;

    std::vector<std::wstring> tokens;
    boost::split(tokens, csv, boost::is_any_of(L",;"));
    for (auto& t : tokens)
    {
        boost::trim(t);
        if (t.empty())
        {
            continue;
        }

        wchar_t* end = nullptr;
        const unsigned long v = wcstoul(t.c_str(), &end, 10);
        if (end && *end == L'\0' && v >= 100 && v <= 599)
        {
            out.insert(static_cast<uint32_t>(v));
        }
    }

    return out;
}

bool IsAcceptableStatus(uint32_t status, const std::set<uint32_t>& acceptStatus)
{
    if (status >= 200 && status < 300)
    {
        return true;
    }

    return acceptStatus.find(status) != acceptStatus.end();
}

uint64_t ResolveOffset(const std::wstring& substituted, uint64_t total)
{
    std::wstring s = substituted;
    boost::trim(s);

    // Anything but a plain number (empty, a sign, a space, a digit run past 64 bits) restarts from the
    // beginning: re-sending costs bandwidth, where honouring a bogus "already at N" would skip data. Note
    // this is deliberately not the old saturating parse, which turned an overflowing value into 'total'
    // and let a hostile or broken server suppress the upload entirely.
    const std::optional<uint64_t> offset = ParseDecimalDigits(s);
    if (!offset.has_value())
    {
        Log::Debug(L"Resume offset '{}' is not a plain number; restarting from the beginning", s);
        return 0;
    }

    return (std::min)(*offset, total);
}

#if ORC_REST_ENABLE_DAV_PROPERTY
std::wstring ExtractDavProperty(const std::wstring& xmlBody, const std::wstring& localName)
{
    // Element names (incl. an optional "ns:" prefix) are short, so bound the per-tag prefix scan. An
    // unbounded scan for the name-ending delimiter would be O(n^2) on attacker-controlled XML (a 207 body
    // of many '<' before any delimiter), a CPU-exhaustion DoS that the response-body size cap cannot bound.
    constexpr size_t kMaxTagNameScan = 256;

    // Comments first, for the same reason the form-input scan strips them: an element a real parser
    // ignores must never be the one we honour. A property spelled inside <!-- --> is not a property.
    const std::wstring body = StripMarkupComments(xmlBody);

    // Find an opening tag whose local name == localName: either "<localName" or "<ns:localName".
    // Exact compare, never a case fold: XML element names are case-SENSITIVE, unlike the HTML tag
    // names the form-input scan matches.
    auto findOpen = [&](size_t from) -> size_t {
        size_t pos = from;
        while ((pos = body.find(L'<', pos)) != std::wstring::npos)
        {
            const size_t nameStart = pos + 1;

            // Locate the first name-token delimiter within a bounded window; only a ':' (a namespace
            // prefix) shifts where the local name starts. Equivalent to scanning for the first of
            // " \t\r\n>:/" but capped so a delimiter-free run of '<' cannot drive quadratic work.
            const size_t scanEnd = (std::min)(body.size(), nameStart + kMaxTagNameScan);
            size_t localStart = nameStart;
            for (size_t i = nameStart; i < scanEnd; ++i)
            {
                const wchar_t c = body[i];
                if (c == L':')
                {
                    localStart = i + 1;  // namespace prefix: the local name starts after the colon
                    break;
                }

                if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'>' || c == L'/')
                {
                    break;  // name token ended with no prefix colon
                }
            }

            if (localStart + localName.size() <= body.size()
                && body.compare(localStart, localName.size(), localName) == 0)
            {
                const wchar_t after = body[localStart + localName.size()];  // size() => L'\0', safe
                if (after == L'>' || after == L' ' || after == L'\t' || after == L'/' || after == L'\r'
                    || after == L'\n')
                {
                    return pos;
                }
            }
            pos = nameStart;
        }
        return std::wstring::npos;
    };

    const size_t open = findOpen(0);
    if (open == std::wstring::npos)
    {
        return {};
    }

    const size_t gt = body.find(L'>', open);
    if (gt == std::wstring::npos || (gt > 0 && body[gt - 1] == L'/'))
    {
        return {};  // malformed or self-closed (<x/>) => no text content
    }

    const size_t close = body.find(L'<', gt + 1);
    if (close == std::wstring::npos)
    {
        return {};
    }

    std::wstring text = body.substr(gt + 1, close - gt - 1);
    boost::trim(text);

    // Trim BEFORE decoding, so the decode can never produce leading or trailing whitespace that then
    // goes untrimmed. No entity in the table decodes to whitespace today, so the two orders agree; this
    // is the one that stays correct if a numeric escape is ever added. The decode is single-pass, so a
    // value carrying "&amp;lt;" arrives as "&lt;" rather than collapsing to "<".
    return DecodeBasicMarkupEntities(text);
}
#endif  // ORC_REST_ENABLE_DAV_PROPERTY

std::map<std::wstring, std::wstring>
ExtractFormInputValues(const std::wstring& html, const std::vector<std::wstring>& inputNames)
{
    std::map<std::wstring, std::wstring> found;

    // Nothing wanted, nothing read. A step with no form_input capture must not pay for a scan and
    // must not touch server-controlled bytes it has no use for.
    if (inputNames.empty())
    {
        return found;
    }

    // How many DISTINCT names are still outstanding. Counting distinct names (not vector entries)
    // is what makes the early stop correct when a caller asks for the same name twice.
    std::set<std::wstring> wanted(inputNames.begin(), inputNames.end());

    // 'body' owns every character the views below point into, and it lives until this function
    // returns - well past the walk and past the last use of any value.
    const std::wstring body = StripMarkupComments(html);
    TagWalk walk {body};

    while (!wanted.empty())
    {
        const std::optional<std::wstring_view> tag = NextTag(walk, L"input", L"Form input scan");
        if (!tag)
        {
            if (walk.refused)
            {
                return {};  // a bound was crossed: no partial result from a body we refused
            }

            break;  // the body ran out
        }

        // ONE sequential walk of this tag's attributes, so 'name' and 'value' cannot be read out of
        // each other's quoted strings (see ReadQuotedAttributes).
        const std::vector<QuotedAttribute> attrs = ReadQuotedAttributes(*tag);

        const std::optional<std::wstring_view> nameAttr = FindAttribute(attrs, L"name");
        if (!nameAttr)
        {
            continue;  // unquoted or absent 'name': skip this tag rather than guess
        }

        // The one allocation this tag still pays for: 'wanted' and 'found' are keyed by std::wstring.
        const std::wstring name(*nameAttr);
        const std::set<std::wstring>::iterator want = wanted.find(name);
        if (want == wanted.end())
        {
            continue;  // not a name we were asked for, or already resolved (first occurrence wins)
        }

        const std::optional<std::wstring_view> valueAttr = FindAttribute(attrs, L"value");
        if (!valueAttr)
        {
            Log::Debug(L"Form input scan: '{}' has no quoted 'value' attribute", name);
            wanted.erase(want);  // it was sent, but unusably: do not keep scanning for another one
            continue;
        }

        found.emplace(name, DecodeBasicMarkupEntities(*valueAttr));

        // Resolved. Dropping it from 'wanted' both empties the loop condition once everything is
        // in (the early stop) and makes any later duplicate of this name a tag we walk straight
        // past, so the first occurrence is the one that counts.
        wanted.erase(want);
    }

    for (const auto& missing : wanted)
    {
        Log::Debug(L"Form input scan: '{}' not found in the response body", missing);
    }

    return found;
}

std::optional<std::wstring> ExtractFormInputValue(const std::wstring& html, const std::wstring& inputName)
{
    if (inputName.empty())
    {
        Log::Debug(L"Form input scan: empty input name requested");
        return std::nullopt;
    }

    const std::map<std::wstring, std::wstring> values = ExtractFormInputValues(html, {inputName});

    const std::map<std::wstring, std::wstring>::const_iterator it = values.find(inputName);
    if (it == values.end())
    {
        return std::nullopt;
    }

    return it->second;
}

std::map<std::wstring, std::wstring>
ExtractAttributeValues(const std::wstring& html, const std::vector<std::wstring>& attributeNames)
{
    std::map<std::wstring, std::wstring> found;

    // Nothing wanted, nothing read. A step with no attribute capture must not pay for a scan and must
    // not touch server-controlled bytes it has no use for.
    if (attributeNames.empty())
    {
        return found;
    }

    std::set<std::wstring> wanted(attributeNames.begin(), attributeNames.end());

    const std::wstring body = StripMarkupComments(html);
    TagWalk walk {body};

    while (!wanted.empty())
    {
        const std::optional<std::wstring_view> tag = NextTag(walk, L"", L"Attribute scan");
        if (!tag)
        {
            if (walk.refused)
            {
                return {};  // a bound was crossed: no partial result from a body we refused
            }

            break;  // the body ran out
        }

        // The same sequential attribute walk the form-input scan uses, so one attribute's quoted value
        // can never impersonate another (see ReadQuotedAttributes).
        const std::vector<QuotedAttribute> attrs = ReadQuotedAttributes(*tag);

        // Every attribute of this tag against every name still outstanding. Both are a handful, and
        // taking them together lets one element resolve several captures at once - which is the normal
        // case, not a contrived one: a page can carry two tokens on the same tag.
        for (const QuotedAttribute& attr : attrs)
        {
            const std::set<std::wstring>::iterator want =
                std::find_if(wanted.begin(), wanted.end(), [&attr](const std::wstring& name) {
                    return EqualsAsciiCaseInsensitive(attr.name, name);
                });
            if (want == wanted.end())
            {
                continue;  // not a name we were asked for, or already resolved (first occurrence wins)
            }

            // Keyed by the CALLER's spelling, not the page's: the match is case-insensitive, so the
            // page's casing is not what the caller will look the value up by.
            found.emplace(*want, DecodeBasicMarkupEntities(attr.value));

            // Resolved. Dropping it empties the loop condition once everything is in (the early stop)
            // and makes any later occurrence of this name one we walk straight past, so the first is
            // the one that counts.
            wanted.erase(want);
        }
    }

    for (const auto& missing : wanted)
    {
        Log::Debug(L"Attribute scan: '{}' not found in the response body", missing);
    }

    return found;
}

std::optional<std::wstring> ExtractAttributeValue(const std::wstring& html, const std::wstring& attributeName)
{
    if (attributeName.empty())
    {
        Log::Debug(L"Attribute scan: empty attribute name requested");
        return std::nullopt;
    }

    const std::map<std::wstring, std::wstring> values = ExtractAttributeValues(html, {attributeName});

    const std::map<std::wstring, std::wstring>::const_iterator it = values.find(attributeName);
    if (it == values.end())
    {
        return std::nullopt;
    }

    return it->second;
}

bool IsSupportedAttributeName(const std::wstring& name)
{
    // Long enough for any real attribute (data-requesttoken is 17) and short enough that a config
    // typo cannot turn into a long scan key.
    constexpr size_t kMaxAttributeNameChars = 64;

    if (name.empty())
    {
        Log::Debug(L"REST <response attribute>: the attribute name is empty");
        return false;
    }

    if (name.size() > kMaxAttributeNameChars)
    {
        Log::Debug(L"REST <response attribute>: the attribute name exceeds {} characters", kMaxAttributeNameChars);
        return false;
    }

    const auto isNameChar = [](wchar_t c) {
        return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-'
            || c == L'_' || c == L':' || c == L'.';
    };

    if (!std::all_of(name.begin(), name.end(), isNameChar))
    {
        Log::Debug(L"REST <response attribute>: the attribute name holds a character the scan cannot match");
        return false;
    }

    return true;
}

std::optional<std::string> UrlEncodeFormValue(const std::wstring& value)
{
    // The form body is bytes, and a non-ASCII value (an accented password, a unicode filename) has
    // to be UTF-8 before it can be percent-encoded at all. Fail closed on a conversion failure: an
    // empty return here would previously have posted a password field as silently empty, exactly the
    // silent misbehaviour this feature exists to avoid.
    std::error_code ec;
    const std::string utf8 = ToUtf8(value, ec);
    if (ec)
    {
        Log::Debug(L"Form value could not be encoded as UTF-8 [{}]", ec);
        return std::nullopt;
    }

    static const char kHex[] = "0123456789ABCDEF";

    std::string out;
    out.reserve(utf8.size());

    for (const char c : utf8)
    {
        const unsigned char byte = static_cast<unsigned char>(c);
        const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')
            || (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' || byte == '_' || byte == '~';

        if (unreserved)
        {
            out.push_back(static_cast<char>(byte));
            continue;
        }

        out.push_back('%');
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0x0F]);
    }

    return out;
}

std::optional<std::string> BuildFormBody(const std::vector<std::pair<std::wstring, std::wstring>>& fields)
{
    std::string body;

    for (const auto& field : fields)
    {
        std::error_code ec;
        const std::string name = ToUtf8(field.first, ec);
        if (ec)
        {
            // Fail closed: a name that cannot be encoded must refuse the WHOLE body, not silently
            // drop the field (which would leave a dangling '&' from the separator below, or - worse -
            // post the remaining fields as if the dropped one had never been asked for).
            Log::Debug(L"Form field name could not be encoded as UTF-8 [{}]", ec);
            return std::nullopt;
        }

        const std::optional<std::string> value = UrlEncodeFormValue(field.second);
        if (!value.has_value())
        {
            return std::nullopt;  // UrlEncodeFormValue already logged the cause
        }

        if (!body.empty())
        {
            body.push_back('&');
        }

        body.append(name);
        body.push_back('=');
        body.append(*value);
    }

    return body;
}

bool IsValidRestRandomSpec(const std::wstring& spec, std::wstring& error)
{
    return ParseRandomSpec(spec, error).has_value();
}

std::wstring MakeRestRandom(const std::wstring& spec)
{
    std::wstring error;
    const std::optional<RandomSpec> parsed = ParseRandomSpec(spec, error);
    if (!parsed.has_value())
    {
        // Unreachable for an embedded configuration (IsValidRestRandomSpec refuses the spec at embed), so
        // log the reason rather than substituting an unexplained empty value.
        Log::Debug(L"Invalid '{{upload.random:{}}}' specification: {}", spec, error);
        return {};
    }

    return GenerateRandom(parsed->charset, parsed->length);
}

bool ScanRestRandomSpecs(const std::wstring& s, std::vector<std::wstring>& specs, std::wstring& error)
{
    static const std::wstring prefix = L"{upload.random:";
    size_t pos = 0;
    while ((pos = s.find(prefix, pos)) != std::wstring::npos)
    {
        const size_t end = s.find(L'}', pos);
        if (end == std::wstring::npos)
        {
            error = L"unterminated '{upload.random:...}' placeholder";
            return false;
        }

        specs.push_back(s.substr(pos + prefix.size(), end - pos - prefix.size()));
        pos = end + 1;
    }

    return true;
}

// The fixed {upload.*} built-in placeholder names - the SINGLE source shared by IsKnownUploadPlaceholder
// (validation) and ResolveUploadTemplate (substitution) below, so the two cannot drift. A name missing
// here fails LOUD at embed (a valid config is refused) rather than silently.
static const wchar_t* const kUploadBuiltins[] = {
    L"upload.range_start",
    L"upload.range_length",
    L"upload.range_end",
    L"upload.total_size",
    L"upload.filename",
    L"upload.filename_base64",
    L"upload.local_path",
};

bool IsReservedUploadName(const std::wstring& name)
{
    return name.rfind(L"upload.", 0) == 0;
}

std::wstring RedactedCaptureMarker(const std::wstring& name)
{
    return L"[redacted:" + name + L"]";
}

std::map<std::wstring, std::wstring> MaskCapturesForLog(const std::map<std::wstring, std::wstring>& captures)
{
    std::map<std::wstring, std::wstring> masked;

    for (const std::pair<const std::wstring, std::wstring>& capture : captures)
    {
        // An 'upload.' key can only be an engine-generated {upload.random:...} value: embed-time
        // validation refuses a <response> capture under that prefix. It names no secret and says which
        // upload directory a request used, so it is kept.
        if (IsReservedUploadName(capture.first))
        {
            masked.emplace(capture.first, capture.second);
            continue;
        }

        // Everything else came from a server response. Substitute a marker built from the capture's
        // NAME, which is operator configuration; the value itself is never copied anywhere.
        masked.emplace(capture.first, RedactedCaptureMarker(capture.first));
    }

    return masked;
}

bool IsKnownUploadPlaceholder(const std::wstring& token)
{
    // {upload.random:<spec>} is validated separately (ScanRestRandomSpecs / IsValidRestRandomSpec).
    if (token.rfind(L"upload.random:", 0) == 0)
    {
        return true;
    }

    return std::any_of(
        std::begin(kUploadBuiltins), std::end(kUploadBuiltins), [&token](const wchar_t* b) { return token == b; });
}

bool IsReadOnlyRestMethod(const std::wstring& method)
{
    for (const wchar_t* readOnly : {L"HEAD", L"GET", L"OPTIONS", L"PROPFIND"})
    {
        if (boost::iequals(method, readOnly))
        {
            return true;
        }
    }

    return false;
}

bool UsesFileBoundPlaceholder(const std::wstring& tmpl)
{
    for (const std::wstring& token : ScanPlaceholderTokens(tmpl))
    {
        if (token == L"upload.local_path" || token == L"upload.total_size"
            || boost::starts_with(token, L"upload.range_") || boost::starts_with(token, L"upload.random:"))
        {
            return true;
        }
    }

    return false;
}

bool UsesPerFilePlaceholder(const std::wstring& tmpl)
{
    if (UsesFileBoundPlaceholder(tmpl))
    {
        return true;
    }

    for (const std::wstring& token : ScanPlaceholderTokens(tmpl))
    {
        if (token == L"upload.filename" || token == L"upload.filename_base64")
        {
            return true;
        }
    }

    return false;
}

std::optional<uint64_t> ParseRemoteSize(const std::wstring& substituted)
{
    std::wstring s = substituted;
    boost::trim(s);

    // Digits only and no overflow, so an unreadable size cannot pass for a small one.
    return ParseDecimalDigits(s);
}

std::vector<std::wstring> ScanPlaceholderTokens(const std::wstring& s)
{
    std::vector<std::wstring> tokens;
    size_t pos = 0;
    while ((pos = s.find(L'{', pos)) != std::wstring::npos)
    {
        const size_t end = s.find(L'}', pos + 1);
        if (end == std::wstring::npos)
        {
            break;  // unterminated '{' - not a placeholder
        }

        tokens.push_back(s.substr(pos + 1, end - pos - 1));
        pos = end + 1;
    }

    return tokens;
}

// base64(UTF-8(text)) for {upload.filename_base64} (e.g. a TUS "Upload-Metadata: filename <base64>"
// header). Empty on any conversion failure: the caller substitutes empty, failing the metadata closed.
static std::wstring Utf8Base64(const std::wstring& text)
{
    if (text.empty())
    {
        return {};
    }

    const int cb =
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (cb <= 0)
    {
        return {};
    }

    std::vector<BYTE> utf8(static_cast<size_t>(cb));
    if (WideCharToMultiByte(
            CP_UTF8,
            0,
            text.c_str(),
            static_cast<int>(text.size()),
            reinterpret_cast<LPSTR>(utf8.data()),
            cb,
            nullptr,
            nullptr)
        != cb)
    {
        return {};
    }

    auto b64 = CryptBinaryToSingleLineString(BufferView(utf8.data(), utf8.size()), CRYPT_STRING_BASE64);
    if (b64.has_error())
    {
        return {};  // CryptBinaryToSingleLineString logged the cause
    }

    return *b64;
}

// Value of a single {token}. Engine built-ins under the reserved 'upload.' namespace are resolved first, so
// an operator capture can never shadow one. This set MUST match kUploadBuiltins above - both live here so
// they cannot drift. std::nullopt for an unknown name: the caller then leaves the token verbatim.
static std::optional<std::wstring> ResolveUploadToken(
    const std::wstring& name,
    const UploadTemplateContext& ctx,
    const std::map<std::wstring, std::wstring>& captures)
{
    if (name == L"upload.range_start")
    {
        return std::to_wstring(ctx.offset);  // chunk start byte offset
    }

    if (name == L"upload.range_length")
    {
        return std::to_wstring(ctx.length);  // this chunk's byte count
    }

    if (name == L"upload.total_size")
    {
        return std::to_wstring(ctx.total);  // whole-file size
    }

    // {upload.range_end}: last byte index of the chunk (range_start + range_length - 1). A primitive, so a
    // template can compose any range syntax, e.g.
    // "Content-Range: bytes {upload.range_start}-{upload.range_end}/{upload.total_size}".
    if (name == L"upload.range_end")
    {
        return std::to_wstring((ctx.length > 0) ? (ctx.offset + ctx.length - 1) : ctx.offset);
    }

    if (name == L"upload.filename")
    {
        return ctx.filename;  // basename of the remote name
    }

    if (name == L"upload.filename_base64")
    {
        return Utf8Base64(ctx.filename);  // base64(UTF-8(filename)), computed only when referenced
    }

    if (name == L"upload.local_path")
    {
        return ctx.inputPath;  // empty for a non-file (stream/buffer) source
    }

    // Operator <response> captures (free-form {name}); the {upload.random:...} values are pre-resolved once
    // per file into this same map.
    const auto capture = captures.find(name);
    if (capture != captures.end())
    {
        return capture->second;
    }

    return std::nullopt;
}

std::wstring ResolveUploadTemplate(
    const std::wstring& tmpl,
    const UploadTemplateContext& ctx,
    const std::map<std::wstring, std::wstring>& captures)
{
    // ONE pass over the template: literal text and substituted values are appended to the output and never
    // rescanned. A capture value is server-controlled, so a repeated-replacement scheme would let it carry a
    // "{other_capture}" token that a later substitution then expands. Building the result once closes that,
    // for captures as well as built-ins, and makes the outcome independent of substitution order.
    std::wstring result;
    result.reserve(tmpl.size());

    size_t pos = 0;
    while (pos < tmpl.size())
    {
        const size_t open = tmpl.find(L'{', pos);
        const size_t close = (open == std::wstring::npos) ? std::wstring::npos : tmpl.find(L'}', open + 1);
        if (close == std::wstring::npos)
        {
            result.append(tmpl, pos, std::wstring::npos);  // no placeholder left (or an unterminated '{')
            return result;
        }

        result.append(tmpl, pos, open - pos);

        const std::optional<std::wstring> value =
            ResolveUploadToken(tmpl.substr(open + 1, close - open - 1), ctx, captures);
        if (value.has_value())
        {
            result.append(*value);
        }
        else
        {
            result.append(tmpl, open, close - open + 1);  // unknown token: kept verbatim
        }

        pos = close + 1;
    }

    return result;
}

bool Orc::IsSafeRequestPath(const std::wstring& resolvedPath)
{
    if (resolvedPath.empty() || resolvedPath[0] != L'/')
    {
        Log::Debug(L"Request path is empty or not rooted at '/'; refusing it");
        return false;
    }

    if (HasControlCharacter(resolvedPath))
    {
        Log::Debug(L"Request path contains control characters; refusing it");
        return false;
    }

    // A backslash is not a URL path separator, but intermediaries and the server's filesystem layer
    // may treat it as one, which would reopen traversal past the '..' check below.
    if (resolvedPath.find(L'\\') != std::wstring::npos)
    {
        Log::Debug(L"Request path contains a backslash; refusing it");
        return false;
    }

    if (resolvedPath.find(L'#') != std::wstring::npos)
    {
        Log::Debug(L"Request path contains a fragment marker; refusing it");
        return false;
    }

    // Reject only a WHOLE ".." segment. A dot inside a name ("archive.7z") and a name that merely
    // starts with dots ("..b") are legitimate, so a plain substring search would be wrong.
    size_t segmentStart = 0;
    while (segmentStart <= resolvedPath.size())
    {
        size_t segmentEnd = resolvedPath.find(L'/', segmentStart);
        if (segmentEnd == std::wstring::npos)
        {
            segmentEnd = resolvedPath.size();
        }

        // segmentEnd >= segmentStart, so the subtraction cannot underflow.
        if (resolvedPath.compare(segmentStart, segmentEnd - segmentStart, L"..") == 0)
        {
            Log::Debug(L"Request path contains a '..' segment; refusing it");
            return false;
        }

        segmentStart = segmentEnd + 1;
    }

    return true;
}

bool Orc::IsSafeHeaderLine(const std::wstring& resolvedHeader)
{
    if (HasControlCharacter(resolvedHeader))
    {
        Log::Debug(L"Header line contains control characters; refusing it");
        return false;
    }

    const size_t colon = resolvedHeader.find(L':');
    if (colon == std::wstring::npos || colon == 0)
    {
        Log::Debug(L"Header line has no name before a ':'; refusing it");
        return false;
    }

    return true;
}

bool Orc::IsAcceptableCaptureValue(const std::wstring& value, RESTResponseCapture::Source source)
{
    if (value.size() > kMaxCaptureValueChars)
    {
        Log::Debug(L"Capture value exceeds {} characters; refusing it", kMaxCaptureValueChars);
        return false;
    }

    if (HasControlCharacter(value))
    {
        Log::Debug(L"Capture value contains control characters; refusing it");
        return false;
    }

    // Both HTML body sources hold the line to printable ASCII. A hidden input and a CSRF token on an
    // attribute are tokens, identifiers and base64 in practice, and keeping them to printable ASCII is
    // what stops a server-controlled value from carrying anything exotic into a form body, a path or a
    // header line. A header or dav_property capture keeps its wider range: an existing TUS dialog's
    // Location is not this scanner's to narrow.
    if (source == RESTResponseCapture::Source::FormInput || source == RESTResponseCapture::Source::Attribute)
    {
        const bool printableAscii =
            std::all_of(value.begin(), value.end(), [](wchar_t c) { return c >= 0x20 && c < 0x7F; });
        if (!printableAscii)
        {
            Log::Debug(L"HTML body capture value is not printable ASCII; refusing it");
            return false;
        }
    }

    return true;
}

}  // namespace Orc
