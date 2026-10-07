@echo off
setlocal
rem Regenerates the Visual Studio project files and opens GratiaVR.sln.
rem Run it again after adding or removing .cpp/.h files.
rem Usage: Open-Code.cmd [EngineRoot]   (default E:\ue\UE_5.8)
set "ENGINE=E:\ue\UE_5.8"
if not "%~1"=="" set "ENGINE=%~1"
set "PROJECT=%~dp0GratiaVR\GratiaVR.uproject"
echo Generating Visual Studio project files...
call "%ENGINE%\Engine\Build\BatchFiles\Build.bat" -projectfiles "-project=%PROJECT%" -game -rocket -progress
if errorlevel 1 (
    echo.
    echo Project file generation failed. See the messages above.
    if not defined GRATIA_NO_PAUSE pause
    exit /b 1
)
start "" "%~dp0GratiaVR\GratiaVR.sln"
exit /b 0
