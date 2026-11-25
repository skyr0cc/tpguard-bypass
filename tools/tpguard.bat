
@echo off
setlocal EnableDelayedExpansion
title skyro.wtf - Roblox Launcher

set "PLACE_ID=109983668079237"
set "URI=roblox://experiences/start?placeId=%PLACE_ID%"

:menu
cls

echo.
echo   [38;5;135m  ███████╗██╗  ██╗██╗   ██╗██████╗  ██████╗[0m
echo   [38;5;135m  ██╔════╝██║ ██╔╝╚██╗ ██╔╝██╔══██╗██╔═══██╗[0m
echo   [38;5;135m  ███████╗█████╔╝  ╚████╔╝ ██████╔╝██║   ██║[0m
echo   [38;5;135m  ╚════██║██╔═██╗   ╚██╔╝  ██╔══██╗██║   ██║[0m
echo   [38;5;135m  ███████║██║  ██╗   ██║   ██║  ██║╚██████╔╝[0m
echo   [38;5;135m  ╚══════╝╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝ ╚═════╝[0m
echo.
echo   [97mskyro[0m[38;5;135m.wtf[0m
echo   [38;5;239m--------------------------------------------[0m
echo.
echo   [38;5;245mAPPLICATION[0m
echo   [97mRoblox Server Launcher[0m
echo   [38;5;245mPLACE ID[0m   [97m%PLACE_ID%[0m
echo.
echo   [38;5;239m+------------------------------------------+[0m
echo   [38;5;239m^|[0m  [38;5;135m01[0m  [97mLaunch experience[0m                  [38;5;239m^|[0m
echo   [38;5;239m^|[0m  [38;5;135m02[0m  [38;5;245mExit[0m                                [38;5;239m^|[0m
echo   [38;5;239m+------------------------------------------+[0m
echo.

set /p "choice=  skyro ^> "

if "%choice%"=="1" start "" "%URI%" & goto menu
if "%choice%"=="01" start "" "%URI%" & goto menu
if "%choice%"=="2" exit /b
if "%choice%"=="02" exit /b

goto menu
