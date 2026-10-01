//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include <string>
#include <filesystem>
#include <map>
#include <cstdint>
#include <optional>
#include <chrono>

#include "OrcLib.h"
#include "ArchiveFormat.h"
#include "TableOutput.h"
#include "RESTStep.h"

#include "OutputSpecTypes.h"
#include "FileDisposition.h"
#include "Text/Encoding.h"
#include "Text/Fmt/OutputSpecTypes.h"

#pragma managed(push, off)

namespace Orc {

class ConfigItem;

struct ProxySettings;
struct HttpClientConfig;

class OutputSpec
{
public:
    using UploadAuthScheme = OutputSpecTypes::UploadAuthScheme;
    using UploadMethod = OutputSpecTypes::UploadMethod;
    using BITSMode = OutputSpecTypes::BITSMode;
    using UploadOperation = OutputSpecTypes::UploadOperation;
    using UploadMode = OutputSpecTypes::UploadMode;
    using Kind = OutputSpecTypes::Kind;
    using Disposition = OutputSpecTypes::Disposition;
    using Status = OutputSpecTypes::Status;
    using Encoding = OutputSpecTypes::Encoding;

    class Upload
    {
    public:
        // All five carry an in-class initializer: 'method' is an optional attribute and Configure() returns
        // early when it is absent, so an <upload> element without it must still leave a fully defined object -
        // Validate() reads Mode and bitsMode unconditionally, and ToolEmbed branches on the result.
        UploadMethod Method = UploadMethod::NoUpload;
        UploadOperation Operation = UploadOperation::NoOp;
        UploadMode Mode = UploadMode::Synchronous;
        BITSMode bitsMode = BITSMode::NoMode;
        UploadAuthScheme AuthScheme = UploadAuthScheme::Anonymous;

        std::wstring ServerName;
        std::wstring RootPath;
        std::wstring UserName;
        std::wstring Password;
        std::wstring JobName;

        std::optional<bool> bitsDeleteSmbShare = false;

        // Retry policy, method-agnostic. Extra attempts at THIS destination, each with a fresh agent, before
        // the next <upload> entry is tried (see Main::CompleteUpload). 0 keeps the historical behaviour of
        // exactly one attempt per entry.
        //
        // The per-attribute ceilings exist so a refusal can name the obviously wrong attribute (a
        // retry_delay written in milliseconds, say); kMaxRetryBudgetSeconds below is the rule that actually
        // bounds the cost, and the delay ceiling matches it so neither can be reached without the other.
        static constexpr uint32_t kDefaultRetryDelay = 30;
        static constexpr uint32_t kMaxRetry = 10;
        static constexpr uint32_t kMaxRetryDelay = 1800;

        // What the product of the two actually costs: how long a run may stand still at the very end, on
        // the main thread, with no way to interrupt it (both the wait and agent::wait are uninterruptible,
        // and the log file is closed by then). A budget spent retrying one entry is also a budget not spent
        // reaching the fallbacks, if something outside kills the process meanwhile.
        static constexpr uint32_t kMaxRetryBudgetSeconds = 1800;

        uint32_t Retry = 0;
        std::chrono::seconds RetryDelay {kDefaultRetryDelay};

        // retry / retry_delay present but unreadable as a whole non-negative number, as name="value"
        // (comma list). Non-empty => Validate refuses, rather than running with a default the operator
        // never wrote. Same contract as rest.HttpsSecurityInvalid.
        std::wstring RetryInvalid;

        std::vector<std::wstring> FilterInclude;
        std::vector<std::wstring> FilterExclude;

        // REST upload fields
        // TLS policy as OS-neutral version bits (bit value order == version order, so they compare
        // directly). A configured version is a MINIMUM/floor: the agent enables it and every higher
        // version up to what the running OS supports (see ResolveTlsVersions), so a future TLS version is
        // picked up by an existing config once this enum/SDK learns its flag - no config change needed.
        enum TlsVersion : uint8_t
        {
            kTls1_0 = 1 << 0,
            kTls1_1 = 1 << 1,
            kTls1_2 = 1 << 2,
            kTls1_3 = 1 << 3,
        };

        // Highest TLS version this build knows how to enable; raise it (add the bit above and its WinHTTP
        // flag in ToWinHttpProtocols) when a newer TLS version ships and the SDK exposes its flag.
        static constexpr uint8_t kHighestKnownTls = kTls1_3;

        // A version tagged with an OS target ("[xp]", "[seven]") is an exception applied only when
        // running on that OS, replacing 'TlsVersions' there (e.g. XP cannot negotiate TLS 1.3).
        enum class OsTarget : uint8_t
        {
            Xp,
            Seven,
        };

        // Where a SigV4 request takes its timestamp. 'Local' logs and fails on a skew rejection;
        // 'Server' permits exactly one re-signed retry against the server's Date header.
        enum class AwsClockSource : uint8_t
        {
            Local,
            Server,
        };

        // REST-only configuration, grouped. Fields keep their exact names; access gains a `rest.` segment.
        struct RestConfig
        {
            std::vector<std::wstring> HttpHeaders;  // merged from attr + child elements
            std::vector<RESTStep> RestSteps;  // ordered <rest> dialog (REST multi-step upload)
            std::wstring RestSupersededAttrs;  // REST: upload-level per-request attrs set but superseded by <rest>
                                               // (comma list; non-empty => Validate refuses, critical at embed)
            // Minimum TLS version (floor) accepted on any OS without a matching exception below; the agent
            // enables this version and every higher one the OS supports. Default https_security=
            // "tls1.3, tls1.2[xp]": floor TLS 1.3 (and up) everywhere, floor TLS 1.2 on XP (its ceiling).
            uint8_t TlsVersions = kTls1_3;
            std::map<OsTarget, uint8_t> TlsOsExceptions {{OsTarget::Xp, kTls1_2}};
            bool TlsOsDefault = false;  // true => system TLS policy, TLS 1.2 proven per connection unless Insecure
            bool TlsOsMax = false;  // true => lock on the single highest TLS version the host supports
            std::vector<std::wstring> HttpsSecurityInvalid;  // entries not read (bad tag, target, version): refused

            // Security opt-in flag (see Validate). 'Insecure' authorizes inherently-insecure transport:
            // plain HTTP, TLS < 1.2, AND disabling TLS certificate validation (all builds). The secure
            // alternative for a self-signed server is 'server_cert_pin', which needs no flag.
            bool Insecure = false;

            // Pinned server certificates, each a standard X.509 cert (PEM or base64). The agent pins the
            // public key (SPKI) extracted from these and accepts a server only if its key matches one -
            // the secure way to use a self-signed server, independent of the OS trust store, surviving
            // cert renewal with the same key. No 'insecure' flag needed. Multiple allowed (rotation).
            std::vector<std::wstring> ServerCertPins;

            bool FollowRedirects = false;

            // Proxy (SYSTEM-safe). The single 'proxy' attribute is classified by ClassifyProxy():
            //   (absent)        -> machine-wide proxy (netsh winhttp set proxy), DIRECT when none is set
            //   ""              -> no proxy (DIRECT), no discovery - same as "direct"
            //   "direct"/"none" -> no proxy
            //   "system"        -> machine-wide proxy, requested explicitly
            //   "wpad"/"auto"   -> WPAD auto-detect (DHCP/DNS)
            //   "pac:<url>"     -> PAC script at <url> (explicit)
            //   "<...>.pac"     -> PAC script (auto-detected by the URL's ".pac" path suffix)
            //   "%VAR%" left as is -> the variable is unset on this host: the upload is refused, never
            //                      silently sent direct or to a host called "%VAR%"
            //   otherwise       -> explicit named proxy "host:port"
            // Absent and empty mean different things on purpose: a config that never mentions a proxy
            // follows the machine configuration (the zero-config case), while proxy="" is the operator
            // writing down "no proxy". An empty %ORC_UPLOAD_PROXY% produces that same explicit DIRECT.
            // Authentication to the proxy is explicit-credential only (ProxyUser/ProxyPassword); the agent
            // forces AUTOLOGON HIGH so the SYSTEM/machine account is never relayed to a proxy.
            std::optional<std::wstring> Proxy;  // nullopt = no 'proxy' attribute at all (see ClassifyProxy)
            std::wstring ProxyBypass;  // WinHTTP bypass list for a named proxy, e.g. "<local>;*.corp"
            std::wstring ProxyUser;  // explicit proxy credentials (no SSO fallback)
            std::wstring ProxyPassword;
            // Anonymous => no proxy authentication. Auth is preemptive, so an authenticating proxy must name
            // an explicit scheme (Basic/NTLM/Negotiate) - Validate refuses proxy credentials with Anonymous.
            UploadAuthScheme ProxyAuthScheme = UploadAuthScheme::Anonymous;

            // AWS SigV4 (authscheme="AWSv4"). Empty unless that scheme is configured; Validate refuses
            // a credential set under any other scheme rather than ignoring it.
            struct AwsConfig
            {
                std::wstring AccessKey;
                std::wstring SecretKey;
                std::wstring SessionToken;
                std::wstring Region;  // required, no default: an implicit us-east-1 makes a forgotten
                                      // attribute a 403 that names nothing
                std::wstring Service = L"s3";
                AwsClockSource ClockSource = AwsClockSource::Local;
            };

            AwsConfig aws;
        };

        RestConfig rest;

        Upload() = default;

        HRESULT Configure(const ConfigItem& item);

        bool IsFileUploaded(const std::wstring& file_name) const;

        enum class Verdict
        {
            Ok,
            Warn,  // works, but insecure and allowed (e.g. BITS, or REST with the 'insecure' flag)
            Refuse,  // must not run: unworkable config, or insecure and not authorized (REST only)
        };

        struct Validation
        {
            Verdict verdict = Verdict::Ok;
            std::wstring message;  // empty when Ok
        };

        // Single source of truth for upload-config validation, shared by the runtime agent factory
        // and ToolEmbed (embed-time early warning). Checks functional validity (a REST config that
        // cannot work, e.g. missing server) and security (REST refuses an unauthorized insecure
        // config; other methods only warn so their existing behavior is unaffected).
        Validation Validate() const;

        // Resolve the effective TLS version set (as TlsVersion bits) for the running OS: take the floor -
        // the per-OS exception if one matches, else the baseline `TlsVersions` - then enable that version
        // and every higher known one up to the OS ceiling (XP/7 cap at TLS 1.2). Pure policy; the agent
        // maps the result to WinHTTP flags. A future TLS version is included automatically once kHighestKnownTls
        // and ToWinHttpProtocols learn it. Never returns 0: an empty floor falls back to TLS 1.2 rather than
        // to the system policy - call this only when TlsOsDefault and TlsOsMax are false.
        uint8_t ResolveTlsVersions(DWORD osMajor, DWORD osMinor) const;

    private:
        // Configure() helpers, one per <upload> config section (parsed only when a 'method' is present).
        HRESULT ParseUploadTarget(const ConfigItem& item);  // server/uri/path/credentials -> scheme + RootPath
        HRESULT ApplyServerScheme(const std::wstring& server);  // http/https/file -> bitsMode + ServerName
        void NormalizeRootPath(const std::wstring& root);  // slash normalization -> RootPath
        void ParseUploadCommon(const ConfigItem& item);  // operation/mode/authscheme/smb-delete/filters
        void ParseHttpAttributes(const ConfigItem& item);  // http_header, https_security (TLS), <header> list
        void ParseRestSteps(const ConfigItem& item);  // superseded-attr detection + the <rest> dialog
        void ParseProxyAndSecurity(const ConfigItem& item);  // insecure, server_cert_pin, proxy_*
        void EmitProxyAdvisories();  // advisory warnings (hard rules live in Validate)

        // Validate() helpers. Each returns Verdict::Ok to mean "passed, keep checking"; Validate() returns
        // the first non-Ok verdict, and ValidateTransportSecurity() returns the final Ok/Warn/Refuse.
        Validation ValidateRestSteps() const;  // REST functional validity + per-template placeholders
        Validation ValidateRestStep(const RESTStep& step) const;  // per-step placeholder/capture/body/chunk checks
        Validation ValidateRestAuth() const;  // REST server-auth fail-closed rules
        Validation ValidateProxy() const;  // proxy credential/scheme + server_cert_pin rules
        Validation ValidateRetryPolicy() const;  // retry / retry_delay bounds and mode compatibility
        Validation ValidateTransportSecurity() const;  // weak-transport aspect + insecure verdict (final)
    };

    Kind Type = Kind::None;
    Kind supportedTypes;

    std::wstring Pattern;
    std::wstring FileName;
    std::wstring Path;

    Disposition disposition = Disposition::CreateNew;
    Status creationStatus = Status::StatusUnknown;

    std::wstring ConnectionString;
    std::wstring TableName;
    std::wstring TableKey;

    TableOutput::Schema Schema;

    ArchiveFormat ArchiveFormat = ArchiveFormat::Unknown;
    std::wstring Compression;
    std::wstring Password;

    // Ordered upload fallback list: front() is the primary, each later entry is tried only when the ones before it
    // failed. An <upload> that fails Configure is skipped, so this may be shorter than the configuration.
    std::vector<std::shared_ptr<Upload>> UploadOutputs;

public:
    Encoding OutputEncoding = Encoding::UTF8;
    LPCWSTR szSeparator = L",";
    LPCWSTR szQuote = L"\"";

    OutputSpec() noexcept = default;
    OutputSpec(OutputSpec&&) noexcept = default;
    OutputSpec(const OutputSpec&) = default;
    OutputSpec& operator=(const OutputSpec&) = default;
    OutputSpec& operator=(OutputSpec&&) noexcept = default;

    bool IsDirectory() const;
    bool IsFile() const;
    bool IsRegularFile() const;
    bool IsTableFile() const;
    bool IsStructuredFile() const;
    bool IsArchive() const;

    static bool IsPattern(const std::wstring& pattern);

    static HRESULT ApplyPattern(const std::wstring& pattern, const std::wstring& name, std::wstring& fileName);

    static OutputSpec::Disposition ToDisposition(Orc::FileDisposition disposition);

    static OutputSpec::Encoding ToEncoding(Text::Encoding);

    static std::filesystem::path Resolve(const std::wstring& path);

    HRESULT Configure(
        OutputSpec::Kind supportedTypes,
        const std::wstring& inputString,
        std::optional<std::filesystem::path> parent = std::nullopt);

    HRESULT
    Configure(const std::wstring& inputString, std::optional<std::filesystem::path> parent = std::nullopt)
    {
        return Configure(supportedTypes, inputString, std::move(parent));
    };

    HRESULT Configure(
        OutputSpec::Kind supportedTypes,
        const ConfigItem& item,
        std::optional<std::filesystem::path> parent = std::nullopt);

    HRESULT
    Configure(const ConfigItem& item, std::optional<std::filesystem::path> parent = std::nullopt)
    {
        return Configure(supportedTypes, item, std::move(parent));
    };
};

Orc::FileDisposition ToFileDisposition(OutputSpec::Disposition disposition);

Text::Encoding ToEncoding(OutputSpec::Encoding encoding);

// The single OutputSpec<->HTTP boundary: translate a REST <upload> config into the generic transport
// types so HttpClient/HttpProxy stay free of OutputSpec. Defined in OutputSpec.cpp. ClassifyProxy is
// also exercised directly by the unit tests (proxy-string classification).
ProxySettings ClassifyProxy(const OutputSpec::Upload& upload);
HttpClientConfig BuildHttpClientConfig(const OutputSpec::Upload& upload);

// The TLS selection chain, exposed for the unit tests that guard it against a downgrade: the <upload>
// https_security policy (ApplyHttpsSecurity), then OutputSpec::Upload::ResolveTlsVersions, then the
// configuration-to-transport bit mapping (ToHttpTlsVersions); HttpClient maps the result to WinHTTP.
void ApplyHttpsSecurity(const std::wstring& value, OutputSpec::Upload::RestConfig& rest);
uint8_t ToHttpTlsVersions(uint8_t versions);

// https_security="os-max": the TLS versions (OutputSpec::Upload::kTls*) HttpClient may lock on. TLS 1.3 alone
// on a build that negotiates it; otherwise TLS 1.2, down to TLS 1.0 when 'insecure' allows it. None when the
// build is unknown: guessing an older build would lock a TLS 1.3 host on TLS 1.2.
uint8_t OsMaxTlsCandidates(std::optional<DWORD> osBuild, bool insecure);

// The 'server_cert_pin' attribute as one entry per certificate: split on ',' and ';', trimmed, empty entries
// dropped. Whether each entry is a certificate is Validate's check.
std::vector<std::wstring> SplitServerCertPins(const std::wstring& value);

// True when an address-bearing attribute still carries a "%NAME%" environment reference after expansion,
// meaning that variable is not set on this host. URI percent-escapes ("%20") are not references.
bool HasUnexpandedEnvReference(const std::wstring& value);

}  // namespace Orc

#pragma managed(pop)
