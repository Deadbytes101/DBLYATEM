# DBLYATEM NATIVE WINDOWS V3 EVIDENCE CAPTURE
#
# Purpose:
#   Establish the native Windows correctness baseline for the immutable
#   probe-v1 workload before DBLYATEM or Winlator comparison.
#
# This script does not benchmark performance and does not claim parity.

[CmdletBinding()]
param(
    [Parameter()]
    [string] $OutputRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ExpectedWorkloadCommit = 'c57130981bca654f33d6a832aecd0d8348b6c02d'
$ExpectedSourcePath = 'benchmarks/windows-x64-workload/probe-v1.hex'
$ExpectedSourceBlobSha1 = '87155048d6ef4d56cb9323ef20fc6b8b1850af0e'
$ExpectedSourceBytes = 3073
$ExpectedSourceSha256 = '12d42d73b87294fd3e4dc635d7f9c5404cb39f18bf07df4320028b65bdfaacac'
$ExpectedExecutableBytes = 1536
$ExpectedExecutableSha256 = 'b32d2a090ce2fde1132f1daba6501a1bc6bf8f2c3e5b950b728eb30f8b211210'

function New-ProcessStartInfo {
    param(
        [Parameter(Mandatory)]
        [string] $FileName,

        [Parameter(Mandatory)]
        [string] $Arguments,

        [Parameter(Mandatory)]
        [string] $WorkingDirectory
    )

    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $FileName
    $info.Arguments = $Arguments
    $info.WorkingDirectory = $WorkingDirectory
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    return $info
}

function Invoke-GitText {
    param(
        [Parameter(Mandatory)]
        [string] $Arguments,

        [Parameter(Mandatory)]
        [string] $WorkingDirectory
    )

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = New-ProcessStartInfo `
        -FileName 'git.exe' `
        -Arguments $Arguments `
        -WorkingDirectory $WorkingDirectory

    try {
        if (-not $process.Start()) {
            throw "failed to start git.exe $Arguments"
        }

        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()

        if ($process.ExitCode -ne 0) {
            throw "git.exe $Arguments failed exit=$($process.ExitCode): $($stderr.Trim())"
        }

        return $stdout.TrimEnd("`r", "`n")
    }
    finally {
        $process.Dispose()
    }
}

function Export-GitBlobBytes {
    param(
        [Parameter(Mandatory)]
        [string] $BlobSha1,

        [Parameter(Mandatory)]
        [string] $Destination,

        [Parameter(Mandatory)]
        [string] $WorkingDirectory
    )

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = New-ProcessStartInfo `
        -FileName 'git.exe' `
        -Arguments "cat-file blob $BlobSha1" `
        -WorkingDirectory $WorkingDirectory

    $destinationStream = $null
    try {
        if (-not $process.Start()) {
            throw 'failed to start git.exe cat-file'
        }

        $destinationStream = [System.IO.File]::Open(
            $Destination,
            [System.IO.FileMode]::Create,
            [System.IO.FileAccess]::Write,
            [System.IO.FileShare]::None
        )

        $process.StandardOutput.BaseStream.CopyTo($destinationStream)
        $destinationStream.Flush()
        $destinationStream.Dispose()
        $destinationStream = $null

        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()

        if ($process.ExitCode -ne 0) {
            throw "git.exe cat-file blob failed exit=$($process.ExitCode): $($stderr.Trim())"
        }
    }
    finally {
        if ($null -ne $destinationStream) {
            $destinationStream.Dispose()
        }
        $process.Dispose()
    }
}

function Get-Sha256Lower {
    param(
        [Parameter(Mandatory)]
        [string] $LiteralPath
    )

    return (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-Equal {
    param(
        [Parameter(Mandatory)]
        $Actual,

        [Parameter(Mandatory)]
        $Expected,

        [Parameter(Mandatory)]
        [string] $Name
    )

    if ($Actual -ne $Expected) {
        throw "$Name mismatch expected=$Expected actual=$Actual"
    }
}

function Get-UInt16LE {
    param(
        [Parameter(Mandatory)]
        [byte[]] $Bytes,

        [Parameter(Mandatory)]
        [int] $Offset
    )

    if (($Offset -lt 0) -or (($Offset + 2) -gt $Bytes.Length)) {
        throw "UInt16 read outside file offset=$Offset bytes=$($Bytes.Length)"
    }

    return [uint16]($Bytes[$Offset] -bor ($Bytes[$Offset + 1] -shl 8))
}

function Get-UInt32LE {
    param(
        [Parameter(Mandatory)]
        [byte[]] $Bytes,

        [Parameter(Mandatory)]
        [int] $Offset
    )

    if (($Offset -lt 0) -or (($Offset + 4) -gt $Bytes.Length)) {
        throw "UInt32 read outside file offset=$Offset bytes=$($Bytes.Length)"
    }

    return [uint32](
        [uint32]$Bytes[$Offset] -bor
        ([uint32]$Bytes[$Offset + 1] -shl 8) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 16) -bor
        ([uint32]$Bytes[$Offset + 3] -shl 24)
    )
}

function Test-AsciiNeedle {
    param(
        [Parameter(Mandatory)]
        [byte[]] $Haystack,

        [Parameter(Mandatory)]
        [string] $Needle
    )

    $needleBytes = [System.Text.Encoding]::ASCII.GetBytes($Needle)
    if ($needleBytes.Length -gt $Haystack.Length) {
        return $false
    }

    for ($i = 0; $i -le ($Haystack.Length - $needleBytes.Length); $i++) {
        $matched = $true
        for ($j = 0; $j -lt $needleBytes.Length; $j++) {
            if ($Haystack[$i + $j] -ne $needleBytes[$j]) {
                $matched = $false
                break
            }
        }
        if ($matched) {
            return $true
        }
    }

    return $false
}

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory)]
        [string] $LiteralPath,

        [Parameter(Mandatory)]
        [string] $Text
    )

    $encoding = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($LiteralPath, $Text, $encoding)
}

$timestampUtc = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$scriptPath = $MyInvocation.MyCommand.Path
$scriptSha256 = Get-Sha256Lower -LiteralPath $scriptPath
$repositoryRoot = $null
$artifactDirectory = $null
$evidencePath = $null
$evidenceHashPath = $null
$sourceArtifactPath = $null
$executablePath = $null
$nativeExitCode = $null
$elapsedMilliseconds = $null
$failure = $null
$verdict = 'FAIL'

try {
    if ($env:OS -ne 'Windows_NT') {
        throw "native baseline requires Windows_NT; observed=$($env:OS)"
    }

    if (-not [Environment]::Is64BitOperatingSystem) {
        throw 'native baseline requires a 64-bit Windows operating system'
    }

    $gitCommand = Get-Command git.exe -ErrorAction Stop
    if ([string]::IsNullOrWhiteSpace($gitCommand.Source)) {
        throw 'git.exe resolution returned an empty path'
    }

    $repositoryRoot = Invoke-GitText `
        -Arguments 'rev-parse --show-toplevel' `
        -WorkingDirectory (Get-Location).Path

    if ([string]::IsNullOrWhiteSpace($repositoryRoot)) {
        throw 'git rev-parse returned an empty repository root'
    }

    $resolvedRoot = (Resolve-Path -LiteralPath $repositoryRoot).Path

    $commitType = Invoke-GitText `
        -Arguments "cat-file -t $ExpectedWorkloadCommit" `
        -WorkingDirectory $resolvedRoot
    Assert-Equal -Actual $commitType -Expected 'commit' -Name 'workload object type'

    $treeEntry = Invoke-GitText `
        -Arguments "ls-tree $ExpectedWorkloadCommit -- $ExpectedSourcePath" `
        -WorkingDirectory $resolvedRoot

    $expectedTreeEntry = "100644 blob $ExpectedSourceBlobSha1`t$ExpectedSourcePath"
    Assert-Equal -Actual $treeEntry -Expected $expectedTreeEntry -Name 'immutable workload tree entry'

    $blobType = Invoke-GitText `
        -Arguments "cat-file -t $ExpectedSourceBlobSha1" `
        -WorkingDirectory $resolvedRoot
    Assert-Equal -Actual $blobType -Expected 'blob' -Name 'source object type'

    $blobSizeText = Invoke-GitText `
        -Arguments "cat-file -s $ExpectedSourceBlobSha1" `
        -WorkingDirectory $resolvedRoot

    $blobSize = 0L
    if (-not [long]::TryParse($blobSizeText, [ref] $blobSize)) {
        throw "source blob size is not an integer: $blobSizeText"
    }
    Assert-Equal -Actual $blobSize -Expected ([long] $ExpectedSourceBytes) -Name 'source blob bytes'

    if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
        $OutputRoot = Join-Path $resolvedRoot 'out\native-windows-v3'
    }

    $artifactDirectory = Join-Path $OutputRoot $timestampUtc
    New-Item -ItemType Directory -Path $artifactDirectory -Force | Out-Null

    $sourceArtifactPath = Join-Path $artifactDirectory 'probe-v1.hex'
    $executablePath = Join-Path $artifactDirectory 'probe-v1.exe'
    $evidencePath = Join-Path $artifactDirectory 'native-windows-v3-evidence.json'
    $evidenceHashPath = Join-Path $artifactDirectory 'native-windows-v3-evidence.json.sha256'

    Export-GitBlobBytes `
        -BlobSha1 $ExpectedSourceBlobSha1 `
        -Destination $sourceArtifactPath `
        -WorkingDirectory $resolvedRoot

    $sourceBytes = [System.IO.File]::ReadAllBytes($sourceArtifactPath)
    Assert-Equal -Actual $sourceBytes.Length -Expected $ExpectedSourceBytes -Name 'source artifact bytes'

    $sourceSha256 = Get-Sha256Lower -LiteralPath $sourceArtifactPath
    Assert-Equal -Actual $sourceSha256 -Expected $ExpectedSourceSha256 -Name 'source artifact SHA-256'

    $sourceText = [System.Text.Encoding]::ASCII.GetString($sourceBytes)
    $sourcePattern = '\A[0-9a-f]{3072}\n\z'
    if (-not [System.Text.RegularExpressions.Regex]::IsMatch(
        $sourceText,
        $sourcePattern,
        [System.Text.RegularExpressions.RegexOptions]::CultureInvariant
    )) {
        throw 'source grammar mismatch; expected 3072 lowercase hex characters followed by one LF'
    }

    $hexText = $sourceText.Substring(0, 3072)
    $executableBytes = [byte[]]::new($hexText.Length / 2)
    for ($i = 0; $i -lt $executableBytes.Length; $i++) {
        $executableBytes[$i] = [Convert]::ToByte($hexText.Substring($i * 2, 2), 16)
    }

    [System.IO.File]::WriteAllBytes($executablePath, $executableBytes)
    Assert-Equal -Actual $executableBytes.Length -Expected $ExpectedExecutableBytes -Name 'executable bytes'

    $executableSha256 = Get-Sha256Lower -LiteralPath $executablePath
    Assert-Equal -Actual $executableSha256 -Expected $ExpectedExecutableSha256 -Name 'executable SHA-256'

    Assert-Equal -Actual $executableBytes[0] -Expected ([byte] 0x4d) -Name 'DOS signature byte 0'
    Assert-Equal -Actual $executableBytes[1] -Expected ([byte] 0x5a) -Name 'DOS signature byte 1'

    $peOffset = [int](Get-UInt32LE -Bytes $executableBytes -Offset 0x3c)
    if (($peOffset + 24) -gt $executableBytes.Length) {
        throw "PE header outside file pe_offset=$peOffset bytes=$($executableBytes.Length)"
    }

    Assert-Equal -Actual $executableBytes[$peOffset] -Expected ([byte] 0x50) -Name 'PE signature byte 0'
    Assert-Equal -Actual $executableBytes[$peOffset + 1] -Expected ([byte] 0x45) -Name 'PE signature byte 1'
    Assert-Equal -Actual $executableBytes[$peOffset + 2] -Expected ([byte] 0x00) -Name 'PE signature byte 2'
    Assert-Equal -Actual $executableBytes[$peOffset + 3] -Expected ([byte] 0x00) -Name 'PE signature byte 3'

    $machine = Get-UInt16LE -Bytes $executableBytes -Offset ($peOffset + 4)
    $optionalHeaderOffset = $peOffset + 24
    $optionalMagic = Get-UInt16LE -Bytes $executableBytes -Offset $optionalHeaderOffset
    $subsystem = Get-UInt16LE -Bytes $executableBytes -Offset ($optionalHeaderOffset + 68)

    Assert-Equal -Actual $machine -Expected ([uint16] 0x8664) -Name 'PE machine AMD64'
    Assert-Equal -Actual $optionalMagic -Expected ([uint16] 0x020b) -Name 'PE32+ optional-header magic'
    Assert-Equal -Actual $subsystem -Expected ([uint16] 3) -Name 'Windows console subsystem'

    foreach ($requiredImportName in @(
        'KERNEL32.dll',
        'SetLastError',
        'GetLastError',
        'ExitProcess'
    )) {
        if (-not (Test-AsciiNeedle -Haystack $executableBytes -Needle $requiredImportName)) {
            throw "required import name absent: $requiredImportName"
        }
    }

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $nativeProcess = Start-Process `
        -FilePath $executablePath `
        -WorkingDirectory $artifactDirectory `
        -NoNewWindow `
        -Wait `
        -PassThru
    $stopwatch.Stop()

    $nativeExitCode = [int] $nativeProcess.ExitCode
    $elapsedMilliseconds = [Math]::Round($stopwatch.Elapsed.TotalMilliseconds, 3)
    Assert-Equal -Actual $nativeExitCode -Expected 0 -Name 'native Windows exit code'

    $verdict = 'PASS'
}
catch {
    $failure = $_.Exception.Message
}
finally {
    if ($null -ne $artifactDirectory) {
        $currentHead = $null
        $workingTreeTrackedChanges = $null
        try {
            $currentHead = Invoke-GitText `
                -Arguments 'rev-parse HEAD' `
                -WorkingDirectory $resolvedRoot
            $workingTreeTrackedChanges = Invoke-GitText `
                -Arguments 'status --porcelain --untracked-files=no' `
                -WorkingDirectory $resolvedRoot
        }
        catch {
            if ($null -eq $failure) {
                $failure = "post-capture git metadata failed: $($_.Exception.Message)"
                $verdict = 'FAIL'
            }
        }

        $windowsVersion = [Environment]::OSVersion.Version.ToString()
        $windowsProductName = $null
        $windowsDisplayVersion = $null
        $windowsBuild = $null
        try {
            $windowsInfo = Get-ItemProperty `
                -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
            $windowsProductName = $windowsInfo.ProductName
            $windowsDisplayVersion = $windowsInfo.DisplayVersion
            $windowsBuild = "$($windowsInfo.CurrentBuildNumber).$($windowsInfo.UBR)"
        }
        catch {
            $windowsProductName = 'UNAVAILABLE'
            $windowsDisplayVersion = 'UNAVAILABLE'
            $windowsBuild = 'UNAVAILABLE'
        }

        $evidence = [ordered]@{
            schema = 'dblyatem.native-windows-v3'
            schema_version = 3
            verdict = $verdict
            failure = $failure
            captured_at_utc = [DateTime]::UtcNow.ToString('o')
            harness = [ordered]@{
                path = $scriptPath
                sha256 = $scriptSha256
                repository_head = $currentHead
                tracked_worktree_changes = $workingTreeTrackedChanges
            }
            workload = [ordered]@{
                commit = $ExpectedWorkloadCommit
                source_path = $ExpectedSourcePath
                source_blob_sha1 = $ExpectedSourceBlobSha1
                source_bytes = $ExpectedSourceBytes
                source_sha256 = $ExpectedSourceSha256
                executable_bytes = $ExpectedExecutableBytes
                executable_sha256 = $ExpectedExecutableSha256
                contract = 'SetLastError(42) -> GetLastError() -> ExitProcess(0 on 42, otherwise 1)'
            }
            pe = [ordered]@{
                format = 'PE32+'
                machine = 'AMD64'
                subsystem = 'WINDOWS_CUI'
                required_import_names = @(
                    'KERNEL32.dll',
                    'SetLastError',
                    'GetLastError',
                    'ExitProcess'
                )
            }
            execution = [ordered]@{
                mode = 'native-windows'
                exit_code = $nativeExitCode
                observed_elapsed_ms = $elapsedMilliseconds
                elapsed_is_benchmark_score = $false
            }
            environment = [ordered]@{
                os_environment = $env:OS
                os_version = $windowsVersion
                product_name = $windowsProductName
                display_version = $windowsDisplayVersion
                build = $windowsBuild
                is_64_bit_os = [Environment]::Is64BitOperatingSystem
                is_64_bit_process = [Environment]::Is64BitProcess
                processor_architecture = $env:PROCESSOR_ARCHITECTURE
                processor_identifier = $env:PROCESSOR_IDENTIFIER
                powershell_edition = $PSVersionTable.PSEdition
                powershell_version = $PSVersionTable.PSVersion.ToString()
            }
            artifacts = [ordered]@{
                directory = $artifactDirectory
                source = $sourceArtifactPath
                executable = $executablePath
                evidence = $evidencePath
            }
            claims = [ordered]@{
                native_correctness = ($verdict -eq 'PASS')
                dblyatem_correctness = $false
                winlator_correctness = $false
                performance_parity = $false
                competitive_victory = $false
            }
        }

        $json = $evidence | ConvertTo-Json -Depth 8
        Write-Utf8NoBom -LiteralPath $evidencePath -Text ($json + "`n")
        $evidenceSha256 = Get-Sha256Lower -LiteralPath $evidencePath
        Write-Utf8NoBom `
            -LiteralPath $evidenceHashPath `
            -Text ("$evidenceSha256 *native-windows-v3-evidence.json`n")

        Write-Host "DBLYATEM_NATIVE_WINDOWS_V3=$verdict"
        Write-Host "WORKLOAD_COMMIT=$ExpectedWorkloadCommit"
        Write-Host "SOURCE_BLOB_SHA1=$ExpectedSourceBlobSha1"
        Write-Host "SOURCE_SHA256=$ExpectedSourceSha256"
        Write-Host "EXECUTABLE_SHA256=$ExpectedExecutableSha256"
        Write-Host "NATIVE_EXIT_CODE=$nativeExitCode"
        Write-Host "EVIDENCE=$evidencePath"
        Write-Host "EVIDENCE_SHA256=$evidenceSha256"

        if ($verdict -ne 'PASS') {
            Write-Error "DBLYATEM native Windows V3 rejected: $failure"
            exit 1
        }
    }
    elseif ($null -ne $failure) {
        Write-Error "DBLYATEM native Windows V3 rejected before evidence directory creation: $failure"
        exit 1
    }
}
