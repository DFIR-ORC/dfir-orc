# Confirm, with curl.exe, the three requests DFIR-ORC's <rest> dialog has to make to upload into a
# password protected Nextcloud public share. Prints one status per step, then a verdict.
#
#   pwsh -File .\Check-NextcloudShare.ps1 -Base https://foo.bar.com -Token abc123 -Password 'pw'
#
# Add -Prefix /index.php if the server does not serve pretty URLs, and -Insecure for a self signed
# certificate. Nothing is written outside %TEMP%\nc-share-check, and the file it uploads is a 13 byte
# probe, not your data.
param(
    [string]$Base     = 'https://foo.bar.com',   # scheme + host (+ :port), no trailing slash
    [string]$Token    = 'SHARETOKEN',            # what follows /s/ in the share URL
    [string]$Password = 'THE-SHARE-PASSWORD',
    [string]$Prefix   = '',                      # set to /index.php if the server has no pretty URLs
    [switch]$Insecure                            # self signed certificate: skip verification
)

$ErrorActionPreference = 'Stop'

# A fresh directory every run. A leftover auth.html from a previous run would make a failing run look
# as though it had found a token.
$dir = Join-Path $env:TEMP 'nc-share-check'
Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $dir | Out-Null
$jar = Join-Path $dir 'jar.txt'
'probe payload' | Set-Content -NoNewline (Join-Path $dir 'probe.txt')
$base_args = if ($Insecure) { @('-sS', '-k') } else { @('-sS') }

function Read-Body([string]$path) {
    if (-not (Test-Path $path)) { return '' }
    $text = Get-Content -Raw $path       # $null on a zero byte file, which a 303 response is
    if ($null -eq $text) { return '' }
    return $text
}

# 1. The authenticate page: it sets the session cookies and carries the CSRF token. Straight at this
#    URL rather than at /s/<token>, which answers a redirect we would then have to follow.
$a = $base_args + @('-c', $jar, '-o', "$dir\auth.html", '-w', '%{http_code}',
                    "$Base$Prefix/s/$Token/authenticate/showshare")
$get = & curl.exe @a
"GET  authenticate page = $get"

$rt = [regex]::Match((Read-Body "$dir\auth.html"), 'data-requesttoken="([^"]+)"').Groups[1].Value
"     data-requesttoken = $(if ($rt) { "$($rt.Length) chars" } else { 'NOT FOUND' })"

# 2. The login. The token goes in the requesttoken header; a form field of the same name also works.
$a = $base_args + @('-b', $jar, '-c', $jar, '-X', 'POST', '-H', "requesttoken: $rt",
                    '--data-urlencode', "password=$Password", '-d', "sharingToken=$Token",
                    '-d', 'sharingType=3', '-D', "$dir\post.head", '-o', "$dir\post.html",
                    '-w', '%{http_code}', "$Base$Prefix/s/$Token/authenticate/showshare")
$post = & curl.exe @a
$loc = (Select-String -Path "$dir\post.head" -Pattern '^location:' -ErrorAction SilentlyContinue).Line
"POST login             = $post$(if ($loc) { "  ($($loc.Trim()))" })"

# A refused password and an unprocessed POST both answer 200 with the dialog again. Nextcloud says
# which in the page's own state blob, base64 encoded: invalidPassword true means the password was
# read and refused, null means the login action never ran. A 303 carries no body, so there is
# nothing to read in the successful case.
$rejected = $false
$state = [regex]::Match((Read-Body "$dir\post.html"),
                        'id="initial-state-core-publicShareAuth"\s+value="([^"]+)"')
if ($state.Success) {
    $json = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($state.Groups[1].Value))
    $rejected = $json -match '"invalidPassword"\s*:\s*true'
    "     page state        = $json"
}

# 3. The upload, on the session cookie alone. No requesttoken header: the browser sends one, the
#    server does not require it.
$a = $base_args + @('-b', $jar, '-T', "$dir\probe.txt", '-o', "$dir\put.txt", '-w', '%{http_code}',
                    "$Base/public.php/dav/files/$Token/orc-check-probe.txt")
$put = & curl.exe @a
"PUT  upload            = $put"

''
if ($put -eq '201' -or $put -eq '204') {
    'VERDICT: the dialog works. DFIR-ORC needs the data-requesttoken capture to reproduce it.'
}
elseif ($get -eq '404') {
    'VERDICT: no such share. Check -Token, and -Prefix if the URL needs /index.php.'
}
elseif (-not $rt) {
    'VERDICT: no data-requesttoken on that page. Save it (Ctrl+U in the browser) and send the HTML.'
}
elseif ($post -eq '412') {
    'VERDICT: CSRF rejected - the token was not accepted. Wrong page, wrong session, or stale token.'
}
elseif ($rejected) {
    'VERDICT: the server refused the password. Check the share password.'
}
elseif ($post -eq '200') {
    'VERDICT: the login was not processed, only re-rendered. Re-run with -Prefix /index.php'
}
else {
    "VERDICT: login looked accepted ($post) but the upload was refused ($put). Read $dir\put.txt"
}
"artefacts: $dir"
