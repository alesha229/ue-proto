param([int]$TimeoutSeconds = 120, [ValidateSet('01','02','03','04')][string]$EvidenceStage = '04')
$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exePath = Join-Path $workspaceRoot 'Builds\Stage1\Windows\GratiaVR\Binaries\Win64\GratiaVR.exe'
$evidenceRoot = Join-Path $workspaceRoot ('evidence\' + $EvidenceStage)
if (-not (Test-Path -LiteralPath $exePath)) { throw "Packaged executable missing: $exePath" }
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
$manifestPath = Join-Path $workspaceRoot 'Builds\Stage1\Windows\build_manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath)) { throw 'Versioned build manifest missing; run Build-Stage1.ps1 first' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$exeHash = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
if ($exeHash -ne $manifest.executable_sha256) { throw 'Executable differs from build manifest' }
$runs = @()
for ($run = 1; $run -le 3; $run++) {
    $logPath = Join-Path $evidenceRoot "packaged_smoke_$run.log"
    $arguments = @('-nohmd', '-windowed', '-ResX=1280', '-ResY=720', '-unattended', '-GratiaSmokeTest')
    if ($run -eq 1) { $arguments += '-GratiaSelfTest' }
    $arguments += ('-abslog="' + $logPath + '"')
    $began = [DateTimeOffset]::UtcNow
    $gameProcess = Start-Process -FilePath $exePath -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $exePath) -WindowStyle Hidden -PassThru
    if (-not $gameProcess.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $gameProcess.Id -ErrorAction SilentlyContinue
        throw "Packaged test $run timed out after $TimeoutSeconds seconds. See $logPath"
    }
    $gameProcess.Refresh()
    $logText = Get-Content -LiteralPath $logPath -Raw
    $smokePassed = $logText.Contains('GRATIA_STAGE1_SMOKE_PASS')
    $selfPassed = $run -ne 1 -or $logText.Contains('GRATIA_STAGE1_SELFTEST_PASS')
    $buildMatched = $logText.Contains('BUILD id=' + $manifest.build_id)
    $materialsPassed = -not ($logText -match 'missing usage flag SkeletalMesh|Failed to compile Material|Default Material will be used in game')
    $runs += [pscustomobject]@{
        run = $run; began_utc = $began.ToString('o'); finished_utc = [DateTimeOffset]::UtcNow.ToString('o')
        executable = $exePath; build_id = $manifest.build_id; executable_sha256 = $exeHash; manifest_sha256 = (Get-FileHash -LiteralPath $manifestPath).Hash
        exit_code = $gameProcess.ExitCode; smoke_pass = $smokePassed; selftest_pass = $selfPassed; materials_pass = $materialsPassed; build_matched = $buildMatched
        check_count = ([regex]::Matches($logText, 'TEST PASS:')).Count; log = $logPath
    }
    $runs | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'packaged_test_results.json') -Encoding utf8
    if ($gameProcess.ExitCode -ne 0 -or -not $smokePassed -or -not $selfPassed -or -not $materialsPassed -or -not $buildMatched -or $logText.Contains('TEST FAIL:')) {
        throw "Packaged test $run failed. See $logPath"
    }
    Write-Output "Packaged run $run passed; checks: $($runs[-1].check_count)"
}
Write-Output 'Three desktop runs passed. Headset tracking, controller alignment and VR frame delivery remain untested.'
