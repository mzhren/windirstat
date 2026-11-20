@ECHO OFF
SETLOCAL EnableExtensions
CALL "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo
IF ERRORLEVEL 1 EXIT /B 1
CD /D "E:\projects\opensource\windirstat\windirstat"
cl /nologo /c /EHsc /std:c++23preview /GR- /utf-8 /D NDEBUG /D WINVER=_WIN32_WINNT_WIN7 /D _WIN32_WINNT=_WIN32_WINNT_WIN7 /D _UNICODE /D UNICODE /D GIT_COUNT=2214 /D PRODUCTION=0 AppMigrate_i.cpp > "E:\projects\opensource\windirstat\i_compile.log" 2>&1
EXIT /B %ERRORLEVEL%
