@echo off
setlocal enabledelayedexpansion

set "QEMU=C:\Program Files\qemu\qemu-system-x86_64.exe"
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
set "FW=OVMF_X64.fd"
set "SOL_DIR=%~dp0"
set "BIN_DIR=%SOL_DIR%bin\Release\"

echo === MicroNT Build + Run ===
echo.

echo [1/4] Building user apps...
cd /d "%SOL_DIR%UserApps"
call build_all.bat
if errorlevel 1 echo FAILED: user apps && goto :end

echo [2/4] Building kernel...
cd /d "%SOL_DIR%"
"%MSBUILD%" "OS.sln" /p:Configuration=Release /p:Platform=x64 /t:Build /v:minimal
if errorlevel 1 echo FAILED: kernel build && goto :end

echo [3/4] Preparing image...
if not exist "image\efi\boot" mkdir "image\efi\boot"
if not exist "image\efi\os"   mkdir "image\efi\os"
if not exist "image\bin"      mkdir "image\bin"
copy /Y "%BIN_DIR%Boot.efi"   "image\efi\boot\bootx64.efi" >nul
copy /Y "%BIN_DIR%Kernel.exe" "image\efi\os\kernel.exe"    >nul
copy /Y "%SOL_DIR%UserApps\*.exe" "image\bin\"             >nul

echo [4/4] Launching QEMU...
"%QEMU%" -m 512 -cpu qemu64 -smp 1 -serial mon:stdio -no-reboot -no-shutdown -device e1000,netdev=net0 -netdev user,id=net0,hostfwd=tcp::8080-:80 -device intel-hda -device hda-output -L . -pflash "%FW%" -drive format=raw,file=fat:rw:image

:end
endlocal
