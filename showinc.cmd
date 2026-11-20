@ECHO OFF
SETLOCAL EnableExtensions
SET "CL_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\HostX86\x64\CL.exe"
SET "PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\HostX86\x64;%PATH%"
SET "INCLUDE=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\include;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\winrt;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\cppwinrt"
CD /D "E:\projects\opensource\windirstat\windirstat"
cl /c /showIncludes /FoAppMigrate_manual.obj /IE:\projects\opensource\windirstat\ /IE:\projects\opensource\windirstat\windirstat\ /IE:\projects\opensource\windirstat\windirstat\Controls\ /IE:\projects\opensource\windirstat\windirstat\Dialogs\ /IE:\projects\opensource\windirstat\windirstat\Pages\ /IE:\projects\opensource\windirstat\windirstat\Views\ /IE:\projects\opensource\windirstat\windirstat\Res\ /D NDEBUG /D WINVER=_WIN32_WINNT_WIN7 /D _WIN32_WINNT=_WIN32_WINNT_WIN7 /D _UNICODE /D UNICODE /EHsc /std:c++23preview /Yu"pch.h" /Fp"E:\projects\opensource\windirstat\intermediate\x64_Release\windirstat\DiskSeek_x64.pch" /GR- /utf-8 /TP AppMigrate.cpp > "E:\projects\opensource\windirstat\showinc.txt" 2>&1
EXIT /B %ERRORLEVEL%
