@echo off
setlocal

set SRC=%~dp0
if "%SRC:~-1%"=="\" set SRC=%SRC:~0,-1%

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (
    echo [-] vswhere.exe not found. Install Visual Studio 2022 with C++ tools.
    exit /b 1
)
for /f "usebackq delims=" %%i in (
    `"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`
) do set VS_PATH=%%i
if not defined VS_PATH (
    echo [-] Visual Studio with C++ workload not found.
    exit /b 1
)
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

cl.exe /nologo /W3 /O2 /I"%SRC%" /Fe"%SRC%\client.exe" "%SRC%\main.c" ^
    /link /SUBSYSTEM:CONSOLE kernel32.lib advapi32.lib shell32.lib ntdll.lib
if errorlevel 1 (echo [-] FAIL & exit /b 1)

echo [+] client.exe
