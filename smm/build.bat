@echo off
setlocal

set SRC=%~dp0
if "%SRC:~-1%"=="\" set SRC=%SRC:~0,-1%
set EDK=%SRC%\..\edk2

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (
    echo vswhere.exe not found. Install Visual Studio 2022 with C++ tools.
    exit /b 1
)
for /f "usebackq delims=" %%i in (
    `"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`
) do set VS_PATH=%%i
if not defined VS_PATH (
    echo Visual Studio with C++ workload not found.
    exit /b 1
)
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

set CL_FLAGS=/nologo /c /GS- /Gy /W3 /O1 /Oi- ^
    /I"%EDK%\MdePkg\Include\X64" ^
    /I"%EDK%\MdePkg\Include" ^
    /I"%EDK%\ShellPkg\Include" ^
    /I"%SRC%" ^
    /D_AMD64_

echo main.c
cl.exe %CL_FLAGS% /Fo"%SRC%\main.obj" "%SRC%\main.c"
if errorlevel 1 goto :fail

echo util.c
cl.exe %CL_FLAGS% /Fo"%SRC%\util.obj" "%SRC%\util.c"
if errorlevel 1 goto :fail

echo linking...
link.exe /nologo /DLL /NODEFAULTLIB /ENTRY:EfiMain ^
    /SUBSYSTEM:EFI_BOOT_SERVICE_DRIVER ^
    /OUT:"%SRC%\smm.efi" "%SRC%\main.obj" "%SRC%\util.obj"
if errorlevel 1 goto :fail

echo smm.efi
del "%SRC%\*.obj" 2>nul
exit /b 0

:fail
echo FAIL
exit /b 1
