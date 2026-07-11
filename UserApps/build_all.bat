@echo off
set MSVC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
set MSVC_BIN=%MSVC%\bin\Hostx64\x64
set MSVC_INC=%MSVC%\include
set CL_OPTS=/c /O1 /GS- /TC /Gm- /fp:precise /Oi- /Gs4096 /DNDEBUG /Iuserlib /I"%MSVC_INC%"
set LINK_OPTS=/subsystem:native /entry:_start /fixed /base:0x400000 /align:4096 /nodefaultlib /merge:.rdata=.text
set USERLIB=userlib\start.obj userlib\syscall.obj userlib\userlib.obj

del *.obj *.exe 2>nul
del userlib\*.obj 2>nul

"%MSVC_BIN%\ml64.exe" /c userlib\start.asm
if not exist start.obj echo FAILED: start.asm && goto :fail
move start.obj userlib\ >nul 2>&1

"%MSVC_BIN%\ml64.exe" /c userlib\syscall.asm
if not exist syscall.obj echo FAILED: syscall.asm && goto :fail
move syscall.obj userlib\ >nul 2>&1

"%MSVC_BIN%\cl.exe" %CL_OPTS% userlib\userlib.c
if not exist userlib.obj echo FAILED: userlib.c && goto :fail
move userlib.obj userlib\ >nul 2>&1

for %%f in (help version ticks reboot halt meminfo ps colors ls cat pwd write rm mkdir cp mv sleep shell audio_test nc test_read) do (
    "%MSVC_BIN%\cl.exe" %CL_OPTS% %%f.c
    if exist %%f.obj (
        "%MSVC_BIN%\link.exe" %LINK_OPTS% %USERLIB% %%f.obj /out:%%f.exe
        if exist %%f.exe (
            echo Built: %%f.exe
        ) else (
            echo FAILED: %%f
        )
        del %%f.obj 2>nul
    ) else (
        echo FAILED: %%f
    )
)

echo Building tcc...
"%MSVC_BIN%\cl.exe" %CL_OPTS% /Itinycc tinycc\tcc.c
if exist tcc.obj (
    "%MSVC_BIN%\link.exe" %LINK_OPTS% /entry:_tcc_start %USERLIB% tcc.obj /out:tcc.exe
    if exist tcc.exe (
        echo Built: tcc.exe
    ) else (
        echo FAILED: tcc.exe
    )
    del tcc.obj 2>nul
) else (
    echo FAILED: tcc.c compile
)

echo.
echo Done.
goto :end

:fail
echo FAILED: userlib build
:end