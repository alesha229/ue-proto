@echo off
rem The game in the headset straight from the project, without Build.cmd (see GratiaVR\Scripts\Play-Project.cmd).
call "%~dp0GratiaVR\Scripts\Play-Project.cmd" vr %*
exit /b %ERRORLEVEL%
