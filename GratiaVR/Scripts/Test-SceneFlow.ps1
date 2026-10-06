param([ValidateRange(30, 600)][int]$TimeoutSeconds = 300, [string]$EngineRoot = 'E:\ue\UE_5.8')
$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$packageRoot = Join-Path $workspaceRoot 'Builds\Windows'
$exePath = Join-Path $packageRoot 'GratiaVR\Binaries\Win64\GratiaVR.exe'
$manifestPath = Join-Path $packageRoot 'build_manifest.json'
$evidenceRoot = Join-Path $workspaceRoot 'evidence\04'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'Versioned build manifest missing' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
$exeHash = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
if ($exeHash -ne $manifest.executable_sha256) { throw 'Executable differs from manifest' }
$pythonPath = Join-Path $EngineRoot 'Engine\Binaries\ThirdParty\Python3\Win64\python.exe'
& $pythonPath (Join-Path $PSScriptRoot 'create_build_manifest.py') --engine $EngineRoot --output $manifestPath --verify
if ($LASTEXITCODE -ne 0) { throw 'Current source/assets differ from the package; build before scene flow QA' }
if (-not $manifest.package_files -or $manifest.package_files.Count -lt 1) { throw 'Manifest has no package file inventory' }
foreach ($file in $manifest.package_files) {
    $path = Join-Path $packageRoot $file.path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) {
        throw "Package file differs from manifest: $($file.path)"
    }
}
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
$logPath = Join-Path $evidenceRoot 'packaged_scene_flow.log'
$resultPath = Join-Path $evidenceRoot 'packaged_scene_flow_result.json'
$shotRoot = Join-Path $packageRoot 'GratiaVR\Saved\Screenshots\Windows'
$settingsPath = Join-Path $packageRoot 'GratiaVR\Saved\SaveGames\GratiaUser.sav'
$settingsHashBefore = if (Test-Path -LiteralPath $settingsPath -PathType Leaf) { (Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash } else { $null }
$began = [DateTimeOffset]::UtcNow
$arguments = @('-nohmd', '-windowed', '-ResX=1280', '-ResY=720', '-unattended', '-GratiaFlowQA', ('-abslog="' + $logPath + '"'))
$gameProcess = Start-Process -FilePath $exePath -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $exePath) -WindowStyle Hidden -PassThru
$timedOut = -not $gameProcess.WaitForExit($TimeoutSeconds * 1000)
if ($timedOut) {
    Stop-Process -Id $gameProcess.Id -ErrorAction SilentlyContinue
    $null = $gameProcess.WaitForExit(5000)
}
$gameProcess.Refresh()
$logText = if (Test-Path -LiteralPath $logPath) { Get-Content -LiteralPath $logPath -Raw } else { '' }
$freshLog = (Test-Path -LiteralPath $logPath) -and (Get-Item -LiteralPath $logPath).LastWriteTimeUtc -ge $began.UtcDateTime
$buildMatched = $logText.Contains('BUILD id=' + $manifest.build_id)
$markerPassed = $logText.Contains('GRATIA_FLOW_QA_PASS')
$failureDetected = $logText -match 'FLOW_QA_CHECK FAIL|GRATIA_FLOW_QA_FAIL|Fatal error:|Assertion failed:|Unhandled Exception|LowLevelFatalError|SOFT_BODY_FAULT'
$materialsPassed = -not ($logText -match 'missing usage flag SkeletalMesh|Failed to compile Material|Default Material will be used in game')
$settingsHashAfter = if (Test-Path -LiteralPath $settingsPath -PathType Leaf) { (Get-FileHash -LiteralPath $settingsPath -Algorithm SHA256).Hash } else { $null }
$settingsUnchanged = $settingsHashBefore -eq $settingsHashAfter
$captures = @()
$captureNames = @([regex]::Matches($logText, 'FLOW_QA_CAPTURE (\S+)') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
foreach ($name in $captureNames) {
    $path = Join-Path $shotRoot ("GratiaSceneFlow_$name.png")
    $fresh = (Test-Path -LiteralPath $path -PathType Leaf) -and (Get-Item -LiteralPath $path).LastWriteTimeUtc -ge $began.UtcDateTime
    $destination = Join-Path $evidenceRoot ("scene_flow_$name.png")
    if ($fresh) { Copy-Item -LiteralPath $path -Destination $destination -Force }
    $captures += [pscustomobject]@{ name = $name; fresh = $fresh; screenshot = if ($fresh) { $destination } else { $null } }
}
$capturesPassed = $captures.Count -ge 4 -and -not ($captures | Where-Object { -not $_.fresh })
$pass = -not $timedOut -and $gameProcess.ExitCode -eq 0 -and $freshLog -and $buildMatched -and $markerPassed -and -not $failureDetected -and $materialsPassed -and $settingsUnchanged -and $capturesPassed
[pscustomobject]@{
    build_id = $manifest.build_id; executable = $exePath; executable_sha256 = $exeHash; manifest_sha256 = $manifestHash
    source_sha256 = $manifest.source_sha256; assets_sha256 = $manifest.assets_sha256; source_assets_matched = $true; package_files_matched = $true
    began_utc = $began.ToString('o'); finished_utc = [DateTimeOffset]::UtcNow.ToString('o')
    exit_code = $gameProcess.ExitCode; timed_out = $timedOut; passed = $pass; fresh_log = $freshLog
    build_matched = $buildMatched; marker_passed = $markerPassed; failure_detected = $failureDetected; materials_passed = $materialsPassed
    settings_unchanged = $settingsUnchanged; captures_passed = $capturesPassed; captures = $captures
    check_count = ([regex]::Matches($logText, 'FLOW_QA_CHECK PASS')).Count; log = $logPath
    scope = 'rendered synthetic scene/menu/playback integration'; real_vr_acceptance = $false; real_tracking = $false; nullrhi = $false
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $resultPath -Encoding utf8
if (-not $pass) { throw "Packaged scene flow QA failed. See $logPath and $resultPath" }
Write-Output "Scene flow QA PASS for build $($manifest.build_id); headset input, contact feel and SteamVR frame delivery remain unverified."
