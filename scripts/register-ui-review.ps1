param([switch]$PrepareOnly,[switch]$SkipBuild,[switch]$Remove)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskLayout=Join-Path $taskRoot 'out/ui-review'
$taskReviewExe=Join-Path $taskLayout 'WidgetUiReview.exe'
# Touch only this separate development provider, never EarbudControls.exe.
$taskRunning=@(Get-Process -Name WidgetUiReview -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $taskReviewExe})
foreach($taskProcess in $taskRunning){Stop-Process -InputObject $taskProcess -Force -ErrorAction SilentlyContinue;if(-not $taskProcess.WaitForExit(5000)){throw 'The UI review provider did not exit; close Win + W before updating it.'}}
if($Remove){Get-AppxPackage -Name 'Kanu.EarbudControls.UIReview' | Remove-AppxPackage;Write-Output 'Removed the UI review provider. The installed app is unchanged.';return}
if(-not $SkipBuild){
    $taskVswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $taskVs=& $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $taskCmake=Join-Path $taskVs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    & $taskCmake -S $taskRoot -B (Join-Path $taskRoot 'build') -G 'Visual Studio 17 2022' -A x64
    if($LASTEXITCODE -ne 0){throw 'Review configuration failed.'}
    & $taskCmake --build (Join-Path $taskRoot 'build') --config Release --target widget-ui-review --parallel 2
    if($LASTEXITCODE -ne 0){throw 'Review build failed.'}
}
$taskBinary=Join-Path $taskRoot 'build/Release/widget-ui-review.exe'
if(-not(Test-Path -LiteralPath $taskBinary)){throw 'Build widget-ui-review first.'}
$taskAssets=Join-Path $taskLayout 'Assets'
$taskIcons=Join-Path $taskLayout 'Public/Icons'
New-Item -ItemType Directory -Path $taskAssets,$taskIcons -Force | Out-Null
Copy-Item -LiteralPath $taskBinary -Destination $taskReviewExe -Force
Copy-Item -LiteralPath (Join-Path $taskRoot 'assets/icons/LUCIDE-LICENSE.txt') -Destination $taskLayout -Force
& (Join-Path $PSScriptRoot 'export-icons.ps1') -Destination $taskIcons
Add-Type -AssemblyName System.Drawing
$taskSource=[Drawing.Bitmap]::FromFile((Join-Path $taskIcons 'bluetooth-accent.png'))
try{foreach($taskSize in 44,50,150){$taskImage=[Drawing.Bitmap]::new($taskSize,$taskSize);$taskGraphics=[Drawing.Graphics]::FromImage($taskImage);try{$taskGraphics.Clear([Drawing.Color]::Transparent);$taskGraphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic;$taskGraphics.DrawImage($taskSource,0,0,$taskSize,$taskSize);$taskName=if($taskSize -eq 50){'StoreLogo.png'}else{"Logo$taskSize.png"};$taskImage.Save((Join-Path $taskAssets $taskName),[Drawing.Imaging.ImageFormat]::Png)}finally{$taskGraphics.Dispose();$taskImage.Dispose()}}}finally{$taskSource.Dispose()}
$taskManifest=[xml](Get-Content -LiteralPath (Join-Path $taskRoot 'packaging/AppxManifest.xml') -Raw)
$taskManifest.Package.Identity.Name='Kanu.EarbudControls.UIReview'
# A changed loose development registration must have a newer package version.
# Bump only this demo identity; preserve the production manifest and data.
if(-not $PrepareOnly){
    $taskInstalled=Get-AppxPackage -Name 'Kanu.EarbudControls.UIReview' | Sort-Object {[version]$_.Version} -Descending | Select-Object -First 1
    if($taskInstalled){
        $taskInstalledVersion=[version]$taskInstalled.Version
        if($taskInstalledVersion -ge [version]$taskManifest.Package.Identity.Version){$taskManifest.Package.Identity.Version=[version]::new($taskInstalledVersion.Major,$taskInstalledVersion.Minor,$taskInstalledVersion.Build,$taskInstalledVersion.Revision+1).ToString()}
    }
}
$taskManifest.Package.Properties.DisplayName='WF-C710N UI review - demo data'
$taskApplication=$taskManifest.SelectSingleNode("//*[local-name()='Application']")
$taskApplication.SetAttribute('Executable','WidgetUiReview.exe')
$taskVisual=$taskManifest.SelectSingleNode("//*[local-name()='VisualElements']")
$taskVisual.SetAttribute('DisplayName','WF-C710N UI review - demo data')
$taskVisual.SetAttribute('Description','Native UI review using demo data; never connects to earbuds.')
$taskServer=$taskManifest.SelectSingleNode("//*[local-name()='ExeServer']")
$taskServer.SetAttribute('Executable','WidgetUiReview.exe')
$taskServer.SelectSingleNode("*[local-name()='Class']").SetAttribute('Id','AE7791C6-45CD-4A7F-B27F-53D2AC701060')
$taskManifest.SelectSingleNode("//*[local-name()='CreateInstance']").SetAttribute('ClassId','AE7791C6-45CD-4A7F-B27F-53D2AC701060')
$taskManifest.SelectSingleNode("//*[local-name()='AppExtension']").SetAttribute('Id','EarbudControls.UIReview.Provider')
$taskDefinition=$taskManifest.SelectSingleNode("//*[local-name()='Definition']")
$taskDefinition.SetAttribute('Id','WF-C710N-UIReview');$taskDefinition.SetAttribute('DisplayName','WF-C710N UI review - demo data');$taskDefinition.SetAttribute('Description','Review only. Demo data. No Bluetooth connection or device writes.')
$taskScreenshot=$taskManifest.SelectSingleNode("//*[local-name()='Screenshot']")
$taskScreenshot.SetAttribute('Path','Assets\Logo150.png');$taskScreenshot.SetAttribute('DisplayAltText','Demo-only native UI review')
$taskManifest.Save((Join-Path $taskLayout 'AppxManifest.xml'))
$taskFingerprint=& (Join-Path $PSScriptRoot 'Get-UiReviewFingerprint.ps1')
[pscustomobject]@{sourceFingerprint=$taskFingerprint;reviewExeSha256=(Get-FileHash -LiteralPath $taskReviewExe -Algorithm SHA256).Hash;demoOnly=$true} | ConvertTo-Json | Set-Content (Join-Path $taskLayout 'ReviewSource.json') -Encoding utf8
if($PrepareOnly){Write-Output "Prepared loose review files: $taskLayout. No MSIX created or app registered.";return}
$taskDeveloper=Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock' -ErrorAction SilentlyContinue
if($taskDeveloper.AllowDevelopmentWithoutDevLicense -ne 1){throw 'Enable Windows Developer Mode before registering this review widget.'}
Add-AppxPackage -Register (Join-Path $taskLayout 'AppxManifest.xml') -ForceApplicationShutdown
Write-Output 'Registered WF-C710N UI review - demo data. No MSIX created; production app unchanged.'
Write-Output 'Manually add the UI review widget in Win + W. Review Large first: Home, Equalizer, Presets, More, and hover states.'



