@echo off
set MSVC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
set SRC=c:\Users\n\Desktop\spoofer\smm
set EDK=%SRC%\edk2

set CL_FLAGS=/nologo /c /GS- /Gy /W3 /O1 /Oi- /I"%EDK%\MdePkg\Include\X64" /I"%EDK%\MdePkg\Include" /I"%EDK%\ShellPkg\Include" /I"%EDK%\CryptoPkg\Include" /I"%SRC%" /D_AMD64_

echo [*] main.c
"%MSVC%\bin\Hostx64\x64\cl.exe" %CL_FLAGS% /Fo"%SRC%\main.obj" "%SRC%\main.c"
if errorlevel 1 goto :fail

echo [*] util.c
"%MSVC%\bin\Hostx64\x64\cl.exe" %CL_FLAGS% /Fo"%SRC%\util.obj" "%SRC%\util.c"
if errorlevel 1 goto :fail

echo [*] linking...
"%MSVC%\bin\Hostx64\x64\link.exe" /nologo /DLL /NODEFAULTLIB /ENTRY:EfiMain /SUBSYSTEM:EFI_BOOT_SERVICE_DRIVER /OUT:"%SRC%\smm.efi" "%SRC%\main.obj" "%SRC%\util.obj"
if errorlevel 1 goto :fail

echo [+] smm.efi
del "%SRC%\*.obj" 2>nul
goto :end

:fail
echo [-] FAIL
:end
