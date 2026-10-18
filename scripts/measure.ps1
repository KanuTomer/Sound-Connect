param([int]$Seconds=60,[ValidateSet('Inactive','Widget','Editor')][string]$Scenario='Inactive')
$ErrorActionPreference='Stop'
if($Seconds -lt 10 -or $Seconds -gt 300){throw 'Measure for 10 to 300 seconds.'}
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskBefore=@(Get-Process -Name EarbudControls -ErrorAction SilentlyContinue)
$taskCpuBefore=0.0
foreach($taskProcess in $taskBefore){$taskCpuBefore+=$taskProcess.TotalProcessorTime.TotalSeconds}
$taskClock=[Diagnostics.Stopwatch]::StartNew()
Start-Sleep -Seconds $Seconds
$taskAfter=@(Get-Process -Name EarbudControls -ErrorAction SilentlyContinue)
$taskCpuAfter=0.0;$taskPrivate=0L;$taskWorking=0L;$taskHandles=0
foreach($taskProcess in $taskAfter){$taskCpuAfter+=$taskProcess.TotalProcessorTime.TotalSeconds;$taskPrivate+=$taskProcess.PrivateMemorySize64;$taskWorking+=$taskProcess.WorkingSet64;$taskHandles+=$taskProcess.HandleCount}
$taskSameIds=(@($taskBefore.Id|Sort-Object)-join ',') -eq (@($taskAfter.Id|Sort-Object)-join ',')
$taskCpu=if($taskSameIds){[Math]::Round([Math]::Max(0,$taskCpuAfter-$taskCpuBefore)/$taskClock.Elapsed.TotalSeconds/[Environment]::ProcessorCount*100,4)}else{$null}
$taskBudget=switch($Scenario){'Inactive'{30};'Widget'{50};'Editor'{100}}
$taskReport=[ordered]@{scenario=$Scenario;elapsed_seconds=[Math]::Round($taskClock.Elapsed.TotalSeconds,2);process_count=$taskAfter.Count;private_mib=[Math]::Round($taskPrivate/1MB,2);working_set_mib=[Math]::Round($taskWorking/1MB,2);handle_count=$taskHandles;machine_cpu_percent=$taskCpu;process_identity_stable=$taskSameIds;memory_budget_mib=$taskBudget;memory_pass=$taskPrivate -le ($taskBudget*1MB);cpu_pass=if($Scenario -eq 'Inactive' -and $null -ne $taskCpu){$taskCpu -lt 0.1}else{$null};note='App processes only. Widgets host overhead and game frame times require separate comparisons.'}
New-Item -Path (Join-Path $taskRoot 'artifacts') -ItemType Directory -Force|Out-Null
$taskJson=$taskReport|ConvertTo-Json
$taskPath=Join-Path $taskRoot ("artifacts\resources-{0}-{1}.json" -f $Scenario.ToLowerInvariant(),(Get-Date -Format 'yyyyMMdd-HHmmss'))
Set-Content -LiteralPath $taskPath -Value $taskJson -Encoding utf8
Write-Output $taskJson
Write-Output "Report: $taskPath"
