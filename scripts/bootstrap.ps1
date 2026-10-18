$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskEngine = Join-Path $taskRoot '.deps\SonyHeadphonesClient'
$taskRevision = 'b98309446e2aff7538959eb6518ab694c5264cbe'
if (-not (Test-Path -LiteralPath $taskEngine)) {
    New-Item -Path $taskEngine -ItemType Directory -Force | Out-Null
    git -C $taskEngine init --quiet
    git -C $taskEngine remote add origin https://github.com/mos9527/SonyHeadphonesClient.git
    git -C $taskEngine fetch --depth 1 origin $taskRevision
    if ($LASTEXITCODE -ne 0) { throw 'Sony engine download failed.' }
    git -C $taskEngine checkout --detach $taskRevision
    if ($LASTEXITCODE -ne 0) { throw 'Could not check out the pinned Sony engine.' }
}
$taskActual = git -C $taskEngine rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $taskActual -ne $taskRevision) {
    throw 'Existing engine checkout does not match the pinned revision; it was left untouched.'
}
Write-Output "Sony engine pinned at $taskRevision"
$taskEqPatch=Join-Path $taskRoot 'patches\wf-c710n-eq-notification.patch'
# A clean checkout is expected to fail the reverse check. In PowerShell 5,
# redirected native stderr otherwise becomes a terminating error before we can
# inspect the exit code. Keep real forward-check/apply failures terminating.
$taskPreviousPreference=$ErrorActionPreference
try{
    $ErrorActionPreference='Continue'
    git -C $taskEngine apply --reverse --check $taskEqPatch 2>$null
    $taskAlreadyPatched=$LASTEXITCODE -eq 0
}finally{$ErrorActionPreference=$taskPreviousPreference}
if(-not $taskAlreadyPatched){
    git -C $taskEngine apply --check $taskEqPatch
    if($LASTEXITCODE -ne 0){throw 'EQ compatibility patch conflicts with the existing engine checkout; files were left untouched.'}
    git -C $taskEngine apply $taskEqPatch
    if($LASTEXITCODE -ne 0){throw 'Could not apply the EQ compatibility patch.'}
}

function Get-TaskNugetPackage([string]$Id,[string]$Version,[string]$Folder) {
    $taskNugetRoot=Join-Path $taskRoot '.deps\nuget'
    $taskDestination=Join-Path $taskNugetRoot $Folder
    if(Test-Path -LiteralPath (Join-Path $taskDestination "$Id.nuspec")){return}
    New-Item -Path $taskNugetRoot -ItemType Directory -Force | Out-Null
    $taskArchive=Join-Path $taskNugetRoot "$Folder.nupkg"
    $taskLower=$Id.ToLowerInvariant()
    Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/$taskLower/$Version/$taskLower.$Version.nupkg" -OutFile $taskArchive
    # Windows PowerShell 5 Expand-Archive rejects the .nupkg extension.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($taskArchive,$taskDestination)
}
Get-TaskNugetPackage 'Microsoft.WindowsAppSDK.Widgets' '2.0.5' 'widgets'
Get-TaskNugetPackage 'Microsoft.Windows.CppWinRT' '3.0.260818.1' 'cppwinrt'
$taskGenerated=Join-Path $taskRoot 'build\generated'
if(-not(Test-Path -LiteralPath (Join-Path $taskGenerated 'winrt\Microsoft.Windows.Widgets.Providers.h'))){
    & (Join-Path $taskRoot '.deps\nuget\cppwinrt\bin\cppwinrt.exe') -input sdk -input (Join-Path $taskRoot '.deps\nuget\widgets\metadata\Microsoft.Windows.Widgets.winmd') -output $taskGenerated
    if($LASTEXITCODE -ne 0){throw 'C++/WinRT projection generation failed.'}
}

