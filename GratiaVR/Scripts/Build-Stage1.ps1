param(
    [string]$EngineRoot = 'E:\ue\UE_5.8',
    [switch]$SkipEditorBuild,
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
& $pythonPath (Join-Path $PSScriptRoot 'create_build_manifest.py') --engine $EngineRoot --output $manifestPath --verify
if ($LASTEXITCODE -ne 0) { throw 'Build fingerprint changed during packaging' }
# Replace the previous package only now that this build succeeded; /MIR also removes stale files.
& robocopy.exe $stagedRoot $packageRoot /MIR /R:3 /W:2 /NFL /NDL /NJH /NJS /NP | Out-Null
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
# The staged copy is an intermediate duplicate of the package; keep only Builds\Windows.
Remove-Item -LiteralPath $stagedRoot -Recurse -Force
Write-Output "Package: $packageRoot (build $($manifest.build_id))"

