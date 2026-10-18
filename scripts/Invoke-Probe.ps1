param(
    [ValidateSet('Classic','BLE','V1')][string]$Transport = 'Classic',
    [switch]$VerifyNoise,
    [switch]$VerifyEq,
    [switch]$EventDriven
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskExe = Join-Path $taskRoot 'build\bin\Release\earbud-probe.exe'
if (-not (Test-Path -LiteralPath $taskExe)) { throw 'Build the probe with scripts/build.ps1 first.' }
$taskArguments = [System.Collections.Generic.List[string]]::new()
if ($Transport -eq 'BLE') { $taskArguments.Add('--ble') }
if ($Transport -eq 'V1') { $taskArguments.Add('--v1') }
if ($VerifyNoise) { $taskArguments.Add('--verify-noise') }
if ($VerifyEq) { $taskArguments.Add('--verify-eq') }
if ($EventDriven) { $taskArguments.Add('--events') }
$taskOut = [System.IO.Path]::GetTempFileName()
$taskErr = [System.IO.Path]::GetTempFileName()
try {
    $taskStart = @{FilePath=$taskExe; PassThru=$true; WindowStyle='Hidden'; RedirectStandardOutput=$taskOut; RedirectStandardError=$taskErr}
    if ($taskArguments.Count) { $taskStart.ArgumentList=$taskArguments.ToArray() }
    $taskProcess = Start-Process @taskStart
    if (-not $taskProcess.WaitForExit(25000)) {
        Stop-Process -Id $taskProcess.Id -ErrorAction SilentlyContinue
        if ($VerifyNoise -or $VerifyEq) { Write-Warning 'Probe exceeded its deadline during a setting test. Check noise and EQ settings in Sony Sound Connect.' }
        throw 'Probe exceeded its 25-second process deadline. It was stopped; no retry was attempted.'
    }
    $taskProcess.WaitForExit()
    $taskJson = Get-Content -LiteralPath $taskOut -Raw
    $taskErrors = Get-Content -LiteralPath $taskErr -Raw
    if ($taskErrors) { Write-Warning $taskErrors.Trim() }
    $taskReport = $taskJson | ConvertFrom-Json
    $taskArtifacts = Join-Path $taskRoot 'artifacts'
    New-Item -Path $taskArtifacts -ItemType Directory -Force | Out-Null
    $taskReportPath = Join-Path $taskArtifacts ("probe-{0}-{1}.json" -f $Transport.ToLowerInvariant(), (Get-Date -Format 'yyyyMMdd-HHmmss'))
    Set-Content -LiteralPath $taskReportPath -Value $taskJson -Encoding utf8
    Write-Output $taskJson
    Write-Output "Report: $taskReportPath"
    if ($taskProcess.ExitCode -ne 0) { throw "Hardware probe failed: $($taskReport.error)" }
} finally {
    Remove-Item -LiteralPath $taskOut,$taskErr -Force -ErrorAction SilentlyContinue
}
