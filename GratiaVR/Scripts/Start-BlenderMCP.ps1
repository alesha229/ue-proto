$ErrorActionPreference = 'Stop'
$taskWorkspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$taskEvidence = Join-Path $taskWorkspace 'evidence\03\blender_mcp'
New-Item -ItemType Directory -Path $taskEvidence -Force | Out-Null
$taskCopy = Join-Path $taskWorkspace 'Exports\Gratia\Gratia_mvp.blend'
if (-not (Test-Path -LiteralPath $taskCopy)) {
    Copy-Item -LiteralPath (Join-Path $taskWorkspace 'Gratia_working.blend') -Destination $taskCopy
}
$taskBridge = Get-NetTCPConnection -State Listen -LocalPort 9876 -ErrorAction SilentlyContinue
if (-not $taskBridge) {
    $taskArgs = '--background "' + $taskCopy.Replace('\','/') + '" --online-mode --disable-autoexec --command blender_mcp --host 127.0.0.1 --port 9876'
    Start-Process -FilePath 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' -ArgumentList $taskArgs -WorkingDirectory $taskWorkspace -WindowStyle Hidden -RedirectStandardOutput (Join-Path $taskEvidence 'bridge.log') -RedirectStandardError (Join-Path $taskEvidence 'bridge_stderr.log') | Out-Null
    $taskDeadline = (Get-Date).AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 500
        $taskBridge = Get-NetTCPConnection -State Listen -LocalPort 9876 -ErrorAction SilentlyContinue
    } until ($taskBridge -or (Get-Date) -gt $taskDeadline)
    if (-not $taskBridge) { throw 'Blender MCP bridge did not listen on 9876; inspect bridge logs.' }
}
$taskServer = Get-NetTCPConnection -State Listen -LocalPort 8100 -ErrorAction SilentlyContinue
if (-not $taskServer) {
    $taskUv = (Get-Command uv.exe -ErrorAction Stop).Source
    $taskPackage = Join-Path $taskWorkspace 'evidence\00\blender_mcp\blender_mcp_src\mcp'
    if (-not (Test-Path -LiteralPath $taskPackage)) {
        $taskPackage = 'git+https://projects.blender.org/lab/blender_mcp.git@2cea8d566dde07fbac28a61d698909d69724e853#subdirectory=mcp'
    }
    $taskServerArgs = 'tool run --from "' + $taskPackage.Replace('\','/') + '" blender-mcp --transport http --host 127.0.0.1 --port 8100'
    Start-Process -FilePath $taskUv -ArgumentList $taskServerArgs -WorkingDirectory $taskWorkspace -WindowStyle Hidden -RedirectStandardOutput (Join-Path $taskEvidence 'server.log') -RedirectStandardError (Join-Path $taskEvidence 'server_stderr.log') | Out-Null
}
Write-Output 'Blender Lab MCP uses Gratia_mvp.blend: http://127.0.0.1:8100/ (bridge 127.0.0.1:9876).'
