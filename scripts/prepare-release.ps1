param([switch]$UserApprovedUi,[switch]$SkipPackage)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'Install-Helpers.ps1')
$taskVersion=& (Join-Path $PSScriptRoot 'Get-AppVersion.ps1')
$taskDisplay=([version]$taskVersion).ToString(3)
if(-not $SkipPackage){& (Join-Path $PSScriptRoot 'package.ps1') -UserApprovedUi:$UserApprovedUi}
$taskRuntimeZip=Join-Path $taskRoot '.deps/Microsoft.WindowsAppRuntime.Redist.2.5.zip'
if(-not(Test-Path -LiteralPath $taskRuntimeZip)){
    Invoke-WebRequest -Uri 'https://aka.ms/windowsappsdk/2.5/2.5.1/Microsoft.WindowsAppRuntime.Redist.2.5.zip' -OutFile $taskRuntimeZip
}
if((Get-FileHash -LiteralPath $taskRuntimeZip -Algorithm SHA256).Hash -ne '3C6D3B4D80A194BB4F9A9A91E00F074B7D539929AB53FB7DDE3EF9D8A21DDBB4'){throw 'Official runtime redistributable hash differs from the reviewed 2.5.1 download.'}
$taskStage=Join-Path $taskRoot ('out/release-stage-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskStage,(Join-Path $taskStage 'scripts'),(Join-Path $taskStage 'Dependencies'),(Join-Path $taskStage 'Licenses') | Out-Null
$taskPackageName="EarbudControls_$taskVersion.msix"
Copy-Item -LiteralPath (Join-Path $taskRoot "out/$taskPackageName"),(Join-Path $taskRoot 'out/EarbudControls.Local.cer'),(Join-Path $taskRoot 'LICENSE'),(Join-Path $taskRoot 'THIRD_PARTY_NOTICES.txt') -Destination $taskStage
foreach($taskScript in 'install.ps1','Install-Helpers.ps1','Certificate-Trust.ps1'){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $taskScript) -Destination (Join-Path $taskStage 'scripts')
}
$taskCmd=([IO.File]::ReadAllText((Join-Path $taskRoot 'packaging/Install.cmd'))).Replace('__PACKAGE__',$taskPackageName)
[IO.File]::WriteAllText((Join-Path $taskStage 'Install.cmd'),$taskCmd.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.Encoding]::ASCII)
Copy-Item -LiteralPath (Join-Path $taskRoot 'packaging/README-install.txt') -Destination (Join-Path $taskStage 'README.txt')
foreach($taskLicense in 'SonyHeadphonesClient-LICENSE.txt','fmt-LICENSE.txt','WindowsAppSDK-LICENSE.txt','LUCIDE-LICENSE.txt'){
    Copy-Item -LiteralPath (Join-Path $taskRoot "out/package/$taskLicense") -Destination (Join-Path $taskStage 'Licenses')
}
Copy-Item -LiteralPath (Join-Path $taskStage 'Licenses/WindowsAppSDK-LICENSE.txt') -Destination (Join-Path $taskStage 'Dependencies')
Add-Type -AssemblyName System.IO.Compression.FileSystem
$taskArchive=[IO.Compression.ZipFile]::OpenRead($taskRuntimeZip)
try{
    $taskEntry=$taskArchive.GetEntry('MSIX/win10-x64/Microsoft.WindowsAppRuntime.2.msix')
    if(-not $taskEntry){throw 'Official x64 framework missing from redistributable.'}
    [IO.Compression.ZipFileExtensions]::ExtractToFile($taskEntry,(Join-Path $taskStage 'Dependencies/Microsoft.WindowsAppRuntime.2.msix'))
}finally{$taskArchive.Dispose()}
$taskRuntime=Join-Path $taskStage 'Dependencies/Microsoft.WindowsAppRuntime.2.msix'
$taskRuntimeIdentity=Get-MsixIdentity $taskRuntime
$taskSignature=Get-AuthenticodeSignature -LiteralPath $taskRuntime
if($taskRuntimeIdentity.Name -ne 'Microsoft.WindowsAppRuntime.2' -or $taskRuntimeIdentity.ProcessorArchitecture -ne 'x64' -or $taskRuntimeIdentity.Version -ne '2.5.1.0' -or $taskSignature.Status -ne 'Valid' -or $taskSignature.SignerCertificate.Subject -notmatch 'Microsoft Corporation'){throw 'Official runtime package identity or signature failed.'}
$taskSums=foreach($taskFile in Get-ChildItem -LiteralPath $taskStage -Recurse -File | Sort-Object FullName){
    (Get-FileHash -LiteralPath $taskFile.FullName -Algorithm SHA256).Hash+'  '+$taskFile.FullName.Substring($taskStage.Length+1).Replace('\','/')
}
$taskSums | Set-Content -LiteralPath (Join-Path $taskStage 'SHA256SUMS.txt') -Encoding ascii
Assert-BundleIntegrity $taskStage
& (Join-Path $taskStage 'scripts/install.ps1') -PackagePath (Join-Path $taskStage $taskPackageName) -CheckOnly
$taskOutput=Join-Path $taskRoot "out/EarbudControls-$taskDisplay-windows-x64.zip"
if(Test-Path -LiteralPath $taskOutput){Remove-Item -LiteralPath $taskOutput}
[IO.Compression.ZipFile]::CreateFromDirectory($taskStage,$taskOutput,[IO.Compression.CompressionLevel]::Optimal,$false)
((Get-FileHash -LiteralPath $taskOutput -Algorithm SHA256).Hash+'  '+[IO.Path]::GetFileName($taskOutput)) | Set-Content -LiteralPath ($taskOutput+'.sha256') -Encoding ascii
Write-Output "Release ZIP: $taskOutput"
Write-Output "Verified stage: $taskStage"
