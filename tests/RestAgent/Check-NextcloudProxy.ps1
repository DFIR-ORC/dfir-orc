# Map what a filtering reverse proxy in front of Nextcloud actually lets through, so a <rest> dialog
# can be written against the policy rather than against Nextcloud's documentation - and so the list of
# things to ask the proxy's owner for is a measurement, not a guess.
#
#   pwsh -File .\Check-NextcloudProxy.ps1 -Base https://foo.bar.com -Token TOK -Password 'pw'
#   pwsh -File .\Check-NextcloudProxy.ps1 ... -AllowWrites      # also probe the write methods
#
# READ-ONLY BY DEFAULT. Without -AllowWrites nothing is created, moved or deleted: the write rows are
# reported as 'skipped'. With it, the script writes one 12 byte file named orc-proxy-probe.txt into the
# share and creates one upload collection with a random name, then removes both if the proxy lets it.
# It never deletes anything it did not create.
#
# WHAT EACH ROW IS FOR. A proxy that filters an upload gateway typically allow-lists paths, methods and
# headers. Those three are what decide which dialogs are possible:
#
#   - Authorization stripped  => no Basic anywhere, so the cookie login is the only route AND chunked
#                                upload is impossible (its collection refuses a cookie).
#   - /uploads/ blocked       => no chunking, whatever the credential.
#   - MKCOL or MOVE blocked   => no chunking, even if /uploads/ is reachable.
#   - PROPFIND blocked        => no resume, which needs it to read the assembled length.
#
# A status the proxy generates and one Nextcloud generates look different: Nextcloud's DAV errors carry
# an XML body naming a Sabre exception, and its CSRF refusal is a 412. A bare 403, a 405 with no DAV
# header, or an HTML body from something that is not Nextcloud, is the proxy answering instead.
param(
    [string]$Base     = 'https://foo.bar.com',
    [string]$Token    = 'SHARETOKEN',
    [string]$Password = 'THE-SHARE-PASSWORD',
    [string]$Prefix   = '',
    [switch]$AllowWrites,
    [switch]$Insecure
)

$ErrorActionPreference = 'Stop'
$dir = Join-Path $env:TEMP 'nc-proxy-check'
Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $dir | Out-Null
$common = if ($Insecure) { @('-sS', '-k') } else { @('-sS') }
$jar    = Join-Path $dir 'jar.txt'
$probeUpload = 'orc-proxy-probe-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
Set-Content -Path (Join-Path $dir 'body.txt') -Value 'orc probe' -NoNewline
Set-Content -Path (Join-Path $dir 'pf.xml') -NoNewline `
    -Value '<?xml version="1.0"?><d:propfind xmlns:d="DAV:"><d:prop><d:getcontentlength/></d:prop></d:propfind>'

# One row. Reports the status, and whether the responder looked like Nextcloud: a Sabre exception name
# or an OCS envelope says the request reached the application, anything else at a 4xx suggests the proxy
# answered on its own.
function Row([string]$label, [string]$method, [string]$url, [string[]]$extra) {
    $head = Join-Path $dir 'h.txt'
    $body = Join-Path $dir 'b.txt'
    $a = $common + @('-X', $method, '-D', $head, '-o', $body, '-w', '%{http_code}') + $extra + @($url)
    $code = & curl.exe @a

    $text = if (Test-Path $body) { (Get-Content -Raw $body) } else { '' }
    if ($null -eq $text) { $text = '' }
    $lines = if (Test-Path $head) { Get-Content $head } else { @() }

    $who = ''
    if ($text -match 'Sabre\\([A-Za-z]+)\\Exception\\([A-Za-z]+)') { $who = "nextcloud: $($Matches[2])" }
    elseif ($text -match '<s:exception>([^<]+)<')                  { $who = "nextcloud: $($Matches[1])" }
    elseif ($text -match '"statuscode"\s*:\s*(\d+)')               { $who = "nextcloud ocs $($Matches[1])" }
    elseif ($code -match '^[45]' -and $text -match '(?i)<html')    { $who = 'HTML body - probably the proxy' }
    elseif ($code -eq '401' -and -not ($lines | Where-Object { $_ -match '^(?i)WWW-Authenticate:' })) {
        $who = 'no credential arrived'
    }

    '{0,-52} {1,-5} {2}' -f $label, $code, $who
}

function Skipped([string]$label) { '{0,-52} {1,-5} {2}' -f $label, '-', 'skipped (no -AllowWrites)' }

"base  : $Base$Prefix"
"share : /s/$Token"
''

# --- is there a Nextcloud behind it, and what does it admit to -------------------------------------
'--- what answers at all -----------------------------------------------------------------------'
Row 'GET      /status.php'                        'GET'  "$Base/status.php" @()
Row 'GET      /ocs/v2.php/cloud/capabilities'     'GET'  "$Base/ocs/v2.php/cloud/capabilities?format=json" @('-H', 'OCS-APIRequest: true')
Row 'GET      /s/<token>'                         'GET'  "$Base$Prefix/s/$Token" @()
''

# --- the login, which also proves CSRF reaches the application ------------------------------------
'--- the cookie login --------------------------------------------------------------------------'
$a = $common + @('-c', $jar, '-o', (Join-Path $dir 'auth.html'), '-w', '%{http_code}',
                 "$Base$Prefix/s/$Token/authenticate/showshare")
$getCode = & curl.exe @a
'{0,-52} {1,-5}' -f 'GET      /s/<token>/authenticate/showshare', $getCode

$rt = [regex]::Match((Get-Content -Raw (Join-Path $dir 'auth.html')), 'data-requesttoken="([^"]+)"').Groups[1].Value
'{0,-52} {1}' -f '         data-requesttoken on that page', $(if ($rt) { "$($rt.Length) chars" } else { 'NOT FOUND' })

# Without the token: a 412 proves Nextcloud's CSRF middleware saw the request, so the POST is reaching
# the application rather than being answered by the proxy.
Row 'POST     login WITHOUT the requesttoken'     'POST' "$Base$Prefix/s/$Token/authenticate/showshare" `
    @('-b', $jar, '--data-urlencode', "password=$Password")
Row 'POST     login WITH the requesttoken'        'POST' "$Base$Prefix/s/$Token/authenticate/showshare" `
    @('-b', $jar, '-c', $jar, '-H', "requesttoken: $rt", '--data-urlencode', "password=$Password",
      '-d', "sharingToken=$Token", '-d', 'sharingType=3')

$a = $common + @('-b', $jar, '-c', $jar, '-o', (Join-Path $dir 'share.html'), '-w', '%{http_code}', "$Base$Prefix/s/$Token")
$null = & curl.exe @a
$rt2 = [regex]::Match((Get-Content -Raw (Join-Path $dir 'share.html')), 'data-requesttoken="([^"]+)"').Groups[1].Value
$xhr = @('-H', 'X-Requested-With: XMLHttpRequest')
$auth = @('-b', $jar) + $xhr + @('-H', "requesttoken: $rt2")
''

# --- does Authorization survive the hop ------------------------------------------------------------
'--- Basic auth, i.e. does the Authorization header reach PHP -----------------------------------'
Row 'PROPFIND files/<token>/ with Basic'          'PROPFIND' "$Base/public.php/dav/files/$Token/" `
    (@('-u', "${Token}:${Password}") + $xhr + @('-H', 'Depth: 0', '--data-binary', "@$dir\pf.xml"))
Row 'PROPFIND public.php/webdav/ with Basic'      'PROPFIND' "$Base/public.php/webdav/" `
    (@('-u', "${Token}:${Password}") + $xhr + @('-H', 'Depth: 0', '--data-binary', "@$dir\pf.xml"))
''

# --- which methods and paths the proxy admits, on the authenticated session ------------------------
'--- methods on the share, on the login session ------------------------------------------------'
Row 'OPTIONS  files/<token>/'                     'OPTIONS'  "$Base/public.php/dav/files/$Token/" $auth
Row 'PROPFIND files/<token>/'                     'PROPFIND' "$Base/public.php/dav/files/$Token/" `
    ($auth + @('-H', 'Depth: 0', '--data-binary', "@$dir\pf.xml"))
Row 'PROPFIND uploads/<token>/<id>/.file'         'PROPFIND' "$Base/public.php/dav/uploads/$Token/$probeUpload/.file" `
    ($auth + @('-H', 'Depth: 0', '--data-binary', "@$dir\pf.xml"))

if ($AllowWrites) {
    Row 'PUT      files/<token>/orc-proxy-probe.txt'   'PUT'   "$Base/public.php/dav/files/$Token/orc-proxy-probe.txt" `
        ($auth + @('-T', (Join-Path $dir 'body.txt')))
    Row 'PUT      the same, WITHOUT X-Requested-With'  'PUT'   "$Base/public.php/dav/files/$Token/orc-proxy-probe2.txt" `
        (@('-b', $jar, '-H', "requesttoken: $rt2", '-T', (Join-Path $dir 'body.txt')))
    Row 'MKCOL    uploads/<token>/<id>'               'MKCOL' "$Base/public.php/dav/uploads/$Token/$probeUpload" $auth
    Row 'PUT      uploads/<token>/<id>/0'             'PUT'   "$Base/public.php/dav/uploads/$Token/$probeUpload/0" `
        ($auth + @('-T', (Join-Path $dir 'body.txt')))
    Row 'MOVE     uploads/<token>/<id>/.file'         'MOVE'  "$Base/public.php/dav/uploads/$Token/$probeUpload/.file" `
        ($auth + @('-H', "Destination: /public.php/dav/files/$Token/orc-proxy-probe3.txt"))
}
else {
    Skipped 'PUT      files/<token>/orc-proxy-probe.txt'
    Skipped 'PUT      the same, WITHOUT X-Requested-With'
    Skipped 'MKCOL    uploads/<token>/<id>'
    Skipped 'PUT      uploads/<token>/<id>/0'
    Skipped 'MOVE     uploads/<token>/<id>/.file'
}
''

'--- the account endpoints, which a public-share gateway usually blocks outright ----------------'
Row 'PROPFIND /remote.php/dav/ (no credential)'   'PROPFIND' "$Base/remote.php/dav/" @('-H', 'Depth: 0')
Row 'PROPFIND /remote.php/webdav/ (no credential)' 'PROPFIND' "$Base/remote.php/webdav/" @('-H', 'Depth: 0')
''

if ($AllowWrites) {
    # Remove only what this script created, and only if the proxy allows it. Leftovers are one small
    # file and one upload collection, which Nextcloud expires by itself.
    foreach ($leftover in "files/$Token/orc-proxy-probe.txt", "files/$Token/orc-proxy-probe2.txt",
                          "files/$Token/orc-proxy-probe3.txt", "uploads/$Token/$probeUpload") {
        $a = $common + @('-X', 'DELETE', '-o', 'NUL', '-w', '%{http_code}') + $auth + @("$Base/public.php/dav/$leftover")
        $null = & curl.exe @a
    }
}

'--- reading it --------------------------------------------------------------------------------'
'A 4xx naming a Sabre exception came from Nextcloud. A bare 403, or an HTML body, came from the proxy.'
'412 on the tokenless login  : good - Nextcloud CSRF, so the POST reaches the application.'
'no credential arrived       : Authorization is stripped. No Basic, so no chunking either.'
'PROPFIND/MKCOL/MOVE blocked : no chunking and no resume. Those three plus the /uploads/ path are'
'                              exactly what to ask the proxy owner to allow.'
"artefacts: $dir"
