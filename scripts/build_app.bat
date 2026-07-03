@echo off
IF NOT EXIST .\build mkdir .\build
pushd .\build

call "%~dp0shared_vars.bat" %1

echo --- Cleaning up old PDBs ---
del app_*.pdb > NUL 2>&1

echo --- Building Application DLL (%CONFIG%) ---
if "%CONFIG%"=="Release" (
    cl %CommonCompilerFlags% -MT ..\src\app.cpp -Fmapp.map -LD /link -incremental:no -opt:ref -EXPORT:AppInit -EXPORT:AppUpdate
) else (
    cl %CommonCompilerFlags% -MTd ..\src\app.cpp -Fmapp.map -LD /link -incremental:no -opt:ref -PDB:app_%RANDOM%.pdb -EXPORT:AppInit -EXPORT:AppUpdate
)

popd
