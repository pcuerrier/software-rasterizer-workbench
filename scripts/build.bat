@echo off

REM Bootstrap x64 MSVC environment if not already configured.
REM VSCMD_ARG_TGT_ARCH is only set (to "x64") after vcvarsall.bat x64 runs.
REM Update this path if your VS installation is in a different location.
if not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
    REM call "D:\Microsoft\VisualStudio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
)

if /i "%~1"=="Release" (
    echo === Building Release ===
) else (
    echo === Building Debug ===
)

call "%~dp0build_app.bat" %1
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

call "%~dp0build_platform.bat" %1
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

call "%~dp0build_tests.bat" %1
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo --- Build Complete! ---
