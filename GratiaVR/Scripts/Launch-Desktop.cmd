@echo off
setlocal
for %%I in ("%~dp0..\..\Builds\Windows") do set "GRATIA_PACKAGE=%%~fI"
set "GRATIA_EXE=%GRATIA_PACKAGE%\GratiaVR\Binaries\Win64\GratiaVR.exe"
set "GRATIA_MANIFEST=%GRATIA_PACKAGE%\build_manifest.json"
if not exist "%GRATIA_EXE%" (
    echo Windows build is missing. Run Build.cmd from the project root.
    if not defined GRATIA_NO_PAUSE pause
    exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "try { $manifestInfo = Get-Content -LiteralPath $env:GRATIA_MANIFEST -Raw | ConvertFrom-Json; if ([string]::IsNullOrWhiteSpace($manifestInfo.build_id)) { throw 'Build manifest has no build id' }; $runtimeHash = (Get-FileHash -LiteralPath $env:GRATIA_EXE -Algorithm SHA256).Hash; if ($runtimeHash -ne $manifestInfo.executable_sha256) { throw 'Executable differs from build manifest. Run Build.cmd again' }; Write-Host ('Build id: ' + $manifestInfo.build_id); Write-Host ('Package: ' + $env:GRATIA_PACKAGE); exit 0 } catch { Write-Host ('Cannot launch: ' + $_.Exception.Message); exit 1 }"
if errorlevel 1 (
    if not defined GRATIA_NO_PAUSE pause
    exit /b 1
)
start "" /D "%GRATIA_PACKAGE%" "%GRATIA_EXE%" -nohmd -windowed -ResX=1280 -ResY=720 %*
exit /b %ERRORLEVEL%
