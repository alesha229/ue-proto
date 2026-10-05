param([int]$TimeoutSeconds = 120, [ValidateSet('01','02','03','04')][string]$EvidenceStage = '04')
$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exePath = Join-Path $workspaceRoot 'Builds\Windows\GratiaVR\Binaries\Win64\GratiaVR.exe'
$evidenceRoot = Join-Path $workspaceRoot ('evidence\' + $EvidenceStage)
$shotRoot = Join-Path $workspaceRoot 'Builds\Windows\GratiaVR\Saved\Screenshots\Windows'
$results = @()
$manifest = Get-Content -LiteralPath (Join-Path $workspaceRoot 'Builds\Windows\build_manifest.json') -Raw | ConvertFrom-Json
$exeHash = (Get-FileHash -LiteralPath $exePath).Hash
if ($exeHash -ne $manifest.executable_sha256) { throw 'Executable differs from manifest' }
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
foreach ($pose in @('Arms','Head','Performance')) {
    $logPath = Join-Path $evidenceRoot ("packaged_pose_$pose.log")
    $began = [DateTimeOffset]::UtcNow
    $arguments = @('-nohmd','-windowed','-ResX=1280','-ResY=720','-unattended','-GratiaSmokeTest',("-GratiaPoseTest=$pose"),('-abslog="' + $logPath + '"'))
    $gameProcess = Start-Process -FilePath $exePath -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $exePath) -WindowStyle Hidden -PassThru
    if (-not $gameProcess.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $gameProcess.Id -ErrorAction SilentlyContinue
        throw "Character pose $pose timed out. See $logPath"
    }
    $gameProcess.Refresh()
    $logText = Get-Content -LiteralPath $logPath -Raw
    $materialsPassed = -not ($logText -match 'missing usage flag SkeletalMesh|Failed to compile Material|Default Material will be used in game')
    $smokePassed = $logText.Contains('GRATIA_STAGE1_SMOKE_PASS') -and $logText.Contains('BUILD id=' + $manifest.build_id) -and -not $logText.Contains('TEST FAIL:')
    $shot = Get-ChildItem -LiteralPath $shotRoot -Filter 'GratiaStage1Smoke*.png' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $shot -or $shot.LastWriteTimeUtc -lt $began.UtcDateTime) { throw "No new screenshot for pose $pose" }
    $shotPath = Join-Path $evidenceRoot ("packaged_pose_$pose.png")
    Copy-Item -LiteralPath $shot.FullName -Destination $shotPath -Force
    $results += [pscustomobject]@{pose=$pose; build_id=$manifest.build_id; executable_sha256=$exeHash; exit_code=$gameProcess.ExitCode; smoke_pass=$smokePassed; materials_pass=$materialsPassed;
        check_count=([regex]::Matches($logText, 'TEST PASS:')).Count; screenshot=$shotPath; log=$logPath}
    $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'packaged_pose_results.json') -Encoding utf8
    if ($gameProcess.ExitCode -ne 0 -or -not $smokePassed -or -not $materialsPassed) { throw "Character pose $pose failed. See $logPath" }
    Write-Output "Character pose $pose passed; screenshot $shotPath"
}
