$ErrorActionPreference='Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts/Install-Helpers.ps1')
function Assert($value,[string]$message){if(-not $value){throw $message}}
Assert (@(Get-SetupProblems 22621 'AMD64' $true $true).Count -eq 0) 'Supported machine rejected.'
Assert (@(Get-SetupProblems 22000 'AMD64' $true $true).Count -eq 1) 'Old Windows must be blocked.'
Assert (@(Get-SetupProblems 22621 'ARM64' $true $true).Count -eq 1) 'Wrong architecture must be blocked.'
Assert (@(Get-SetupProblems 22621 'AMD64' $false $true).Count -eq 1) 'Developer Mode must be required.'
Assert (@(Get-SetupProblems 22621 'AMD64' $true $false).Count -eq 1) 'Widgets must be required.'
Assert (Test-RuntimeRequired @()) 'Missing runtime not detected.'
Assert (Test-RuntimeRequired @([pscustomobject]@{Name='Microsoft.WindowsAppRuntime.2';Architecture='X64';Version='2.4.0.0'})) 'Old runtime accepted.'
Assert (Test-RuntimeRequired @([pscustomobject]@{Name='Microsoft.WindowsAppRuntime.2';Architecture='X86';Version='2.5.1.0'})) 'Wrong runtime architecture accepted.'
Assert (-not(Test-RuntimeRequired @([pscustomobject]@{Name='Microsoft.WindowsAppRuntime.2';Architecture='X64';Version='2.5.1.0'}))) 'Matching runtime rejected.'
Assert (-not(Test-RuntimeRequired @([pscustomobject]@{Name='Microsoft.WindowsAppRuntime.2';Architecture='X64';Version='2.6.0.0'}))) 'Newer runtime rejected.'
$taskRoot=Join-Path ([IO.Path]::GetTempPath()) ('EarbudSetupTests-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskRoot | Out-Null
try{
    Set-Content -LiteralPath (Join-Path $taskRoot 'payload.txt') -Value 'expected'
    $taskHash=(Get-FileHash -LiteralPath (Join-Path $taskRoot 'payload.txt')).Hash
    Set-Content -LiteralPath (Join-Path $taskRoot 'SHA256SUMS.txt') -Value "$taskHash  payload.txt"
    Assert-BundleIntegrity $taskRoot
    Set-Content -LiteralPath (Join-Path $taskRoot 'payload.txt') -Value 'tampered'
    $taskFailed=$false;try{Assert-BundleIntegrity $taskRoot}catch{$taskFailed=$true};Assert $taskFailed 'Tampering must be rejected.'
    Set-Content -LiteralPath (Join-Path $taskRoot 'SHA256SUMS.txt') -Value "$taskHash  ../outside.txt"
    $taskFailed=$false;try{Assert-BundleIntegrity $taskRoot}catch{$taskFailed=$true};Assert $taskFailed 'Traversal must be rejected.'
}finally{
    $taskResolved=[IO.Path]::GetFullPath($taskRoot)
    if($taskResolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase) -and (Split-Path $taskResolved -Leaf) -match '^EarbudSetupTests-[a-f0-9]{32}$'){Remove-Item -LiteralPath $taskResolved -Recurse -Force}
}
Write-Output 'Passed: setup prerequisites, runtime version/architecture, checksums and traversal rejection. No app/certificate changes.'
