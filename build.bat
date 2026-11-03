@echo off
setlocal enabledelayedexpansion
rem Build with the Visual Studio C++ compiler
cd /d "%~dp0"
where cl >nul 2>nul
if errorlevel 1 (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -prerelease -products * -property installationPath`) do set "VSPATH=%%i"
  call "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
)
rem word list resource (data\korean_nouns.txt)
rc /nologo /fo minigame_gui.res minigame_gui.rc
cl /nologo /EHsc /utf-8 /O2 /std:c++17 /W3 minigame_gui.cpp minigame_gui.res /Fe:minigame_gui.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED
del *.obj *.res 2>nul
