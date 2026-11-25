@echo off
setlocal
cd /d "%~dp0"

if not exist build mkdir build
if not exist build\assets mkdir build\assets

cl /nologo /std:c++17 /W4 /permissive- /DUNICODE /D_UNICODE /EHsc /O2 ^
   src\skyro_wtf.cpp /Fe:build\skyro_wtf.exe ^
   /link /SUBSYSTEM:WINDOWS ws2_32.lib user32.lib gdi32.lib shell32.lib gdiplus.lib
if errorlevel 1 exit /b 1

xcopy /E /I /Y assets build\assets >nul
echo built build\skyro_wtf.exe
