@echo off
rem The game on the monitor straight from the project, without Build.cmd (see GratiaVR\Scripts\Play-Project.cmd).
call "%~dp0GratiaVR\Scripts\Play-Project.cmd" desktop %*
exit /b %ERRORLEVEL%
