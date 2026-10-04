param([int]$TimeoutSeconds = 120, [ValidateSet('04')][string]$EvidenceStage = '04')
$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exePath = Join-Path $workspaceRoot 'Builds\Stage1\Windows\GratiaVR\Binaries\Win64\GratiaVR.exe'
$evidenceRoot = Join-Path $workspaceRoot ('evidence\' + $EvidenceStage)
$shotRoot = Join-Path $workspaceRoot 'Builds\Stage1\Windows\GratiaVR\Saved\Screenshots\Windows'
$manifest = Get-Content -LiteralPath (Join-Path $workspaceRoot 'Builds\Stage1\Windows\build_manifest.json') -Raw | ConvertFrom-Json
$exeHash = (Get-FileHash -LiteralPath $exePath).Hash
if ($exeHash -ne $manifest.executable_sha256) { throw 'Executable differs from manifest' }
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
$fixtures = @(
    @{cue='Face'; zone='Face'; corrective=''},
    @{cue='Hand'; zone='Left hand'; corrective='Game_HandRef_'},
    @{cue='Cheer'; zone='Upper costume'; corrective='Game_CheerRef_'}
)
$results = @()
foreach ($fixture in $fixtures) {
    $assetName = 'A_Gratia_Game_React' + $fixture.cue
    $clipPath = '/Game/Gratia/GameRig/' + $assetName + '.' + $assetName
    foreach ($view in @('Front','Face')) {
        $runName = $fixture.cue + '_' + $view
        $logPath = Join-Path $evidenceRoot ("packaged_reaction_$runName.log")
        $began = [DateTimeOffset]::UtcNow
        $arguments = @('-nohmd','-windowed','-ResX=1280','-ResY=720','-unattended',
            ('-GratiaReactionZone="' + $fixture.zone + '"'),
            ('-GratiaExpectedReactionClip=' + $clipPath),
            ("-GratiaViewTest=$view"), ('-abslog="' + $logPath + '"'))
        if ($fixture.corrective) { $arguments += '-GratiaCorrectivePrefix=' + $fixture.corrective }
        $gameProcess = Start-Process -FilePath $exePath -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $exePath) -WindowStyle Hidden -PassThru
        if (-not $gameProcess.WaitForExit($TimeoutSeconds * 1000)) {
            Stop-Process -Id $gameProcess.Id -ErrorAction SilentlyContinue
            throw "Reaction resource QA $runName timed out. See $logPath"
        }
        $gameProcess.Refresh()
        $logText = Get-Content -LiteralPath $logPath -Raw
        $pass = $gameProcess.ExitCode -eq 0 -and $logText.Contains('GRATIA_REACTION_QA_PASS') -and -not $logText.Contains('TEST FAIL:')
        $pass = $pass -and $logText.Contains('BUILD id=' + $manifest.build_id)
        $pass = $pass -and $logText.Contains('REACTION QA SELECTED:') -and $logText.Contains('clip=' + $clipPath)
        $pass = $pass -and $logText.Contains('source=synthetic-resource-QA') -and $logText.Contains('REACTION QA CAPTURE:')
        $pass = $pass -and -not ($logText -match 'missing usage flag SkeletalMesh|Failed to compile Material|Default Material will be used in game')
        $shot = Get-ChildItem -LiteralPath $shotRoot -Filter 'GratiaReactionQA*.png' | Where-Object { $_.LastWriteTimeUtc -ge $began.UtcDateTime } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
        $shotPath = $null
        if ($shot) {
            $shotPath = Join-Path $evidenceRoot ("packaged_reaction_$runName.png")
            Copy-Item -LiteralPath $shot.FullName -Destination $shotPath -Force
        } else { $pass = $false }
        $results += [pscustomobject]@{
            cue=$fixture.cue; zone=$fixture.zone; view=$view; expected_clip=$clipPath;
            corrective_prefix=$fixture.corrective; source='synthetic-resource-QA';
            build_id=$manifest.build_id; executable_sha256=$exeHash; passed=$pass;
            screenshot=$shotPath; log=$logPath; real_vr_acceptance=$false
        }
        $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'packaged_reaction_results.json') -Encoding utf8
        if (-not $pass) { throw "Reaction resource QA $runName failed. See $logPath" }
        Write-Output "Reaction resource QA $runName passed (synthetic desktop resource test)"
    }
}
