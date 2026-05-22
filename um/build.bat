@echo off
set MSVC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
set SDKINC=C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0
set SDKLIB=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0
set SRC=c:\Users\n\Desktop\spoofer\um

"%MSVC%\bin\Hostx64\x64\cl.exe" /nologo /W3 /O2 /I"%MSVC%\include" /I"%SDKINC%\ucrt" /I"%SDKINC%\um" /I"%SDKINC%\shared" /I"%SRC%" /Fe"%SRC%\client.exe" "%SRC%\main.c" /link /SUBSYSTEM:CONSOLE /LIBPATH:"%MSVC%\lib\x64" /LIBPATH:"%SDKLIB%\ucrt\x64" /LIBPATH:"%SDKLIB%\um\x64" kernel32.lib advapi32.lib shell32.lib
if errorlevel 1 (echo [-] FAIL & exit /b 1)
echo [+] built: client.exe
