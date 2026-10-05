param([int]$TimeoutSeconds = 120)
$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$archiveRoot = Join-Path $workspaceRoot 'Builds\Windows'
$exePath = Join-Path $archiveRoot 'GratiaVR\Binaries\Win64\GratiaVR.exe'
$manifestPath = Join-Path $archiveRoot 'build_manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$exeHash = (Get-FileHash -LiteralPath $exePath).Hash
if ($exeHash -ne $manifest.executable_sha256) { throw 'Executable differs from manifest' }
$evidenceRoot = Join-Path $workspaceRoot 'evidence\04'
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
$results = @()
foreach ($profile in @('DA_Gratia','DA_Mannequin')) {
    $logPath = Join-Path $evidenceRoot ("packaged_profile_$profile.log")
    $arguments = @('-nohmd','-windowed','-ResX=1280','-ResY=720','-unattended','-GratiaSmokeTest','-GratiaSelfTest',
        "-GratiaCharacterProfile=/Game/Characters/Profiles/$profile", ('-abslog="' + $logPath + '"'))
    $began = [DateTimeOffset]::UtcNow
    $gameProcess = Start-Process -FilePath $exePath -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $exePath) -WindowStyle Hidden -PassThru
    if (-not $gameProcess.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $gameProcess.Id -ErrorAction SilentlyContinue
        throw "Profile test timed out: $profile"
    }
    $gameProcess.Refresh()
    $logText = Get-Content -LiteralPath $logPath -Raw
    $pass = $gameProcess.ExitCode -eq 0 -and $logText.Contains('GRATIA_STAGE1_SELFTEST_PASS') -and $logText.Contains('GRATIA_STAGE1_SMOKE_PASS') -and
        $logText.Contains('BUILD id=' + $manifest.build_id) -and -not $logText.Contains('TEST FAIL:') -and -not ($logText -match 'Failed to compile Material|Default Material will be used in game')
    $results += [pscustomobject]@{profile=$profile; build_id=$manifest.build_id; executable_sha256=$exeHash; began_utc=$began.ToString('o');
        finished_utc=[DateTimeOffset]::UtcNow.ToString('o'); exit_code=$gameProcess.ExitCode; pass=$pass; log=$logPath;
        pass_count=([regex]::Matches($logText,'TEST PASS:')).Count; skip_count=([regex]::Matches($logText,'TEST SKIP:')).Count}
    $results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'packaged_profile_results.json') -Encoding utf8
    if (-not $pass) { throw "Character profile test failed: $profile. See $logPath" }
    Write-Output "Profile $profile PASS ($($results[-1].pass_count) checks, $($results[-1].skip_count) explicit skips)"
}
