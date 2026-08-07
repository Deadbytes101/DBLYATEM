# DBLYATEM NATIVE WINDOWS V4 EVIDENCE CAPTURE
#
# V4 preserves the reviewed V3 capture logic except for the immutable
# workload commit/blob binding and one exact UInt16 little-endian reader fix.
# V3 correctly rejected stale source objects before native execution.

[CmdletBinding()]
param(
    [Parameter()]
    [string] $OutputRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ExpectedHarnessV3BlobSha1 = '92a885c9a3a2bb00893c65dc1e579badbe38ff7d'
$OldWorkloadCommit = 'c57130981bca654f33d6a832aecd0d8348b6c02d'
$NewWorkloadCommit = '6da963345f55daf7c384568644cacd7ce5be6371'
$OldSourceBlobSha1 = '87155048d6ef4d56cb9323ef20fc6b8b1850af0e'
$NewSourceBlobSha1 = 'd5f7d3e6fca91683d8f7d783647fabe3a693f293'

$OldUInt16Reader = 'return [uint16]($Bytes[$Offset] -bor ($Bytes[$Offset + 1] -shl 8))'
$NewUInt16Reader = 'return [uint16]([uint32]$Bytes[$Offset] -bor ([uint32]$Bytes[$Offset + 1] -shl 8))'

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$HarnessV3 = Join-Path $ScriptRoot 'CAPTURE-NATIVE-WINDOWS-V3.ps1'

if (-not (Test-Path -LiteralPath $HarnessV3 -PathType Leaf)) {
    throw "V3 HARNESS MISSING: $HarnessV3"
}

$ObservedHarnessV3BlobSha1 = (
    git -C $ScriptRoot hash-object -- $HarnessV3
).Trim().ToLowerInvariant()

if ($LASTEXITCODE -ne 0) {
    throw 'V3 HARNESS GIT HASH-OBJECT FAILED'
}

if ($ObservedHarnessV3BlobSha1 -cne $ExpectedHarnessV3BlobSha1) {
    throw (
        'V3 HARNESS BLOB SHA1 MISMATCH ' +
        "expected=$ExpectedHarnessV3BlobSha1 " +
        "actual=$ObservedHarnessV3BlobSha1"
    )
}

$V3Text = [System.IO.File]::ReadAllText(
    $HarnessV3,
    [System.Text.Encoding]::UTF8
)

$WorkloadCommitBindingCount = (
    [System.Text.RegularExpressions.Regex]::Matches(
        $V3Text,
        [System.Text.RegularExpressions.Regex]::Escape(
            $OldWorkloadCommit
        ),
        [System.Text.RegularExpressions.RegexOptions]::CultureInvariant
    )
).Count

if ($WorkloadCommitBindingCount -ne 1) {
    throw (
        'V3 WORKLOAD COMMIT BINDING COUNT MISMATCH ' +
        "expected=1 actual=$WorkloadCommitBindingCount"
    )
}

$SourceBlobBindingCount = (
    [System.Text.RegularExpressions.Regex]::Matches(
        $V3Text,
        [System.Text.RegularExpressions.Regex]::Escape(
            $OldSourceBlobSha1
        ),
        [System.Text.RegularExpressions.RegexOptions]::CultureInvariant
    )
).Count

if ($SourceBlobBindingCount -ne 1) {
    throw (
        'V3 SOURCE BLOB BINDING COUNT MISMATCH ' +
        "expected=1 actual=$SourceBlobBindingCount"
    )
}

$UInt16ReaderCount = (
    [System.Text.RegularExpressions.Regex]::Matches(
        $V3Text,
        [System.Text.RegularExpressions.Regex]::Escape(
            $OldUInt16Reader
        ),
        [System.Text.RegularExpressions.RegexOptions]::CultureInvariant
    )
).Count

if ($UInt16ReaderCount -ne 1) {
    throw (
        'V3 UINT16 READER BINDING COUNT MISMATCH ' +
        "expected=1 actual=$UInt16ReaderCount"
    )
}

$V4Text = $V3Text.Replace(
    $OldWorkloadCommit,
    $NewWorkloadCommit
).Replace(
    $OldSourceBlobSha1,
    $NewSourceBlobSha1
).Replace(
    $OldUInt16Reader,
    $NewUInt16Reader
)

if ($V4Text.Contains($OldWorkloadCommit)) {
    throw 'STALE WORKLOAD COMMIT REMAINED AFTER V4 PATCH'
}

if ($V4Text.Contains($OldSourceBlobSha1)) {
    throw 'STALE SOURCE BLOB REMAINED AFTER V4 PATCH'
}

if ($V4Text.Contains($OldUInt16Reader)) {
    throw 'STALE UINT16 READER REMAINED AFTER V4 PATCH'
}

if (-not $V4Text.Contains($NewUInt16Reader)) {
    throw 'FIXED UINT16 READER ABSENT AFTER V4 PATCH'
}

$TemporaryRoot = Join-Path (
    [System.IO.Path]::GetTempPath()
) ('dblyatem-native-v4-' + [guid]::NewGuid().ToString('N'))

$TemporaryHarness = Join-Path $TemporaryRoot 'CAPTURE-NATIVE-WINDOWS-V4-INNER.ps1'

try {
    [System.IO.Directory]::CreateDirectory($TemporaryRoot) | Out-Null

    [System.IO.File]::WriteAllText(
        $TemporaryHarness,
        $V4Text,
        [System.Text.UTF8Encoding]::new($false)
    )

    $Tokens = $null
    $ParseErrors = $null
    $null = [System.Management.Automation.Language.Parser]::ParseFile(
        $TemporaryHarness,
        [ref]$Tokens,
        [ref]$ParseErrors
    )

    if (@($ParseErrors).Count -ne 0) {
        $Details = @(
            $ParseErrors |
            ForEach-Object { $_.Message }
        ) -join "`n"
        throw "V4 INNER POWERSHELL PARSER FAILURE`n$Details"
    }

    Write-Host 'DBLYATEM NATIVE WINDOWS V4 BINDING: PASS'
    Write-Host ('V3_HARNESS_BLOB_SHA1=' + $ObservedHarnessV3BlobSha1)
    Write-Host ('WORKLOAD_COMMIT=' + $NewWorkloadCommit)
    Write-Host ('SOURCE_BLOB_SHA1=' + $NewSourceBlobSha1)
    Write-Host 'UINT16_LE_READER_FIX=APPLIED'

    if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
        & powershell.exe `
            -NoLogo `
            -NoProfile `
            -ExecutionPolicy Bypass `
            -File $TemporaryHarness
    }
    else {
        & powershell.exe `
            -NoLogo `
            -NoProfile `
            -ExecutionPolicy Bypass `
            -File $TemporaryHarness `
            -OutputRoot $OutputRoot
    }

    $InnerExitCode = $LASTEXITCODE

    if ($InnerExitCode -ne 0) {
        throw "DBLYATEM NATIVE WINDOWS V4 INNER EXIT=$InnerExitCode"
    }
}
finally {
    if (Test-Path -LiteralPath $TemporaryRoot) {
        Remove-Item -LiteralPath $TemporaryRoot -Recurse -Force
    }
}
