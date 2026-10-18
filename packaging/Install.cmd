@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install.ps1" -PackagePath "%~dp0__PACKAGE__" -TrustCertificate
set "setupResult=%ERRORLEVEL%"
if not "%setupResult%"=="0" echo Setup did not complete. Read the message above.
pause
exit /b %setupResult%
