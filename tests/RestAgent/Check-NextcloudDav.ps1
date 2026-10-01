# Report which WebDAV endpoints a Nextcloud instance exposes, and under which authentication, so a
# <rest> dialog can be written from measurement instead of assumption.
#
#   pwsh -File .\Check-NextcloudDav.ps1 -Base https://foo.bar.com -Token SHARETOKEN -Password 'pw'
#
# Add -Prefix /index.php if the instance has no pretty URLs, -Insecure for a self signed certificate,
# and -Account user:password to probe the account endpoints too.
#
# THE QUESTION THIS ANSWERS FIRST: does an Authorization header reach PHP at all. On a public share the
# share token is the Basic username and the share password is the Basic password, so a working Basic
# credential means the whole login dialog is unnecessary AND chunked upload becomes possible. A front
# end that strips Authorization (Apache without CGIPassAuth, nginx/PHP-FPM without
# fastcgi_pass_header Authorization) forces the cookie login and rules chunking out entirely.
#
# Nothing here writes anything except one MKCOL of an upload collection, which Nextcloud expires on its
# own and which this script deletes again when it can.
param(
    [string]$Base     = 'https://foo.bar.com',
    [string]$Token    = 'SHARETOKEN',
    [string]$Password = 'THE-SHARE-PASSWORD',
    [string]$Prefix   = '',
    [string]$Account  = '',                       # user:password, to probe /remote.php as well
    [switch]$Insecure
)

$ErrorActionPreference = 'Stop'
$dir = Join-Path $env:TEMP 'nc-dav-check'
Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $dir | Out-Null
$common = if ($Insecure) { @('-sS', '-k') } else { @('-sS') }

# A fresh name per run: a leftover collection answers MKCOL with 405, which reads as a failure here
# when it only means the endpoint worked on an earlier run.
$probeDir = 'orc-dav-probe-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)

$propfind = '<?xml version="1.0"?><d:propfind xmlns:d="DAV:"><d:prop><d:getcontentlength/></d:prop></d:propfind>'
Set-Content -Path (Join-Path $dir 'pf.xml') -Value $propfind -NoNewline

# One request, reported as one line. The status alone is not enough: WWW-Authenticate is what separates
# "no credential arrived" from "credential refused", and the DAV header says whether the endpoint is a
# DAV collection at all.
function Send([string]$label, [string]$method, [string]$url, [string[]]$extra) {
    $head = Join-Path $dir 'h.txt'
    $a = $common + @('-X', $method, '-D', $head, '-o', (Join-Path $dir 'b.txt'), '-w', '%{http_code}') + $extra + @($url)
    $code = & curl.exe @a
    $lines = if (Test-Path $head) { Get-Content $head } else { @() }
    $wwwAuth = ($lines | Where-Object { $_ -match '^(?i)WWW-Authenticate:' }) -join ' '
    $dav     = ($lines | Where-Object { $_ -match '^(?i)DAV:' }) -join ' '
    $note = ''
    if ($code -eq '401') {
        # Nextcloud's public DAV answers a MISSING credential with no WWW-Authenticate and an empty
        # <s:message/>, and a WRONG one with both. That distinction is the whole point of this script.
        $note = if ($wwwAuth) { 'credential refused' } else { 'NO CREDENTIAL ARRIVED' }
    }
    elseif ($dav) { $note = ($dav -replace '^(?i)DAV:\s*', 'DAV: ') }

    '{0,-46} {1,-5} {2}' -f $label, $code, $note
}

"base   : $Base$Prefix"
"share  : /s/$Token"
''

# --- what the instance is -------------------------------------------------------------------------
$a = $common + @('-o', (Join-Path $dir 'status.json'), '-w', '%{http_code}', "$Base/status.php")
$code = & curl.exe @a
if ($code -eq '200') {
    $status = Get-Content -Raw (Join-Path $dir 'status.json') | ConvertFrom-Json
    "instance: $($status.productname) $($status.versionstring)   (edition '$($status.edition)')"
}
else {
    "instance: /status.php answered $code"
}
''

$basic = @('-u', "${Token}:${Password}")
$xhr   = @('-H', 'X-Requested-With: XMLHttpRequest')
$pf    = @('-H', 'Depth: 0', '-H', 'Content-Type: application/xml', '--data-binary', "@$dir\pf.xml")

'--- public share DAV, with the share token as the Basic username -------------------------------'
Send 'PROPFIND /public.php/dav/files/<token>/' 'PROPFIND' "$Base/public.php/dav/files/$Token/" ($basic + $xhr + $pf)
Send 'PROPFIND /public.php/webdav/ (legacy)'   'PROPFIND' "$Base/public.php/webdav/"           ($basic + $xhr + $pf)
Send 'MKCOL    /public.php/dav/uploads/<token>/' 'MKCOL'   "$Base/public.php/dav/uploads/$Token/$probeDir" ($basic + $xhr)
Send 'OPTIONS  /public.php/dav/files/<token>/'  'OPTIONS'  "$Base/public.php/dav/files/$Token/"  ($basic + $xhr)
''

'--- the same, with NO credential at all (what a stripped Authorization looks like) -------------'
Send 'PROPFIND /public.php/dav/files/<token>/' 'PROPFIND' "$Base/public.php/dav/files/$Token/" ($xhr + $pf)
''

if ($Account) {
    $acct = @('-u', $Account)
    $user = ($Account -split ':')[0]
    '--- account DAV, with the account credential ---------------------------------------------------'
    Send "PROPFIND /remote.php/dav/files/$user/"  'PROPFIND' "$Base/remote.php/dav/files/$user/"  ($acct + $pf)
    Send 'PROPFIND /remote.php/webdav/ (legacy)'  'PROPFIND' "$Base/remote.php/webdav/"           ($acct + $pf)
    Send "MKCOL    /remote.php/dav/uploads/$user/" 'MKCOL'   "$Base/remote.php/dav/uploads/$user/$probeDir" $acct
    ''
}

# Best effort cleanup of the only thing this script creates. Nextcloud expires an unfinished upload
# collection on its own, so a failure here leaves nothing permanent.
$a = $common + @('-X', 'DELETE', '-o', 'NUL', '-w', '%{http_code}') + $basic + $xhr +
     @("$Base/public.php/dav/uploads/$Token/$probeDir")
$null = & curl.exe @a

if ($Account) {
    $acctUser = ($Account -split ':')[0]
    $a = $common + @('-X', 'DELETE', '-o', 'NUL', '-w', '%{http_code}', '-u', $Account) +
         @("$Base/remote.php/dav/uploads/$acctUser/$probeDir")
    $null = & curl.exe @a
}

'--- how to read this --------------------------------------------------------------------------'
'207 on files/  under Basic : the share token authenticates. The login dialog is unnecessary:'
'                             use user="<token>" password="<share password>" authscheme="Basic".'
'201 on uploads/ under Basic : chunked upload works. rest-nextcloud-share-chunked.xml applies.'
'401 NO CREDENTIAL ARRIVED  : the Authorization header is not reaching PHP. Ask whoever owns the'
'                             front end for CGIPassAuth On (Apache) or fastcgi_pass_header'
'                             Authorization (nginx + PHP-FPM). Until then: the cookie login only,'
'                             and no chunking at all.'
'401 credential refused     : the header arrives but the token or password is wrong.'
"artefacts: $dir"
