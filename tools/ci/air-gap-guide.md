# Air-gap toolchain guide

> **Goal:** Download, layout, and install Visual Studio Build Tools and dependencies on an air-gapped host.

Use Get-Toolchain.ps1 or follow the manual steps below.

```ps1
# Connected host
Get-ToolChain.ps1 -Output .\toolchain

# Air-gapped host
Get-ToolChain.ps1 -Install
```

---

## Requirements

| Item | Details |
|------|---------|
| Installer | `vs_buildtools.exe` from `https://aka.ms/vs/17/release/vs_buildtools.exe` |
| Disk space | ~12 GB total (installer: 2 GB, install: 4 GB, build tree: 5 GB) |
| Network | Only needed on the **connected** host for steps 1 and the downloads |

> **Note:** `Microsoft.VisualStudio.Component.Git` is intentionally excluded -- Git is installed separately.

---

## Step 1 -- Download the VS Build Tools Layout (connected host)

```powershell
Invoke-WebRequest `
    -Uri     "https://aka.ms/vs/17/release/vs_buildtools.exe" `
    -OutFile "vs_buildtools.exe"

.\vs_buildtools.exe `
    --layout .\toolchain\vslayout `
    --lang en-US `
    --norestart `
    --add Microsoft.VisualStudio.Workload.VCTools `
    --add Microsoft.VisualStudio.Component.VC.CMake.Project `
    --add Microsoft.VisualStudio.Component.VC.v141.x86.x64 `
    --add Microsoft.VisualStudio.Component.VC.v141.ATL `
    --add Microsoft.VisualStudio.Component.WinXP `
    --add Microsoft.VisualStudio.Component.Windows11SDK.22621
```

### Download the Code Signing CA (required on air-gapped host)

```powershell
Invoke-WebRequest `
    -Uri     "https://www.microsoft.com/pkiops/certs/Microsoft%20Windows%20Code%20Signing%20PCA%202024.crt" `
    -OutFile ".\toolchain\Microsoft Windows Code Signing PCA 2024.crt"
```

### Download VC Redistributable (x86)

```powershell
curl.exe -L "https://aka.ms/vc14/vc_redist.x86.exe" -o ".\toolchain\vc_redist.x86.exe"
```

### Download Git for Windows

```powershell
(Invoke-RestMethod https://api.github.com/repos/git-for-windows/git/releases/latest).assets `
    | Where-Object { $_.browser_download_url -match '64-bit.exe$' } `
    | ForEach-Object { curl.exe -L $_.browser_download_url -o ".\toolchain\git-installer.exe" }
```

---

## Step 2 -- Install Build Tools (offline host)

The layout contains `vs_installer.opc`, which is the Visual Studio Installer package.
It has a signing chain that may not be trusted out of the box -- pick **one** of the two alternatives below.

---

### Alternative 1 -- Install the missing certificate

```powershell
certutil -addstore -f "CA" ".\toolchain\Microsoft Windows Code Signing PCA 2024.crt"

& ".\toolchain\vslayout\vs_buildtools.exe" install `
    --in        ".\toolchain\vslayout\Response.json" `
    --noweb `
    --norestart `
    --passive
```

---

### Alternative 2 -- Extract VS Installer manually from `vs_installer.opc`

`vs_installer.opc` is a ZIP archive. Extract its `Contents\*` entries to:

```
C:\Program Files (x86)\Microsoft Visual Studio\Installer
```

```powershell
Add-Type -AssemblyName System.IO.Compression.FileSystem

$zip    = [System.IO.Compression.ZipFile]::OpenRead(".\toolchain\vslayout\vs_installer.opc")
$target = "C:\Program Files (x86)\Microsoft Visual Studio\Installer"

foreach ($entry in $zip.Entries) {
    if (-not $entry.FullName.StartsWith("Contents/")) { continue }

    $relative = $entry.FullName.Substring("Contents/".Length)
    if ($relative -eq "") { continue }  # skip the directory entry itself

    $dest = Join-Path $target $relative
    $dir  = Split-Path $dest -Parent
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }

    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dest, $true)
}

$zip.Dispose()

& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe" install `
    --in         ".\toolchain\vslayout\Response.json" `
    --noweb `
    --layoutPath ".\toolchain\vslayout" `
    --norestart `
    --passive
```

#### Optional -- install any remaining CA certificates

```powershell
Get-ChildItem ".\toolchain\vslayout\certificates\*.cer" |
    ForEach-Object { certutil -addstore -f "CA" $_ }
```

---

## Step 3 -- Install VC Redistributable

Required by CMake. Also available at `https://aka.ms/vc14/vc_redist.x86.exe`.

```powershell
& ".\toolchain\vc_redist.x86.exe" /quiet /norestart
```

---

## Step 4 -- Install Git for Windows

Latest release: `https://github.com/git-for-windows/git/releases/latest`

```powershell
& ".\toolchain\Git-2.54.0-64-bit.exe" /VERYSILENT /NORESTART /SUPPRESSMSGBOXES /SP-
```
