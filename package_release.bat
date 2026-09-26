@echo off
setlocal
where pwsh >nul 2>nul
if %ERRORLEVEL% equ 0 (
    pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_release.ps1" %*
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_release.ps1" %*
)
set "AURA_PACKAGE_EXIT=%ERRORLEVEL%"
endlocal & exit /b %AURA_PACKAGE_EXIT%
