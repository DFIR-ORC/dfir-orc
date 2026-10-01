# Run the REST-upload test scenarios against a live sink host and verify each result.
#
# Nothing about a particular test bench is hardcoded here. The address of the host, how to reach it
# over ssh, where the DFIR-ORC binary lives and which fixtures to use all come from a settings file
# that is NEVER committed:
#
#     tests/RestAgent/vmtest.local.psd1      (git-ignored; copy vmtest.local.example.psd1)
#
# That keeps site details - host addresses, ssh aliases, local paths, and any fixture carrying a
# certificate pin or a credential - out of the repository. Every setting can also be overridden on
# the command line, which wins over the file.
#
# The vm-*.xml fixtures carry no address either: server= and proxy= are %ORC_UPLOAD_SERVER% /
# %ORC_UPLOAD_PROXY%, expanded at parse time by OutputSpec.cpp. This runner sets those variables per
# scenario before launching DFIR-ORC, which the child process inherits.
#
# Usage:
#   .\run-vmtest.ps1 -Tag http                 # one scenario
#   .\run-vmtest.ps1 -Tag http,chunked         # several
#   .\run-vmtest.ps1 -Tag all                  # every scenario the configured squid mode supports
#   .\run-vmtest.ps1 -Tag all -VmHost 10.0.0.5 # override one setting for this run
#   .\run-vmtest.ps1 -ListTags                 # show the scenarios and exit
#
# DFIR-ORC links requireAdministrator: run this from an ELEVATED shell.
param(
    [string[]]$Tag,
    [switch]$ListTags,
    [string]$SettingsFile,
    [string]$VmHost,
    [string]$SshHost,
    [string]$Orc,
    [string]$ConfigDir,
    [string]$MainConfig,
    [string]$OutRoot,
    [string]$SafeTemp,
    [ValidateSet('ntlm', 'basic', 'none')][string]$SquidMode
)

$ErrorActionPreference = 'Continue'
$scriptDir = $PSScriptRoot

# ---------------------------------------------------------------------------------------------
# Settings: built-in defaults <- vmtest.local.psd1 <- command line.
# ---------------------------------------------------------------------------------------------
$settings = @{
    VmHost        = '127.0.0.1'
    SshHost       = ''                 # ssh alias for the sink host; empty => remote checks are skipped
    Orc           = ''                 # capsule (DFIR-ORC.exe) or a raw per-arch build
    OrcPrefixArgs = @()                # @('WolfLauncher') for a raw build, empty for a capsule
    ConfigDir     = $scriptDir         # where the vm-*.xml fixtures live; point at your local copies
    MainConfig    = ''                 # /config= file, only for a binary with no embedded config
    OutRoot       = (Join-Path $env:TEMP 'orc-vmtest')   # outside the repo: run logs name the host
    SafeTemp      = (Join-Path $env:TEMP 'orc-vmtest-safetemp')
    ReceivedDir   = '/root/received/up'
    ClearScript   = '/root/clear_received.sh'
    BigSeed       = ''                 # incompressible local file the big-* scenarios build their payload from
    SquidMode     = 'none'
    # The authoritative port map: give a new sink a port from here, never one that merely looks free
    # on the bench. 8455 looked free only because SeaweedFS had not been provisioned yet - its volume
    # server owns it, beside 8454 and 8456 - so installing SeaweedFS later would have collided and
    # failed every seaweedfs scenario, not just the one sink.
    Ports         = @{ Http = 8080; Https = 8443; TlsCap = 8444; TlsCap11 = 8445; Challenge = 8447; Nextcloud = 8448; LexChunks = 8449; Minio = 8450; RustFs = 8452; SeaweedFs = 8454; ShareLogin = 8460; Proxy = 8888; ProxyAuth = 8889; Squid = 3128 }
}

if (-not $SettingsFile) {
    $SettingsFile = Join-Path $scriptDir 'vmtest.local.psd1'
}

if (Test-Path $SettingsFile) {
    # Import-PowerShellDataFile parses data only - the settings file cannot run code.
    $local = Import-PowerShellDataFile -Path $SettingsFile
    foreach ($k in $local.Keys) {
        if (-not $settings.ContainsKey($k)) {
            Write-Host "warn  : unknown setting '$k' in $SettingsFile"
        }

        $settings[$k] = $local[$k]
    }
    Write-Host "config: $SettingsFile"
}
else {
    Write-Host "config: none ($SettingsFile not found; using defaults - copy vmtest.local.example.psd1)"
}

foreach ($p in 'VmHost', 'SshHost', 'Orc', 'ConfigDir', 'MainConfig', 'OutRoot', 'SafeTemp', 'SquidMode') {
    $v = (Get-Variable -Name $p -ValueOnly -ErrorAction SilentlyContinue)
    if ($v) { $settings[$p] = $v }
}

# A relative MainConfig would be resolved by DFIR-ORC against whatever directory the shell happens to
# be in, and a config it cannot find aborts the run before any upload - twenty scenarios reporting
# "sink received nothing", none of them about the sink. Anchor it on this script instead, which is
# where the example file lives.
if ($settings.MainConfig -and -not [System.IO.Path]::IsPathRooted($settings.MainConfig)) {
    $settings.MainConfig = Join-Path $scriptDir $settings.MainConfig
}

$ports     = $settings.Ports
$httpUrl   = "http://$($settings.VmHost):$($ports.Http)"
$httpsUrl  = "https://$($settings.VmHost):$($ports.Https)"
$tlsCapUrl = "https://$($settings.VmHost):$($ports.TlsCap)"   # sink capped at TLS 1.2 (--max-tls tls1.2)
$tlsCap11Url = "https://$($settings.VmHost):$($ports.TlsCap11)"   # sink capped at TLS 1.1 (--max-tls tls1.1)
# The build from which https_security="os-max" locks on TLS 1.3 (OsMaxTlsCandidates).
$hasTls13  = [Environment]::OSVersion.Version.Build -ge 20348
# Whether a system-policy connection (https_security="os-default") may complete a TLS 1.1 handshake at all: WinHTTP's
# DefaultSecureProtocols when set (0x200 = TLS 1.1), else the Windows default, which stops at TLS 1.2 from Windows 11.
$winHttpDefault = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Internet Settings\WinHttp' -ErrorAction SilentlyContinue).DefaultSecureProtocols
$osDefaultOffersTls11 = if ($null -ne $winHttpDefault) { ($winHttpDefault -band 0x200) -ne 0 } else { [Environment]::OSVersion.Version.Build -lt 22000 }
$challengeUrl = "https://$($settings.VmHost):$($ports.Challenge)"   # sink answering each request first with a 401

# Nextcloud (vm/setup_nextcloud.sh) is a real server, not the python sink: it stores the upload in its
# own account collection, so those scenarios override where the runner looks and what it wipes.
$nextcloudUrl = "https://$($settings.VmHost):$($ports.Nextcloud)"
$ncReceived   = '/var/nextcloud-data/orc/files/orc-upload'
$ncClear      = '/root/clear_nextcloud.sh'

# The public share's own collection. A share is a folder of the same account, so it is the same instance
# and the same clear script (which wipes both collections); only where the upload lands differs.
$ncShareReceived = '/var/nextcloud-data/orc/files/orc-share'

# The sink that assembles WebDAV chunks lexicographically instead of numerically (--chunk-sort), for the
# negative scenario proving offset-named chunks depend on the numeric order.
$lexChunksUrl = "https://$($settings.VmHost):$($ports.LexChunks)"

# The sink mocking a SERVER-RENDERED password-protected public share login (--share-login), started by
# start_sinks.sh with token 'orcvmshare1' / password 'orcvmshare1-secret' - see vm-form-login.xml.
$shareLoginUrl = "https://$($settings.VmHost):$($ports.ShareLogin)"
# The fixture's path is /public.php/dav/files/orcvmshare1/..., not /up/..., so - like Nextcloud/MinIO -
# this scenario needs its own ReceivedDir; clear_received.sh only wipes /root/received/up, so it also
# needs its own ClearScript (an inline command; no separate script file to deploy).
$shareLoginReceived = '/root/received/public.php/dav/files/orcvmshare1'
$shareLoginClear = "rm -rf $shareLoginReceived && mkdir -p $shareLoginReceived"

# MinIO (vm/setup_minio.sh) stores an upload as an S3 object - <bucket>/<key>/xl.meta plus part files,
# small ones inlined - so there is no file on disk to hash. Its scenario reads the object back through
# the S3 API first (FetchScript) into a plain directory, which is the one the runner then looks at.
$minioUrl      = "https://$($settings.VmHost):$($ports.Minio)"
$minioReceived = '/root/received-minio'
$minioClear    = '/root/clear_minio.sh'
$minioFetch    = '/root/fetch_minio.sh'

# RustFS (vm/setup_rustfs.sh) is the second S3 server, and stores objects the same way, so it needs the
# same export step. Two implementations answering one fixture is what shows the dialog is S3 rather
# than one vendor's reading of it.
$rustfsUrl      = "https://$($settings.VmHost):$($ports.RustFs)"
$rustfsReceived = '/root/received-rustfs'
$rustfsClear    = '/root/clear_rustfs.sh'
$rustfsFetch    = '/root/fetch_rustfs.sh'

# The signed counterparts of 'minio'/'rustfs': same servers, a SEPARATE bucket (orc-signed) with no
# anonymous grant at all, uploaded to with authscheme="AWSv4". setup_minio.sh/setup_rustfs.sh
# provision it beside the anonymous one, unchanged.
$minioSigv4Received  = '/root/received-minio-sigv4'
$minioSigv4Clear     = '/root/clear_minio_sigv4.sh'
$minioSigv4Fetch     = '/root/fetch_minio_sigv4.sh'
$rustfsSigv4Received = '/root/received-rustfs-sigv4'
$rustfsSigv4Clear    = '/root/clear_rustfs_sigv4.sh'
$rustfsSigv4Fetch    = '/root/fetch_rustfs_sigv4.sh'

# SeaweedFS (vm/setup_seaweedfs.sh) is the third S3 server, and the least related to the other two: an
# S3 gateway over a distributed store rather than another S3 clone. Its objects live as chunks inside
# volume files addressed through a filer, so like the other two it needs the export step before the
# byte comparison.
$seaweedUrl      = "https://$($settings.VmHost):$($ports.SeaweedFs)"
$seaweedReceived = '/root/received-seaweedfs'
$seaweedClear    = '/root/clear_seaweedfs.sh'
$seaweedFetch    = '/root/fetch_seaweedfs.sh'
$seaweedSigv4Received = '/root/received-seaweedfs-sigv4'
$seaweedSigv4Clear    = '/root/clear_seaweedfs_sigv4.sh'
$seaweedSigv4Fetch    = '/root/fetch_seaweedfs_sigv4.sh'

# The big-* scenarios need a local file that does not compress, and they need it in sizes no single
# file has: %ORC_BIG_SOURCES% is the 'copy /b' source list wolf-main-big.xml concatenates, so the
# seed named once, twice or three times IS the size under test, and only the seed sits on the disk
# between runs. Absent or unset, those scenarios skip with a reason rather than fail.
$bigSeedOk = [bool]$settings.BigSeed -and (Test-Path -LiteralPath $settings.BigSeed)
$bigSkip   = if ($bigSeedOk) { $null }
             else { "BigSeed is not set or not found ('$($settings.BigSeed)') - see vmtest.local.example.psd1" }

function Get-BigSources {
    param([int]$Times)

    if (-not $bigSeedOk) { return '' }

    return ((, ("`"$($settings.BigSeed)`"")) * $Times) -join '+'
}

# Each scenario: the fixture, whether the upload is expected to be refused, the ORC_UPLOAD_*
# environment it resolves against, and which squid mode (if any) it needs. Credentials and pins stay
# in the fixtures - they are each test's subject - so only addresses live here.
$scenarios = [ordered]@{
    'http'            = @{ Config = 'vm-http.xml';                  Env = @{ ORC_UPLOAD_SERVER = $httpUrl } }
    'chunked'         = @{ Config = 'vm-http-chunked.xml';          Env = @{ ORC_UPLOAD_SERVER = $httpUrl } }
    'https-pin'       = @{ Config = 'vm-https-pin.xml';             Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    'https-pin-bad'   = @{ Config = 'vm-https-pin-bad.xml'; Negative = $true; Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    # Every request answered first with a 401 and resent: the pin must pass on the resend too. ExpectLog proves the
    # resend happened, so a sink that stopped challenging fails this scenario instead of passing it untested.
    # ExpectLog matches the stable prefix only: DescribeChallenge renders the rest as "HTTP 401 from the server,
    # offering <schemes>", and the scheme list is the server's to choose.
    'pin-resend'      = @{ Config = 'vm-https-pin-resend.xml'; ExpectLog = 'Authentication challenge (HTTP 401'; Env = @{ ORC_UPLOAD_SERVER = $challengeUrl } }
    # The same challenge on an anonymous upload is not resent: nothing could answer it, and the body would be sent
    # again for nothing. ExpectLog proves the 401 was reached, so a refusal for another reason does not pass.
    'challenge-anonymous' = @{ Config = 'vm-https-challenge-anonymous.xml'; Negative = $true; ExpectLog = 'failed with status 401'; Env = @{ ORC_UPLOAD_SERVER = $challengeUrl } }
    'https-basic'     = @{ Config = 'vm-https-basic.xml';           Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    'https-insecure'  = @{ Config = 'vm-https-insecure.xml';        Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    'redirect'        = @{ Config = 'vm-https-redirect.xml';        Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    'redirect-noflow' = @{ Config = 'vm-https-redirect-noflow.xml'; Negative = $true; Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    # A pinned request redirected to another server (the ports are the sink's, see vm/start_sinks.sh): one that
    # serves the pinned key must be reached, and one serving another key must be refused - which only holds if
    # the pin is checked again on the redirected connection. The first is the control for the second.
    'redirect-other-host' = @{ Config = 'vm-https-redirect-other-host.xml'; Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    'redirect-other-key'  = @{ Config = 'vm-https-redirect-other-key.xml'; Negative = $true; Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    # TLS version selection against sinks that top out below what the client requires. Each refusal names the
    # versions the client enabled (ExpectLog), so it proves the floor held rather than any failure at all; the
    # 'tls-ok' and 'tls11-ok' controls prove each capped sink completes a handshake at its own ceiling.
    'tls-downgrade'   = @{ Config = 'vm-https-tls13.xml'; Negative = $true; ExpectLog = 'this client requires tls1.3'; Env = @{ ORC_UPLOAD_SERVER = $tlsCapUrl } }
    'tls-ok'          = @{ Config = 'vm-https-tls12.xml';            Env = @{ ORC_UPLOAD_SERVER = $tlsCapUrl } }
    'tls-default-downgrade'   = @{ Config = 'vm-https-tls-default.xml'; Negative = $true; ExpectLog = 'this client requires tls1.3'; Env = @{ ORC_UPLOAD_SERVER = $tlsCapUrl } }
    'tls-exception-downgrade' = @{ Config = 'vm-https-tls13-os-exceptions.xml'; Negative = $true; ExpectLog = 'this client requires tls1.3'; Env = @{ ORC_UPLOAD_SERVER = $tlsCapUrl } }
    # os-max locks on TLS 1.3 from build 20348: the capped sink must refuse it. An older host locks on TLS 1.2,
    # which that sink accepts, so there the same run is a positive one.
    'os-max-downgrade' = if ($hasTls13) {
        @{ Config = 'vm-https-os-max.xml'; Negative = $true; ExpectLog = 'this client requires tls1.3'; Env = @{ ORC_UPLOAD_SERVER = $tlsCapUrl } }
    } else {
        @{ Config = 'vm-https-os-max.xml'; ExpectLog = 'locked on tls1.2'; Env = @{ ORC_UPLOAD_SERVER = $tlsCapUrl } }
    }
    'os-max-ok'       = @{ Config = 'vm-https-os-max.xml'; ExpectLog = $(if ($hasTls13) { 'locked on tls1.3' } else { 'locked on tls1.2' }); Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    'tls11-ok'        = @{ Config = 'vm-https-tls11-insecure.xml';   Env = @{ ORC_UPLOAD_SERVER = $tlsCap11Url } }
    'tls12-downgrade' = @{ Config = 'vm-https-tls12-floor.xml'; Negative = $true; ExpectLog = 'this client requires tls1.2, tls1.3'; Env = @{ ORC_UPLOAD_SERVER = $tlsCap11Url } }
    'tls12-exception-downgrade' = @{ Config = 'vm-https-tls12-os-exceptions.xml'; Negative = $true; ExpectLog = 'this client requires tls1.2, tls1.3'; Env = @{ ORC_UPLOAD_SERVER = $tlsCap11Url } }
    # Tests the per-connection TLS 1.2 proof, which only runs after the system policy completed a TLS 1.1 handshake.
    'os-default-downgrade' = @{ Config = 'vm-https-os-default.xml'; Negative = $true; SkipReason = $(if (-not $osDefaultOffersTls11) { 'the WinHTTP default protocols of this host exclude TLS 1.1, so the handshake fails before the per-connection proof runs (see README)' }); ExpectLog = 'the connection negotiated TLS 1.1'; Env = @{ ORC_UPLOAD_SERVER = $tlsCap11Url } }
    'os-max-insecure-downgrade' = if ($hasTls13) {
        @{ Config = 'vm-https-os-max-insecure.xml'; Negative = $true; ExpectLog = 'this client requires tls1.3'; Env = @{ ORC_UPLOAD_SERVER = $tlsCap11Url } }
    } else {
        @{ Config = 'vm-https-os-max-insecure.xml'; Negative = $true; ExpectLog = 'this client requires tls1.2'; Env = @{ ORC_UPLOAD_SERVER = $tlsCap11Url } }
    }
    # The two below say what an ABSENT 'proxy' attribute means versus an empty one. Both deliver
    # either way, so the byte-exact verdict alone does not prove the path taken: run them with
    # 'netsh winhttp set proxy <VmHost>:8888' on the ORC host and read /root/proxy.log on the sink -
    # 'proxy-machine' must appear there as a CONNECT, 'proxy-none' must not. Remember to
    # 'netsh winhttp reset proxy' afterwards; it is a machine-wide setting.
    'proxy-machine'   = @{ Config = 'vm-proxy-machine.xml';         Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    'proxy-none'      = @{ Config = 'vm-proxy-none.xml';            Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    'proxy'           = @{ Config = 'vm-proxy.xml';                 Env = @{ ORC_UPLOAD_SERVER = $httpsUrl; ORC_UPLOAD_PROXY = "$($settings.VmHost):$($ports.Proxy)" } }
    # Same fixture, with %ORC_UPLOAD_PROXY% deliberately left unset: the reference reaches the agent
    # unexpanded, names no reachable host, and must refuse the upload instead of connecting direct.
    'proxy-unset'     = @{ Config = 'vm-proxy.xml'; Negative = $true; Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    'proxy-auth'      = @{ Config = 'vm-proxy-auth.xml';            Env = @{ ORC_UPLOAD_SERVER = $httpsUrl; ORC_UPLOAD_PROXY = "$($settings.VmHost):$($ports.ProxyAuth)" } }
    # Byte-exact here means the launcher noticed that attempt #1 failed and ran the fallback.
    'fallback'        = @{ Config = 'vm-fallback.xml';              Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    # Same shape, but the fallback excludes the archive: nothing may arrive, which only holds if the
    # replayed request is re-decided against the entry it is replayed to.
    'fallback-filtered' = @{ Config = 'vm-fallback-filtered.xml'; Negative = $true; Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    # Three attempts at ONE dead destination. The sink receiving nothing is also what a build with no
    # retry produces, so the verdict is the ExpectLog naming the LAST retry, not the empty sink.
    'retry-exhausted' = @{ Config = 'vm-retry-exhausted.xml'; Negative = $true
                           ExpectLog = 'attempt 2/3 failed; retrying in'
                           Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    # Entry #1 retries twice against a dead port before entry #2 is tried at all (exhaust-then-advance,
    # not round-robin). Byte-exact says the fallback still delivered; the ExpectLog says entry #1 really
    # retried first - falling straight through would deliver the same bytes.
    'retry-then-fallback' = @{ Config = 'vm-retry-then-fallback.xml'
                               ExpectLog = 'Upload #1 failed after 2 attempt(s)'
                               Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    # The mirror: the PRIMARY excludes the archive and its destination is dead, the fallback asks for
    # everything. Receiving it proves the record is not gated on what the first entry accepted.
    'fallback-record' = @{ Config = 'vm-fallback-record.xml';      Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    # Sink seeded with a file larger than the archive, so the resume offset covers it and nothing is
    # sent. KeepsLocal: with operation="move", the archive surviving IS the assertion.
    'resume-complete' = @{ Config = 'vm-resume-complete.xml'; KeepsLocal = $true
                           Seed = "dd if=/dev/urandom of=$($settings.ReceivedDir)/SystemInfo.7z bs=1024 count=1024 2>/dev/null"
                           Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    'squid-basic'     = @{ Config = 'vm-proxy-squid-basic.xml';     Squid = 'basic'; Env = @{ ORC_UPLOAD_SERVER = $httpsUrl; ORC_UPLOAD_PROXY = "$($settings.VmHost):$($ports.Squid)" } }
    'squid-ntlm-dom'  = @{ Config = 'vm-proxy-squid-ntlm-dom.xml';  Squid = 'ntlm'; Env = @{ ORC_UPLOAD_SERVER = $httpsUrl; ORC_UPLOAD_PROXY = "$($settings.VmHost):$($ports.Squid)" } }
    'squid-ntlm-bare' = @{ Config = 'vm-proxy-squid-ntlm.xml'; Negative = $true; Squid = 'ntlm'; Env = @{ ORC_UPLOAD_SERVER = $httpsUrl; ORC_UPLOAD_PROXY = "$($settings.VmHost):$($ports.Squid)" } }
    'squid-ntlm-nopin'= @{ Config = 'vm-proxy-squid-ntlm-nopin.xml'; Negative = $true; Squid = 'ntlm'; Env = @{ ORC_UPLOAD_SERVER = $httpsUrl; ORC_UPLOAD_PROXY = "$($settings.VmHost):$($ports.Squid)" } }
    # The WebDAV dialog against the python sink: MKCOL/405, a control step after the body step, and MOVE
    # with a Destination header, none of which any other sink scenario reaches. The Nextcloud tags below
    # confirm a real server accepts the same shape; these are where the mechanisms themselves are tested.
    'webdav'          = @{ Config = 'vm-webdav.xml'; ExpectLog = "MOVE '/up/staging/"
                           Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    # One request per chunk, each to its own offset-named path, assembled by the sink on MOVE. chunk_size
    # is tiny so the rig's ~900 byte archive really splits; ExpectLog names the SECOND chunk, since a
    # single-chunk upload would arrive intact and pass the byte comparison without testing anything.
    'webdav-chunked'  = @{ Config = 'vm-webdav-chunked.xml'; ExpectLog = "/64' -> "
                           Env = @{ ORC_UPLOAD_SERVER = $httpsUrl } }
    # The same upload against the sink that assembles those chunks in the WRONG order. Nothing is refused
    # and the archive does arrive, so this is scored on the sha256 DIFFERING: it proves the byte-exact
    # check discriminates, and that the numeric order the other scenarios rely on is load-bearing.
    'webdav-chunked-lex' = @{ Config = 'vm-webdav-chunked-lex.xml'; ExpectMismatch = $true
                           ExpectLog = "MOVE '/up/uploads/"
                           Env = @{ ORC_UPLOAD_SERVER = $lexChunksUrl } }
    # The mock server-rendered public-share login (rest_test_server.py --share-login on port 8460, see
    # start_sinks.sh and README "Form login"): GET reads three hidden form inputs and a session cookie,
    # POST logs in with the share password, and the PUT then uploads under that cookie alone - no HTTP
    # credential of its own. Stock Nextcloud cannot serve this dialog (see vm-form-login.xml's header
    # comment and the design doc), so this proves the ENGINE'S mechanism against a mock; a real Nextcloud
    # is verified by hand, not by this rig (rest-form-login.xml). ExpectLog names the POST to /s/<token>
    # specifically - a fixture that degraded to a plain PUT would still pass the byte comparison alone.
    'form-login'      = @{ Config = 'vm-form-login.xml'; ReceivedDir = $shareLoginReceived; ClearScript = $shareLoginClear
                           ExpectLog = "POST '/s/orcvmshare1' ->"
                           Env = @{ ORC_UPLOAD_SERVER = $shareLoginUrl } }
    # The same sink and token, with the GET/POST login steps removed: just the PUT, with no session
    # cookie ever obtained. Proves the upload path is genuinely cookie-gated, not merely reachable - the
    # sink must refuse it with 401 and nothing may arrive. ExpectLog names the 401 specifically, so a
    # refusal for some other reason (a bad path, a TLS failure) does not pass this scenario by accident.
    # Same ReceivedDir/ClearScript as 'form-login': without them this would check the wrong (and never
    # cleared) directory and could pass on a leftover file from an earlier 'form-login' run instead of
    # on this run genuinely receiving nothing.
    'form-login-nocookie' = @{ Config = 'vm-form-login-nocookie.xml'; Negative = $true
                           ReceivedDir = $shareLoginReceived; ClearScript = $shareLoginClear
                           ExpectLog = 'failed with status 401'
                           Env = @{ ORC_UPLOAD_SERVER = $shareLoginUrl } }
    # Against a real Nextcloud rather than the python sink. Each ExpectLog names a request only that
    # fixture's shape issues, so a fixture that degraded to a plain PUT fails instead of passing on the
    # byte comparison, which a plain PUT would also satisfy.
    'nextcloud'       = @{ Config = 'vm-nextcloud.xml'; ReceivedDir = $ncReceived; ClearScript = $ncClear
                           ExpectLog = "PUT '/remote.php/dav/files/orc/orc-upload/"
                           Env = @{ ORC_UPLOAD_SERVER = $nextcloudUrl } }
    'nextcloud-chunked' = @{ Config = 'vm-nextcloud-chunked.xml'; ReceivedDir = $ncReceived; ClearScript = $ncClear
                           ExpectLog = "MOVE '/remote.php/dav/uploads/orc/"
                           Env = @{ ORC_UPLOAD_SERVER = $nextcloudUrl } }
    # The one that actually splits: at chunk_size="64" the rig's ~900 byte archive becomes 14 chunks whose
    # lexicographic order differs from their numeric one, so byte-exact proves Nextcloud sorted them
    # numerically. ExpectLog names the SECOND chunk - with one chunk the file would still arrive intact.
    'nextcloud-chunked-many' = @{ Config = 'vm-nextcloud-chunked-many.xml'; ReceivedDir = $ncReceived; ClearScript = $ncClear
                           ExpectLog = "/64' -> "
                           Env = @{ ORC_UPLOAD_SERVER = $nextcloudUrl } }
    # Into a PASSWORD-PROTECTED PUBLIC SHARE, through the four-request dialog Nextcloud's own web client
    # uses: scrape data-requesttoken, POST the password, scrape the token the regenerated session serves,
    # then PUT under the public DAV root on the cookie alone. ExpectLog names the public DAV path, which
    # only this dialog reaches - the account scenarios above go through /remote.php - so a fixture that
    # degraded to an authenticated PUT fails instead of passing on the byte comparison. That the login
    # worked is not assumed either: without it the PUT is answered 401 and nothing arrives to compare.
    'nextcloud-form' = @{ Config = 'vm-nextcloud-form.xml'; ReceivedDir = $ncShareReceived; ClearScript = $ncClear
                           ExpectLog = "PUT '/public.php/dav/files/orcvmshare00000/"
                           Env = @{ ORC_UPLOAD_SERVER = $nextcloudUrl } }
    # The same share, chunked, under the share's own Basic credential (token as the username) instead of
    # the login dialog: /public.php/dav/uploads/ answers 401 to a session cookie and 201 to Basic, so
    # this is the only way to chunk into a public share. ExpectLog names the SECOND chunk, at
    # chunk_size="256" - with one chunk the file would arrive intact and prove nothing about assembly.
    # The negative half of the pair: the SAME share, the SAME login, but read with form_input instead of
    # attribute. Nextcloud's dialog is script-rendered, so nothing is found and the capture stops the
    # dialog rather than posting an empty token. ExpectLog names that refusal, so a run that failed for
    # any other reason - a wrong path, a dead server - does not pass as if the rule had fired.
    'forminput-vs-nextcloud' = @{ Config = 'vm-forminput-vs-nextcloud.xml'; ReceivedDir = $ncShareReceived
                           ClearScript = $ncClear; Negative = $true
                           ExpectLog = "Form input scan: 'requesttoken' not found in the response body"
                           Env = @{ ORC_UPLOAD_SERVER = $nextcloudUrl } }
    'nextcloud-share-chunked' = @{ Config = 'vm-nextcloud-share-chunked.xml'; ReceivedDir = $ncShareReceived
                           ClearScript = $ncClear; ExpectLog = "/256' -> "
                           Env = @{ ORC_UPLOAD_SERVER = $nextcloudUrl } }
    # Against a real MinIO: the one S3 dialog this engine can hold, an anonymous path-style PUT of the
    # whole object (no SigV4, so no credential; no multipart, so no chunked companion). ExpectLog names
    # the bucket path so a fixture that degraded to some other PUT fails instead of passing on the byte
    # comparison. The archive is read back out of S3 by FetchScript before that comparison.
    'minio'           = @{ Config = 'vm-minio.xml'; ReceivedDir = $minioReceived; ClearScript = $minioClear
                           FetchScript = $minioFetch
                           ExpectLog = "PUT '/orc/orc-upload/"
                           Env = @{ ORC_UPLOAD_SERVER = $minioUrl } }
    # The same fixture shape against a second, independent S3 implementation. It is not redundant with
    # 'minio': one server accepting the dialog says the dialog suits that server, two say it is S3.
    'rustfs'          = @{ Config = 'vm-rustfs.xml'; ReceivedDir = $rustfsReceived; ClearScript = $rustfsClear
                           FetchScript = $rustfsFetch
                           ExpectLog = "PUT '/orc/orc-upload/"
                           Env = @{ ORC_UPLOAD_SERVER = $rustfsUrl } }
    # And a third, on purpose the least related to the other two: SeaweedFS is an S3 gateway over a
    # distributed store, not another S3 clone, so it agrees with the fixture for different reasons.
    'seaweedfs'       = @{ Config = 'vm-seaweedfs.xml'; ReceivedDir = $seaweedReceived; ClearScript = $seaweedClear
                           FetchScript = $seaweedFetch
                           ExpectLog = "PUT '/orc/orc-upload/"
                           Env = @{ ORC_UPLOAD_SERVER = $seaweedUrl } }
    # The signed counterpart of 'minio': same server, a SEPARATE bucket with no anonymous grant,
    # authscheme="AWSv4" instead of nothing. ExpectLog names the signed bucket's path so a fixture that
    # degraded to some other PUT (or to the anonymous bucket) fails instead of passing on the byte
    # comparison alone.
    'minio-sigv4'     = @{ Config = 'vm-minio-sigv4.xml'; ReceivedDir = $minioSigv4Received; ClearScript = $minioSigv4Clear
                           FetchScript = $minioSigv4Fetch
                           ExpectLog = "PUT '/orc-signed/orc-upload/"
                           Env = @{ ORC_UPLOAD_SERVER = $minioUrl } }
    # The same signed fixture against RustFS. Two implementations accepting the same signed dialog is
    # what shows the signer is standard SigV4 rather than tuned to one server's reading of it.
    'rustfs-sigv4'    = @{ Config = 'vm-rustfs-sigv4.xml'; ReceivedDir = $rustfsSigv4Received; ClearScript = $rustfsSigv4Clear
                           FetchScript = $rustfsSigv4Fetch
                           ExpectLog = "PUT '/orc-signed/orc-upload/"
                           Env = @{ ORC_UPLOAD_SERVER = $rustfsUrl } }
    # The same signed fixture against SeaweedFS. This is the scenario the signer is really on trial in:
    # MinIO and RustFS are close enough that a shared misreading of SigV4 would go unnoticed by both,
    # while SeaweedFS verifies the signature in an entirely separate implementation. Three independent
    # servers accepting one signature is what makes it standard SigV4 rather than a dialect.
    'seaweedfs-sigv4' = @{ Config = 'vm-seaweedfs-sigv4.xml'; ReceivedDir = $seaweedSigv4Received; ClearScript = $seaweedSigv4Clear
                           FetchScript = $seaweedSigv4Fetch
                           ExpectLog = "PUT '/orc-signed/orc-upload/"
                           Env = @{ ORC_UPLOAD_SERVER = $seaweedUrl } }
    # The SAME signed fixture against a server URL that spells out the DEFAULT port. WinHTTP omits the
    # port from the Host header when it is the scheme default, so the signer must too: it has to sign
    # "host", not "host:443", or S3 recomputes a different canonical request and answers 403
    # SignatureDoesNotMatch.
    #
    # ':443' and not a bare hostname, deliberately. With no port the server name is already portless,
    # so a signer that did NOT normalise would sign the same string and the scenario could never fail.
    # Spelling the default port out is the only form that discriminates: it is what SignedHostForServer
    # exists to strip, and what an operator may reasonably write.
    #
    # Needs 443 to reach the S3 server. Apache owns *:443 on the bench (it also serves Nextcloud on
    # 8448), so setup_rustfs.sh adds a NAT redirect for this client only, leaving apache untouched for
    # every other source. See 'sigv4 default port' in that script.
    'rustfs-sigv4-port443' = @{ Config = 'vm-rustfs-sigv4.xml'; ReceivedDir = $rustfsSigv4Received
                           ClearScript = $rustfsSigv4Clear
                           FetchScript = $rustfsSigv4Fetch
                           ExpectLog = "PUT '/orc-signed/orc-upload/"
                           Env = @{ ORC_UPLOAD_SERVER = "https://$($settings.VmHost):443" } }
    # A signed upload with the WRONG secret_key must be refused as a signature error, not silently
    # accepted or refused for some other reason. This was meant to carry a wrong aws_region instead
    # (also proving the region is actually part of what gets signed), but MinIO and RustFS both derive
    # the signature they expect from whatever region the request's OWN Authorization header names
    # rather than checking it against one they enforce - confirmed on throwaway instances with
    # MINIO_REGION/RUSTFS_REGION explicitly pinned - so no region value this engine could send is ever
    # refused by either sink. A wrong secret_key exercises the identical AwsV4Authenticator refusal
    # path and is genuinely refused by both, so it is what actually proves the path against this bench.
    # ReceivedDir/FetchScript point at the SAME signed bucket 'minio-sigv4' does: without them the
    # negative verdict would read the unrelated python-sink directory, which is empty regardless of
    # what this scenario actually did, and every run would pass for the wrong reason.
    'minio-sigv4-badkey' = @{ Config = 'vm-minio-sigv4-badkey.xml'; Negative = $true
                           ReceivedDir = $minioSigv4Received; ClearScript = $minioSigv4Clear; FetchScript = $minioSigv4Fetch
                           ExpectLog = 'SignatureDoesNotMatch'
                           Env = @{ ORC_UPLOAD_SERVER = $minioUrl } }

    # ---- big files -------------------------------------------------------------------------------
    # Every scenario above uploads a few hundred bytes, so none of them says anything about the sizes
    # a real collection reaches. These carry a multi-GB payload instead: one main configuration
    # (wolf-main-big.xml) concatenating the BigSeed as many times as the size needs. They are
    # 'Heavy': left out of "all", and their archive and the sink's copy are freed as soon as the
    # verdict is in.
    #
    # The three sizes are the boundaries the upload path actually has:
    #   x1  1.87 GiB - big, still below 2 GiB
    #   x2  3.75 GiB - above 2^31: nothing on the path may treat a size or an offset as signed
    #   x3  5.62 GiB - above the 4 GB a single WinHTTP request body can carry
    'big-http'        = @{ Config = 'vm-http.xml';        MainConfig = 'wolf-main-big.xml'; Heavy = $true
                           SkipReason = $bigSkip
                           Env = @{ ORC_UPLOAD_SERVER = $httpUrl; ORC_BIG_SOURCES = (Get-BigSources 1) } }
    'big-2g'          = @{ Config = 'vm-http.xml';        MainConfig = 'wolf-main-big.xml'; Heavy = $true
                           SkipReason = $bigSkip
                           Env = @{ ORC_UPLOAD_SERVER = $httpUrl; ORC_BIG_SOURCES = (Get-BigSources 2) } }
    # Above the per-request cap with no chunk_size: the upload must be refused, and because this
    # fixture uses operation="move" - a run that believes it delivered deletes its archive - the
    # archive surviving is what proves the refusal was not taken for a delivery. The 'local :' line
    # above the verdict is that evidence.
    'big-4g-refused'  = @{ Config = 'vm-big-move.xml';    MainConfig = 'wolf-main-big.xml'; Heavy = $true
                           SkipReason = $bigSkip; Negative = $true
                           # The stable prefix of the refusal only: the rest of that message has been
                           # reworded once already, and a bench whose binary predates the wording would
                           # fail this scenario for the wording rather than for the cap.
                           ExpectLog = 'bytes remain from offset'
                           Env = @{ ORC_UPLOAD_SERVER = $httpUrl; ORC_BIG_SOURCES = (Get-BigSources 3) } }
    # The same payload, 1 GiB at a time with Content-Range: the documented way past that cap.
    'big-4g-chunked'  = @{ Config = 'vm-big-chunked.xml'; MainConfig = 'wolf-main-big.xml'; Heavy = $true
                           SkipReason = $bigSkip
                           Env = @{ ORC_UPLOAD_SERVER = $httpUrl; ORC_BIG_SOURCES = (Get-BigSources 3) } }
    # A big body over TLS to a real S3 server, signed: AWSv4 signs UNSIGNED-PAYLOAD, so the size must
    # not make the signature depend on reading the file a second time.
    'big-sigv4'       = @{ Config = 'vm-minio-sigv4.xml'; MainConfig = 'wolf-main-big.xml'; Heavy = $true
                           SkipReason = $bigSkip
                           ReceivedDir = $minioSigv4Received; ClearScript = $minioSigv4Clear; FetchScript = $minioSigv4Fetch
                           Env = @{ ORC_UPLOAD_SERVER = $minioUrl; ORC_BIG_SOURCES = (Get-BigSources 1) } }
}

# 'all' = every scenario whose squid requirement the configured bench actually meets. A squid can
# only be in one auth mode at a time, so the other mode's scenarios would fail for the wrong reason.
# 'Heavy' scenarios move multiple GB per run and are left out: they are asked for by name.
$allTags = @($scenarios.Keys | Where-Object {
        (-not $scenarios[$_].Heavy) -and ((-not $scenarios[$_].Squid) -or ($scenarios[$_].Squid -eq $settings.SquidMode))
    })

if ($ListTags) {
    Write-Host "squid mode: $($settings.SquidMode)"
    foreach ($k in $scenarios.Keys) {
        $need = $scenarios[$k].Squid
        $in   = if ($allTags -contains $k) { 'in "all"' }
                elseif ($scenarios[$k].Heavy) { 'heavy: by name only' }
                else { "needs squid '$need'" }
        Write-Host ("  {0,-18} {1,-34} {2}" -f $k, $scenarios[$k].Config, $in)
    }
    return
}

if (-not $Tag) {
    Write-Host "nothing to do: pass -Tag <name[,name]> or -Tag all (see -ListTags)"
    return
}

$tags = if ($Tag.Count -eq 1 -and $Tag[0] -eq 'all') { $allTags } else { $Tag }

# Every verdict is decided by what the sink did or did not receive, so without SshHost there is nothing
# to compare against - see the guard in Invoke-OneScenario, which is what keeps this empty string from
# ever reaching a verdict.
$remoteChecks = [bool]$settings.SshHost

function Invoke-Remote {
    param([string]$Command)

    if (-not $remoteChecks) {
        return ''
    }

    return (ssh $settings.SshHost $Command | Out-String)
}

function Invoke-OneScenario {
    param([string]$Name, [hashtable]$Scn)

    $cfgPath  = Join-Path $settings.ConfigDir $Scn.Config
    $outDir   = Join-Path $settings.OutRoot $Name
    $negative = [bool]$Scn.Negative
    # A scenario whose sink is not the python one (Nextcloud stores uploads in its own account
    # collection) names where the archive lands and what wipes it between runs.
    $receivedDir = if ($Scn.ReceivedDir) { $Scn.ReceivedDir } else { $settings.ReceivedDir }
    $clearScript = if ($Scn.ClearScript) { $Scn.ClearScript } else { $settings.ClearScript }
    New-Item -ItemType Directory -Force -Path $outDir, $settings.SafeTemp | Out-Null
    Get-ChildItem -Path $outDir -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

    Write-Host "==================== [$Name] $($Scn.Config) ===================="

    if ($Scn.SkipReason) {
        Write-Host "SKIP  : $($Scn.SkipReason)"
        Write-Host ""
        return [pscustomobject]@{ Tag = $Name; Verdict = 'SKIP' }
    }
    if (-not (Test-Path $cfgPath)) {
        Write-Host "FAIL  : fixture not found: $cfgPath"
        return [pscustomobject]@{ Tag = $Name; Verdict = 'FAIL' }
    }

    # Clear every ORC_UPLOAD_* and ORC_BIG_* var, then set only this scenario's, so a prior scenario
    # cannot leak - a stale ORC_BIG_SOURCES would silently upload the previous scenario's size.
    Get-ChildItem Env: |
        Where-Object { $_.Name -like 'ORC_UPLOAD_*' -or $_.Name -like 'ORC_BIG_*' } |
        ForEach-Object { Remove-Item "Env:$($_.Name)" }
    foreach ($k in $Scn.Env.Keys) {
        Set-Item "Env:$k" $Scn.Env[$k]
        Write-Host ("  env  : {0}={1}" -f $k, $Scn.Env[$k])
    }

    Invoke-Remote $clearScript | Out-Null

    # A scenario about what the server already holds needs the sink put in that state first, after the
    # wipe and before the run.
    if ($Scn.Seed) {
        Write-Host ("  seed : {0}" -f $Scn.Seed)
        Invoke-Remote $Scn.Seed | Out-Null
    }

    # A scenario may bring its own main configuration instead of the bench's: the big-file ones
    # collect a multi-GB payload where the default collects one cheap command. A relative name is
    # anchored on this script, as $settings.MainConfig is - a main config carries no certificate pin
    # and no credential, so unlike the vm-*.xml fixtures it needs no per-bench copy in ConfigDir.
    $mainConfig = $settings.MainConfig
    if ($Scn.MainConfig) {
        $mainConfig = if ([System.IO.Path]::IsPathRooted($Scn.MainConfig)) { $Scn.MainConfig }
                      else { Join-Path $scriptDir $Scn.MainConfig }
    }

    $orcArgs = @($settings.OrcPrefixArgs) + @('/key=SystemInfo', '/debug', '/overwrite', "/out=$outDir", "/local=$cfgPath")
    if ($mainConfig) { $orcArgs += "/config=$mainConfig" }
    if ($settings.SafeTemp)   { $orcArgs += "/safetemp=$($settings.SafeTemp)" }

    # The ToString() is not a no-op, do not remove it: DFIR-ORC logs to stderr, '2>&1' makes each line an
    # ErrorRecord, and PowerShell renders those through the formatting system, which WRAPS them at the
    # host width (120) - stdout, by contrast, is written verbatim. ExpectLog matches one line at a time,
    # so a wrapped message silently fails a scenario whose run was correct.
    & $settings.Orc @orcArgs 2>&1 |
        ForEach-Object { $_.ToString() } |
        Tee-Object -FilePath (Join-Path $outDir 'run.log') | Out-Null

    $localArchive = Get-ChildItem -Path $outDir -Filter '*.7z' -ErrorAction SilentlyContinue | Select-Object -First 1

    # A sink that does not keep the upload as a file names what exports it into $receivedDir. MinIO is
    # the one: an S3 object is <bucket>/<key>/xl.meta plus part files (inlined when small), so the
    # listing and the sha256 below would otherwise have nothing to read.
    if ($Scn.FetchScript) {
        Invoke-Remote $Scn.FetchScript | Out-Null
    }

    $received = (Invoke-Remote "ls -1 $receivedDir/*.7z 2>/dev/null").Trim()

    Write-Host "---- result [$Name] ----"
    $localSha = $null
    if ($localArchive) {
        $localSha = (Get-FileHash -Algorithm SHA256 $localArchive.FullName).Hash.ToLower()
        Write-Host ("local : {0}  {1}" -f $localSha, $localArchive.Name)
    }
    else {
        Write-Host "local : (no .7z produced)"
    }

    # No sink to inspect: score nothing. An empty '$received' is indistinguishable from "the sink was never
    # asked", and for a negative scenario it would read as the very proof the test exists to produce -
    # "no archive arrived, the upload was refused" - handing out a PASS for a check that never ran.
    if (-not $remoteChecks) {
        Write-Host "SKIP  : no SshHost configured - the sink cannot be inspected, so this scenario has no verdict"
        Write-Host ""
        return [pscustomobject]@{ Tag = $Name; Verdict = 'SKIP' }
    }

    $verdict = 'FAIL'
    if ($Scn.KeepsLocal) {
        # The subject is the LOCAL file, not the sink: with operation="move" a run that believes it
        # uploaded deletes the archive, so the archive still being there is the whole assertion.
        if ($localArchive) {
            $verdict = 'PASS'
            Write-Host "PASS  : the local archive was kept (nothing was sent, so nothing was deleted)"
        }
        else {
            Write-Host "FAIL  : the local archive is gone after a run that sent no data"
        }
        # Only the archive's own line: the other outputs of the run (the log file) upload normally and
        # would otherwise bury it.
        Select-String -Path (Join-Path $outDir 'run.log') -Pattern 'sent no data' |
            Select-Object -First 1 | ForEach-Object { Write-Host ("  " + $_.Line.Trim()) }
    }
    elseif ($negative) {
        if ([string]::IsNullOrWhiteSpace($received)) {
            $verdict = 'PASS'
            Write-Host "PASS  : sink received no archive (upload refused as expected)"
        }
        else {
            Write-Host "FAIL  : sink received a file but the upload should have been refused"
        }
        Select-String -Path (Join-Path $outDir 'run.log') -Pattern 'refused|matches no configured|\bpin\b|407|critical' |
            Select-Object -Last 4 | ForEach-Object { Write-Host ("  " + $_.Line.Trim()) }
    }
    else {
        if ([string]::IsNullOrWhiteSpace($received)) {
            Write-Host "FAIL  : sink received nothing"
        }
        else {
            $remoteSha = (Invoke-Remote "sha256sum $receivedDir/*.7z 2>/dev/null | awk '{print `$1}'").Trim()
            Write-Host ("remote: {0}" -f $remoteSha)

            # ExpectMismatch inverts the comparison: the scenario is about a server that accepts everything
            # and still corrupts the result, so an archive that DID arrive intact means the corruption the
            # scenario exists to cause never happened. Deliberately not folded into 'Negative', which is
            # scored on nothing arriving at all.
            if ($Scn.ExpectMismatch) {
                if (-not $localArchive) {
                    Write-Host "FAIL  : no local archive to compare against"
                }
                elseif ($remoteSha -eq $localSha) {
                    Write-Host "FAIL  : the archive arrived byte-exact, so it was never reassembled out of order"
                }
                else {
                    $verdict = 'PASS'
                    Write-Host "PASS  : the archive arrived corrupt, as expected (sha256 local != remote)"
                }
            }
            elseif ($localArchive -and ($remoteSha -eq $localSha)) {
                $verdict = 'PASS'
                Write-Host "PASS  : byte-exact (sha256 local == remote)"
            }
            else {
                Write-Host "FAIL  : sha mismatch (local != remote)"
            }
        }
    }
    # A scenario about a code path, not only its result, names a log line (literal text) proving the path ran.
    # Without it, a sink that stopped exercising that path would still hand out a PASS.
    if ($Scn.ExpectLog -and $verdict -eq 'PASS') {
        if (-not (Select-String -Path (Join-Path $outDir 'run.log') -Pattern $Scn.ExpectLog -SimpleMatch -Quiet)) {
            $verdict = 'FAIL'
            Write-Host "FAIL  : the run never logged '$($Scn.ExpectLog)', so the path this scenario tests did not run"
        }
    }

    Write-Host ""
    return [pscustomobject]@{ Tag = $Name; Verdict = $verdict }
}

# Refuse to run on a half-copied template. vmtest.local.example.psd1 writes its placeholders two
# ways - '<name>' for a value to substitute, 'c:\path\to\...' for a path - so match both; a value
# still in either shape was never filled in. Failing here is the whole point: the alternative is a
# run that reaches for whatever 'c:\path\to\...' happens to be, or that surfaces a confusing
# connection error three scenarios later.
$kPlaceholder = '<[^>]+>|\\path\\to\\'
$placeholders = @(
    $settings.Keys | Where-Object {
        $v = $settings[$_]
        ($v -is [string] -and $v -match $kPlaceholder) -or
        ($v -is [array] -and ($v | Where-Object { $_ -is [string] -and $_ -match $kPlaceholder }))
    })

if ($placeholders.Count -gt 0) {
    Write-Host "error : these settings are still template placeholders, fill them in first:"
    $placeholders | ForEach-Object { Write-Host ("        {0} = {1}" -f $_, ($settings[$_] -join ', ')) }
    Write-Host "        (in $SettingsFile - copied from vmtest.local.example.psd1)"
    return
}

if (-not $settings.Orc -or -not (Test-Path $settings.Orc)) {
    Write-Host "error : DFIR-ORC binary not found: '$($settings.Orc)' - set Orc in $SettingsFile"
    return
}

$results = foreach ($t in $tags) {
    if (-not $scenarios.Contains($t)) { Write-Host "skip  : unknown tag '$t'"; continue }
    $scn = $scenarios[$t]
    Invoke-OneScenario -Name $t -Scn $scn

    # A scenario normally leaves its archive behind for inspection, and the next run of the same tag
    # wipes it. A multi-GB one cannot wait that long: two of them outlive the disk. 'Heavy' asks for
    # both sides to be freed as soon as the verdict is in.
    if ($scn.Heavy) {
        $heavyOut = Join-Path $settings.OutRoot $t
        Get-ChildItem -Path $heavyOut -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Extension -ne '.log' } | Remove-Item -Force -ErrorAction SilentlyContinue
        $heavyClear = if ($scn.ClearScript) { $scn.ClearScript } else { $settings.ClearScript }
        Invoke-Remote $heavyClear | Out-Null
        Write-Host ("cleanup: freed {0} and the sink; C: {1} GB free" -f $heavyOut, [int]((Get-PSDrive C).Free / 1GB))
        Write-Host ""
    }
}

Write-Host "==================== SUMMARY ===================="
$results | ForEach-Object { Write-Host ("{0,-18} {1}" -f $_.Tag, $_.Verdict) }
$pass = @($results | Where-Object { $_.Verdict -eq 'PASS' }).Count
$skip = @($results | Where-Object { $_.Verdict -eq 'SKIP' }).Count
Write-Host ("{0}/{1} passed" -f $pass, @($results).Count)
if ($skip) {
    Write-Host ("{0} skipped: each SKIP line above says why (no SshHost in {1} skips every scenario)" -f $skip, $SettingsFile)
}
