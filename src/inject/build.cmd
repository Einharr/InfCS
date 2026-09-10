@echo off
rem Inject build: cpinject.dll with the ConsolePort module linked in (MSVC x86).
setlocal
chcp 65001 >nul
set HERE=%~dp0
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
if errorlevel 1 ( echo vcvars32.bat not found & exit /b 1 )
call "%HERE%..\consoleport\build.cmd" lib
if errorlevel 1 exit /b 1
if not exist "%HERE%out" mkdir "%HERE%out"
set CP_OUTPUT=cpinject.dll
if not "%~1"=="" set CP_OUTPUT=%~1
pushd "%HERE%out"
cl /nologo /LD /EHsc /std:c++17 /O2 /MT /utf-8 /W3 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /I"%HERE%..\consoleport\." "%HERE%cpinject.cpp" "%HERE%..\consoleport\out\consoleport.lib" /Fe:%CP_OUTPUT% /link /MACHINE:X86 user32.lib
if errorlevel 1 ( popd & exit /b 1 )
echo built: %HERE%out\%CP_OUTPUT%
popd
