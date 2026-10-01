//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
#pragma once

#include <string>
#include <vector>
#include <set>
#include <map>
#include <cstdint>
#include <optional>
#include <utility>

// Compile in the <response dav_property="..."> capture source, which reads one property's text out of
// a WebDAV PROPFIND 207 multistatus body. OFF by default; build with -DORC_REST_ENABLE_DAV_PROPERTY=1
// to enable it.
//
// WHY IT IS OPTIONAL. Every other capture source reads a value the HTTP framing already carries: a
// response header, or bounded text out of an HTML body. This one has to walk XML, and the document is
// entirely server-controlled, so it is the largest attack surface of the three and the only one no
// shipped dialog needs. A capability nothing uses should not be compiled into a forensic collector
// that runs as SYSTEM on someone else's machine.
//
// WHAT IT UNLOCKS. Resuming a chunked upload into a Nextcloud public share. The upload collection
// exposes a virtual '.file' whose length is the sum of the chunks already stored, which is exactly the
// offset the next chunk must start at - and PROPFIND is the only way to that number, because Nextcloud
// answers 405 to a HEAD on '.file'. See tests/RestAgent/rest-nextcloud-share-resume.xml.
//
// WHEN IT IS OFF, a configuration using it is refused at EMBED time, naming this flag, rather than
// silently ignoring the capture at collection time (OutputSpec::Upload::ValidateRestStep). Failing
// closed where an operator can still fix it is the point; a dialog that quietly loses its resume
// offset would restart every upload from zero and nobody would know why.
//
// THIS IS THE EXAMPLE to copy. It is the one place where the otherwise protocol-agnostic <rest> engine
// needs protocol-specific code, so a future source of the same kind should follow its shape: a bounded
// parser with explicit caps (ExtractDavProperty bounds its tag-name scan so a hostile body cannot drive
// quadratic work), compiled out unless asked for, and refused at embed time when absent - never a
// silent fallback.
#ifndef ORC_REST_ENABLE_DAV_PROPERTY
#    define ORC_REST_ENABLE_DAV_PROPERTY 0
#endif

namespace Orc {

// One value captured from a step's HTTP response into a named variable, reusable by later steps
// as "{name}". Bounded by design: the source is one of a closed set (a response header, or a DAV
// property from a PROPFIND 207 body), never an arbitrary expression.
struct RESTResponseCapture
{
    enum class Source
    {
        Header,       // read a response header by name
        DavProperty,  // read a <DAV:name> element's text from a PROPFIND 207 body
        FormInput,    // read a hidden <input name="...">'s value from an HTML body
        Attribute,    // read an attribute's value from an HTML body, on whichever tag carries it
    };

    Source source = Source::Header;
    std::wstring key;           // header name or DAV property local name
    std::wstring name;          // variable name (referenced later as "{name}")
    std::wstring defaultValue;  // used when the source is absent / step not-found

    // Set by the parser when the XML gave more than one source attribute on one <response>. The
    // parser picks one by precedence; the validator refuses the ambiguity outright, because which
    // source an operator meant is not guessable and guessing wrong reads the wrong value.
    bool bothSourcesGiven = false;
};

// One application/x-www-form-urlencoded field of a <form> step. 'field' is a literal name validated
// at embed time; 'value' is a template, so it may reference a capture from an earlier step.
struct RESTFormField
{
    std::wstring field;
    std::wstring value;
};

// One HTTP request in the upload dialog. Control steps (MKCOL/MOVE/...) have hasBody == false.
// Exactly one step per upload has hasBody == true (it carries a "body=" attribute).
struct RESTStep
{
    std::wstring method;                        // HTTP verb (PUT, MKCOL, MOVE, PROPFIND, ...)
    std::wstring path;                          // request path template (placeholders allowed)
    std::wstring query;                         // query string template (may be empty)
    std::vector<std::wstring> headers;          // per-step header templates
    std::set<uint32_t> acceptStatus;            // extra non-2xx codes treated as success
    std::vector<RESTResponseCapture> captures;  // <response> children

    // Non-empty => this step sends an application/x-www-form-urlencoded body built from these
    // fields. Distinct from hasBody, which stays the LOCAL FILE body: a dialog has exactly one file
    // body step, but may have any number of form steps, and no step may have both.
    std::vector<RESTFormField> formFields;

    // session="once": run this step once per agent instead of once per file. A login is session
    // state, not per-file work, and re-running it per file would replay the credential at a server
    // that has already answered.
    bool runOnce = false;

    // Set by the parser when 'session' carried a value it did not recognize. Silently treating a
    // typo as "per file" would run a login for every file and replay the credential each time.
    bool hasUnknownSessionToken = false;

    // Set by the parser when the step carried a <form> element that produced no usable field. An
    // empty formFields vector alone cannot say whether <form> was absent or merely empty.
    bool declaredEmptyForm = false;

    // Body step only (hasBody == true):
    bool hasBody = false;
    std::wstring bodySource;  // local file path template, normally "{input_path}"
    std::wstring offsetExpr;  // start-read offset template, e.g. "{remote_size}" (empty => 0)
    uint64_t chunkSize = 0;   // 0 => single request (<=4 GB); >0 => chunked
};

// Parse a comma/semicolon-separated list of HTTP status codes (e.g. "201,405"). Non-numeric or
// out-of-range (not 100..599) tokens are skipped.
std::set<uint32_t> ParseAcceptStatus(const std::wstring& csv);

// A response status is acceptable if it is 2xx OR present in the per-step accept set.
bool IsAcceptableStatus(uint32_t status, const std::set<uint32_t>& acceptStatus);

// Parse an already-substituted offset string to a byte offset, clamped to [0, total]. Empty or
// unparsable input (a leading sign, spaces, any non-digit, a value past 64 bits) yields 0 (start
// from the beginning).
// This is the single point where a possibly server-derived value becomes a number; the clamp to
// 'total' (the file's own size) is the cap that bounds a malicious/MITM-supplied size.
uint64_t ResolveOffset(const std::wstring& substituted, uint64_t total);

// repeat="Once" remote check. A step whose method only reads remote state (HEAD, GET, OPTIONS, PROPFIND) may run
// before anything is uploaded; any other method (MKCOL, POST, PUT, MOVE...) changes the server and is skipped.
bool IsReadOnlyRestMethod(const std::wstring& method);

// True when 'tmpl' uses a placeholder that only exists while a file is uploaded ({upload.local_path},
// {upload.total_size}, {upload.range_*}) or a per-file {upload.random:...} value: a remote check cannot resolve it.
//
// This answers "can a remote check (CheckFileUpload) resolve this without an open file?". That caller
// is always given the remote filename (szRemoteName), so {upload.filename} IS resolvable there and is
// deliberately NOT in this set. Do not widen it to include filename - that would break resume support.
// A session="once" step asks a different question; see UsesPerFilePlaceholder below.
bool UsesFileBoundPlaceholder(const std::wstring& tmpl);

// True when 'tmpl' uses any placeholder that cannot be resolved before a specific file is chosen: every
// UsesFileBoundPlaceholder token, plus {upload.filename} and {upload.filename_base64}.
//
// This answers a different question from UsesFileBoundPlaceholder: a session="once" step runs once per
// agent, before any file exists, so unlike the remote-check caller above, it has no filename either.
// Reusing UsesFileBoundPlaceholder here would miss {upload.filename}, and Task 9's EstablishSession
// calls SendRequest with an empty remoteFileName for a once-step, so that placeholder would silently
// resolve to an empty string at runtime instead of being refused at embed time.
bool UsesPerFilePlaceholder(const std::wstring& tmpl);

// A substituted remote size: digits only and no overflow, else nullopt. Unlike ResolveOffset nothing is clamped
// or defaulted, since an unreadable size must not pass for an empty remote file.
std::optional<uint64_t> ParseRemoteSize(const std::wstring& substituted);

#if ORC_REST_ENABLE_DAV_PROPERTY
// Extract the text of the first DAV property element with the given local name from a PROPFIND 207
// body (e.g. localName "getcontentlength"). Namespace-prefix agnostic (matches "<ns:local>" or
// "<local>"); returns empty if absent or self-closed. A bounded local-name scan, NOT a general XPath.
std::wstring ExtractDavProperty(const std::wstring& xmlBody, const std::wstring& localName);
#endif

// Read the 'value' of each named hidden <input> out of an HTML body, in ONE pass.
//
// A BOUNDED LOCAL SCAN, not an HTML parser. The body is attacker-controlled, so it refuses rather
// than guesses: quoted attribute values only, a tag length cap, a tag count cap, comments skipped.
// Tag and attribute NAMES match case-insensitively (HTML says they are); the 'name' VALUE matches
// case-sensitively.
//
// The scan does no work it was not asked for. An empty 'inputNames' returns immediately WITHOUT
// examining the body, and the walk stops as soon as every requested name has been resolved, so a
// page is never read past the last value the dialog needs. The first occurrence of a name therefore
// wins, and an attacker who can only append to the page cannot change what is captured.
//
// Only resolved names appear in the result: a missing key means the server did not send that input,
// while a key mapped to an empty string means it sent it empty. The caller needs that distinction,
// because "absent" is a hard failure and "empty" is a value. A tripped bound yields an empty map.
//
// One call per response, not one per capture: a WinHTTP body is a consuming stream, so a second
// read of the same response returns nothing.
std::map<std::wstring, std::wstring>
ExtractFormInputValues(const std::wstring& html, const std::vector<std::wstring>& inputNames);

// The single-name case, in terms of the primitive above. std::nullopt means absent, malformed or a
// tripped bound; an engaged optional holding an empty string means the input was found empty.
std::optional<std::wstring> ExtractFormInputValue(const std::wstring& html, const std::wstring& inputName);

// Every requested ATTRIBUTE value found in an HTML body, keyed by the name the caller asked for.
//
// Same scan, same bounds, same fail-closed rules as ExtractFormInputValues - it shares the tag walk -
// with two differences, both of them what a CSRF token on a page needs:
//
//   - every tag is examined, not only <input>, because any element can carry the attribute. Nextcloud
//     puts data-requesttoken on <head>, and a deployment is free to move it.
//   - the attribute NAME matches case-insensitively (HTML says attribute names are), so the result is
//     keyed by the caller's spelling rather than the page's.
//
// A tag may resolve several names at once. First occurrence wins, the walk stops once everything
// asked for is in, and an empty 'attributeNames' returns without touching the body.
std::map<std::wstring, std::wstring>
ExtractAttributeValues(const std::wstring& html, const std::vector<std::wstring>& attributeNames);

// The single-name case, in terms of the primitive above. std::nullopt means absent, malformed or a
// tripped bound; an engaged optional holding an empty string means the attribute was found empty.
std::optional<std::wstring> ExtractAttributeValue(const std::wstring& html, const std::wstring& attributeName);

// True if 'name' is an attribute name a <response attribute="..."> may ask for: 1..64 characters of
// letters, digits, '-', '_', ':' or '.'. That is the part of the HTML attribute-name grammar worth
// accepting; whitespace, '=', a quote, '<', '>' or '/' would describe something the scan cannot match
// anyway, so it is refused at embed time rather than silently never found at collection time.
bool IsSupportedAttributeName(const std::wstring& name);

// One application/x-www-form-urlencoded value: UTF-8, then percent-encode everything outside the
// unreserved set (A-Za-z0-9 - . _ ~), space included, as %20 rather than '+'.
//
// This is what keeps a server-controlled capture DATA rather than STRUCTURE: without it a captured
// value carrying '&' or '=' would append or replace fields in the body it is placed in.
//
// std::nullopt on a UTF-8 conversion failure - fail closed rather than silently posting the value as
// empty (a password that fails to encode must not become a structurally valid but empty field).
std::optional<std::string> UrlEncodeFormValue(const std::wstring& value);

// The request body for a <form> step: "name=value&name=value", every value through
// UrlEncodeFormValue. Field names are literals validated at embed time, so they are emitted as-is.
//
// std::nullopt if ANY field's name or value fails to encode - the whole body is refused rather than
// silently dropping or emptying the field that failed.
std::optional<std::string> BuildFormBody(const std::vector<std::pair<std::wstring, std::wstring>>& fields);

// {upload.random:<charset>:<length>[:<id>]} placeholder support. `spec` is the token text after
// "upload.random:" (e.g. "hex:4:shard1"). Charsets: hex (0-9a-f), num (0-9), alpha (a-z), alnum
// (a-z0-9); length 1..64; the optional ':<id>' only differentiates otherwise-identical tokens (it does
// not affect the generated value's content).

// Validate a spec for embed-time refusal; on failure sets a human-readable reason.
bool IsValidRestRandomSpec(const std::wstring& spec, std::wstring& error);

// Generate an unbiased random string (CSPRNG) for a valid spec; returns empty on an invalid spec or an
// RNG failure (the caller then substitutes empty / fails closed).
std::wstring MakeRestRandom(const std::wstring& spec);

// Append the spec body of every {upload.random:<spec>} token found in `s` to `specs`. Returns false and sets
// `error` on an unterminated token. Single scanner shared by embed-time validation and the runtime
// pre-resolver so the two cannot drift.
bool ScanRestRandomSpecs(const std::wstring& s, std::vector<std::wstring>& specs, std::wstring& error);

// True if 'name' is under the engine-reserved 'upload.' placeholder namespace. Single source of truth for
// the reserved-prefix rule: a <response> capture may not use this prefix (it would shadow a built-in) and
// a server-supplied value may never be allowed to populate it.
bool IsReservedUploadName(const std::wstring& name);

// The marker that stands in for a redacted capture value in a logged request path, e.g. "[redacted:rt]".
// The capture NAME is operator-authored configuration, which ApplyResponseCaptures already logs; only the
// VALUE is server-supplied, and that is what never appears.
std::wstring RedactedCaptureMarker(const std::wstring& name);

// The capture map as it may appear in a log: engine-generated entries kept, operator <response> captures
// replaced by RedactedCaptureMarker.
//
// Resolving a path template against this instead of the real map gives a path that is still useful to
// diagnose with - a chunk's offset, the filename, which random upload directory was used - while a
// server-supplied value never reaches the log. Logging the bare template instead would hide all of that
// too, and logging the fully resolved path would leak a captured session token.
//
// IsReservedUploadName is the discriminator, and it is sound because embed-time validation refuses a
// <response> name under the 'upload.' prefix: an entry with that prefix can only be an engine-generated
// {upload.random:...} value, which names no secret. Anything else came from a server and is masked.
std::map<std::wstring, std::wstring> MaskCapturesForLog(const std::map<std::wstring, std::wstring>& captures);

// True if 'token' (the text inside a '{...}') is a recognized engine placeholder: one of the fixed
// {upload.*} built-ins or a {upload.random:<spec>} token. The built-in set is the SINGLE source
// shared with ResolveUploadTemplate (both live in RESTStep.cpp, so they cannot drift).
bool IsKnownUploadPlaceholder(const std::wstring& token);

// Enumerate the inner text of every '{...}' group in 's' (e.g. "a{x}/{y}" -> {"x","y"}). An unterminated
// '{' ends the scan. Used by embed-time validation to vet placeholder usage.
std::vector<std::wstring> ScanPlaceholderTokens(const std::wstring& s);

// Engine-provided values for the {upload.*} built-in placeholders. Operator <response> captures are
// passed separately (the third argument of ResolveUploadTemplate), never via this struct.
struct UploadTemplateContext
{
    uint64_t offset = 0;    // {upload.range_start} - chunk start byte offset
    uint32_t length = 0;    // {upload.range_length}; {upload.range_end} = offset + length - 1
    uint64_t total = 0;     // {upload.total_size} - whole-file size
    std::wstring filename;  // {upload.filename} / {upload.filename_base64}
    std::wstring inputPath; // {upload.local_path} - local source path
};

// Substitute every {token} in 'tmpl' in a single pass; substituted values are never rescanned. A token names
// a fixed {upload.*} built-in from 'ctx' before any operator <response> capture, so a server-controlled
// capture can never shadow a built-in. An unknown token is kept verbatim. The {upload.random:...} values are
// pre-resolved into 'captures'.
std::wstring ResolveUploadTemplate(
    const std::wstring& tmpl, const UploadTemplateContext& ctx, const std::map<std::wstring, std::wstring>& captures);

// Guards run on a request path/header AFTER placeholder substitution, at the point of use.
//
// ResolveUploadTemplate is a blind substitution that does not know where its output goes, and a
// captured value is server-controlled, so the check belongs at the call site that does know. A form
// value is made safe by encoding (UrlEncodeFormValue); a path and a header cannot be blanket-encoded
// without breaking legitimate uses (a captured TUS Location IS a path), so they are validated instead.

// Reject '..' path segments, '\', '#', control characters, and anything not rooted at '/'.
// A genuine TUS Location path passes; a traversal or a request-line split does not.
bool IsSafeRequestPath(const std::wstring& resolvedPath);

// Reject a line without a non-empty name before its first ':', and any control character (CR/LF
// would split the request and inject a header of the server's choosing).
bool IsSafeHeaderLine(const std::wstring& resolvedHeader);

// Whether a server-supplied capture value may be used at all.
//
// Common to every source: no control characters (CR/LF would split a request line or inject a
// header) and a length cap, because unlike a header a body-derived value has no natural bound.
// A form_input value must additionally be printable ASCII: a token never is anything else, and
// refusing the whole non-ASCII range closes homoglyph and normalization tricks before the value
// can reach a path or a body. Header captures keep the looser rule so existing dialogs do not
// regress.
bool IsAcceptableCaptureValue(const std::wstring& value, RESTResponseCapture::Source source);

}  // namespace Orc
