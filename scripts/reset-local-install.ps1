param([string]$BackupPath,[switch]$Execute)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$taskThumbprint='458DC5875423917D74F77A536EBBB42720D1B67A'
$taskDefinitions=@(
    @{Name='Kanu.EarbudControls';FullName='Kanu.EarbudControls_0.3.2.0_x64__e3tdbr9m4vmva';Family='Kanu.EarbudControls_e3tdbr9m4vmva'},
    @{Name='Kanu.EarbudControls.UIReview';FullName='Kanu.EarbudControls.UIReview_0.3.0.3_x64__e3tdbr9m4vmva';Family='Kanu.EarbudControls.UIReview_e3tdbr9m4vmva'}
)
function Assert-NormalPath([string]$Path){
    $full=[IO.Path]::GetFullPath($Path);$cursor=$full
    while($cursor){
        if((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Refusing reparse point: $cursor"}
        $parent=Split-Path $cursor -Parent;if($parent -eq $cursor){break};$cursor=$parent
    }
    return $full
}
function Assert-NormalTree([string]$Path){
    $null=Assert-NormalPath $Path
    $queue=[Collections.Generic.Stack[string]]::new();$queue.Push($Path)
    while($queue.Count){foreach($item in Get-ChildItem -LiteralPath $queue.Pop() -Force){
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Refusing reparse-point traversal: $($item.FullName)"}
        if($item.PSIsContainer){$queue.Push($item.FullName)}
    }}
}
$taskInstalled=@(Get-AppxPackage | Where-Object {$_.Name -in $taskDefinitions.Name})
foreach($taskPackage in $taskInstalled){
    $taskExpected=@($taskDefinitions | Where-Object {$_.Name -eq $taskPackage.Name})[0]
    if($taskPackage.PackageFullName -ne $taskExpected.FullName){throw "Unexpected installed package; update the removal list before proceeding: $($taskPackage.PackageFullName)"}
}
$taskFiles=@('0.1.0.0','0.1.1.0','0.1.2.0','0.3.0.0','0.3.1.0') | ForEach-Object {Join-Path $taskRoot "out/EarbudControls_$_.msix"}
$taskFolders=@((Join-Path $taskRoot 'out/ui-review'),(Join-Path $env:LOCALAPPDATA 'EarbudControlsDev'))
foreach($taskDefinition in $taskDefinitions){Write-Output "UNINSTALL current user: $($taskDefinition.FullName)";Write-Output "Windows-owned data: $(Join-Path $env:LOCALAPPDATA ('Packages/'+$taskDefinition.Family))"}
foreach($taskPath in @($taskFiles)+@($taskFolders)){Write-Output "DELETE if present: $taskPath"}
Write-Output "REMOVE public trust only: Cert:\LocalMachine\TrustedPeople\$taskThumbprint"
Write-Output "PRESERVE private signing key: Cert:\CurrentUser\My\$taskThumbprint"
if(-not $Execute){Write-Output 'Preview only. Nothing changed. Execution requires -Execute -BackupPath <existing backup folder>.';return}
if(-not $BackupPath){throw 'A verified rollback backup is required.'}
$taskBackup=Assert-NormalPath $BackupPath
$taskBackupRoot=Join-Path $taskRoot 'artifacts/cleanup-backup'
if(-not $taskBackup.StartsWith($taskBackupRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or (Split-Path $taskBackup -Leaf) -notmatch '^\d{8}-\d{6}$'){throw 'Backup must be a timestamp folder under artifacts/cleanup-backup.'}
foreach($taskRequired in 'EarbudControls_0.3.1.0.msix','EarbudControls.Local.cer','installations-before.json'){
    if(-not(Test-Path -LiteralPath (Join-Path $taskBackup $taskRequired))){throw "Missing rollback backup: $taskRequired"}
}
if(Test-Path -LiteralPath (Join-Path $taskRoot 'out/EarbudControls_0.3.1.0.msix')){
    if((Get-FileHash -LiteralPath (Join-Path $taskRoot 'out/EarbudControls_0.3.1.0.msix')).Hash -ne (Get-FileHash -LiteralPath (Join-Path $taskBackup 'EarbudControls_0.3.1.0.msix')).Hash){throw 'Rollback installer backup differs.'}
}
foreach($taskPath in $taskFiles){$null=Assert-NormalPath $taskPath}
foreach($taskPath in $taskFolders){if(Test-Path -LiteralPath $taskPath){Assert-NormalTree $taskPath}}
$taskState=Join-Path $env:LOCALAPPDATA 'Packages/Kanu.EarbudControls_e3tdbr9m4vmva/LocalState'
if(Test-Path -LiteralPath $taskState){Assert-NormalTree $taskState;Copy-Item -LiteralPath $taskState -Destination (Join-Path $taskBackup ('pre-reset-LocalState-'+(Get-Date -Format 'HHmmss'))) -Recurse}
$taskInstalled | Select-Object Name,Version,PackageFullName,InstallLocation | ConvertTo-Json | Set-Content (Join-Path $taskBackup 'installations-pre-reset.json')
$taskJournal=[Collections.Generic.List[string]]::new()
foreach($taskPackage in $taskInstalled){
    $taskExpectedExe=Join-Path $taskPackage.InstallLocation $(if($taskPackage.Name -eq 'Kanu.EarbudControls'){'EarbudControls.exe'}else{'WidgetUiReview.exe'})
    foreach($taskProcess in Get-Process -Name EarbudControls,WidgetUiReview -ErrorAction SilentlyContinue){
        if($taskProcess.Path -and $taskProcess.Path -eq $taskExpectedExe){Stop-Process -InputObject $taskProcess -Force;$null=$taskProcess.WaitForExit(5000)}
    }
    Remove-AppxPackage -Package $taskPackage.PackageFullName
    $taskJournal.Add('Uninstalled '+$taskPackage.PackageFullName)
}
if(@(Get-AppxPackage | Where-Object {$_.Name -in $taskDefinitions.Name}).Count){throw 'App registration remains; no directory cleanup will proceed.'}
foreach($taskPath in $taskFiles){if(Test-Path -LiteralPath $taskPath){Remove-Item -LiteralPath $taskPath -Force;$taskJournal.Add('Deleted '+$taskPath)}}
foreach($taskPath in $taskFolders){if(Test-Path -LiteralPath $taskPath){Assert-NormalTree $taskPath;Remove-Item -LiteralPath $taskPath -Recurse -Force;$taskJournal.Add('Deleted directory '+$taskPath)}}
foreach($taskDefinition in $taskDefinitions){
    $taskLocalState=Join-Path $env:LOCALAPPDATA ('Packages/'+$taskDefinition.Family+'/LocalState')
    if(Test-Path -LiteralPath $taskLocalState){Assert-NormalTree $taskLocalState;Remove-Item -LiteralPath $taskLocalState -Recurse -Force;$taskJournal.Add('Removed remaining app state '+$taskLocalState)}
}
$taskJournal | Set-Content (Join-Path $taskBackup 'cleanup-actions.txt')
if(Test-Path -LiteralPath "Cert:\LocalMachine\TrustedPeople\$taskThumbprint"){
    $taskHelper=Join-Path $PSScriptRoot 'Certificate-Trust.ps1'
    $taskCertPath=Join-Path $taskBackup 'EarbudControls.Local.cer'
    $taskPowerShell=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
    $taskArguments='-NoProfile -ExecutionPolicy Bypass -File "'+$taskHelper+'" -CertificatePath "'+$taskCertPath+'" -Operation Remove'
    $taskChild=Start-Process -FilePath $taskPowerShell -ArgumentList $taskArguments -Verb RunAs -WindowStyle Hidden -PassThru
    $taskChild.WaitForExit()
    if($taskChild.ExitCode -ne 0){throw 'Public certificate trust removal was cancelled or failed.'}
}
if(Test-Path -LiteralPath "Cert:\LocalMachine\TrustedPeople\$taskThumbprint"){throw 'Public certificate trust remains.'}
if(-not(Test-Path -LiteralPath "Cert:\CurrentUser\My\$taskThumbprint")){throw 'Expected preserved signing key is missing.'}
$taskJournal.Add('Removed public certificate trust; preserved private signing key.')
$taskJournal | Set-Content (Join-Path $taskBackup 'cleanup-actions.txt')
Write-Output 'Reset completed. Production and demo uninstalled; old installers/data/trust removed. App left uninstalled.'
Write-Output "Rollback backup: $taskBackup"
