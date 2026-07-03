REM Call with "Release" as the first argument for an optimized build. Defaults to Debug.
set INCLUDES=-I..\src\ -I..\lib\SDL\include\ -I..\lib\spdlog\include\

if /i "%~1"=="Release" (
    set CONFIG=Release
    set CommonCompilerFlags=-diagnostics:column -std:c++20 -WL -O2 -nologo -fp:fast -fp:except- -Gm- -GR- -EHsc- -Oi -WX -Wall -FC -GS- -Gs9999999 -utf-8 %INCLUDES%
    set CommonLinkerFlags=-STACK:0x100000,0x100000 /LIBPATH:..\lib\SDL\build\Release\ /LIBPATH:..\lib\SDL\build\ /LIBPATH:..\lib\spdlog\build\Release\ -incremental:no -opt:ref user32.lib gdi32.lib winmm.lib kernel32.lib opengl32.lib SDL3.lib
) else (
    set CONFIG=Debug
    set CommonCompilerFlags=-DDEBUG -diagnostics:column -std:c++20 -WL -Od -nologo -fp:fast -fp:except- -Gm- -GR- -EHsc- -Zo -Oi -WX -Wall -FC -Z7 -GS- -Gs9999999 -utf-8 %INCLUDES%
    set CommonLinkerFlags=-STACK:0x100000,0x100000 /LIBPATH:..\lib\SDL\build\Debug\ /LIBPATH:..\lib\SDL\build\ /LIBPATH:..\lib\spdlog\build\Debug\ -incremental:no -opt:ref user32.lib gdi32.lib winmm.lib kernel32.lib opengl32.lib SDL3.lib
)
