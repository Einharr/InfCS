@echo off
rem ConsolePort module build, MSVC x86 (client swgemu.exe is i386).
rem   build.cmd           -> out\cp_tests.exe (CP_MOCK_BINARY) and run tests
rem   build.cmd lib       -> out\consoleport.lib (real calls into the binary, for QoL DLL)
rem   build.cmd test name -> run only tests whose name contains "name"
setlocal
chcp 65001 >nul
set HERE=%~dp0
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
if errorlevel 1 ( echo vcvars32.bat not found & exit /b 1 )
if not exist "%HERE%out" mkdir "%HERE%out"
pushd "%HERE%out"
set CFLAGS=/nologo /EHsc /std:c++17 /W4 /wd4100 /wd4505 /O2 /MT /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /I"%HERE%."
set CORE="%HERE%core\binary.cpp" "%HERE%core\node.cpp" "%HERE%core\ui.cpp" "%HERE%core\theme.cpp" "%HERE%core\runtime.cpp" "%HERE%core\hooks.cpp"
set MODS=
for %%d in (input crossbar cursor windows world) do if exist "%HERE%%%d\*.cpp" call set MODS=%%MODS%% "%HERE%%%d\*.cpp"
if "%~1"=="lib" (
  del /q *.obj 2>nul
  cl %CFLAGS% /c %CORE% %MODS%
  if errorlevel 1 ( popd & exit /b 1 )
  lib /nologo /OUT:consoleport.lib *.obj
  if errorlevel 1 ( popd & exit /b 1 )
  echo built: %HERE%out\consoleport.lib
  popd & exit /b 0
)
set TESTS="%HERE%tests\*.cpp"
cl %CFLAGS% /DCP_MOCK_BINARY %CORE% %MODS% %TESTS% /Fe:cp_tests.exe /link /MACHINE:X86
if errorlevel 1 ( popd & exit /b 1 )
if "%~1"=="test" ( "%HERE%out\cp_tests.exe" %2 ) else ( "%HERE%out\cp_tests.exe" )
set RC=%errorlevel%
popd
exit /b %RC%
