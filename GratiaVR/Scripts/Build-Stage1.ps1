param(
    [string]$EngineRoot = 'E:\ue\UE_5.8',
    [switch]$SkipEditorBuild,
    # Re-run the lobby/scene generator (setup_scene_experience.py) before packaging.
    [switch]$RegenerateScenes,
    [ValidateSet('01','02','03','04')][string]$EvidenceStage = '04'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'GratiaVR.uproject'
$evidenceRoot = Join-Path (Split-Path -Parent $projectRoot) ('evidence\' + $EvidenceStage)
# The one runnable package: Builds\Windows next to Build.cmd / Start-VR.cmd.
$packageRoot = Join-Path (Split-Path -Parent $projectRoot) 'Builds\Windows'
$stagedRoot = Join-Path $projectRoot 'Saved\StagedBuilds\Windows'
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
$pythonPath = Join-Path $EngineRoot 'Engine\Binaries\ThirdParty\Python3\Win64\python.exe'
$manifestPath = Join-Path $projectRoot 'Saved\Build\build_manifest.json'
& $pythonPath (Join-Path $PSScriptRoot 'create_build_manifest.py') --engine $EngineRoot --output $manifestPath
if ($LASTEXITCODE -ne 0) { throw 'Build stamp failed' }
if (-not $SkipEditorBuild) {
    & (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') GratiaVREditor Win64 Development "-Project=$projectFile" -WaitMutex -NoHotReloadFromIDE 2>&1 | Tee-Object -FilePath (Join-Path $evidenceRoot 'editor_build.log')
    if ($LASTEXITCODE -ne 0) { throw "Editor build failed ($LASTEXITCODE)" }
}
# The lobby/scene generator rewrites what it made (Experience_* actors, materials, DA_SceneLibrary),
# so it runs only on request (or when the library is missing): hand edits in the editor survive builds.
# With the switch it keeps existing assets byte-for-byte when its authoring signature matches.
$sceneLibrary = Join-Path $projectRoot 'Content\Gratia\Experience\DA_SceneLibrary.uasset'
if ($RegenerateScenes -or -not (Test-Path -LiteralPath $sceneLibrary)) {
    $editorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    # Fab environments (packs from the Epic Games Launcher, see docs/EXPERIENCE.md), then their baked light.
    $fabScript = (Join-Path $PSScriptRoot 'setup_fab_environments.py').Replace('\','/')
    & $editorExe $projectFile '-run=pythonscript' "-script=$fabScript" '-unattended' '-nop4' '-nosplash' '-NullRHI' '-nohmd' '-ddc=InstalledNoZenLocalFallback' '-SkipZenStore' 2>&1 | Tee-Object -FilePath (Join-Path $evidenceRoot 'fab_environments.log')
    if ($LASTEXITCODE -ne 0) { throw "Fab environment preparation failed ($LASTEXITCODE)" }
    $fabReport = Get-Content -LiteralPath (Join-Path (Split-Path -Parent $projectRoot) 'evidence\experience\fab_environments.json') -Raw | ConvertFrom-Json
    foreach ($bake in $fabReport.bake) {
        $bakeMap = $bake.map
        $mapName = Split-Path -Leaf $bakeMap
        & $editorExe $projectFile '-run=ResavePackages' '-BuildLighting' "-Quality=$($bake.quality)" '-AllowCommandletRendering' "-Map=$mapName" '-unattended' '-nop4' '-nosplash' '-nohmd' '-ddc=InstalledNoZenLocalFallback' '-SkipZenStore' 2>&1 | Tee-Object -FilePath (Join-Path $evidenceRoot "fab_lighting_$mapName.log")
        if ($LASTEXITCODE -ne 0) { throw "Light bake failed for $bakeMap ($LASTEXITCODE)" }
        if (Select-String -Path (Join-Path $evidenceRoot "fab_lighting_$mapName.log") -Pattern 'Skipping Lighting Build' -Quiet) { throw "Light bake was skipped for $bakeMap" }
        $stampRoot = Join-Path $projectRoot 'Saved\FabBake'
        New-Item -ItemType Directory -Path $stampRoot -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $stampRoot "$mapName.stamp") -Value ([DateTimeOffset]::UtcNow.ToString('o')) -Encoding utf8
    }
    $experienceScript = (Join-Path $PSScriptRoot 'setup_scene_experience.py').Replace('\','/')
    & $editorExe $projectFile '-run=pythonscript' "-script=$experienceScript" '-unattended' '-nop4' '-nosplash' '-NullRHI' '-nohmd' '-ddc=InstalledNoZenLocalFallback' '-SkipZenStore' 2>&1 | Tee-Object -FilePath (Join-Path $evidenceRoot 'experience_assets.log')
    if ($LASTEXITCODE -ne 0) { throw "Scene asset preparation failed ($LASTEXITCODE)" }
}
# The final stamp includes any assets authored above, before the game target compiles.
& $pythonPath (Join-Path $PSScriptRoot 'create_build_manifest.py') --engine $EngineRoot --output $manifestPath
if ($LASTEXITCODE -ne 0) { throw 'Final build stamp failed' }
$uatArguments = @(
    'BuildCookRun', "-project=$projectFile", '-platform=Win64',
    '-clientconfig=Development', '-build', '-cook',
    '-map=/Game/Gratia/Maps/L_Stage1', '-stage', '-pak', '-iostore',
    '-package',
    '-nop4', '-unattended', '-utf8output', '-NoCompileEditor', '-SkipBuildEditor',
    '-AdditionalCookerOptions=-nohmd -ddc=InstalledNoZenLocalFallback -SkipZenStore'
)
& (Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat') @uatArguments 2>&1 | Tee-Object -FilePath (Join-Path $evidenceRoot 'package.log')
if ($LASTEXITCODE -ne 0) { throw "Packaging failed ($LASTEXITCODE)" }
$cookLogText = Get-Content -LiteralPath (Join-Path $evidenceRoot 'package.log') -Raw
if ($cookLogText -match 'Failed to compile Material|Default Material will be used in game|doesn.t have a valid ShaderMap') {
    throw 'Cooked material compilation failed; previous runnable package was retained. See package.log'
}
& $pythonPath (Join-Path $PSScriptRoot 'create_build_manifest.py') --engine $EngineRoot --output $manifestPath --verify
if ($LASTEXITCODE -ne 0) { throw 'Build fingerprint changed during packaging' }
# Replace the previous package only now that this build succeeded; /MIR also removes stale files.
# A running game locks the package: stop before touching it (the staged build stays for a rerun).
if (Get-Process -Name GratiaVR -ErrorAction SilentlyContinue) { throw "The game is running from $packageRoot; close it and build again" }
# Player logs and screenshots in the package (GratiaVR\Saved) are VR evidence: never mirrored away.
& robocopy.exe $stagedRoot $packageRoot /MIR /XD (Join-Path $packageRoot 'GratiaVR\Saved') /R:3 /W:2 /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Copy to $packageRoot failed (robocopy $LASTEXITCODE); close the running game and build again" }
$packageFiles = @()
foreach ($stagedFile in Get-ChildItem -LiteralPath $stagedRoot -File -Recurse) {
    $relative = $stagedFile.FullName.Substring($stagedRoot.Length + 1)
    $packagedFile = Join-Path $packageRoot $relative
    if (-not (Test-Path -LiteralPath $packagedFile -PathType Leaf)) { throw "Package file missing: $relative" }
    $stagedHash = (Get-FileHash -LiteralPath $stagedFile.FullName -Algorithm SHA256).Hash
    if ((Get-FileHash -LiteralPath $packagedFile -Algorithm SHA256).Hash -ne $stagedHash) { throw "Package file differs from staged build: $relative" }
    $packageFiles += [pscustomobject]@{ path = $relative; sha256 = $stagedHash; bytes = $stagedFile.Length }
}
$exePath = Join-Path $packageRoot 'GratiaVR\Binaries\Win64\GratiaVR.exe'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$manifest | Add-Member -NotePropertyName executable_sha256 -NotePropertyValue (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
$manifest | Add-Member -NotePropertyName packaged_utc -NotePropertyValue ([DateTimeOffset]::UtcNow.ToString('o'))
$manifest | Add-Member -NotePropertyName package_files -NotePropertyValue $packageFiles
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $packageRoot 'build_manifest.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $packageRoot 'build_manifest.json') -Destination (Join-Path $evidenceRoot 'build_manifest.json')
# The staged copy and the cooked content are intermediate duplicates of the package; keep only Builds\Windows.
$resolvedStage = [IO.Path]::GetFullPath($stagedRoot)
$allowedStage = [IO.Path]::GetFullPath((Join-Path $projectRoot 'Saved\StagedBuilds')) + [IO.Path]::DirectorySeparatorChar
if (-not $resolvedStage.StartsWith($allowedStage, [StringComparison]::OrdinalIgnoreCase)) { throw 'Refusing cleanup outside Saved/StagedBuilds' }
Remove-Item -LiteralPath $resolvedStage -Recurse -Force
$cookedRoot = Join-Path $projectRoot 'Saved\Cooked'
if (Test-Path -LiteralPath $cookedRoot) { Remove-Item -LiteralPath $cookedRoot -Recurse -Force }
Write-Output "Package: $packageRoot (build $($manifest.build_id))"
exit 0

