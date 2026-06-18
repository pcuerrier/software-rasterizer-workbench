@echo off
IF NOT EXIST .\build mkdir .\build
pushd .\build

call "%~dp0shared_vars.bat" %1

echo --- Building Platform (%CONFIG%) ---
if "%CONFIG%"=="Release" (
    cl %CommonCompilerFlags% ..\src\main.cpp -Fe:main.exe -Fmwin32.map /link /SUBSYSTEM:windows %CommonLinkerFlags%
) else (
    cl %CommonCompilerFlags% ..\src\main.cpp -Fe:main_debug.exe -Fmwin32.map /link /SUBSYSTEM:windows %CommonLinkerFlags%
)

REM Copy the SDL3.dll to the build directory so the game can find it when it runs
copy ..\lib\SDL\build\%CONFIG%\SDL3.dll .\ > NUL 2>&1

popd
