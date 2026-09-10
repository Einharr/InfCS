@echo off
rem Build the 32-bit XInput+DirectInput probe -> harness\xiprobe.exe
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
if errorlevel 1 ( echo vcvars32.bat not found & exit /b 1 )
pushd "%~dp0"
cl /nologo /EHsc /std:c++17 /O2 /MT /W3 /utf-8 xiprobe.cpp /Fe:xiprobe.exe /link /MACHINE:X86 dinput8.lib dxguid.lib user32.lib
set RC=%errorlevel%
del /q xiprobe.obj 2>nul
popd
exit /b %RC%
