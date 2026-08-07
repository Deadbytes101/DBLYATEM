# DBLYATEM NATIVE WINDOWS V5 ACCEPTANCE CAPTURE
#
# V5 is the native-Windows acceptance gate for probe-v1.
# It does not benchmark performance and does not make DBLYATEM/Winlator claims.
#
# Sequence:
#   1. Bind the reviewed V4 harness and canonical workload Git objects.
#   2. Run V4 into a fresh bounded output root.
#   3. Require V4 evidence PASS and exact executable identity.
#   4. Execute the same PE three additional times with raw stdout/stderr capture.
#   5. Accept only exit=0 and zero stdout/stderr bytes for every captured run.

[CmdletBinding()]
param(
    [Parameter()]
    [string] $OutputRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ExpectedV4Commit = '23ff31cdcc5f894f05b32256cb0b2a517bfca174'
$ExpectedV4Path = 'benchmarks/windows-x64-workload/CAPTURE-NATIVE-WINDOWS-V4.ps1'
$ExpectedV4BlobSha1 = '7e04f59267ab6b24d49f04562abdd701a9573a2f'

$ExpectedWorkloadCommit = '6da963345f55daf7c384568644cacd7ce5be6371'
$ExpectedSourcePath = 'benchmarks/windows-x64-workload/probe-v1.hex'
$ExpectedSourceBlobSha1 = 'd5f7d3e6fca91683d8f7d783647fabe3a693f293'
$ExpectedSourceBytes = 3073
$ExpectedSourceSha256 = '12d42d73b87294fd3e4dc635d7f9c5404cb39f18bf07df4320028b65bdfaacac'
$ExpectedExecutableBytes = 1536
$ExpectedExecutableSha256 = 'b32d2a090ce2fde1132f1daba6501a1bc6bf8f2c3e5b950b728eb30f8b211210'
$RequiredCapturedRuns = 3

function Get-Sha256Lower {
    param(
        [Parameter(Mandatory)]
        [string] $LiteralPath
    )

    return (
        Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256
    ).Hash.ToLowerInvariant()
}

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory)]
        [string] $LiteralPath,

        [Parameter(Mandatory)]
        [string] $Text
    )

    [System.IO.File]::WriteAllText(
        $LiteralPath,
        $Text,
        [System.Text.UTF8Encoding]::new($false)
    )
}

function Invoke-GitText {
    param(
        [Parameter(Mandatory)]
        [string[]] $Arguments,

        [Parameter(Mandatory)]
        [string] $WorkingDirectory
    )

    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = 'git.exe'
    $info.WorkingDirectory = $WorkingDirectory
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.Arguments = (($Arguments | ForEach-Object {
        if ($_ -match '[\s"]') {
            '"' + ($_ -replace '"', '\"') + '"'
        }
        else {
            $_
        }
    }) -join ' ')

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $info

    try {
        if (-not $process.Start()) {
            throw "failed to start git.exe $($info.Arguments)"
        }

        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $stdout = $stdoutTask.Result
        $stderr = $stderrTask.Result

        if ($process.ExitCode -ne 0) {
            throw (
                "git.exe $($info.Arguments) failed exit=$($process.ExitCode) " +
                "stderr=$($stderr.Trim())"
            )
        }

        return $stdout.TrimEnd("`r", "`n")
    }
    finally {
        $process.Dispose()
    }
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
        throw "$Name mismatch expected=[$Expected] actual=[$Actual]"
    }
}

function Invoke-RawCapturedProcess {
    param(
        [Parameter(Mandatory)]
        [string] $FilePath,

        [Parameter(Mandatory)]
        [string] $WorkingDirectory,

        [Parameter(Mandatory)]
        [string] $StdoutPath,

        [Parameter(Mandatory)]
        [string] $StderrPath,

        [Parameter(Mandatory)]
        [int] $RunIndex
    )

    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $FilePath
    $info.WorkingDirectory = $WorkingDirectory
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $info
    $stdoutMemory = [System.IO.MemoryStream]::new()
    $stderrMemory = [System.IO.MemoryStream]::new()
    $stopwatch = [System.Diagnostics.Stopwatch]::new()

    try {
        $stopwatch.Start()
        if (-not $process.Start()) {
            throw "native captured run $RunIndex failed to start"
        }

        $stdoutTask = $process.StandardOutput.BaseStream.CopyToAsync($stdoutMemory)
        $stderrTask = $process.StandardError.BaseStream.CopyToAsync($stderrMemory)

        $process.WaitForExit()
        [System.Threading.Tasks.Task]::WaitAll(
            [System.Threading.Tasks.Task[]]@($stdoutTask, $stderrTask)
        )
        $stopwatch.Stop()

        $stdoutBytes = $stdoutMemory.ToArray()
        $stderrBytes = $stderrMemory.ToArray()

        [System.IO.File]::WriteAllBytes($StdoutPath, $stdoutBytes)
        [System.IO.File]::WriteAllBytes($StderrPath, $stderrBytes)

        $exitCode = [int]$process.ExitCode
        $elapsedMs = [Math]::Round($stopwatch.Elapsed.TotalMilliseconds, 3)

        Assert-Equal -Actual $exitCode -Expected 0 -Name "captured run $RunIndex exit code"
        Assert-Equal -Actual $stdoutBytes.Length -Expected 0 -Name "captured run $RunIndex stdout bytes"
        Assert-Equal -Actual $stderrBytes.Length -Expected 0 -Name "captured run $RunIndex stderr bytes"

        return [ordered]@{
            run = $RunIndex
            exit_code = $exitCode
            stdout_bytes = $stdoutBytes.Length
            stderr_bytes = $stderrBytes.Length
            elapsed_ms = $elapsedMs
            elapsed_is_benchmark_score = $false
            stdout_artifact = $StdoutPath
            stderr_artifact = $StderrPath
        }
    }
    finally {
        if ($stopwatch.IsRunning) {
            $stopwatch.Stop()
        }
        $stdoutMemory.Dispose()
        $stderrMemory.Dispose()
        $process.Dispose()
    }
}

$timestampUtc = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$scriptPath = $MyInvocation.MyCommand.Path
$scriptSha256 = Get-Sha256Lower -LiteralPath $scriptPath
$repositoryRoot = $null
$sessionDirectory = $null
$proofPath = $null
$proofHashPath = $null
$failure = $null
$verdict = 'FAIL'
$runs = @()

try {
    if ($env:OS -ne 'Windows_NT') {
        throw "native acceptance requires Windows_NT; observed=$($env:OS)"
    }

    if (-not [Environment]::Is64BitOperatingSystem) {
        throw 'native acceptance requires a 64-bit Windows operating system'
    }

    $repositoryRoot = Invoke-GitText `
        -Arguments @('rev-parse', '--show-toplevel') `
        -WorkingDirectory (Get-Location).Path

    if ([string]::IsNullOrWhiteSpace($repositoryRoot)) {
        throw 'repository root resolution returned empty text'
    }

    $repositoryRoot = (Resolve-Path -LiteralPath $repositoryRoot).Path

    $v4TreeEntry = Invoke-GitText `
        -Arguments @('ls-tree', $ExpectedV4Commit, '--', $ExpectedV4Path) `
        -WorkingDirectory $repositoryRoot
    $expectedV4TreeEntry = "100644 blob $ExpectedV4BlobSha1`t$ExpectedV4Path"
    Assert-Equal -Actual $v4TreeEntry -Expected $expectedV4TreeEntry -Name 'immutable V4 tree entry'

    $sourceTreeEntry = Invoke-GitText `
        -Arguments @('ls-tree', $ExpectedWorkloadCommit, '--', $ExpectedSourcePath) `
        -WorkingDirectory $repositoryRoot
    $expectedSourceTreeEntry = "100644 blob $ExpectedSourceBlobSha1`t$ExpectedSourcePath"
    Assert-Equal -Actual $sourceTreeEntry -Expected $expectedSourceTreeEntry -Name 'immutable source tree entry'

    $sourceBlobSize = Invoke-GitText `
        -Arguments @('cat-file', '-s', $ExpectedSourceBlobSha1) `
        -WorkingDirectory $repositoryRoot
    Assert-Equal -Actual $sourceBlobSize -Expected ([string]$ExpectedSourceBytes) -Name 'canonical source blob bytes'

    $v4Path = Join-Path $repositoryRoot ($ExpectedV4Path.Replace('/', '\'))
    if (-not (Test-Path -LiteralPath $v4Path -PathType Leaf)) {
        throw "V4 harness missing from worktree: $v4Path"
    }

    $observedV4Blob = Invoke-GitText `
        -Arguments @('hash-object', '--', $v4Path) `
        -WorkingDirectory $repositoryRoot
    Assert-Equal -Actual $observedV4Blob -Expected $ExpectedV4BlobSha1 -Name 'worktree V4 blob SHA1'

    if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
        $OutputRoot = Join-Path $repositoryRoot 'out\native-windows-v5'
    }

    $sessionDirectory = Join-Path $OutputRoot $timestampUtc
    if (Test-Path -LiteralPath $sessionDirectory) {
        throw "create-new V5 session directory already exists: $sessionDirectory"
    }
    [System.IO.Directory]::CreateDirectory($sessionDirectory) | Out-Null

    $innerOutputRoot = Join-Path $sessionDirectory 'v4-inner'
    [System.IO.Directory]::CreateDirectory($innerOutputRoot) | Out-Null

    & powershell.exe `
        -NoLogo `
        -NoProfile `
        -ExecutionPolicy Bypass `
        -File $v4Path `
        -OutputRoot $innerOutputRoot

    $v4ExitCode = $LASTEXITCODE
    Assert-Equal -Actual $v4ExitCode -Expected 0 -Name 'V4 acceptance prerequisite exit code'

    $innerDirectories = @(
        Get-ChildItem -LiteralPath $innerOutputRoot -Directory
    )
    Assert-Equal -Actual $innerDirectories.Count -Expected 1 -Name 'V4 inner artifact directory count'

    $innerDirectory = $innerDirectories[0].FullName
    $innerEvidencePath = Join-Path $innerDirectory 'native-windows-v3-evidence.json'
    $executablePath = Join-Path $innerDirectory 'probe-v1.exe'

    foreach ($requiredPath in @($innerEvidencePath, $executablePath)) {
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "required V4 artifact missing: $requiredPath"
        }
    }

    $innerEvidence = Get-Content -LiteralPath $innerEvidencePath -Raw | ConvertFrom-Json
    Assert-Equal -Actual $innerEvidence.verdict -Expected 'PASS' -Name 'V4 evidence verdict'
    Assert-Equal -Actual $innerEvidence.workload.commit -Expected $ExpectedWorkloadCommit -Name 'V4 evidence workload commit'
    Assert-Equal -Actual $innerEvidence.workload.source_blob_sha1 -Expected $ExpectedSourceBlobSha1 -Name 'V4 evidence source blob SHA1'
    Assert-Equal -Actual $innerEvidence.workload.source_sha256 -Expected $ExpectedSourceSha256 -Name 'V4 evidence source SHA256'
    Assert-Equal -Actual $innerEvidence.workload.executable_sha256 -Expected $ExpectedExecutableSha256 -Name 'V4 evidence executable SHA256'
    Assert-Equal -Actual ([int]$innerEvidence.execution.exit_code) -Expected 0 -Name 'V4 evidence native exit code'

    $executableInfo = Get-Item -LiteralPath $executablePath
    Assert-Equal -Actual ([long]$executableInfo.Length) -Expected ([long]$ExpectedExecutableBytes) -Name 'V5 executable bytes'
    $observedExecutableSha256 = Get-Sha256Lower -LiteralPath $executablePath
    Assert-Equal -Actual $observedExecutableSha256 -Expected $ExpectedExecutableSha256 -Name 'V5 executable SHA256'

    for ($runIndex = 1; $runIndex -le $RequiredCapturedRuns; $runIndex++) {
        $stdoutPath = Join-Path $sessionDirectory ("run-{0}.stdout.bin" -f $runIndex)
        $stderrPath = Join-Path $sessionDirectory ("run-{0}.stderr.bin" -f $runIndex)
        $runResult = Invoke-RawCapturedProcess `
            -FilePath $executablePath `
            -WorkingDirectory $innerDirectory `
            -StdoutPath $stdoutPath `
            -StderrPath $stderrPath `
            -RunIndex $runIndex
        $runs += $runResult
    }

    Assert-Equal -Actual $runs.Count -Expected $RequiredCapturedRuns -Name 'captured run count'
    $verdict = 'PASS'
}
catch {
    $failure = $_.Exception.Message
}
finally {
    if ($null -ne $sessionDirectory) {
        $currentHead = $null
        try {
            $currentHead = Invoke-GitText `
                -Arguments @('rev-parse', 'HEAD') `
                -WorkingDirectory $repositoryRoot
        }
        catch {
            if ($null -eq $failure) {
                $failure = "post-capture HEAD resolution failed: $($_.Exception.Message)"
                $verdict = 'FAIL'
            }
        }

        $proofPath = Join-Path $sessionDirectory 'native-windows-v5-proof.json'
        $proofHashPath = Join-Path $sessionDirectory 'native-windows-v5-proof.json.sha256'

        $proof = [ordered]@{
            schema = 'dblyatem.native-windows-v5'
            schema_version = 5
            verdict = $verdict
            failure = $failure
            captured_at_utc = [DateTime]::UtcNow.ToString('o')
            harness = [ordered]@{
                path = $scriptPath
                sha256 = $scriptSha256
                repository_head = $currentHead
                v4_commit = $ExpectedV4Commit
                v4_blob_sha1 = $ExpectedV4BlobSha1
            }
            workload = [ordered]@{
                commit = $ExpectedWorkloadCommit
                source_path = $ExpectedSourcePath
                source_blob_sha1 = $ExpectedSourceBlobSha1
                source_bytes = $ExpectedSourceBytes
                source_sha256 = $ExpectedSourceSha256
                executable_bytes = $ExpectedExecutableBytes
                executable_sha256 = $ExpectedExecutableSha256
            }
            acceptance = [ordered]@{
                required_captured_runs = $RequiredCapturedRuns
                captured_runs = @($runs)
                every_exit_code_zero = (@($runs | Where-Object { $_.exit_code -ne 0 }).Count -eq 0)
                every_stdout_empty = (@($runs | Where-Object { $_.stdout_bytes -ne 0 }).Count -eq 0)
                every_stderr_empty = (@($runs | Where-Object { $_.stderr_bytes -ne 0 }).Count -eq 0)
                elapsed_is_benchmark_score = $false
            }
            claims = [ordered]@{
                native_windows_correctness = ($verdict -eq 'PASS')
                dblyatem_correctness = $false
                winlator_correctness = $false
                performance_parity = $false
                competitive_victory = $false
            }
        }

        $json = $proof | ConvertTo-Json -Depth 10
        Write-Utf8NoBom -LiteralPath $proofPath -Text ($json + "`n")
        $proofSha256 = Get-Sha256Lower -LiteralPath $proofPath
        Write-Utf8NoBom `
            -LiteralPath $proofHashPath `
            -Text ("$proofSha256 *native-windows-v5-proof.json`n")

        Write-Host ('DBLYATEM_NATIVE_WINDOWS_V5=' + $verdict)
        Write-Host ('WORKLOAD_COMMIT=' + $ExpectedWorkloadCommit)
        Write-Host ('SOURCE_BLOB_SHA1=' + $ExpectedSourceBlobSha1)
        Write-Host ('SOURCE_SHA256=' + $ExpectedSourceSha256)
        Write-Host ('EXECUTABLE_SHA256=' + $ExpectedExecutableSha256)
        Write-Host ('CAPTURED_RUNS=' + [string]$runs.Count)
        foreach ($run in $runs) {
            Write-Host (
                'RUN_{0}=exit:{1},stdout_bytes:{2},stderr_bytes:{3},elapsed_ms:{4}' -f
                $run.run,
                $run.exit_code,
                $run.stdout_bytes,
                $run.stderr_bytes,
                $run.elapsed_ms
            )
        }
        Write-Host ('PROOF=' + $proofPath)
        Write-Host ('PROOF_SHA256=' + $proofSha256)

        if ($verdict -ne 'PASS') {
            Write-Error "DBLYATEM native Windows V5 rejected: $failure"
            exit 1
        }
    }
    elseif ($null -ne $failure) {
        Write-Error "DBLYATEM native Windows V5 rejected before proof directory creation: $failure"
        exit 1
    }
}
