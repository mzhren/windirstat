@ECHO OFF
SETLOCAL EnableExtensions
CALL "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo
IF ERRORLEVEL 1 EXIT /B 1
msbuild "E:\projects\opensource\windirstat\windirstat.sln" /p:Configuration=Release /p:Platform=x64 /t:Build /m /v:m /nologo "/p:WslLLVMDirectory=C:/Program Files/LLVM/bin/" /p:PlatformToolset=v143
EXIT /B %ERRORLEVEL%
