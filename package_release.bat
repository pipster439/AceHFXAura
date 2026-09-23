@rem WinUI ZIP by default; --legacy explicitly selects the old C++ launcher.
@echo off
setlocal
echo =========================================================
echo  ROG Falchion Ace HFX - Aura 独立发布包自动化构建
echo =========================================================
python "%~dp0tools\package_release.py" %*
set "AURA_PACKAGE_EXIT=%ERRORLEVEL%"
if %AURA_PACKAGE_EXIT% EQU 0 (
    echo.
    echo [OK] 发布产物构建完成，请查看 dist/ 目录。
) else (
    echo.
    echo [ERROR] 构建过程出现异常，请检查上述错误信息。
)
endlocal & exit /b %AURA_PACKAGE_EXIT%
