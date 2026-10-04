@echo off
set "GRATIA_EXE=%~dp0..\..\Builds\Stage1\Windows\GratiaVR\Binaries\Win64\GratiaVR.exe"
if not exist "%GRATIA_EXE%" (
    echo Stage 1 build is missing. Run Build-Stage1.ps1 first.
    pause
    exit /b 1
)
echo Start SteamVR and connect the headset and both controllers before launching.
start "" "%GRATIA_EXE%" -vr -windowed -ResX=1280 -ResY=720
