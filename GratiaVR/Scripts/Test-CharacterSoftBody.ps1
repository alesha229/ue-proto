param([ValidateRange(1, 600)][int]$TimeoutSeconds = 90, [ValidateSet('04')][string]$EvidenceStage = '04')
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
$pass = -not $timedOut -and $gameProcess.ExitCode -eq 0 -and $freshLog -and $buildMatched -and $markerPassed -and -not $failureDetected -and $materialsPassed
[pscustomobject]@{
    build_id = $manifest.build_id; executable = $exePath; executable_sha256 = $exeHash; manifest_sha256 = $manifestHash
    source_sha256 = $manifest.source_sha256; assets_sha256 = $manifest.assets_sha256
    began_utc = $began.ToString('o'); finished_utc = [DateTimeOffset]::UtcNow.ToString('o')
    exit_code = $gameProcess.ExitCode; timed_out = $timedOut; passed = $pass; fresh_log = $freshLog
    build_matched = $buildMatched; marker_passed = $markerPassed; failure_detected = $failureDetected; materials_passed = $materialsPassed
    check_count = ([regex]::Matches($logText, 'SOFT_BODY_QA_CHECK PASS')).Count
    log = $logPath; source = 'synthetic-soft-body-QA'; real_vr_acceptance = $false; nullrhi = $false
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $resultPath -Encoding utf8
if ($timedOut) { throw "Packaged soft body QA timed out after $TimeoutSeconds seconds. See $logPath" }
if (-not $pass) { throw "Packaged soft body QA failed. See $logPath" }
Write-Output "KawaiiPhysics soft body QA PASS for build $($manifest.build_id) (rendered desktop test; real VR input remains unverified)"
