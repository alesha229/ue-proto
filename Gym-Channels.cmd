@echo off
rem Channel gym: shoots every channel case, then re-shoots on each change of GratiaVR\Saved\ChannelGym\gym.json (docs\CHANNEL_GYM.md).
call "%~dp0GratiaVR\Scripts\Play-Project.cmd" desktop -GratiaChannelShots -GratiaChannelGym %*
exit /b %ERRORLEVEL%
