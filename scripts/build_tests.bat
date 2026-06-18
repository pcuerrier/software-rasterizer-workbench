@echo off
IF NOT EXIST .\build mkdir .\build
IF NOT EXIST .\build\tests mkdir .\build\tests
pushd .\build\tests

call "%~dp0shared_vars.bat" %1

set GTEST_INCLUDES=-I..\..\lib\googletest\googletest\include -I..\..\lib\googletest\googlemock\include
set TestIncludes=-I..\..\src\ %GTEST_INCLUDES%

REM -EHsc: exceptions enabled, required by googletest
if "%CONFIG%"=="Release" (
    set TestCompilerFlags=-diagnostics:column -std:c++20 -nologo -O2 -MD -W4 -EHsc -FC -utf-8
    set TestLinkerFlags=/LIBPATH:..\..\lib\googletest\build\lib\Release gtest.lib gtest_main.lib -incremental:no -opt:ref
) else (
    set TestCompilerFlags=-diagnostics:column -std:c++20 -nologo -Od -MDd -Z7 -W4 -EHsc -FC -utf-8 -DENGINE_DEBUG -DDEBUG
    set TestLinkerFlags=/LIBPATH:..\..\lib\googletest\build\lib\Debug gtest.lib gtest_main.lib -incremental:no -opt:ref
)

echo --- Building Tests (%CONFIG%) ---
cl %TestCompilerFlags% %TestIncludes% ..\..\tests\*.cpp -Fe:run_tests.exe /link /SUBSYSTEM:CONSOLE %TestLinkerFlags%
if %ERRORLEVEL% neq 0 (
    popd
    exit /b %ERRORLEVEL%
)

echo --- Running Tests ---
.\run_tests.exe
set TEST_RESULT=%ERRORLEVEL%

popd
exit /b %TEST_RESULT%
