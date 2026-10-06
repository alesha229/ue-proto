param([ValidateRange(1, 600)][int]$TimeoutSeconds = 200, [ValidateSet('04')][string]$EvidenceStage = '04')
$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$archiveRoot = Join-Path $workspaceRoot 'Builds\Windows'
$exePath = Join-Path $archiveRoot 'GratiaVR\Binaries\Win64\GratiaVR.exe'
$manifestPath = Join-Path $archiveRoot 'build_manifest.json'
$evidenceRoot = Join-Path $workspaceRoot ('evidence\' + $EvidenceStage)
if (-not (Test-Path -LiteralPath $exePath)) { throw "Packaged executable missing: $exePath" }
if (-not (Test-Path -LiteralPath $manifestPath)) { throw 'Versioned build manifest missing; run Build-Stage1.ps1 first' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ([string]::IsNullOrWhiteSpace($manifest.build_id)) { throw 'Build manifest has no build id' }
$exeHash = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
if ($exeHash -ne $manifest.executable_sha256) { throw 'Executable differs from build manifest' }
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
$logPath = Join-Path $evidenceRoot 'packaged_soft_body_qa.log'
$resultPath = Join-Path $evidenceRoot 'packaged_soft_body_result.json'
$shotRoot = Join-Path $archiveRoot 'GratiaVR\Saved\Screenshots\Windows'
if (Test-Path -LiteralPath $shotRoot) { Get-ChildItem -LiteralPath $shotRoot -Filter 'Gratia*_*.png' | Where-Object { $_.Name -like 'GratiaSoftPress_*' -or $_.Name -like 'GratiaBodyGrip_*' } | Remove-Item -Force }
$began = [DateTimeOffset]::UtcNow
# A normal RHI keeps the packaged animation and hand mesh paths identical to play;
# NullRHI is not used for this rendered desktop run.
$arguments = @('-nohmd', '-windowed', '-ResX=1280', '-ResY=720', '-unattended', '-GratiaSoftBodyQA',
    '-GratiaCharacterProfile=/Game/Characters/Profiles/DA_Gratia', ('-abslog="' + $logPath + '"'))
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
$markerPassed = $logText.Contains('GRATIA_SOFT_BODY_QA PASS')
$failureDetected = $logText -match 'GRATIA_SOFT_BODY_QA FAIL|TEST FAIL:|SOFT_BODY_FAULT|Fatal error:|Assertion failed:|Unhandled Exception|LowLevelFatalError'
$materialsPassed = -not ($logText -match 'missing usage flag SkeletalMesh|Failed to compile Material|Default Material will be used in game')
# Press captures per zone (same camera): rest -> pressed must visibly change; the surface
# dent alone is the difference between pressed with and without the material offset.
Add-Type -AssemblyName System.Drawing
function Get-ChangedPercent([string]$PathA, [string]$PathB) {
    $a = [System.Drawing.Bitmap]::new($PathA); $b = [System.Drawing.Bitmap]::new($PathB)
    try {
        $changed = 0; $total = 0
        for ($y = 0; $y -lt [Math]::Min($a.Height, $b.Height); $y += 4) {
            for ($x = 0; $x -lt [Math]::Min($a.Width, $b.Width); $x += 4) {
                $p = $a.GetPixel($x, $y); $q = $b.GetPixel($x, $y); $total++
                if ([Math]::Max([Math]::Abs($p.R - $q.R), [Math]::Max([Math]::Abs($p.G - $q.G), [Math]::Abs($p.B - $q.B))) -gt 10) { $changed++ }
            }
        }
        return [Math]::Round(100.0 * $changed / [Math]::Max(1, $total), 2)
    } finally { $a.Dispose(); $b.Dispose() }
}
$press = @()
foreach ($zone in ([regex]::Matches($logText, 'SOFT_BODY_QA_ZONE (\S+)') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)) {
    $files = @{}
    foreach ($state in 'rest', 'on', 'off') { $files[$state] = Join-Path $shotRoot ("GratiaSoftPress_{0}_{1}.png" -f $zone, $state) }
    $present = ($files.Values | Where-Object { Test-Path -LiteralPath $_ }).Count -eq 3
    $entry = [ordered]@{ zone = $zone; captures = $present; press_changed_percent = $null; dent_changed_percent = $null }
    if ($present) {
        $entry.press_changed_percent = Get-ChangedPercent $files['rest'] $files['on']
        $entry.dent_changed_percent = Get-ChangedPercent $files['off'] $files['on']
        foreach ($state in 'rest', 'on', 'off') { Copy-Item -LiteralPath $files[$state] -Destination (Join-Path $evidenceRoot ("soft_press_{0}_{1}.png" -f $zone, $state)) -Force }
    }
    $press += [pscustomobject]$entry
}
# Wrapping-grip captures (forearm, thigh, waist) for visual review.
$gripShots = @(Get-ChildItem -LiteralPath $shotRoot -Filter 'GratiaBodyGrip_*.png' -ErrorAction SilentlyContinue)
foreach ($shot in $gripShots) { Copy-Item -LiteralPath $shot.FullName -Destination (Join-Path $evidenceRoot ('body_grip_' + $shot.Name.Substring(15))) -Force }
$gripChecks = ([regex]::Matches($logText, 'SOFT_BODY_QA_CHECK PASS .*(palm rests|palm faces|fingers wrap|stay outside the body)')).Count
$pressPassed = $press.Count -ge 1 -and -not ($press | Where-Object { -not $_.captures -or $_.press_changed_percent -lt 1.0 -or $_.dent_changed_percent -le 0 })
$pass = -not $timedOut -and $gameProcess.ExitCode -eq 0 -and $freshLog -and $buildMatched -and $markerPassed -and -not $failureDetected -and $materialsPassed -and $pressPassed
[pscustomobject]@{
    build_id = $manifest.build_id; executable = $exePath; executable_sha256 = $exeHash; manifest_sha256 = $manifestHash
    source_sha256 = $manifest.source_sha256; assets_sha256 = $manifest.assets_sha256
    began_utc = $began.ToString('o'); finished_utc = [DateTimeOffset]::UtcNow.ToString('o')
    exit_code = $gameProcess.ExitCode; timed_out = $timedOut; passed = $pass; fresh_log = $freshLog
    build_matched = $buildMatched; marker_passed = $markerPassed; failure_detected = $failureDetected; materials_passed = $materialsPassed
    press_captures_passed = $pressPassed; press = $press; body_grip_shots = $gripShots.Count; body_grip_checks = $gripChecks
    check_count = ([regex]::Matches($logText, 'SOFT_BODY_QA_CHECK PASS')).Count
    log = $logPath; source = 'synthetic-soft-body-QA'; real_vr_acceptance = $false; nullrhi = $false
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $resultPath -Encoding utf8
if ($timedOut) { throw "Packaged soft body QA timed out after $TimeoutSeconds seconds. See $logPath" }
if (-not $pass) { throw "Packaged soft body QA failed. See $logPath" }
$press | ForEach-Object { Write-Output ("Press {0}: rest->pressed {1}% of frame, dent on/off {2}%" -f $_.zone, $_.press_changed_percent, $_.dent_changed_percent) }
Write-Output "KawaiiPhysics soft body QA PASS for build $($manifest.build_id) (rendered desktop test; real VR input remains unverified)"
