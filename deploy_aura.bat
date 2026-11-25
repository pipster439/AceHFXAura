@echo off
setlocal
set "AURA_PS_CMD=powershell"
where pwsh >nul 2>nul
if %ERRORLEVEL% equ 0 (
    set "AURA_PS_CMD=pwsh"
)
%AURA_PS_CMD% -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy_aura.ps1" %*
set "AURA_DEPLOY_EXIT=%ERRORLEVEL%"
if "%~1"=="" (
    if /I not "%AURA_NO_PAUSE%"=="1" (
        echo.
        pause
    )
)
endlocal & exit /b %AURA_DEPLOY_EXIT%
