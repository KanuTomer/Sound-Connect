$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskManifest=[xml](Get-Content -LiteralPath (Join-Path $taskRoot 'packaging/AppxManifest.xml') -Raw)
[version]::Parse($taskManifest.Package.Identity.Version).ToString()
