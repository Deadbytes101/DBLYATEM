# DBLYATEM NATIVE WINDOWS V4 EVIDENCE CAPTURE
#
# V4 preserves the reviewed V3 capture logic byte-for-byte except for the
# immutable workload commit/blob binding. V3 correctly rejected the stale
# c5713098 blob because it contained 174733 bytes instead of 3073 bytes.

[CmdletBinding()]
param(
    [Parameter()]
    [string] $OutputRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ExpectedHarnessV3Sha256 = 'a5e9ca45525bafdd25b250275b974f27c98a4cd997485876c568ecbf077e11b7'
$OldWorkloadCommit = 'c57130981bca654f33d6a832aecd0d8348b6c02d'
$NewWorkloadCommit = 'd7adf9bd65aa3c28732c482ee505510a86109e6c'
$OldSourceBlobSha1 = '87155048d6ef4d56cb9323ef20fc6b8b1850af0e'
$NewSourceBlobSha1 = '6c9c0f270b7de8569cacd253d3a8f4b601c5fdc4'

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$HarnessV3 = Join-Path $ScriptRoot 'CAPTURE-NATIVE-WINDOWS-V3.ps1'

if (-not (Test-Path -LiteralPath $HarnessV3 -PathType Leaf)) {
    throw "V3 HARNESS MISSING: $HarnessV3"
}

$ObservedHarnessV3Sha256 = (
    Get-FileHash -LiteralPath $HarnessV3 -Algorithm SHA256
).Hash.ToLowerInvariant()

if ($ObservedHarnessV3Sha256 -cne $ExpectedHarnessV3Sha256) {
    throw (
        'V3 HARNESS SHA256 MISMATCH ' +
        "expected=$ExpectedHarnessV3Sha256 " +
        "actual=$ObservedHarnessV3Sha256"
    )
}

$V3Text = [System.IO.File]::ReadAllText(
    $HarnessV3,
    [System.Text.Encoding]::UTF8
)

if (($V3Text.Split($OldWorkloadCommit).Count - 1) -ne 1) {
    throw 'V3 WORKLOAD COMMIT BINDING COUNT MISMATCH'
}

if (($V3Text.Split($OldSourceBlobSha1).Count - 1) -ne 1) {
    throw 'V3 SOURCE BLOB BINDING COUNT MISMATCH'
}

$V4Text = $V3Text.Replace(
    $OldWorkloadCommit,
    $NewWorkloadCommit
).Replace(
    $OldSourceBlobSha1,
    $NewSourceBlobSha1
)

if ($V4Text.Contains($OldWorkloadCommit)) {
    throw 'STALE WORKLOAD COMMIT REMAINED AFTER V4 PATCH'
}

if ($V4Text.Contains($OldSourceBlobSha1)) {
    throw 'STALE SOURCE BLOB REMAINED AFTER V4 PATCH'
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

    Write-Host 'DBLYATEM NATIVE WINDOWS V4 BINDING: PASS'
    Write-Host ('V3_HARNESS_SHA256=' + $ObservedHarnessV3Sha256)
    Write-Host ('WORKLOAD_COMMIT=' + $NewWorkloadCommit)
    Write-Host ('SOURCE_BLOB_SHA1=' + $NewSourceBlobSha1)

    $Arguments = @(
        '-NoLogo',
        '-NoProfile',
        '-ExecutionPolicy',
        'Bypass',
        '-File',
        ('"' + $TemporaryHarness + '"')
    )

    if (-not [string]::IsNullOrWhiteSpace($OutputRoot)) {
        $Arguments += @(
            '-OutputRoot',
            ('"' + $OutputRoot + '"')
        )
    }

    & powershell.exe $Arguments
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
