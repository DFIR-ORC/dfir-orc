//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "stdafx.h"

#include "RESTAgent.h"
#include "RESTCapture.h"
#include "OutputSpec.h"
#include "Log/Log.h"
#include "Log/VerboseDiagnostics.h"
#include "Text/Fmt/std_error_code.h"
#include "Utils/Guard.h"
#include "Utils/String.h"
#include "CryptoHashStream.h"
#include "BinaryBuffer.h"

#include <wincrypt.h>
#include <algorithm>
#include <optional>
#include <vector>
#include <string_view>
#include <cstdint>

#pragma comment(lib, "Winhttp.lib")
#pragma comment(lib, "crypt32.lib")

using namespace Orc;

namespace {

// If 'value' is an absolute http(s) URL, return just its path+query (everything from the first '/'
// after "scheme://authority"); otherwise return it unchanged. A captured TUS Location header
// (OpenCloud/OCIS) is an absolute URL, but requests run on a connection already bound to the
// configured host:port, so only the path component is usable. A non-URL (an ordinary path template)
// is passed through untouched.
std::wstring UrlPathAndQuery(const std::wstring& value)
{
    // Skip past the scheme + "://" so the path search below doesn't stop on that "//".
    constexpr std::wstring_view kHttps = L"https://";
    constexpr std::wstring_view kHttp = L"http://";

    size_t authorityStart = 0;
    if (value.rfind(kHttps, 0) == 0)
    {
        authorityStart = kHttps.size();
    }
    else if (value.rfind(kHttp, 0) == 0)
    {
        authorityStart = kHttp.size();
    }
    else
    {
        return value;
    }

    const size_t pathStart = value.find(L'/', authorityStart);
    if (pathStart == std::wstring::npos)
    {
        return L"/";  // URL with an empty path
    }

    return value.substr(pathStart);
}

// Append every '{upload.random:<spec>}' spec used by 'value' to 'specs'. False - with the reason logged -
// when the placeholder is malformed. The caller must fail closed on that: an unterminated placeholder
// makes the scanner collect nothing, so the token survives substitution and the literal
// "{upload.random:..." would be sent as part of a request path or header.
bool CollectRandomSpecs(const std::wstring& value, std::vector<std::wstring>& specs)
{
    std::wstring error;
    if (ScanRestRandomSpecs(value, specs, error))
    {
        return true;
    }

    Log::Error(L"REST upload template '{}' is malformed ({}); refusing upload", value, error);
    return false;
}

// True when a request for 'step' needs a value that only exists while a file is uploaded, in the step itself or in
// the headers every request carries.
bool StepUsesFileBoundPlaceholder(const RESTStep& step, const std::vector<std::wstring>& globalHeaders)
{
    if (UsesFileBoundPlaceholder(step.path) || UsesFileBoundPlaceholder(step.query))
    {
        return true;
    }

    const auto usesFileBound = [](const std::wstring& header) { return UsesFileBoundPlaceholder(header); };
    return std::any_of(step.headers.begin(), step.headers.end(), usesFileBound)
        || std::any_of(globalHeaders.begin(), globalHeaders.end(), usesFileBound);
}

// True when 'headers' already sets a Content-Type, so the caller does not override an operator's own
// choice of dialect with the engine's application/x-www-form-urlencoded default.
//
// Matches the header NAME up to its ':' with the surrounding OWS trimmed, not a fixed "Content-Type:"
// prefix: WinHttpAddRequestHeaders accepts (and some servers tolerate) a space before the colon, e.g.
// "Content-Type : application/json", which a plain prefix compare would miss and cause a second
// Content-Type header to be appended.
bool HasContentTypeHeader(const std::vector<std::wstring>& headers)
{
    const auto matches = [](std::wstring_view header) {
        return EqualsAsciiCaseInsensitive(TrimSpacesAndTabs(header.substr(0, header.find(L':'))), L"Content-Type");
    };
    return std::any_of(headers.begin(), headers.end(), matches);
}

// The application/x-www-form-urlencoded body for a <form> step: every field's value resolved through
// the same template context as the request path and headers, then encoded together by BuildFormBody.
// Every value goes through UrlEncodeFormValue (inside BuildFormBody), which is what keeps a
// server-supplied capture data rather than structure: an unencoded '&' or '=' would append or replace
// fields in this body.
//
// std::nullopt when BuildFormBody fails closed (a field's name or value could not be encoded as
// UTF-8) - the caller must refuse the request rather than send a body missing or emptying a field.
std::optional<std::string> BuildResolvedFormBody(
    const RESTStep& step,
    const UploadTemplateContext& ctx,
    const std::map<std::wstring, std::wstring>& captures)
{
    std::vector<std::pair<std::wstring, std::wstring>> fields;
    fields.reserve(step.formFields.size());
    for (const auto& field : step.formFields)
    {
        fields.emplace_back(field.field, ResolveUploadTemplate(field.value, ctx, captures));
    }

    return BuildFormBody(fields);
}

}  // namespace

RESTAgent::RESTAgent(
    UploadMessage::ISource& msgSource,
    UploadMessage::ITarget& msgTarget,
    UploadNotification::ITarget& target)
    : UploadAgent(msgSource, msgTarget, target)
{
}

RESTAgent::~RESTAgent()
{
    UnInitialize();
}

HRESULT RESTAgent::Initialize()
{
    if (m_config.Method != OutputSpec::UploadMethod::REST)
    {
        Log::Error("RESTAgent failed to initialize because of invalid method");
        return E_UNEXPECTED;
    }

    const HttpClientConfig cfg = BuildHttpClientConfig(m_config);

    std::wstring proxyDesc;
    switch (cfg.proxy.mode)
    {
        case ProxyMode::Direct:
            proxyDesc = L"direct (no proxy)";
            break;
        case ProxyMode::System:
            proxyDesc = L"system (machine-wide)";
            break;
        case ProxyMode::Wpad:
            proxyDesc = L"WPAD auto-detect";
            break;
        case ProxyMode::Pac:
            proxyDesc = L"PAC (" + cfg.proxy.pacUrl + L")";
            break;
        case ProxyMode::Named:
            proxyDesc = L"named (" + cfg.proxy.server + L")";
            break;
        case ProxyMode::Invalid:
            // HttpClient::Open refuses this below; name it here so the log says what was configured
            // rather than leaving the line empty.
            proxyDesc = L"unresolved (" + cfg.proxy.server + L")";
            break;
    }
    Log::Info(L"Proxy: {}", proxyDesc);

    if (auto result = HttpClient::Open(cfg, m_http); result.has_error())
    {
        return ToHRESULT(result.error());
    }

    return S_OK;
}

HRESULT RESTAgent::SendRequest(
    const RESTStep& step,
    HANDLE hBodyFile,
    uint64_t offset,
    DWORD length,
    uint64_t totalSize,
    const std::wstring& remoteFileName,
    const std::wstring& inputPath,
    std::map<std::wstring, std::wstring>& captures)
{
    if (m_authRejected)
    {
        Log::Debug(L"Not sending {} '{}': this upload's authentication was refused", step.method, step.path);
        return E_ACCESSDENIED;
    }

    const UploadTemplateContext ctx {offset, static_cast<uint32_t>(length), totalSize, remoteFileName, inputPath};

    // Build the request path: step path + optional query, substitute placeholders, reduce an absolute
    // URL (a captured TUS Location is a full https://host/... URL) to a path on the already-connected
    // host, then guarantee a leading slash. Order matters: the URL reduction must see the substituted
    // value, and the leading-slash fix must run after the reduction (else it would turn the substituted
    // "https://..." into "/https://..." and defeat the reduction).
    std::wstring pathTemplate = step.path;
    if (!step.query.empty())
    {
        pathTemplate += step.query;
    }

    std::wstring path = ResolveUploadTemplate(pathTemplate, ctx, captures);

    // The same template resolved against MASKED captures, and this is what gets logged; 'path' is what
    // gets sent.
    //
    // A capture value is server-controlled and routinely a secret - ApplyResponseCaptures refuses to log
    // one, noting that a TUS upload URL captured from 'Location' embeds a session token - so the fully
    // resolved path cannot be logged either, or the same secret arrives by the back door. Withholding the
    // whole path instead would hide the {upload.*} built-ins too, and those are generated here, hold no
    // secret, and are exactly what a diagnosis needs: a chunked upload's offset lives in its path. So the
    // built-ins are substituted and only the capture values are replaced.
    //
    // A verbose-diagnostics build logs the fully resolved path instead (Log/VerboseDiagnostics.h); that
    // switch is project-wide, not one this file owns.
    const std::wstring loggedPath = ResolveUploadTemplate(pathTemplate, ctx, MaskCapturesForLog(captures));

    const std::wstring reduced = UrlPathAndQuery(path);
    if (reduced != path)
    {
        // This fires EXACTLY when the substituted value was an absolute URL, which is the captured
        // TUS-'Location' case where a session token lives, so 'reduced' is a secret and never logged.
        // 'loggedPath' already shows which capture the URL came from, so there is nothing to add here
        // beyond the fact that a reduction happened.
#if ORC_HAS_VERBOSE_DIAGNOSTICS
        Log::Debug(L"Reduced absolute URL to path '{}' for the connected host", reduced);
#else
        Log::Debug(L"Reduced an absolute URL to a path for the connected host");
#endif
    }

    path = reduced;

    if (path.empty() || path[0] != L'/')
    {
        path.insert(0, 1, L'/');
    }

    // The resolved path may carry a server-supplied capture. ResolveUploadTemplate does not know
    // where its output goes; this is the one place that does.
    if (!IsSafeRequestPath(path))
    {
        Log::Error(
            L"REST: the resolved path for {} '{}' is not safe to send; refusing the request", step.method, step.path);
        return E_INVALIDARG;
    }

    std::vector<std::wstring> headers;
    const std::vector<std::wstring>& globalHeaders = m_config.rest.HttpHeaders;
    for (const auto* hs : {&globalHeaders, &step.headers})
    {
        for (const auto& headerTemplate : *hs)
        {
            std::wstring resolved = ResolveUploadTemplate(headerTemplate, ctx, captures);

            if (!IsSafeHeaderLine(resolved))
            {
                Log::Error(
                    L"REST: a resolved header of {} '{}' is not safe to send; refusing the request",
                    step.method,
                    step.path);
                return E_INVALIDARG;
            }

            headers.push_back(std::move(resolved));
        }
    }

    HttpRequest req;
    req.method = step.method;
    req.path = path;
    req.headers = std::move(headers);
    req.bodyFile = hBodyFile;
    req.bodyOffset = offset;
    req.bodyLength = length;

    // A <form> step sends its fields as an application/x-www-form-urlencoded body instead of the
    // per-file body handled above; the two are mutually exclusive (validated at embed time and again
    // refused by HttpClient::Send).
    if (!step.formFields.empty())
    {
        const std::optional<std::string> formBody = BuildResolvedFormBody(step, ctx, captures);
        if (!formBody.has_value())
        {
            // BuildFormBody/UrlEncodeFormValue already logged which field failed to encode. Refuse the
            // request rather than send a body with a field silently dropped or emptied - a password
            // that fails to encode must not go out as an empty field.
            Log::Error(
                L"REST: could not build the form body for {} '{}'; refusing the request", step.method, step.path);
            return E_FAIL;
        }

        req.bodyData = *formBody;

        // Only supply the default Content-Type when the step did not already set one, so an operator
        // can still name a dialect the server expects without the engine overriding it.
        if (!HasContentTypeHeader(req.headers))
        {
            req.headers.push_back(L"Content-Type: application/x-www-form-urlencoded");
        }

        // Never log the body: it carries the share password and the captured tokens.
        Log::Debug(L"{} '{}': sending a form body of {} bytes", step.method, step.path, req.bodyData.size());
    }

    HttpResponse resp;
    if (auto result = m_http.Send(req, resp); result.has_error())
    {
        return ToHRESULT(result.error());
    }

    // Whatever this step reads of the response, the rest is discarded before the handle closes.
    const auto drain = Guard::CreateScopeGuard([&resp]() { DrainResponseBody(resp); });

    if (!IsAcceptableStatus(resp.statusCode, step.acceptStatus))
    {
        Log::Debug(
            L"{} '{}' failed with status {} (offset {} length {})",
            step.method,
            ORC_DIAGNOSTIC_VALUE(path, loggedPath),
            resp.statusCode,
            offset,
            length);

        if (resp.statusCode == 401 || resp.statusCode == 407)
        {
            OnAuthenticationRejected(resp);
        }

        LogResponseDiagnostics(resp);
        return m_authRejected ? E_ACCESSDENIED : E_FAIL;
    }

    // Capture this step's <response> values (e.g. the resume size) into 'captures'.
    if (HRESULT hr = ApplyResponseCaptures(step, resp, captures); FAILED(hr))
    {
        return hr;
    }

    Log::Debug(
        L"{} '{}' -> {} (offset={} length={})",
        step.method,
        ORC_DIAGNOSTIC_VALUE(path, loggedPath),
        resp.statusCode,
        offset,
        length);
    return S_OK;
}

void RESTAgent::OnAuthenticationRejected(const HttpResponse& resp)
{
    m_authRejected = true;

    const bool fromProxy = resp.statusCode == 407;
    std::wstring_view reason;
    if (!fromProxy)
    {
        reason =
            L"the server refused the request or the credentials sent to it (check 'user', 'password' and "
            L"'authscheme')";
    }
    else if (m_http.ProxyInUse())
    {
        reason =
            L"the proxy refused the request or the credentials sent to it (check 'proxy_user', "
            L"'proxy_password' and 'proxy_authscheme')";
    }
    else
    {
        reason =
            L"no proxy handles this connection, so 'proxy_user' was not sent: the challenge came from something "
            L"else in the path";
    }

    // Every further request would present the same credentials again, and each refusal can count as a failed logon
    // against the account until the directory locks it. Nothing else is sent through this <upload>: its failure
    // hands the files over to the next <upload>, whose own credentials are its own business.
    Log::Error(
        L"REST upload: authentication refused ({}); {}. No further request is sent through this upload, so that the "
        L"account is not locked out; the files not uploaded are kept for the next <upload>",
        DescribeAuthChallenge(resp),
        reason);
}

HRESULT RESTAgent::UploadFile(
    const std::wstring& strLocalName,
    const std::wstring& strRemoteName,
    bool bDeleteWhenCopied,
    const std::shared_ptr<const UploadMessage>& /*request*/)
{
    // Read in a nested scope so the handle is closed before a possible operation="move" delete.
    uint64_t totalSize = 0;
    {
        Guard::FileHandle hFile(CreateFileW(
            strLocalName.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL));
        if (!hFile.IsValid())
        {
            DWORD dwError = GetLastError();
            Log::Debug(L"Failed to open file '{}' [{}]", strLocalName, Win32Error(dwError));
            m_hasFailure = true;
            return HRESULT_FROM_WIN32(dwError);
        }

        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(hFile.value(), &fileSize))
        {
            DWORD dwError = GetLastError();
            Log::Debug(L"GetFileSizeEx failed for '{}' [{}]", strLocalName, Win32Error(dwError));
            m_hasFailure = true;
            return HRESULT_FROM_WIN32(dwError);
        }

        totalSize = static_cast<uint64_t>(fileSize.QuadPart);
    }

    bool bodySent = false;
    if (HRESULT hr = RunStepsForFileUpload(strLocalName, strRemoteName, totalSize, bodySent); FAILED(hr))
    {
        m_hasFailure = true;
        return hr;
    }

    // Every step reported success, yet no body request was ever issued: the dialog declares no body step,
    // or a resume offset already covered the whole file. This run therefore delivered nothing, and saying
    // otherwise would be worse than a wasted upload - under operation="move" it would destroy what may be
    // the only copy of the collection on the strength of a completeness only the server asserted. This needs
    // no hostile server: a remote object already at full size (a re-run, a name colliding between hosts) is
    // enough. Report the failure so the fallback list is tried, and keep the file whatever the operation says.
    if (!bodySent)
    {
        Log::Error(
            L"Upload of '{}' to '{}' sent no data: the <rest> dialog has no body step, or a resume offset "
            L"already covered the whole file. The upload is incomplete and the local file is kept.",
            strLocalName,
            strRemoteName);
        m_hasFailure = true;
        return E_FAIL;
    }

    Log::Info(L"Successfully uploaded file '{}' to '{}'", strLocalName, strRemoteName);

    // operation="move": remove the local file once it is safely uploaded.
    if (bDeleteWhenCopied)
    {
        if (!DeleteFileW(strLocalName.c_str()))
        {
            Log::Warn(L"Failed to delete file '{}' after upload [{}]", strLocalName, LastWin32Error());
        }
        else
        {
            Log::Debug(L"Deleted local file '{}' after upload", strLocalName);
        }
    }

    return S_OK;
}

HRESULT RESTAgent::EstablishSession()
{
    if (m_sessionFailed)
    {
        Log::Debug(L"REST: the session steps already failed; not retrying them");
        return E_ACCESSDENIED;
    }

    if (m_sessionEstablished)
    {
        return S_OK;
    }

    const auto& steps = m_config.rest.RestSteps;
    const bool anyOnce = std::any_of(steps.begin(), steps.end(), [](const RESTStep& s) { return s.runOnce; });
    if (!anyOnce)
    {
        m_sessionEstablished = true;
        return S_OK;
    }

    // Validation refuses a per-file {upload.*} placeholder in a once-step, so an empty file context
    // is correct here: nothing in these steps can depend on a file.
    for (const auto& step : steps)
    {
        if (!step.runOnce)
        {
            continue;
        }

        if (HRESULT hr = SendRequest(step, INVALID_HANDLE_VALUE, 0, 0, 0, L"", L"", m_sessionCaptures); FAILED(hr))
        {
            // Critical, not Error: this is the top of the stack for the session, nothing above it
            // reports the cause, and every file in the run is about to fail because of it.
            Log::Critical(
                L"REST: the session step {} '{}' failed; no file can be uploaded [{}]",
                step.method,
                step.path,
                SystemError(hr));
            m_sessionFailed = true;
            return hr;
        }
    }

    m_sessionEstablished = true;
    Log::Debug(L"REST: the session steps completed; {} value(s) captured", m_sessionCaptures.size());
    return S_OK;
}

HRESULT RESTAgent::RunStepsForFileUpload(
    const std::wstring& strLocalName,
    const std::wstring& strRemoteName,
    uint64_t totalSize,
    bool& bodySent)
{
    if (HRESULT hr = EstablishSession(); FAILED(hr))
    {
        return hr;
    }

    std::wstring remoteFileName = strRemoteName;
    if (const size_t slash = remoteFileName.find_last_of(L"/\\"); slash != std::wstring::npos)
    {
        remoteFileName = remoteFileName.substr(slash + 1);
    }

    std::map<std::wstring, std::wstring> captures;
    if (HRESULT hr = ResolveRandomTokens(captures); FAILED(hr))
    {
        Log::Debug(L"Failed to resolve random tokens [{}]", SystemError(hr));
        return hr;
    }

    // The session captures are visible to every per-file step. Seeding rather than sharing keeps a
    // per-file capture from leaking into the next file: this map is discarded when the file is done.
    captures.insert(m_sessionCaptures.begin(), m_sessionCaptures.end());

    for (const auto& step : m_config.rest.RestSteps)
    {
        if (step.runOnce)
        {
            continue;  // already run by EstablishSession
        }

        if (step.hasBody)
        {
            if (HRESULT hr =
                    RunBodyStep(step, strLocalName, strRemoteName, remoteFileName, totalSize, captures, bodySent);
                FAILED(hr))
            {
                Log::Debug(L"Failed to upload body [{}]", SystemError(hr));
                return hr;
            }

            continue;
        }

        // Control step (MKCOL/MOVE/PROPFIND/HEAD/...): a single request, no body. The chunk
        // placeholders {start}/{end}/{length} are 0 here (meaningful only on the body step).
        if (HRESULT hr =
                SendRequest(step, INVALID_HANDLE_VALUE, 0, 0, totalSize, remoteFileName, strLocalName, captures);
            FAILED(hr))
        {
            Log::Debug(L"Failed to send REST request [{}]", SystemError(hr));
            return hr;
        }
    }

    return S_OK;
}

HRESULT RESTAgent::ResolveRandomTokens(std::map<std::wstring, std::wstring>& captures) const
{
    // Resolve every {upload.random:<spec>} token used by this file's dialog ONCE, up front, so each token's
    // value is stable across all steps and chunks (all steps agree on a random path) while the next file gets
    // fresh values. Fail closed if the CSPRNG fails - never upload to a corrupted/predictable path. Values are
    // cached in `captures` under the reserved 'upload.' prefix (cannot collide with an operator capture).
    std::vector<std::wstring> specs;
    for (const auto& step : m_config.rest.RestSteps)
    {
        if (!CollectRandomSpecs(step.path, specs) || !CollectRandomSpecs(step.query, specs)
            || !CollectRandomSpecs(step.bodySource, specs) || !CollectRandomSpecs(step.offsetExpr, specs))
        {
            return E_INVALIDARG;
        }

        for (const auto& header : step.headers)
        {
            if (!CollectRandomSpecs(header, specs))
            {
                return E_INVALIDARG;
            }
        }

        // A <form> value is a template too: {upload.random:...} there must be pre-resolved the same
        // way as in the path/query/headers, or it would be posted verbatim.
        for (const auto& field : step.formFields)
        {
            if (!CollectRandomSpecs(field.value, specs))
            {
                return E_INVALIDARG;
            }
        }
    }

    for (const auto& header : m_config.rest.HttpHeaders)
    {
        if (!CollectRandomSpecs(header, specs))
        {
            return E_INVALIDARG;
        }
    }

    for (const auto& spec : specs)
    {
        const std::wstring key = L"upload.random:" + spec;
        if (captures.count(key))
        {
            continue;
        }

        std::wstring value = MakeRestRandom(spec);
        if (value.empty())
        {
            Log::Error(L"Failed to generate '{{upload.random:{}}}' (RNG or spec error); refusing upload", spec);
            return E_FAIL;
        }

        captures.emplace(key, std::move(value));
    }

    return S_OK;
}

HRESULT RESTAgent::RunBodyStep(
    const RESTStep& step,
    const std::wstring& strLocalName,
    const std::wstring& strRemoteName,
    const std::wstring& remoteFileName,
    uint64_t totalSize,
    std::map<std::wstring, std::wstring>& captures,
    bool& bodySent)
{
    // The body PATH can only be resolved against the uploaded file's size, since that is all that is known
    // before the file is open. Everything after the open is sized from the file actually opened.
    const std::wstring bodyPath =
        ResolveUploadTemplate(step.bodySource, {0, 0, totalSize, remoteFileName, strLocalName}, captures);

    Guard::FileHandle hBody(CreateFileW(
        bodyPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL));
    if (!hBody.IsValid())
    {
        DWORD dwError = GetLastError();
        Log::Debug(L"Failed to open body source '{}' [{}]", bodyPath, Win32Error(dwError));
        return HRESULT_FROM_WIN32(dwError);
    }

    // 'body=' accepts an arbitrary literal path, so the bytes being sent are not necessarily the file
    // being uploaded. Content-Length, every chunk length and every {upload.*} size placeholder must
    // describe the file on the handle: sized from the uploaded file instead, a larger body is silently
    // truncated and a smaller one fails at EOF, both while the request claims the other file's length.
    LARGE_INTEGER bodySize = {};
    if (!GetFileSizeEx(hBody.value(), &bodySize))
    {
        DWORD dwError = GetLastError();
        Log::Debug(L"GetFileSizeEx failed for body source '{}' [{}]", bodyPath, Win32Error(dwError));
        return HRESULT_FROM_WIN32(dwError);
    }

    const uint64_t bodyTotal = static_cast<uint64_t>(bodySize.QuadPart);
    if (bodyTotal != totalSize)
    {
        Log::Debug(
            L"Body source '{}' is {} bytes, the uploaded file '{}' is {}; the transfer follows the body",
            bodyPath,
            bodyTotal,
            strLocalName,
            totalSize);
    }

    // Resolved after the open so a server-supplied resume offset is clamped against the real body size.
    const std::wstring offsetStr =
        ResolveUploadTemplate(step.offsetExpr, {0, 0, bodyTotal, remoteFileName, strLocalName}, captures);
    const uint64_t startOffset = ResolveOffset(offsetStr, bodyTotal);

    // A zero-byte file still has to be sent, as one request with an empty body. It cannot go through the
    // paths below: SendChunked's loop would not run, and "startOffset >= bodyTotal" would report success
    // without any request at all - UploadFile would log "Successfully uploaded file" while the remote got
    // nothing, and with operation="move" the local file would then be deleted.
    if (bodyTotal == 0)
    {
        Log::Debug(L"Uploading empty file '{}' to '{}'", strLocalName, strRemoteName);
        if (HRESULT hr = SendRequest(step, hBody.value(), 0, 0, 0, remoteFileName, strLocalName, captures); FAILED(hr))
        {
            return hr;
        }

        bodySent = true;  // an empty file IS delivered, by that zero-length request
        return S_OK;
    }

    // The server says it already holds the whole file. Nothing is sent, and 'bodySent' stays false so the
    // caller keeps the local copy: this run has no evidence of what the remote actually holds.
    if (startOffset >= bodyTotal)
    {
        Log::Debug(L"Resume offset {} >= size {}; nothing to upload for '{}'", startOffset, bodyTotal, bodyPath);
        return S_OK;
    }

    Log::Debug(
        L"Uploading file '{}' to '{}' (total={} start={} remaining={})",
        strLocalName,
        strRemoteName,
        bodyTotal,
        startOffset,
        bodyTotal - startOffset);

    const HRESULT hr = step.chunkSize == 0
        ? SendWholeRemainder(step, hBody.value(), startOffset, bodyTotal, remoteFileName, strLocalName, captures)
        : SendChunked(step, hBody.value(), startOffset, bodyTotal, remoteFileName, strLocalName, captures);
    if (FAILED(hr))
    {
        // A partial transfer lands here too: the bytes that did go out are not an upload, and leaving
        // 'bodySent' false is what keeps the local file from being deleted after one.
        return hr;
    }

    bodySent = true;
    return S_OK;
}

HRESULT RESTAgent::SendWholeRemainder(
    const RESTStep& step,
    HANDLE hBody,
    uint64_t startOffset,
    uint64_t totalSize,
    const std::wstring& remoteFileName,
    const std::wstring& strLocalName,
    std::map<std::wstring, std::wstring>& captures)
{
    // One request for the whole remainder; the WinHTTP body length is a DWORD, so this caps at 4 GB PER
    // REQUEST. 'chunk_size' only helps a server that can reassemble chunks from an offset it is told
    // (Content-Range, a query parameter or a per-chunk path - see the Refuse in OutputSpec.cpp for a
    // chunked step that names none of them); against S3 that offset is never carried, so chunking there
    // silently corrupts the object instead of extending this limit. S3 needs multipart, which this
    // engine does not implement.
    const uint64_t remaining = totalSize - startOffset;
    if (remaining > MAXDWORD)
    {
        Log::Debug(
            L"{} bytes remain from offset {}; the 4 GB cap is per request, and 'chunk_size' only helps "
            L"a server that accepts a per-chunk offset (this engine cannot do S3 multipart)",
            remaining,
            startOffset);
        return E_INVALIDARG;
    }

    return SendRequest(
        step, hBody, startOffset, static_cast<DWORD>(remaining), totalSize, remoteFileName, strLocalName, captures);
}

HRESULT RESTAgent::SendChunked(
    const RESTStep& step,
    HANDLE hBody,
    uint64_t startOffset,
    uint64_t totalSize,
    const std::wstring& remoteFileName,
    const std::wstring& strLocalName,
    std::map<std::wstring, std::wstring>& captures)
{
    // Defense in depth (Validate refuses this at embed): a chunk is one DWORD-sized request, so chunk_size
    // must fit a DWORD or the cast below would truncate.
    if (step.chunkSize > MAXDWORD)
    {
        Log::Debug(L"chunk_size {} exceeds the 4 GB per-request limit", step.chunkSize);
        return E_INVALIDARG;
    }

    uint64_t cur = startOffset;
    while (cur < totalSize)
    {
        const DWORD n = static_cast<DWORD>((std::min)(step.chunkSize, totalSize - cur));
        if (HRESULT hr = SendRequest(step, hBody, cur, n, totalSize, remoteFileName, strLocalName, captures);
            FAILED(hr))
        {
            return hr;
        }

        cur += n;
    }

    return S_OK;
}

// repeat="Once": read the remote size the way the dialog's own resume does, by running the read-only steps before
// the body step and resolving the body step's offset. S_FALSE (unknown or absent) collects the set again; only a
// size the server actually reported, above zero, lets the caller call the remote copy complete.
HRESULT RESTAgent::CheckFileUpload(const std::wstring& szRemoteName, std::optional<uint64_t>& fileSize)
{
    fileSize = std::nullopt;

    const auto& steps = m_config.rest.RestSteps;
    const auto body = std::find_if(steps.begin(), steps.end(), [](const RESTStep& step) { return step.hasBody; });
    if (body == steps.end() || body->offsetExpr.empty())
    {
        Log::Debug(L"REST: '{}' has no resume offset to read the remote size from", szRemoteName);
        return S_FALSE;
    }

    // A session="once" step is NOT run here. This check is advisory (its caller logs and continues),
    // while establishing a session replays the share credential and changes server-side state, and a
    // failure here would latch m_sessionFailed and refuse every real upload afterwards. Without the
    // session the later steps would run unauthenticated and report nonsense, so decline instead.
    //
    // This costs nothing in practice: the guard above already returned S_FALSE unless the body step
    // carries an offset=, and a public-share login dialog has no resume offset.
    if (std::any_of(steps.begin(), body, [](const RESTStep& step) { return step.runOnce; }))
    {
        Log::Debug(
            L"REST: not checking the remote copy of '{}': its dialog needs a session this check does not "
            L"establish",
            szRemoteName);
        return S_FALSE;
    }

    if (UsesFileBoundPlaceholder(body->offsetExpr))
    {
        Log::Debug(L"REST: the resume offset of '{}' depends on the local file; remote size unknown", szRemoteName);
        return S_FALSE;
    }

    std::wstring remoteFileName = szRemoteName;
    if (const size_t slash = remoteFileName.find_last_of(L"/\\"); slash != std::wstring::npos)
    {
        remoteFileName = remoteFileName.substr(slash + 1);
    }

    std::map<std::wstring, std::wstring> captures;
    for (auto step = steps.begin(); step != body; ++step)
    {
        if (!IsReadOnlyRestMethod(step->method))
        {
            Log::Debug(L"REST: not running '{}' to check '{}': it changes the server", step->method, szRemoteName);
            continue;
        }

        if (StepUsesFileBoundPlaceholder(*step, m_config.rest.HttpHeaders))
        {
            Log::Debug(
                L"REST: '{}' needs the local file to check '{}'; remote size unknown", step->method, szRemoteName);
            return S_FALSE;
        }

        if (HRESULT hr = SendRequest(*step, INVALID_HANDLE_VALUE, 0, 0, 0, remoteFileName, L"", captures); FAILED(hr))
        {
            Log::Debug(L"REST: failed to check the remote copy of '{}' [{}]", szRemoteName, SystemError(hr));
            return hr;
        }
    }

    const UploadTemplateContext context {0, 0, 0, remoteFileName, L""};
    const std::optional<uint64_t> remoteSize =
        ParseRemoteSize(ResolveUploadTemplate(body->offsetExpr, context, captures));
    // 0 is unknown, not an empty remote copy: it is what a capture's default typically yields on a 404.
    if (!remoteSize.has_value() || *remoteSize == 0)
    {
        Log::Debug(L"REST: no remote copy of '{}' reported", szRemoteName);
        return S_FALSE;
    }

    fileSize = *remoteSize;
    return S_OK;
}

std::wstring Orc::RemoteUrlPath(const std::wstring& rootPath, const std::wstring& remoteName)
{
    std::wstring path = rootPath;
    std::replace(path.begin(), path.end(), L'\\', L'/');

    if (path.empty() || path.front() != L'/')
    {
        path.insert(0, 1, L'/');
    }

    if (path.back() != L'/')
    {
        path += L'/';
    }

    const size_t nameStart = remoteName.find_first_not_of(L"/\\");
    return nameStart == std::wstring::npos ? path : path + remoteName.substr(nameStart);
}

std::wstring RESTAgent::GetRemoteFullPath(const std::wstring& strRemoteName)
{
    const std::wstring scheme = (m_config.bitsMode == OutputSpec::BITSMode::HTTPS) ? L"https" : L"http";
    return scheme + L"://" + m_config.ServerName + RemoteUrlPath(m_config.RootPath, strRemoteName);
}

std::wstring RESTAgent::GetRemotePath(const std::wstring& strRemoteName)
{
    return RemoteUrlPath(m_config.RootPath, strRemoteName);
}

HRESULT RESTAgent::IsComplete(bool bReadyToExit, bool& hasFailure)
{
    hasFailure = m_hasFailure;
    return S_OK;
}

HRESULT RESTAgent::Cancel()
{
    return UnInitialize();
}

HRESULT RESTAgent::UnInitialize()
{
    // Releases the connection (child) before the session (parent), per WinHTTP handle ordering.
    m_http.Close();

    // The session captures are server-supplied and routinely secret (a session token, an upload
    // URL). Drop them with the connection rather than leaving them in memory for the process
    // lifetime; WinHTTP's cookie jar goes the same way when the session handle closes.
    m_sessionCaptures.clear();
    m_sessionEstablished = false;

    return S_OK;
}
