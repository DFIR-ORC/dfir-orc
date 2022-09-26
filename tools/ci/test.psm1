function Find-7z {
    # https://www.7-zip.org/
    return Find-CommandPath "7z.exe" @("C:\Program Files\7-Zip\7z.exe")
}

function Find-Openssl {
    # https://gitforwindows.org/
    # https://www.msys2.org/

    $Locations = @(
        "c:\Program Files\Git\usr\bin\openssl.exe"
        "c:\Program Files\Git\mingw64\bin\openssl.exe"
        "c:\msys64\usr\bin\openssl.exe"
    )

    return Find-CommandPath "openssl.exe" $Locations
}

function Find-Unstream {
    # https://github.com/DFIR-ORC/orc-decrypt

    $Locations = @(
        "$PSScriptRoot\unstream.exe"
    )

    return Find-CommandPath "unstream.exe" $Locations
}

function Find-NuShell {
    $Locations = @(
        "c:\Program Files\nu\nu.exe"
        "c:\Program Files\nu\bin\nu.exe"
    )

    # https://github.com/nushell/nushell/releases/tag/0.35.0
    return Find-CommandPath "nu.exe" $Locations
}

function Find-CommandPath {
    <#
    .SYNOPSIS
        Return the file path to the provided command (executable, ...).

    .PARAMETER Command
        Command to look for (ex: 'git.exe').

    .PARAMETER Locations
        Locations to check if not successful with 'Get-Command' ($PATH).

    .OUTPUTS
        The path to the command.
    #>
    Param(
        [Parameter(Mandatory)]
        [String]
        $Command,
        [Parameter()]
        [String[]]
        $Locations
    )

    $FoundCommand = Get-Command $Command -ErrorAction SilentlyContinue
    if ($FoundCommand)
    {
        Write-Verbose "Found: $($FoundCommand.Path)"
        return $FoundCommand.Path
    }

    foreach ($Location in $Locations)
    {
        $Path = Get-Item -Path $Location
        if ($Path)
        {
            Write-Verbose "Found: $($Path.FullName)"
            return $Path.FullName
        }
    }
}

function Write-HostLog($Msg) {
    Write-Host "$(get-date -Format "hh:mm:ss") $Msg"
}

function IsVirtualHardDisk() {
    Param(
        [Parameter(Mandatory)]
        [String]
        $Path
    )
    return $(Split-Path -Extension $Path) -in (".vhd", ".vhdx")
}

# Manage mount point: allow to easily mount/dismount filesystem. Currently only support vhd files.
class MountPoint : System.IDisposable
{
    [bool]$HasMounted = $false
    [bool]$ReadOnly = $false
    [bool]$AutoUnmount = $true
    [String]$Source = ""
    [String]$Path = ""

    MountPoint([String]$Source) {
        $this.Source = $Source
    }

    MountPoint([String]$Source, [bool]$ReadOnly) {
        $this.Source = $Source
        $this.ReadOnly = $ReadOnly
    }

    MountPoint([String]$Source, [bool]$ReadOnly, [bool]$AutoUnmount) {
        $this.Source = $Source
        $this.ReadOnly = $ReadOnly
        $this.AutoUnmount = $AutoUnmount
    }

    # BEWARE: Unfortunately this function will NOT be called automatically
    [void] Dispose() {
        if ($this.HasMounted -and $this.AutoUnmount)
        {
            Write-Verbose "Dismount $($this.Source)"
            Dismount-VHD $this.Source
        }
    }

    [String] Mount() {
        if (Test-Path -PathType Container $this.Source)
        {
            Write-Verbose "Path does not need to be mounted: $($this.Source)"
            $this.Path = $this.Source
            $this.HasMounted = $false
            return $this.Source
        }

        if (IsVirtualHardDisk($this.Source))
        {
            $VHD = Get-VHD -Path $this.Source
            if (-Not $($VHD.Attached))
            {
                $VHD = Mount-VHD -PassThru -ReadOnly:$this.ReadOnly $this.Source
                $this.HasMounted = $true
            }
            else {
                Write-Verbose "$($this.Source) already mounted"
            }

            $Disk = Get-DiskImage $this.Source
            $DriveLetters = `
                Get-Partition -DiskNumber $Disk.Number `
                | Get-Volume `
                | Sort-Object -Property Size -Descending `
                | Select-Object -ExpandProperty DriveLetter

            if ($DriveLetters.Count -ne 1)
            {
                Write-Warning "Virtual disk has multiple drive letter assigned: '$($this.Source)', using '$($DriveLetters[0])' [$DriveLetters]"
            }

            $this.Path = [System.IO.DirectoryInfo]"$($DriveLetters[0]):"
            return $this.Path
        }

        Write-Error "Unsupported path: '$($this.Source)'"
        return $this.Source
    }
}

function New-OrcLocalConfig() {
    <#
    .SYNOPSIS
        Output a "local.xml" Orc configuration file.

    .PARAMETER Output
        Defines output directory after being processed in 'WorkingTemp'
        and before being uploaded.

    .PARAMETER Upload
        Network URI to upload output files.
        Accepted schemes: 'http', 'https', 'file' (or <empty>)

        Ex: 'file:\\192.168.0.1\Orc'

    .PARAMETER AllowInvalidUri
        Force the use of upload uri event if it is not standard.
        Allow to specify URI containing environment variables.

    .PARAMETER BitsTransfer
        Enable BITS transfer mode.

    .PARAMETER EnableKeyword
        Set the additional keywords to enable.

    .PARAMETER DisableKeyword
        Set the additional keywords to disable.

    .PARAMETER PublicKey
        Enable encrypted output to p7b archive file (cms format).

    .PARAMETER Temporary
        Defines Temporary directory.
    #>
    Param(
        [Parameter()]
        [String]
        $Output,
        [Parameter()]
        [System.Uri]
        $Upload,
        [Parameter()]
        [Switch]
        $AllowInvalidUri,
        [Parameter()]
        [Switch]
        $BitsTransfer,
        [Parameter()]
        [String[]]
        $EnableKeyword,
        [Parameter()]
        [String[]]
        $DisableKeyword,
        [Parameter()]
        [String]
        $PublicKey,
        [Parameter()]
        [String]
        $Temporary
    )

    if ($Output)
    {
        $Output="<output>${Output}</output>`n"
    }

    if ($Temporary)
    {
        $Temporary="<temporary>${Temp}</temporary>`n"
    }

    if ($Upload -or $ForceUri)
    {
        # Check upload directory accessibility
        if (-not $(Test-Path $Upload.LocalPath))
        {
            Write-Warning "Upload path check failed with '$($Upload.LocalPath)'"
        }

        $UploadMethod = $BitsTransfer ? "bits" : "filecopy"
        $UploadMode = $BitsTransfer ? "async" : "sync"
        $UploadOperation = "move"

        if (-not $AllowInvalidUri)
        {
        $UploadXml = `
"<upload
    job=`"orc`"
    method=`"$UploadMethod`"
    server=`"$($Upload.Scheme)://$($Upload.Host)`"
    path=`"$($Upload.PathAndQuery)`"
    mode=`"$UploadMode`"
    operation=`"$UploadOperation`"
/>`n"
        }
        else
        {
            $UploadXml = `
"<upload
    job=`"orc`"
    method=`"$UploadMethod`"
    uri=`"$Upload`"
    mode=`"$UploadMode`"
    operation=`"$UploadOperation`"
/>`n"
        }
    }

    if ($PublicKey)
    {
        $PublicKeyXml = "<recipient name='test' archive='*' >$(Get-Content $PublicKey)</recipient>"
    }

    foreach ($Keyword in $EnableKeyword)
    {
        $EnableKeyXml += "<enable_key>$Keyword</enable_key>`n"
    }


    foreach ($Keyword in $DisableKeyword)
    {
        $DisableKeyXml += "<disable_key>$Keyword</disable_key>`n"
    }

    return "<dfir-orc>`n${PublicKeyXml}${UploadXml}${EnableKeyXml}${DisableKeyXml}${Output}${Temporary}</dfir-orc>"
}

}

function Invoke-OrcOffline {
    <#
    .SYNOPSIS
        Execute ORC on a disk dump or a virtual hard disk.

    .PARAMETER Path
        Path to Orc configurated executable.

    .PARAMETER Disk
        Path to input disk.

    .PARAMETER Destination
        Output directory.

    .PARAMETER PublicKey
        Enable encrypted output to p7b archive file (cms format).

    .PARAMETER Argument
        Argument(s) to forward to Orc.

    .PARAMETER Force
        Overwrite any existing results.

    .EXAMPLE
        Invoke-OrcOffline -Path c:\release\10.1.0\orc.exe -Disk w10.vhd -Argument "/key=GetEvt /overwrite"
    #>
    Param(
        [Parameter(Mandatory)]
        [System.IO.FileInfo]
        $Path,
        [Parameter(Mandatory)]
        [System.IO.FileInfo]
        $Disk,
        [Parameter(Mandatory)]
        [System.IO.DirectoryInfo]
        $Destination,
        [Parameter()]
        [String]
        $PublicKey,
        [Parameter()]
        [String]
        $Argument,
        [Parameter()]
        [Switch]
        $Force
    )
    New-Alias OrcExe "$Path"

    if ($Force)
    {
        $Overwrite = "/overwrite"
    }

    if ($Force -or -not $(Test-Path $Destination))
    {
        if (IsVirtualHardDisk($Disk))
        {
            $ReadOnly = $true
            $DiskMountPoint = [MountPoint]::New($Disk, $ReadOnly)
            $Disk = $DiskMountPoint.Mount()
        }

        if ($PublicKey)
        {
            $LocalXmlPath = "$(New-TemporaryFile).xml"
            New-OrcLocalConfig -PublicKey $PublicKey | Out-File $LocalXmlPath
            $LocalXml = "/local=$LocalXmlPath"
        }

        Write-HostLog "Executing '$Path' on '$Disk' to '$Destination'"
        OrcExe $Argument $Overwrite /Offline=$Disk /Out=$Destination $LocalXml

        if ($LocalXmlPath)
        {
            Remove-Item $LocalXmlPath
        }
    }
    else
    {
        Write-HostLog "Found existing results '$Destination'"
    }

    if ($DiskMountPoint)
    {
        $DiskMountPoint.Dispose()
    }
}

function Invoke-OrcVM {
    <#
    .SYNOPSIS
        Configure, drop, execute ORC in a virtual machine.

        The configuration is made using custom 'local.xml' side file.

    .PARAMETER Path
        Path to Orc configurated binaries.

    .PARAMETER VMName
        Name of the Hyper-V virtual machine.

    .PARAMETER Checkpoint
        Name of the Hyper-V virtual machine checkpoint to restore.

    .PARAMETER BitsTransfer
        Switch to enable BITS transfer mode.

    .PARAMETER Upload
        Network URI to upload output files.
        Accepted schemes: 'http', 'https', 'file' (or <empty>)

        Ex: 'file:\\192.168.0.1\Orc'

    .PARAMETER PublicKey
        Enable encrypted output to p7b archive file (cms format).

    .PARAMETER Credential
        Credentials for the VM user (expects 'Get-Credential' output).

    .PARAMETER Argument
        Argument(s) to forward to Orc.

    .EXAMPLE
        Invoke-OrcVM `
            -Path "c:\orc\build\orc.exe" `
            -VMName w10 `
            -Checkpoint clean `
            -Upload "\\172.28.112.1\Bits\Orc\Current" `
            -BitsTranfer `
            -PublicKey "c:\orc\test.pem" `
            -Argument "/key=GetEvt /overwrite"
    #>
    Param(
        [Parameter(Mandatory)]
        [System.IO.FileInfo]
        $Path,
        [Parameter(Mandatory)]
        [String]
        $VMName,
        [Parameter()]
        [String]
        $Checkpoint,
        [Parameter(Mandatory)]
        [System.Uri]
        $Upload,
        [Parameter()]
        [Switch]
        $BitsTransfer,
        [Parameter()]
        [System.Management.Automation.PSCredential]
        $Credential,
        [Parameter()]
        [String]
        $PublicKey,
        [Parameter()]
        [String]
        $Argument = $null
    )

    $OrcLocalPath = $Path
    $OrcRemotePath = "c:\windows\temp\orc.exe"
    $ConfigRemotePath = "c:\windows\temp\local.xml"

    $LocalConfig = $null
    if ($Upload -or $PublicKey)
    {
        $LocalConfig = New-OrcLocalConfig -PublicKey:$PublicKey -Upload:$Upload
    }

    if ($Checkpoint)
    {
        if ((Get-VM $VMName).State -ne "Off")
        {
            Stop-VM -Force -TurnOff $VMName
        }

        Restore-VMCheckpoint -VMName $VMName -Name $Checkpoint -Confirm:$false
    }

    if ((Get-VM $VMName).State -ne "Running")
    {
        Start-VM $VMName
        Wait-VM $VMName
    }

    Copy-VMFile `
        -Name $VMName `
        -Force `
        -SourcePath $OrcLocalPath `
        -DestinationPath $OrcRemotePath `
        -FileSource Host

    if (-Not $Session) {
        if (-Not $Credential) {
            $Credential = Get-Credential
        }

        $Session = New-PSSession -VMName $VMName -Credential $Credential
    }

    Start-Sleep 5

    Invoke-Command -Session $Session -Scriptblock {
        $Argument = "$using:Argument"

        if ($using:LocalConfig)
        {
            # Drop the local configuration file with custom upload settings
            $using:LocalConfig | Out-File -FilePath $using:ConfigRemotePath
            $Argument = "$Argument /local=$using:ConfigRemotePath"
        }

        Start-Process $using:OrcRemotePath -ArgumentList "$Argument" -WorkingDirectory "C:\Windows\Temp"
        Start-Sleep 5
        Write-Host "Waiting for 'Orc' to end"
        Wait-Process "Orc"
    }
}

function Get-ArchivePasswordInput {
    <#
    .SYNOPSIS
        Ask user for a password if required

    .PARAMETER Path
        Path to the Orc archive.

    .OUTPUTS
        The password as a string.
    #>
    Param(
        [Parameter(Mandatory)]
        [System.IO.FileInfo]
        $Path,
        [Parameter()]
        [String]
        $Password
    )

    $7zExePath = Find-7z
    if (-not $7zExePath)
    {
        Write-Verbose "Get-ArchivePassword requires '7z.exe'"
        return
    }

    Set-Alias 7zExe $7zExePath

    if ($Password)
    {
        $Output = 7zExe t -p"$Password" $Path *>&1
        if ($Output -like "*Wrong password?*")
        {
            Write-Error "Invalid password"
            return
        }

        return $Password
    }
    else
    {
        $Output = 7zExe t -pfoobar $Path *>&1
        if ($Output -like "*Wrong password?*")
        {
            $Password = Read-Host -Prompt "Enter password for $Path"
            return Get-ArchivePasswordInput -Password $Password $Path
        }
    }
}

function Get-ArchivePassword {
    <#
    .SYNOPSIS
        Return the password for an Orc archive from *_cli_confix.xml files.

    .PARAMETER Path
        Path to the Orc archive.

    .OUTPUTS
        The password as a string.
    #>
    Param(
        [Parameter(Mandatory)]
        [System.IO.FileInfo]
        $Path
    )

    $ConfigXmlPath = "$($Path.Directory)/$($Path.BaseName)_cli_config.xml"
    if (-Not (Test-Path $ConfigXmlPath))
    {
        return Get-ArchivePasswordInput $Path
    }

    $Commands = @(
        "getthis"
        "getsamples"
    )

    ForEach ($Command in $Commands)
    {
        # XPath 1.0 is case sensitive
        [xml]$ConfigXml = (Get-Content $ConfigXmlPath).ToLower();

        $Password =  ` $ConfigXml
            | Select-Xml -XPath "/$Command/output/@password" `
            | Select-Object -ExpandProperty Node `
            | Select-Object -ExpandProperty Value

        if ($Password)
        {
            return $Password
        }
    }
}

function Expand-OrcArchive {
    <#
    .SYNOPSIS
        Extract one or multiple DFIR-ORc archive recursively.

    .PARAMETER Path
        Path to a DFIR-Orc archive or a directory with multiples archives.

    .PARAMETER Destination
        Path to the destination directory.

    .PARAMETER PrivateKey
        Path to the private key file for p7b extraction.

    .PARAMETER Password
        Password for "sub" archives from '-Path'.

    .PARAMETER Force
        Overwite any output file.
    #>
    Param(
        [Parameter(Mandatory)]
        [String]
        $Path,
        [Parameter(Mandatory)]
        [String]
        $Destination,
        [Parameter()]
        [String]
        $PrivateKey,
        [Parameter()]
        [String]
        $Password,
        [Parameter()]
        [switch]
        $Force = $false
    )

    if (Test-Path $Path -PathType Container)
    {
        $Archives = Get-ChildItem -Path "$Path\*.7z", "$Path\*.p7b"
        foreach ($Archive in $Archives)
        {
            Expand-OrcArchive `
                -Path $Archive `
                -Destination $Destination `
                -Password:$Password `
                -PrivateKey:$PrivateKey `
                -Force:$Force
        }

        return
    }

    $Archive = Get-Item $Path
    if (-Not $Archive)
    {
        return
    }

    $7zExePath = Find-7z
    if (-not $7zExePath)
    {
        Write-Error "Expand-OrcArchive requires '7z.exe'"
        return
    }

    Set-Alias 7zExe $7zExePath

    if ($PrivateKey)
    {
        $OpensslExePath = Find-Openssl
        if (-not $OpensslExePath)
        {
            Write-Error "Expand-OrcResults with '-PrivateKey' requires openssl to be installed"
            return
        }

        New-Alias OpensslExe $OpensslExePath

        $UnstreamExePath = Find-Unstream
        if (-not $UnstreamExePath)
        {
            Write-Error "Expand-OrcResults with '-PrivateKey' requires unstream to be available"
            return
        }

        New-Alias UnstreamExe $UnstreamExePath
    }

    if ($Archive.Length -eq 0)
    {
        Write-Warning "Ignored empty archive '$Archive'"
        return
    }

    New-Item -ItemType Directory $Destination -ErrorAction SilentlyContinue | Out-Null
    $Destination = Get-Item $Destination

    if ($Archive.Extension -eq ".p7b")
    {
        if (-not $PrivateKey)
        {
            Write-Error "Cannot decrypt '$Archive': missing key file"
            continue
        }

        Write-Verbose "Decrypting archive: '$Archive'"
        $DecryptOutput = Join-Path $Destination ($Archive.BaseName + ".7zs")
        Remove-Item $DecryptOutput -ErrorAction SilentlyContinue

        # FIX: Calling 'OpensslExe' break color output so it is done with 'Start-Process'
        #OpensslExe cms -decrypt -in $Archive -out $DecryptOutput -inkey $PrivateKey -inform DER -binary
        Start-Process `
            -WindowStyle Hidden `
            -Wait `
            -FilePath $OpensslExePath `
            -ArgumentList `
                "cms",
                "-decrypt",
                "-in","`"$Archive`"",
                "-out","`"$DecryptOutput`"",
                "-inkey","`"$PrivateKey`"",
                "-inform","DER",
                "-binary"

        Write-Verbose "Unstreaming archive journal: '$DecryptOutput'"
        $UnstreamOutput = Join-Path $Destination $Archive.BaseName
        Remove-Item $UnstreamOutput -ErrorAction SilentlyContinue
        UnstreamExe $DecryptOutput $UnstreamOutput
        Remove-Item $DecryptOutput

        $Archive = Get-Item $UnstreamOutput
    }

    $ExpandDirectory = Join-Path $Destination $Archive.BaseName
    if (-Not $Force -And $(Test-Path $ExpandDirectory))
    {
        Write-Warning "Extraction aborted: file already exists '$ExpandDirectory'"
        return $ExpandDirectory
    }

    New-Item -Force:$Force -Type Directory $ExpandDirectory -ErrorAction SilentlyContinue | Out-Null

    if ($Force)
    {
        $OverwriteMode = "-aoa"
    }

    # BEWARE: 7z password prompt will be hidden if not in verbose mode
    # TODO: add a check to detect if archive is password protected
    7zExe x $Archive -r -o"$ExpandDirectory" $OverwriteMode * | Write-Verbose
    if ($LastExitCode -ne 0)
    {
        Write-Error "Failed to unpack '$Path' into '$ExpandDirectory'"
    }

    if ($UnstreamOutput)
    {
        Remove-Item $UnstreamOutput
    }

    $Archives = Get-ChildItem -Recurse -File -Path $ExpandDirectory -Filter *.7z
    foreach ($SubArchive in $Archives)
    {
        if ($(Get-Item $SubArchive).Length -eq 0)
        {
            Write-Warning "Ignored empty archive '$Archive'"
            continue
        }

        $SubDir = "$($SubArchive.Directory)\$($SubArchive.BaseName)"
        Write-Verbose "DFIR-Orc archive 7z extraction: $SubArchive, Destination: $SubDir"
        New-Item -Force:$Force -Type Directory $SubDir | Out-Null

        if (-Not $Password)
        {
            $Password = Get-ArchivePassword "$SubArchive"
        }

        7zExe x $SubArchive -p"$Password" $OverwriteMode -r -o"$SubDir" * | Write-Verbose
        if ($LastExitCode -ne 0)
        {
            Write-Error "Failed to unpack '$SubArchive' into '$SubDir'"
        }

        Remove-Item $SubArchive
    }

    return $ExpandDirectory
}

function Expand-OrcResults {
    <#
    .SYNOPSIS
        Extract all DFIR-Orc archives referenced by Outcome file(s).

    .PARAMETER Path
        Path to a directory with Outcome files or to an Outcome file.

    .PARAMETER Destination
        Path to output directory.

    .PARAMETER PrivateKey
        Path to the private key file for p7b extraction.

    .PARAMETER Force
        Overwrite any output file.

    .OUTPUTS
        Array of expanded directories.
    #>
    Param(
        [Parameter(Mandatory)]
        [String]
        $Path,
        [Parameter(Mandatory)]
        [String]
        $Destination,
        [Parameter()]
        [String]
        $PrivateKey,
        [Parameter()]
        [switch]
        $Force = $false
    )

    $Path = Resolve-Path $Path
    if (-Not $Path)
    {
        return
    }

    $ExpandedDirectories = @()
    $OutcomeFiles = Find-OrcOutcome $Path
    foreach ($OutcomePath in $OutcomeFiles)
    {
        $Outcome = Get-Content $OutcomePath | ConvertFrom-Json
        $ComputerName = $Outcome."dfir-orc"."outcome"."computer_name"
        Write-Verbose "Expand Orc archives from '$ComputerName' ('$OutcomePath')"

        $Archives = @()
        foreach ($CommandSet in $Outcome."dfir-orc"."outcome"."command_set")
        {
            foreach ($Archive in $CommandSet."Archive")
            {
                $ArchivePath = Join-Path $OutcomePath.Directory $Archive.Name
                $Archives += Get-ChildItem $ArchivePath
            }
        }

        $ExpandDirectory = Join-Path $Destination $ComputerName
        foreach ($Archive in $Archives)
        {
            # TODO: move this into Expand-OrcArchive
            if ($Archive.Length -eq 0)
            {
                Write-Error "Ignored empty archive '$Archive'"
                continue
            }

            Write-Verbose "Expand-OrcArchive -Path $Archive -Destination $ExpandDirectory"
            $ExpandedDirectories += Expand-OrcArchive -Path $Archive -Destination $ExpandDirectory -PrivateKey:$PrivateKey -Force:$Force
        }

        Copy-Item $OutcomePath -Destination $ExpandDirectory
    }

    return $ExpandedDirectories
}

function Get-EmptyFiles {
    Param(
        [Parameter(Mandatory)]
        [System.IO.DirectoryInfo]
        $Path,
        [Parameter()]
        [String]
        $Filter,
        [Parameter()]
        [Switch]
        $Recurse,
        [Parameter()]
        [String[]]
        $Exclude,
        [Parameter()]
        [String[]]
        $Include
    )

    return Get-ChildItem -Recurse:$Recurse -File -Filter:$Filter -Path:$Path -Exclude:$Exclude -Include:$Include | Where-Object { $_.Length -eq 0 }
}

function Test-EmptyFiles {
    Param(
        [Parameter(Mandatory)]
        [System.IO.DirectoryInfo]
        $Path,
        [Parameter()]
        [String[]]
        $Exclude,
        [Parameter()]
        [String[]]
        $Include
    )

    $EmptyFiles = Get-EmptyFiles -Recurse -Exclude:$Exclude -Include:$Include -Path:$Path
    if ($EmptyFiles)
    {
        $Files = $EmptyFiles -join "`n    "
        Write-Warning "Found empty file(s): `n    $Files"
    }

    return $Emptyfiles
}
function Find-OrcOutcome() {
    <#
    .SYNOPSIS
        Find ORC outcome json files.

    .PARAMETER Path
        Path to a specific Outcome file or a directory for multiple lookup.

    .PARAMETER Recurse
        Search the directory specified by Path and its subdirectories.
    #>
    Param(
        [Parameter(Mandatory)]
        [String]
        $Path,
        [Parameter()]
        [Switch]
        $Recurse
    )

    Write-Verbose "Looking for Outcome in $Path"
    $OutcomeList = @()
    $JsonFiles = Get-ChildItem -Recurse:$Recurse -Filter "*.json" $Path
    foreach ($JsonFile in $JsonFiles)
    {
        Write-Verbose "Checking $JsonFile"
        $Json = Get-Content $JsonFile | ConvertFrom-Json
        if ($Json."dfir-orc"."outcome")
        {
            Write-Verbose "Found Orc outcome '$JsonFile'"
            $OutcomeList += $JsonFile
        }
    }

    return $OutcomeList
}

function Get-OrcOutcome {
    <#
    .SYNOPSIS
        Parse ORC outcome json file(s) and return structured output summary.

    .PARAMETER Path
        Path to a specific Outcome file or a directory for multiple processing.

    .PARAMETER Exclude
        List of commands which exit code should be ignored.

    .LINK
        Find-OrcOutcome
        Test-OrcOutcome
    #>
    Param(
        [Parameter(Mandatory)]
        [String]
        $Path,
        [Parameter()]
        [String[]]
        $Exclude
    )
    function Private:HasError {
        param($Commands)

        $Count = 0
        foreach ($Command in $Commands)
        {
            if ($Command.exit_code -ne 0)
            {
                $Count += 1
            }
        }

        return $Count
    }

    $OutcomeFiles = Find-OrcOutcome $Path
    if (-not $OutcomeFiles)
    {
        Write-Error "Failed to locate Orc outcome json file in '$Path'"
        return
    }

    $OutcomeList = @()
    foreach ($OutcomePath in $OutcomeFiles)
    {
        $Outcome = Get-Content $OutcomePath | ConvertFrom-Json

        $MaxMemoryPeak = 0
        $MaxMemoryPeakSetName = $null
        $Failures = [System.Collections.ArrayList]::New()
        foreach ($Set in $Outcome."dfir-orc"."outcome".command_set)
        {
            $ProcessMemoryPeak = $Set."statistics"."process_memory_peak"
            if ($ProcessMemoryPeak -gt $MaxMemoryPeak)
            {
                $MaxMemoryPeak = $ProcessMemoryPeak
                $MaxMemoryPeakSetName = $Set.Name
            }

            if (-Not (HasError $Set.commands))
            {
                continue
            }

            foreach ($Command in $Set.commands)
            {
                if ($Exclude -Contains $Command.name)
                {
                    continue
                }

                if ($Command.exit_code -eq 0)
                {
                    continue
                }

                $Start = $Command.start | Get-Date -DisplayHint Time
                $Duration = New-TimeSpan –Start $Command.start –End $Command.end

                [void]$Failures.Add(
                    @{
                        "Set" = $Set.name
                        "Command" = $Command.name
                        "Code" = $Command.exit_code
                        "Duration" = $Duration.ToString("dd\.hh\:mm\:ss")
                        "Start" = $Start.DateTime
                    }
                )
            }
        }

        $Start = $Outcome."dfir-orc"."outcome"."start"
        $End = $Outcome."dfir-orc"."outcome"."end"
        $Duration = New-TimeSpan –Start $Start –End $End

        $OutcomeList += (
            @{
                "ComputerName" = $Outcome."dfir-orc"."outcome"."computer_name"
                "Duration" = $Duration.ToString("dd\.hh\:mm\:ss")
                "Failures" = $Failures
                "MemoryPeak" = @{
                    "Set" = $MaxMemoryPeakSetName
                    "Value" = $MaxMemoryPeak
                }
            }
        )
    }

    return $OutcomeList
}

function Test-OrcOutcome {
    <#
    .SYNOPSIS
        Parse a DFIR-Orc Outcome file(s) and print a summary.

    .PARAMETER Path
        Path to a specific Outcome file or a directory with multiple Outcome files.

    .PARAMETER Exclude
        List of commands which exit code should be ignored.

    .PARAMETER PassThru
        Return the summary as structured output.

    .LINK
        Find-OrcOutcome
        Get-OrcOutcome
    #>
    Param(
        [Parameter(Mandatory)]
        [String]
        $Path,
        [Parameter()]
        [String[]]
        $Exclude,
        [Parameter()]
        [Switch]
        $PassThru
    )

    $OutcomeList = Get-OrcOutcome $Path
    foreach ($Outcome in $OutcomeList)
    {
        foreach ($Failure in $Outcome["Failures"])
        {
            if ($Exclude -and ($Exclude.Contains($Failure.Set) -or $Exclude.Contains($Failure.Command)))
            {
                continue
            }

            $ExitCode = ([int32]($Failure.Code)).ToString("x")

            Write-Warning ("$($Outcome["ComputerName"]): Failed " `
                + "$($Failure.Set)/" `
                + "$($Failure.Command) " `
                + "(code: 0x$ExitCode, " `
                + "duration: $($Failure.Duration), " `
                + "start: $($Failure.Start))")
        }
    }

    if ($PassThru)
    {
        return $OutcomeList
    }
}

function Test-OrcExpandedResults {
    <#
    .SYNOPSIS
        Check DFIR-Orc results using Outcome's commands exit code and do some basic integrity tests.

    .PARAMETER Path
        Directory with Orc's already expanded results (see 'Expand-OrcResults').

    .PARAMETER Exclude
        List of commands that should be ignored during the checks.

    .PARAMETER AllowEmpty
        List of expected empty files that should be ignored during the check.

    .PARAMETER PassThru
        Return structured result output.

    .EXAMPLE
        Test-OrcExpandedResults `
>>          -Path C:\orc\results `
>>          -Exclude `
>>                 "Azure_Identity", `
>>                 "Azure_Attested", `
>>                 "Azure_Instance", `
>>                 "Azure_ScheduledEvents" `
>>          -AllowEmpty `
>>                 "Azure_Attested.json", `
>>                 "Azure_Identity.json", `
>>                 "Azure_Instance.json", `
>>                 "Azure_ScheduledEvents.json", `
>>                 "processes1.log", `
>>                 "processes2.log", `
>>                 "Residents.log", `
>>                 "WFP_dumper.log", `
>>                 "EventConsumer.log", `
>>                 "AD_computers.log", `
>>                 "DNS_records.log"

    .LINK
        Expand-OrcResults
        Test-OrcOutcome
        Test-EmptyFiles
        Get-OrcResultsStatistics
    #>
    Param(
        [Parameter(Mandatory)]
        [String]
        $Path,
        [Parameter()]
        [String[]]
        $Exclude,
        [Parameter()]
        [String[]]
        $AllowEmpty,
        [Parameter()]
        [Switch]
        $PassThru
    )

    $Path = Resolve-Path $Path
    if (-Not $Path)
    {
        return
    }

    $OucomePath = Find-OrcOutcome -Recurse $Path
    if ($OucomePath.Count -gt 1)
    {
        Write-Error "Found multiple Outcome files"
        return
    }

    $Outcome = Test-OrcOutcome -Exclude:$Exclude -PassThru $OucomePath[0]

    $Emptyfiles = Get-EmptyFiles `
        -Path $Path `
        -Recurse `
        -Include "*.log", "*.csv", "*.xml", "*.json", "*.txt", "*.dmp" `
        -Exclude:$AllowEmpty

    foreach ($EmptyFile in $EmptyFiles)
    {
        Write-Warning "$($Outcome["ComputerName"]): Empty file: $($EmptyFile.Name)"
    }

    if ($PassThru)
    {
        return @{
            "Failures" = $Outcome.Failures
            "EmptyFiles" = $Emptyfiles | Select-Object -ExpandProperty "FullName"
        }
    }
}

function Test-OrcResults {
     <#
    .SYNOPSIS
        Check one or multiple DFIR-Orc results using Outcome's commands exit code and do some basic integrity tests.

    .DESCRIPTION
        This command will look for all outcome json files in the specified 'Path'. It will expand all results in 'Destination' while
        checking for result integrity. It will alert user with warning messages and with structured output if '-PassThru' is on.

    .PARAMETER Path
        Directory with DFIR-Orc Outcome and archives.

    .PARAMETER Destination
        Path to output directory for expanded results.

    .PARAMETER Recurse
        Search the directory specified by Path and its subdirectories.

    .PARAMETER Exclude
        List of commands that should be ignored during the checks.

    .PARAMETER AllowEmpty
        List of expected empty files that should be ignored during the check.

    .PARAMETER PassThru
        Return structured result output.

    .PARAMETER PrivateKey
        Path to the private key file for p7b extraction.

    .PARAMETER Force
        Overwrite any output file.

    .EXAMPLE
        Test-OrcResults `
>>          -Path C:\orc\output `
>>          -Exclude `
>>                 "Azure_Identity", `
>>                 "Azure_Attested", `
>>                 "Azure_Instance", `
>>                 "Azure_ScheduledEvents" `
>>          -AllowEmpty `
>>                 "Azure_Attested.json", `
>>                 "Azure_Identity.json", `
>>                 "Azure_Instance.json", `
>>                 "Azure_ScheduledEvents.json", `
>>                 "processes1.log", `
>>                 "processes2.log", `
>>                 "Residents.log", `
>>                 "WFP_dumper.log", `
>>                 "EventConsumer.log", `
>>                 "AD_computers.log", `
>>                 "DNS_records.log"

    .LINK
        Expand-OrcExpandResults
        Test-OrcOutcome
        Test-EmptyFiles
        Get-OrcResultsStatistics
    #>
    Param(
        [Parameter(Mandatory)]
        [String]
        $Path,
        [Parameter(Mandatory)]
        [String]
        $Destination,
        [Parameter()]
        [switch]
        $Recurse,
        [Parameter()]
        [String[]]
        $Exclude,
        [Parameter()]
        [String[]]
        $AllowEmpty,
        [Parameter()]
        [String]
        $PrivateKey,
        [Parameter()]
        [switch]
        $Force,
        [Parameter()]
        [Switch]
        $PassThru
    )

    $Path = Resolve-Path $Path
    if (-Not $Path)
    {
        return
    }

    $OrcResultsList = @()
    $OutcomeFiles = Find-OrcOutcome -Recurse:$Recurse $Path
    foreach ($OutcomePath in $OutcomeFiles)
    {
        Write-Verbose "Expand DFIR-Orc results from '$OutcomePath'"
        $ExpandedDirectories = Expand-OrcResults -Force:$Force -Path $OutcomePath -Destination $Destination -PrivateKey:$PrivateKey
        $ExpandedDirectories = `
            $ExpandedDirectories | ForEach-Object { ([System.IO.DirectoryInfo]$_).Parent } | Select-Object -Unique

        ForEach ($ExpandDirectory in $ExpandedDirectories)
        {
            $OrcResults = Test-OrcExpandedResults `
                -Path $ExpandDirectory `
                -Exclude:$Exclude `
                -AllowEmpty:$AllowEmpty `
                -PassThru

            $Outcome = Get-Content $OutcomePath | ConvertFrom-Json
            $ComputerName = $Outcome."dfir-orc"."outcome"."computer_name"
            $OrcResultsList += @{
                "Name" = $ComputerName
                "Results" = $OrcResults
            }
        }
    }

    if ($PassThru)
    {
        return $OrcResultsList
    }
}

function Get-HumanReadableSize($num) {
    $suffix = "B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"
    $index = 0
    while ($num -gt 1kb)
    {
        $num = $num / 1kb
        $index++
    }

    "{0:N1} {1}" -f $num, $suffix[$index]
}

function Get-OrcStatistics {
    <#
    .SYNOPSIS
        Get multiple statistics about Orc results.

    .PARAMETER Path
        Directory with Orc's already expanded results (see 'Expand-OrcResults').

    .LINK
        Expand-OrcResults
    #>
    Param(
        [Parameter(Mandatory)]
        [String]
        $Path,
        [Parameter()]
        [Switch]
        $PassThru,
        [Parameter()]
        [Switch]
        $Details
    )

    $OucomePath = Find-OrcOutcome -Recurse $Path
    if ($OucomePath.Count -gt 1)
    {
        Write-Error "Found multiple Outcome files"
        return
    }

    $Outcome = Get-OrcOutcome $OucomePath -ErrorAction Continue
    if (-not $Outcome)
    {
        return
    }

    $Files = Get-ChildItem -File -Recurse $Path
    $FileStats = @{
        "*" = @{
            "Size" = [uint64]($Files | Measure-Object -Property Length -sum | Select-Object -ExpandProperty Sum)
            "Count" = $Files.Count
        }
    }

    $Extensions = $Files | Sort-Object -Property Extension -Unique | Select-Object -ExpandProperty Extension
    foreach ($Extension in $Extensions)
    {
        $FileSet = $Files | Where-Object { $_.Extension -eq $Extension }

        $FileStats[$Extension] = @{
            "Size" = [uint64]($FileSet | Measure-Object -Property Length -sum | Select-Object -ExpandProperty Sum)
            "Count" = $FileSet.Count
        }
    }

    $ExtensionStats = @()
    foreach ($Extension in $Extensions)
    {
        $ExtFileCount = $FileStats[$Extension]["Count"]
        $ExtFileSize = $FileStats[$Extension]["Size"]

        $ExtensionStats += "${Extension}: ${ExtFileCount} ($(Get-HumanReadableSize ${ExtFileSize}))"
    }

    $FileSize = $FileStats["*"]["Size"]
    $FileCount = $FileStats["*"]["Count"]
    $FileSizeHR = $(Get-HumanReadableSize $FileSize)

    $ComputerName = $Outcome["ComputerName"]
    $Duration = $Outcome["Duration"] ? "Execution: $($Outcome.Duration)" : ""
    $MaxMemoryPeak = Get-HumanReadableSize $Outcome["MemoryPeak"]["Value"]
    $MaxMemoryName = $($Outcome["MemoryPeak"]["Set"])
    $Failures = $Outcome["Failures"].Count
    Write-Host "${ComputerName}: ${Duration}, fails: $Failures, max memory: $MaxMemoryPeak ($MaxMemoryName), files: $FileCount ($FileSizeHR)"

    if ($Details)
    {
        Write-Host $(Join-String -InputObject $ExtensionStats -OutputPrefix "    " -Separator "`n    ")
    }

    if ($PassThru)
    {
        return @{
            "outcome" = $Outcome
            "files" = $FileStats
        }
    }
}

function New-TemporaryDirectory {
    $Parent = [System.IO.Path]::GetTempPath()
    [string] $Name = [System.Guid]::NewGuid()
    New-Item -ItemType Directory -Path (Join-Path $Parent $Name) -ErrorAction Stop
}

function ListContainsAllOf {
    <#
    .SYNOPSIS
        Return true if '$Items' elements are all contained in '$Container'.
    #>
    Param($Container, [String[]]$Items)
    return ($Items | Where-Object { $_ -In $Container }).Count -eq $Items.Count
}

function ListContainsAnyOf {
    <#
    .SYNOPSIS
        Return true if any $Container element exists in $Items'.
    #>
    Param($Container, $Items)
    return ($Items | Where-Object { $_ -In $Container }).Count -ne 0
}


#
# Inspired from:
# https://community.idera.com/database-tools/powershell/powertips/b/tips/posts/get-text-file-encoding
#
function Get-Encoding {
    Param(
        [Parameter(Mandatory,ValueFromPipeline,ValueFromPipelineByPropertyName)]
        [Alias('FullName')]
        [string]
        $Path
    )
    $bom = New-Object -TypeName System.Byte[](4)
    $file = New-Object System.IO.FileStream($Path, 'Open', 'Read')

    $null = $file.Read($bom, 0, 4)
    $file.Close()
    $file.Dispose()

    $enc = [Text.Encoding]::ASCII
    if ($bom[0] -eq 0x2b -and $bom[1] -eq 0x2f -and $bom[2] -eq 0x76) { $enc =  [Text.Encoding]::UTF7 }
    if ($bom[0] -eq 0xff -and $bom[1] -eq 0xfe) { $enc =  [Text.Encoding]::Unicode }
    if ($bom[0] -eq 0xfe -and $bom[1] -eq 0xff) { $enc =  [Text.Encoding]::BigEndianUnicode }
    if ($bom[0] -eq 0x00 -and $bom[1] -eq 0x00 -and $bom[2] -eq 0xfe -and $bom[3] -eq 0xff) { $enc =  [Text.Encoding]::UTF32}
    if ($bom[0] -eq 0xef -and $bom[1] -eq 0xbb -and $bom[2] -eq 0xbf) { $enc =  [Text.Encoding]::UTF8}

    # Autodetection of utf-16 file without bom
    if ($bom[0] -ge 0x20 -and $bom[0] -le 0x7e -and $bom[1] -eq 0x00) { $enc =  [Text.Encoding]::Unicode }

    [PSCustomObject]@{
        Type = $enc
        Path = $Path
    }
}

#
# About "Live" ORC made on a running system.
#
# The conversion to a diffable output is actually limited. A running system will modify a lot of files even with a
# disabled network and services. Currently only human will be able to do a diff (using Total Commander's compare is
# recommended). Ideally diff should be smarter to be able to handle only meaningfull changes and ignoring hard link
# count, temporary files, modification date...).
#
function ConvertTo-OrcDiffableResults {
    <#
    .SYNOPSIS
        Convert Orc results so they can be compared between multiple execution.

        Suits better for DFIR-Orc's 'offline' results.

    .DESCRIPTION
        Strip and sort data expected to change between two run on the same disk image.

    .PARAMETER Path
        Directory with Orc's already expanded results (see 'Expand-OrcResults').

    .PARAMETER Destination
        Path to an output directory.

    .PARAMETER Exclude
        Specifies files to be excluded from copy to 'Destination'.

    .PARAMETER Include
        Specifies additional files to be included for copy to 'Destination'.

    .PARAMETER NotOffline
        Specify that input results does NOT come from an 'Offline' DFIR-Orc.

    .PARAMETER Force
        Overwrite any existing file.

    .EXAMPLE
        ConvertTo-OrcDiffableResults `
            -Path w10_enterprise_offline\10.1.0-rc10\out\ `
            -Destination w10_enterprise_offline\10.1.0-rc10\diffable\ `
            -Offline `
            -Exclude `
                "*.log", `
                "config.xml", `
                "volstats.csv", `
                "JobStatistics.csv", `
                "ProcessStatistics.csv", `
                "*_Outline.json", `
                "*_Outcome.json", `
                "GetSectors.csv"
    #>
    Param(
        [Parameter(Mandatory)]
        [String]
        $Path,
        [Parameter(Mandatory)]
        [String]
        $Destination,
        [Parameter()]
        [String[]]
        $Exclude,
        [Parameter()]
        [String[]]
        $Include,
        [Parameter()]
        [switch]
        $NotOffline,
        [Parameter()]
        [Switch]
        $Force
    )

    $NuExePath = Find-NuShell
    if (-not $NuExePath)
    {
        Write-Error "'ConvertTo-OrcDiffableResults' requires nu.exe 0.35.0 to be installed, see https://github.com/nushell/nushell"
        return
    }

    New-Alias NuExe $NuExePath

    $Path = Resolve-Path $Path
    if (-Not $Path)
    {
        return
    }

    New-Item -ItemType Directory $Destination -ErrorAction SilentlyContinue | Out-Null
    $Destination = Resolve-Path $Destination

    Copy-Item $Path/*.json -Destination $Destination

    $ExcludedColumns = @(
        "SampleCollectionDate"
    )

    if ($NotOffline)
    {
        # Must be removed as their content differ a lot
        $ExcludedColumns += (
            "CreationDate",
            "LastAccessDate",
            "LastModificationDate",
            "FileNameLastAccessDate",
            "LastAttrChangeDate"
        )
    }

    # Strip from CSV all changing columns like creation timestamps...
    foreach ($CSV in (Get-ChildItem $Path -Recurse -Exclude:$Exclude -Include:$Include -Filter "*.csv"))
    {
        # Exclude subdirectories containing excluded keyword
        if (ListContainsAnyOf $CSV.FullName.Split("\") $Exclude)
        {
            Write-Verbose "Exclude: $CSV"
            continue
        }

        Write-Verbose "Processing: $CSV"
        [System.IO.FileInfo]$Out = Join-Path $Destination $(Resolve-RelativePath $CSV -Base $Path)

        if (-Not $Out.Directory.Exists)
        {
            New-Item -ItemType Directory $Out.Directory.FullName | Out-Null
        }

        if ($NotOffline)
        {
            # The autoruns.csv file is utf-16 encoded without any bom. Recent autoruns versions accept '-o' option
            # to write utf-8 file instead of capturing stdout.
            $Encoding = Get-Encoding $CSV
            if ($Encoding.Type.WebName -notin @("utf-8","us-ascii"))
            {
                if (-not $TemporaryDirectory)
                {
                    $TemporaryDirectory = New-TemporaryDirectory
                }

                $Utf8 = "$TemporaryDirectory\$($CSV.Name)"
                Get-Content -Encoding $Encoding.Type $CSV | Out-File -Encoding "UTF8" $Utf8
                $CSV = $Utf8
            }
        }

        # Do a raw copy for csv containing only header and no row
        if ($CSV.Length -le 4096 -and $(Import-Csv $CSV).Count -eq 0)
        {
            Copy-Item $CSV $Out
            continue
        }

        # Retrieve all columns and remove those excluded
        $Columns = NuExe -c `
            "dataframe open $CSV | dataframe first 1 | dataframe show | pivot | get Column0 | to json" | ConvertFrom-Json
        $Columns = $Columns | Where-Object { $_ -NotIn $ExcludedColumns } | ForEach-Object { "'$_'" }

        # Results output files are not described by any metadata. To be able to sort using the correct columns, schema
        # is "detected" relying on column presence.
        $SortKeys = @(
            @("Disk", "DumpOffset"), # GetSectors
            @("FRN", "ParentName", "File"),  # NtfsInfo
            @("FRN", "FullName", "SampleName"),  # GetThis
            @("FRN", "VolumeID", "Name", "FilenameFlags")  # NtfsInfo I30
            @("FullPath", "FileName", "Loaded", "Registry", "Running"),  # GetSamples (SampleInfo)
            @("ProcessID", "ParentID", "FullPath", "Type", "Time"),  # GetSamples (Timeline)
            @("ObjectPath", "ObjectName", "ObjectType"),  # GetObjInfo
            @("Domain", "Processor(s)"),  # SystemInfo
            @("Time", "Entry", "Enabled", "Path"),  # Autoruns
            @("VolumeID", "Location")
        )

        $EscapedSortKeys = @(@())
        ForEach ($KeySet in $SortKeys)
        {
            $EscapedSortKeys += ,($KeySet | ForEach-Object { "'$_'"})
        }

        $SortKeys = $EscapedSortKeys
        $SortSelection = $SortKeys | Where-Object { ListContainsAllOf $Columns $_ } | Select-Object -First 1

        # TODO: Eventually use [all] random column to sort without having to detect the tool it is coming from. Could introduce delays...
        # Fallback on the first column as sorting key, this could not work for some files where the first column is always the same value.
        if (-not $SortSelection)
        {
            $SortSelection = $Columns[0]
            Write-Warning "$($CSV.Name): No sort keys detected, using: $SortSelection"
        }
        else
        {
            Write-Verbose "$($CSV.Name): Found sort keys: $SortSelection"
        }

        $Selection = $Columns | Join-String -Separator " "
        Write-Verbose "Calling nu with: dataframe open $CSV | dataframe select $Selection | dataframe sort $SortSelection | dataframe to-csv $Out"
        NuExe -c `
            "dataframe open $CSV | dataframe select $Selection | dataframe sort $SortSelection | dataframe to-csv $Out"

        Write-Output ""
    }

    # Copy every other files
    Write-Verbose "Copying remaining files"
    foreach ($Item in (Get-ChildItem $Path -File -Recurse -Exclude:($Exclude + @("*.csv", "*.log")) -Include:$Include))
    {
        # Exclude subdirectories containing excluded keyword
        if (ListContainsAnyOf $Item.FullName.Split("\") $Exclude)
        {
            continue
        }

        [System.IO.FileInfo]$Out = Join-Path $Destination $(Resolve-RelativePath $Item -Base $Path)
        if (-Not $Out.Directory.Exists)
        {
            New-Item -ItemType Directory $Out.Directory.FullName | Out-Null
        }

        Write-Debug "Copy '$Item' to '$Out'"
        Copy-Item -LiteralPath "$Item" "$Out"
    }

    if ($TemporaryDirectory)
    {
        Remove-Item -Recurse $TemporaryDirectory
    }
}

function Resolve-RelativePath {
    Param(
        [Parameter(Mandatory)]
        [System.IO.FileInfo]
        $Path,
        [Parameter(Mandatory)]
        [System.IO.DirectoryInfo]
        $Base
    )

    Push-Location
    Set-Location $Base
    $RelativePath = Resolve-Path -Relative -LiteralPath $Path
    Pop-Location
    return $RelativePath.Substring(2)
}

function Compare-FileMD5 {
    Param(
        [Parameter(Mandatory)]
        [System.IO.FileInfo]
        $Path1,
        [Parameter(Mandatory)]
        [System.IO.FileInfo]
        $Path2
    )

    $FileHash1 = Get-FileHash -Algorithm MD5 -LiteralPath $Path1
    $FileHash2 = Get-FileHash -Algorithm MD5 -LiteralPath $Path2

    return $($FileHash1).Hash -eq $($FileHash2).Hash
}

function Compare-OrcDiffableResults {
    <#
    .SYNOPSIS
        Compare two different set of Orc results that as already been prepared for diff.

    .PARAMETER Path
        Directory with Orc's prepared diffable results (see 'ConvertTo-OrcDiffableResults').

    .PARAMETER Reference
        Directory with Orc's prepared diffable results used as reference for comparison (see 'ConvertTo-OrcDiffableResults').

    .PARAMETER Include
        List of files to include for comparison.

    .PARAMETER Exclude
        List of files to exclude for comparison.

    .EXAMPLE
        Compare-OrcDiffableResults `
            -Path w10_enterprise_offline\10.1.0-rc10\diffable\ `
            -Reference w10_enterprise_offline\10.1.0-rc9\diffable\ `
            -Exclude `
                "*.log", `
                "config.xml", `
                "volstats.csv", `
                "JobStatistics.csv", `
                "ProcessStatistics.csv", `
                "*_Outline.json", `
                "*_Outcome.json", `
                "GetSectors.csv"

    .LINK
        Expand-OrcResults
        ConvertTo-OrcDiffableResults
        Compare-OrcResults
    #>
    Param(
        [Parameter(Mandatory)]
        [String]
        $Path,
        [Parameter(Mandatory)]
        [String]
        $Reference,
        [Parameter()]
        [String[]]
        $Include,
        [Parameter()]
        [String[]]
        $Exclude
    )

    $ErrorMissingFileCount = 0
    $ErrorDiffFileCount = 0

    Write-HostLog "Statistics for '$Path'"
    Get-OrcStatistics $Path

    Write-HostLog "Statistics for '$Reference'"
    Get-OrcStatistics $Reference

    $ReferenceFiles = Get-ChildItem -File -Recurse -Include:$Include -Exclude:$Exclude $Reference
    foreach ($ReferenceItem in $ReferenceFiles)
    {
        $ItemRelativePath = $(Resolve-RelativePath $ReferenceItem -Base $Reference)
        [System.Io.FileInfo]$ResultItem = "$Path\$ItemRelativePath"

        if (-not (Test-Path -PathType Leaf -LiteralPath $ResultItem))
        {
            Write-Warning "File does not exists: $ItemRelativePath"
            $ErrorMissingFileCount += 1
            continue
        }

        if (-not $(Compare-FileMD5 $ResultItem $ReferenceItem))
        {
            Write-Warning "Files differ: '$ItemRelativePath'"
            $ErrorDiffFileCount += 1
        }
    }

    if ($ErrorMissingFileCount)
    {
        Write-Host "Missing file(s) count: $ErrorMissingFileCount"
    }

    if ($ErrorDiffFileCount)
    {
        Write-Host "Differing file(s) count: $ErrorDiffFileCount"
    }
}
