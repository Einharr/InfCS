@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cl /nologo /EHsc /std:c++17 /MT /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Fe:swg\hook\inject\out\diagnose_hid.exe /Fo:swg\hook\inject\out\ swg\hook\inject\diagnose_hid.cpp swg\hook\consoleport\input\dualsense.cpp /link /MACHINE:X86
