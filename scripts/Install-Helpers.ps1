# Pure checks and read-only package inspection, shared by setup and tests.
function Get-SetupProblems([int]$OSBuild,[string]$Architecture,[bool]$DeveloperMode,[bool]$WidgetsAvailable){
    if($OSBuild -lt 22621){'Windows 11 build 22621 or newer is required.'}
    if($Architecture -ne 'AMD64'){'This prerelease supports x64 Windows only.'}
    if(-not $DeveloperMode){'Enable Developer Mode in Windows Settings > System > Advanced > For developers, then run setup again.'}
    if(-not $WidgetsAvailable){'Install or update Windows Web Experience Pack from Microsoft Store, then run setup again.'}
}
function Test-RuntimeRequired($Packages){
    return -not @($Packages | Where-Object {$_.Name -eq 'Microsoft.WindowsAppRuntime.2' -and [string]$_.Architecture -eq 'X64' -and [version]$_.Version -ge [version]'2.5.1.0'}).Count
}
function Get-MsixIdentity([string]$Path){
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive=[IO.Compression.ZipFile]::OpenRead($Path)
    try{
        $entry=$archive.GetEntry('AppxManifest.xml');if(-not $entry){throw 'MSIX manifest is missing.'}
        $reader=[IO.StreamReader]::new($entry.Open())
        try{$manifest=[xml]$reader.ReadToEnd()}finally{$reader.Dispose()}
        return $manifest.Package.Identity
    }finally{$archive.Dispose()}
}
function Assert-BundleIntegrity([string]$Root){
    $sums=Join-Path $Root 'SHA256SUMS.txt'
    if(-not(Test-Path -LiteralPath $sums)){throw 'SHA256SUMS.txt is missing from the extracted release.'}
    $prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\';$seen=@{}
    foreach($line in Get-Content -LiteralPath $sums){
        if($line -notmatch '^([A-Fa-f0-9]{64})  (.+)$'){throw 'Invalid checksum entry.'}
        $expected=$Matches[1];$relative=$Matches[2]
        if($relative -match '(^[\\/]|(^|[\\/])\.\.([\\/]|$)|:|[?*])'){throw 'Unsafe checksum path.'}
        $path=[IO.Path]::GetFullPath((Join-Path $Root $relative))
        if(-not $path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Checksum path leaves the release folder.'}
        $cursor=$path
        while($cursor -and $cursor.Length -ge $prefix.Length){
            if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Release files must not be links.'}
            $cursor=Split-Path $cursor -Parent
        }
        if($seen.ContainsKey($relative)){throw 'Duplicate checksum entry.'};$seen[$relative]=$true
        if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $expected){throw "Checksum mismatch: $relative"}
    }
    foreach($file in Get-ChildItem -LiteralPath $Root -File -Recurse){
        $relative=$file.FullName.Substring($prefix.Length).Replace('\','/')
        if($relative -ne 'SHA256SUMS.txt' -and -not $seen.ContainsKey($relative)){throw "Unlisted release file: $relative"}
    }
}
