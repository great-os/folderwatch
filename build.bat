@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "SRC=src\ui.c src\etw.c src\fmap.c src\util.c"
set "OUT=folderwatch.exe"
set "OBJDIR=obj"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

set "GCC=C:\msys64\ucrt64\bin\gcc.exe"
if not exist "%GCC%" set "GCC=gcc"

"C:\msys64\ucrt64\bin\windres.exe" -O coff src\app.rc "%OBJDIR%\app_res.o"
if errorlevel 1 exit /b 1

"%GCC%" -Os -flto -municode -mwindows -ffunction-sections -fdata-sections ^
  -fno-asynchronous-unwind-tables -fno-ident -fno-stack-protector -mno-stack-arg-probe ^
  -fno-exceptions -Wall -DUNICODE -D_UNICODE -Isrc %SRC% "%OBJDIR%\app_res.o" ^
  -o "%OUT%" -Wl,--gc-sections -Wl,--no-insert-timestamp -s -static ^
  -lkernel32 -luser32 -lshell32 -lole32 -ladvapi32 -lcomctl32
if errorlevel 1 exit /b 1

for %%F in ("%OUT%") do echo Built %%~nxF  %%~zF bytes
exit /b 0
