@echo off
setlocal
cd /d "%~dp0"

if not exist build mkdir build
if not exist build\assets mkdir build\assets

set IMGUI=vendor\imgui
cl /nologo /std:c++17 /W4 /permissive- /DUNICODE /D_UNICODE /EHsc /O2 /MT /I%IMGUI% ^
   src\skyro_wtf.cpp ^
   %IMGUI%\imgui.cpp ^
   %IMGUI%\imgui_draw.cpp ^
   %IMGUI%\imgui_tables.cpp ^
   %IMGUI%\imgui_widgets.cpp ^
   %IMGUI%\backends\imgui_impl_win32.cpp ^
   %IMGUI%\backends\imgui_impl_dx11.cpp ^
   /Fe:build\skyro_wtf.exe ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib gdiplus.lib d3d11.lib dxgi.lib d3dcompiler.lib dwmapi.lib
if errorlevel 1 exit /b 1

xcopy /E /I /Y assets build\assets >nul
echo built build\skyro_wtf.exe
