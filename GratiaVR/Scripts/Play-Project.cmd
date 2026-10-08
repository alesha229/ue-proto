@echo off
setlocal
rem Runs the game straight from the project, without packaging: the editor binaries start it with -game and
rem read the uncooked content. C++ changes are compiled first (incremental: seconds when nothing changed).
rem Usage: Play-Project.cmd [vr|desktop] [extra game arguments]   (engine: GRATIA_ENGINE or E:\ue\UE_5.8)
set "ENGINE=E:\ue\UE_5.8"
if defined GRATIA_ENGINE set "ENGINE=%GRATIA_ENGINE%"
for %%I in ("%~dp0..\GratiaVR.uproject") do set "PROJECT=%%~fI"
set "MODE=vr"
if /I "%~1"=="desktop" (set "MODE=desktop" & shift) else if /I "%~1"=="vr" shift
set "EXTRA="
:collect
if "%~1"=="" goto collected
set "EXTRA=%EXTRA% %1"
shift
goto collect
:collected
echo GratiaVR from the project (no build), mode: %MODE%
echo Project: "%PROJECT%"
echo Engine:  "%ENGINE%"
if not exist "%ENGINE%\Engine\Binaries\Win64\UnrealEditor.exe" (
    echo Unreal Engine 5.8 is not found. Set GRATIA_ENGINE to its folder.
    if not defined GRATIA_NO_PAUSE pause
    exit /b 1
)
tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>nul | find /I "UnrealEditor.exe" >nul
if errorlevel 1 (
    echo Compiling C++ ^(GratiaVREditor, incremental^)...
    call "%ENGINE%\Engine\Build\BatchFiles\Build.bat" GratiaVREditor Win64 Development "-Project=%PROJECT%" -WaitMutex -NoHotReloadFromIDE
    if errorlevel 1 (
        echo.
        echo C++ compile failed; the game was not started. See the errors above.
        if not defined GRATIA_NO_PAUSE pause
        exit /b 1
    )
) else (
    echo Unreal Editor is open: C++ is not recompiled ^(use Live Coding in the editor^).
)
if /I "%MODE%"=="vr" (
    echo Start SteamVR and connect the headset and both controllers before launching.
    start "" "%ENGINE%\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJECT%" -game -vr -windowed -ResX=1280 -ResY=720 -log %EXTRA%
) else (
    start "" "%ENGINE%\Engine\Binaries\Win64\UnrealEditor.exe" "%PROJECT%" -game -nohmd -windowed -ResX=1600 -ResY=900 -log %EXTRA%
)
exit /b 0
