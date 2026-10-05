@echo off
setlocal
echo GratiaVR Windows build
echo Project: "%~dp0GratiaVR\GratiaVR.uproject"
echo Package: "%~dp0Builds\Windows"
echo Close the running game and Unreal Editor before rebuilding.
echo.
pushd "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0GratiaVR\Scripts\Build-Stage1.ps1" %*
set "GRATIA_RESULT=%ERRORLEVEL%"
popd
echo.
if "%GRATIA_RESULT%"=="0" (
    echo Build completed. Run Start-VR.cmd or Start-Desktop.cmd.
) else (
    echo Build failed with exit code %GRATIA_RESULT%. See evidence\04\editor_build.log and package.log.
)
if not defined GRATIA_NO_PAUSE pause
exit /b %GRATIA_RESULT%
