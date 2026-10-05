param([string]$EngineRoot = 'E:\ue\UE_5.8', [switch]$SkipBuild, [switch]$VisualChecks)
$ErrorActionPreference = 'Stop'
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'Build-Stage1.ps1') -EngineRoot $EngineRoot -EvidenceStage '04' }
& (Join-Path $PSScriptRoot 'Test-Stage1.ps1') -EvidenceStage '04'
& (Join-Path $PSScriptRoot 'Test-CharacterProfiles.ps1')
& (Join-Path $PSScriptRoot 'Test-CharacterSoftBody.ps1')
if ($VisualChecks) {
    & (Join-Path $PSScriptRoot 'Test-CharacterPoses.ps1') -EvidenceStage '04'
    & (Join-Path $PSScriptRoot 'Test-CharacterViews.ps1') -EvidenceStage '04'
    & (Join-Path $PSScriptRoot 'Test-CharacterReactions.ps1') -EvidenceStage '04'
}
Write-Output 'Versioned desktop verification passed. Hardware VR acceptance is separate.'
