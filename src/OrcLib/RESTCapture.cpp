//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): DFIR-ORC Team
//
#include "stdafx.h"

#include "RESTCapture.h"

#include "Log/Log.h"
#include "Text/Iconv.h"

#include <optional>

namespace Orc {

namespace {

// What one step wants out of its response BODY, by source. Only the names the page actually carried
// appear; a missing key is an absent value, which is what S5 turns into a hard failure.
struct HtmlBodyCaptures
{
    std::map<std::wstring, std::wstring> formInputs;
    std::map<std::wstring, std::wstring> attributes;
};

// Resolve EVERY body-sourced capture of one step in a single read and a single decode.
//
// A WinHTTP response body is a consuming stream: reading it once per capture would hand the page to
// the first capture and nothing to the rest, so a step with several body captures (a login page needs
// a token, and a server-rendered form three inputs) would never work past the first one. The body is
// attacker-controlled input a scanner is about to walk, so it is read under the tight capture cap
// rather than the generic one, and it is not read at all unless a capture wants it.
//
// The two scans are separate walks over the same decoded string. Separate because they look for
// different things - one element by name then two of its attributes, versus one attribute on any
// element - and each is bounded on its own; shared would mean one walk doing both jobs with two sets
// of rules, which is harder to reason about than two short walks over a string already in hand.
HtmlBodyCaptures ReadHtmlBodyCaptures(const HttpResponse& response, const std::vector<RESTResponseCapture>& captures)
{
    std::vector<std::wstring> inputNames;
    std::vector<std::wstring> attributeNames;
    for (const auto& cap : captures)
    {
        if (cap.source == RESTResponseCapture::Source::FormInput)
        {
            inputNames.push_back(cap.key);
        }
        else if (cap.source == RESTResponseCapture::Source::Attribute)
        {
            attributeNames.push_back(cap.key);
        }
    }

    if (inputNames.empty() && attributeNames.empty())
    {
        return {};  // no body capture on this step: never touch the body
    }

    const std::optional<std::string> body = ReadResponseBody(response, kMaxCaptureBodyBytes);
    if (!body)
    {
        Log::Debug(L"Body captures: the response body could not be read in full");
        return {};
    }

    std::error_code ec;
    const std::wstring html = ToUtf16(*body, ec);
    if (ec)
    {
        Log::Debug(L"Body captures: the response body is not valid UTF-8 [{}]", ec);
        return {};
    }

    HtmlBodyCaptures found;
    found.formInputs = ExtractFormInputValues(html, inputNames);
    found.attributes = ExtractAttributeValues(html, attributeNames);
    return found;
}

// Resolve one <response> capture from the response. std::nullopt => the caller uses the default (a
// not-found 404, an absent header, an absent DAV property, an absent form input or attribute).
// 'bodyValues' holds everything already scanned out of this response's body (see
// ReadHtmlBodyCaptures) - this function never reads the body itself.
std::optional<std::wstring>
ReadCaptureValue(const HttpResponse& response, const RESTResponseCapture& cap, const HtmlBodyCaptures& bodyValues)
{
    if (response.statusCode == 404)
    {
        return std::nullopt;
    }

    if (cap.source == RESTResponseCapture::Source::Header)
    {
        return GetResponseHeader(response, cap.key);
    }

    if (cap.source == RESTResponseCapture::Source::FormInput || cap.source == RESTResponseCapture::Source::Attribute)
    {
        const std::map<std::wstring, std::wstring>& scanned =
            cap.source == RESTResponseCapture::Source::FormInput ? bodyValues.formInputs : bodyValues.attributes;

        const auto it = scanned.find(cap.key);
        if (it == scanned.end())
        {
            return std::nullopt;
        }

        return it->second;
    }

#if ORC_REST_ENABLE_DAV_PROPERTY
    // DavProperty: a 207 body is UTF-8. Decode it properly (invalid sequences become U+FFFD rather
    // than per-byte mojibake) before the bounded local-name scan.
    const std::optional<std::string> body = ReadResponseBody(response);
    if (!body)
    {
        return std::nullopt;  // truncated/failed read: treat the capture as absent (use the default)
    }

    std::error_code ec;
    const std::wstring xml = ToUtf16(*body, ec);
    if (ec)
    {
        Log::Warn(L"Capture '{}': could not decode the response body as UTF-8; ignoring it", cap.name);
        return std::nullopt;
    }

    const std::wstring text = ExtractDavProperty(xml, cap.key);
    if (text.empty())
    {
        return std::nullopt;
    }

    return text;
#else
    // DAV-property parsing is compiled out (ORC_REST_ENABLE_DAV_PROPERTY=0). A dav_property capture is
    // already refused at embed time, so this is unreachable in practice; fail closed to the default in
    // case an unvalidated config reaches here, and never touch the server-supplied body.
    return std::nullopt;
#endif
}

}  // namespace

HRESULT ApplyResponseCaptures(
    const RESTStep& step,
    const HttpResponse& response,
    std::map<std::wstring, std::wstring>& captures)
{
    // One read for every body-sourced capture of this step. Nothing happens here when the step has
    // none.
    const HtmlBodyCaptures bodyValues = ReadHtmlBodyCaptures(response, step.captures);

    for (const auto& cap : step.captures)
    {
        // Defense in depth: config validation already refuses a capture name under the reserved 'upload.'
        // namespace (OutputSpec::Upload::Validate), but never let a server-controlled value land on an
        // 'upload.*' built-in even if an unvalidated config somehow reaches here.
        if (IsReservedUploadName(cap.name))
        {
            Log::Warn(L"Ignoring capture '{}': the reserved 'upload.' namespace cannot be set by a response", cap.name);
            continue;
        }

        std::optional<std::wstring> captured = ReadCaptureValue(response, cap, bodyValues);
        if (captured && !IsAcceptableCaptureValue(*captured, cap.source))
        {
            // The value failed the rules (length, control characters, or - for a body source - non-ASCII).
            // Fall back exactly as if it had been absent rather than forwarding it.
            Log::Debug(L"Capture '{}': the server value was refused; using the default", cap.name);
            captured.reset();
        }

        // The hard failure below is scoped to the BODY sources only. A header or dav_property capture
        // keeps today's fall-back-to-default behaviour, so no existing dialog regresses: a TUS resume
        // capture that legitimately reports nothing (e.g. a size header the server omits) must stay a
        // fall-back, not a stop.
        const bool bodySourced = cap.source == RESTResponseCapture::Source::FormInput
            || cap.source == RESTResponseCapture::Source::Attribute;
        if (!captured && bodySourced && cap.defaultValue.empty())
        {
            // No value and no default. Substituting "" here would send a structurally valid but
            // meaningless request (a login with an empty token) and the failure would surface several
            // steps later as an opaque server error. Fail where the cause is known.
            //
            // Debug, not Error: this function detects and propagates the failure, it does not report
            // it. The caller chain is the reporting site - EstablishSession logs Critical for a
            // session="once" step (naming the cause in its own message), and a per-file step's
            // failure is reported through the generic upload-failure notification, the same way every
            // other propagated RESTAgent failure in this file is. Logging Error here too would report
            // the same failure twice.
            Log::Debug(
                L"REST capture '{}' found no value in the response and has no 'default'; the dialog cannot "
                L"continue. Check that the server still serves this value.",
                cap.name);
            return E_FAIL;
        }

        captures[cap.name] = captured.value_or(cap.defaultValue);

        // Never log the value. It is server-controlled and routinely a secret - a TUS upload URL captured
        // from 'Location' embeds a session token - which is why HttpClient already refuses to print
        // Set-Cookie/Authorization values. The length plus where it came from is what a diagnosis needs:
        // it separates "captured nothing" from "captured the wrong thing".
        Log::Debug(
            L"Capture '{}': {} characters ({})",
            cap.name,
            captures[cap.name].size(),
            captured ? L"from the response" : L"default");
    }

    return S_OK;
}

}  // namespace Orc
