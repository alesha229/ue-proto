param([int]$TimeoutSeconds = 120, [ValidateSet('03','04')][string]$EvidenceStage = '04')
$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exePath = Join-Path $workspaceRoot 'Builds\Stage1\Windows\GratiaVR\Binaries\Win64\GratiaVR.exe'
$evidenceRoot = Join-Path $workspaceRoot ('evidence\' + $EvidenceStage)
$shotRoot = Join-Path $workspaceRoot 'Builds\Stage1\Windows\GratiaVR\Saved\Screenshots\Windows'
$results = @()
$manifest = Get-Content -LiteralPath (Join-Path $workspaceRoot 'Builds\Stage1\Windows\build_manifest.json') -Raw | ConvertFrom-Json
$exeHash = (Get-FileHash -LiteralPath $exePath).Hash
if ($exeHash -ne $manifest.executable_sha256) { throw 'Executable differs from manifest' }
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
foreach ($view in @('Front','Left','Right','Back','Face')) {
    $logPath = Join-Path $evidenceRoot ("packaged_view_$view.log")
    $began = [DateTimeOffset]::UtcNow
    $arguments = @('-nohmd','-windowed','-ResX=1280','-ResY=720','-unattended','-GratiaSmokeTest',
        ("-GratiaViewTest=$view"), ('-abslog="' + $logPath + '"'))
    $gameProcess = Start-Process -FilePath $exePath -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $exePath) -WindowStyle Hidden -PassThru
    if (-not $gameProcess.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $gameProcess.Id -ErrorAction SilentlyContinue
        throw "Character view $view timed out. See $logPath"
    }
    $gameProcess.Refresh()
    $logText = Get-Content -LiteralPath $logPath -Raw
    $pass = $gameProcess.ExitCode -eq 0 -and $logText.Contains('GRATIA_STAGE1_SMOKE_PASS') -and -not $logText.Contains('TEST FAIL:')
    $pass = $pass -and -not ($logText -match 'missing usage flag SkeletalMesh|Failed to compile Material|Default Material will be used in game')
    $shot = Get-ChildItem -LiteralPath $shotRoot -Filter 'GratiaStage1Smoke*.png' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $shot -or $shot.LastWriteTimeUtc -lt $began.UtcDateTime) { throw "No new screenshot for view $view" }
    $shotPath = Join-Path $evidenceRoot ("packaged_view_$view.png")
    Copy-Item -LiteralPath $shot.FullName -Destination $shotPath -Force
    $pass = $pass -and $logText.Contains('BUILD id=' + $manifest.build_id)
    $results += [pscustomobject]@{view=$view; build_id=$manifest.build_id; executable_sha256=$exeHash; passed=$pass; screenshot=$shotPath; log=$logPath}
    $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'packaged_view_results.json') -Encoding utf8
    if (-not $pass) { throw "Character view $view failed. See $logPath" }
    Write-Output "Character view $view passed"
}
