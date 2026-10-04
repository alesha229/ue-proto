param(
    [string]$EngineRoot = 'E:\ue\UE_5.8',
    [switch]$SkipEditorBuild
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'GratiaVR.uproject'
$evidenceRoot = Join-Path (Split-Path -Parent $projectRoot) 'evidence\01'
$archiveRoot = Join-Path (Split-Path -Parent $projectRoot) 'Builds\Stage1'
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
if (-not $SkipEditorBuild) {
    & (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') GratiaVREditor Win64 Development "-Project=$projectFile" -WaitMutex -NoHotReloadFromIDE 2>&1 | Tee-Object -FilePath (Join-Path $evidenceRoot 'editor_build.log')
    if ($LASTEXITCODE -ne 0) { throw "Editor build failed ($LASTEXITCODE)" }
}
$uatArguments = @(
    'BuildCookRun', "-project=$projectFile", '-platform=Win64',
    '-clientconfig=Development', '-build', '-cook',
    '-map=/Game/Gratia/Maps/L_Stage1', '-stage', '-pak', '-iostore',
    '-package', '-archive', "-archivedirectory=$archiveRoot",
    '-nop4', '-unattended', '-utf8output', '-NoCompileEditor', '-SkipBuildEditor',
    '-AdditionalCookerOptions=-nohmd -ddc=InstalledNoZenLocalFallback -SkipZenStore'
)
& (Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat') @uatArguments 2>&1 | Tee-Object -FilePath (Join-Path $evidenceRoot 'package.log')
if ($LASTEXITCODE -ne 0) { throw "Packaging failed ($LASTEXITCODE)" }
Write-Output "Stage 1 archive: $archiveRoot"

