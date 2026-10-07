@echo off
setlocal
rem Compiles the C++ editor module and opens GratiaVR in Unreal Editor.
rem Usage: Open-Editor.cmd [EngineRoot]   (default E:\ue\UE_5.8)
set "ENGINE=E:\ue\UE_5.8"
if not "%~1"=="" set "ENGINE=%~1"
set "PROJECT=%~dp0GratiaVR\GratiaVR.uproject"
echo GratiaVR editor
echo Project: "%PROJECT%"
echo Engine:  "%ENGINE%"
echo.
tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>nul | find /I "UnrealEditor.exe" >nul
if not errorlevel 1 (
    echo Unreal Editor is already running. Close it to recompile C++, or keep working in it.
    if not defined GRATIA_NO_PAUSE pause
    exit /b 1
)
echo Compiling C++ ^(GratiaVREditor^)...
call "%ENGINE%\Engine\Build\BatchFiles\Build.bat" GratiaVREditor Win64 Development "-Project=%PROJECT%" -WaitMutex -NoHotReloadFromIDE
if errorlevel 1 (
    echo.
    echo C++ compile failed; the editor was not opened. See the errors above.
    if not defined GRATIA_NO_PAUSE pause
    exit /b 1
)
start "" "%ENGINE%\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJECT%"
exit /b 0
