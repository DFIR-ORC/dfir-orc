//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include "UploadAgent.h"
#include "RESTStep.h"
#include "Network/Http/HttpClient.h"

#include <map>
#include <cstdint>

namespace Orc {

class RESTAgent : public UploadAgent
{
public:
    RESTAgent(
        UploadMessage::ISource& msgSource,
        UploadMessage::ITarget& msgTarget,
        UploadNotification::ITarget& target);
    ~RESTAgent();

    HRESULT Initialize() override;

    HRESULT UploadFile(
        const std::wstring& strLocalName,
        const std::wstring& strRemoteName,
        bool bDeleteWhenCopied,
        const std::shared_ptr<const UploadMessage>& request) override;

    HRESULT IsComplete(bool bReadyToExit, bool& hasFailure) override;

    HRESULT Cancel() override;

    HRESULT UnInitialize() override;

    HRESULT CheckFileUpload(const std::wstring& szRemoteName, std::optional<uint64_t>& fileSize) override;

    std::wstring GetRemoteFullPath(const std::wstring& strRemoteName) override;

    std::wstring GetRemotePath(const std::wstring& strRemoteName) override;

private:
    HttpClient m_http;
    bool m_hasFailure = false;

    // Set once a request met a 401/407 its step does not accept. From then on SendRequest refuses without sending, for
    // every file and check left: presenting the same credentials again only adds failed logons against the account.
    bool m_authRejected = false;

    // session="once" state. The once-steps establish the session (a cookie, and the captures the
    // later per-file steps read) and run at most once for the life of the agent.
    std::map<std::wstring, std::wstring> m_sessionCaptures;
    bool m_sessionEstablished = false;

    // Set when the once-steps failed. From then on every file fails without sending: a login the
    // server just refused does not become correct by being replayed, and replaying it per file
    // hammers the share password at a server that may rate-limit or lock the share.
    bool m_sessionFailed = false;

    // Run the session="once" steps if they have not run yet. Idempotent; the recorded outcome is
    // returned on every later call.
    HRESULT EstablishSession();

    // Record and log a 401/407 refusal: who challenged, the schemes offered, and what the configuration should be
    // checked for. Never a header value or a credential.
    void OnAuthenticationRejected(const HttpResponse& resp);

    // Run the ordered <rest> dialog to upload one file. 'bodySent' reports whether a body request was
    // actually issued and accepted: every step can succeed while nothing is sent (a dialog with no body
    // step, or a resume offset already covering the file), and the caller must not call that an upload.
    HRESULT RunStepsForFileUpload(
        const std::wstring& strLocalName,
        const std::wstring& strRemoteName,
        uint64_t totalSize,
        bool& bodySent);

    // Send ONE HTTP request for `step`. hBodyFile == INVALID_HANDLE_VALUE => no request body (a
    // control step such as MKCOL/MOVE/PROPFIND); otherwise stream `length` bytes from `hBodyFile`
    // at `offset` as the body (one chunk). Reads `captures` for placeholder substitution and may
    // populate it from the response (a <response> capture).
    HRESULT SendRequest(
        const RESTStep& step,
        HANDLE hBodyFile,
        uint64_t offset,
        DWORD length,
        uint64_t totalSize,
        const std::wstring& remoteFileName,
        const std::wstring& inputPath,
        std::map<std::wstring, std::wstring>& captures);

    // Pre-resolve every {upload.random:<spec>} token used by this file's dialog ONCE into 'captures'.
    HRESULT ResolveRandomTokens(std::map<std::wstring, std::wstring>& captures) const;

    // Stream the single body step: open the (clamped) source, then send the remainder whole or chunked.
    // Sets 'bodySent' only once a body request has succeeded (a zero-length one for an empty file).
    HRESULT RunBodyStep(
        const RESTStep& step,
        const std::wstring& strLocalName,
        const std::wstring& strRemoteName,
        const std::wstring& remoteFileName,
        uint64_t totalSize,
        std::map<std::wstring, std::wstring>& captures,
        bool& bodySent);

    // Send the whole remaining file [startOffset, totalSize) in one request (caps at 4 GB / DWORD).
    HRESULT SendWholeRemainder(
        const RESTStep& step,
        HANDLE hBody,
        uint64_t startOffset,
        uint64_t totalSize,
        const std::wstring& remoteFileName,
        const std::wstring& strLocalName,
        std::map<std::wstring, std::wstring>& captures);

    // Send the remaining file in chunk_size-byte requests from startOffset.
    HRESULT SendChunked(
        const RESTStep& step,
        HANDLE hBody,
        uint64_t startOffset,
        uint64_t totalSize,
        const std::wstring& remoteFileName,
        const std::wstring& strLocalName,
        std::map<std::wstring, std::wstring>& captures);
};

// The URL path of 'remoteName' under 'rootPath': a single leading '/', and exactly one '/' before the name
// whatever separators the root carries. Used to name the remote file in messages; each <rest> step builds its
// own request path.
std::wstring RemoteUrlPath(const std::wstring& rootPath, const std::wstring& remoteName);

}  // namespace Orc
