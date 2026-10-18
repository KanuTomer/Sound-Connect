param([Parameter(Mandatory=$true)][string]$CertificatePath,[ValidateSet('Import','Remove')][string]$Operation='Import')
$ErrorActionPreference='Stop'
$taskThumbprint='458DC5875423917D74F77A536EBBB42720D1B67A'
$taskCertificate=[Security.Cryptography.X509Certificates.X509Certificate2]::new($CertificatePath)
if($taskCertificate.Thumbprint -ne $taskThumbprint -or $taskCertificate.Subject -ne 'CN=EarbudControls.Local'){throw 'Unexpected public certificate.'}
$taskPrincipal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $taskPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Certificate trust changes require administrator approval.'}
$taskPath="Cert:\LocalMachine\TrustedPeople\$taskThumbprint"
if($Operation -eq 'Import'){
    if(-not(Test-Path -LiteralPath $taskPath)){Import-Certificate -FilePath $CertificatePath -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null}
    if(-not(Test-Path -LiteralPath $taskPath)){throw 'Certificate trust was not added.'}
}else{
    if(Test-Path -LiteralPath $taskPath){Remove-Item -LiteralPath $taskPath}
    if(Test-Path -LiteralPath $taskPath){throw 'Certificate trust was not removed.'}
}
