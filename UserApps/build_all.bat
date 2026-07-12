@echo off
set MSVC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
set MSVC_BIN=%MSVC%\bin\Hostx64\x64
set MSVC_INC=%MSVC%\include
set CL_OPTS=/c /O1 /GS- /TC /Gm- /nologo /fp:precise /Oi- /Gs4096 /DNDEBUG /Iuserlib /Iuserlib/wolfssl /DWOLFSSL_USER_SETTINGS /I"%MSVC_INC%"
set LINK_OPTS=/subsystem:native /entry:_start /fixed /base:0x400000 /align:4096 /nodefaultlib /merge:.rdata=.text
set USERLIB=userlib\start.obj userlib\syscall.obj userlib\userlib.obj userlib\tls_wrapper.obj userlib\hmac.obj userlib\coding.obj userlib\error.obj userlib\kdf.obj userlib\wolfmath.obj userlib\memory.obj userlib\logging.obj userlib\wc_port.obj userlib\sha256.obj userlib\sha512.obj userlib\hash.obj userlib\asn.obj userlib\aes.obj userlib\tfm.obj userlib\random.obj userlib\ecc.obj userlib\curve25519.obj userlib\fe_operations.obj userlib\rsa.obj userlib\signature.obj userlib\ssl.obj userlib\internal.obj userlib\tls.obj userlib\wolfio.obj userlib\tls13.obj userlib\keys.obj

del *.obj *.exe *.map 2>nul
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

"%MSVC_BIN%\cl.exe" %CL_OPTS% userlib\tls_wrapper.c
if not exist tls_wrapper.obj echo FAILED: tls_wrapper.c && goto :fail
move tls_wrapper.obj userlib\ >nul 2>&1

set WOLFSSL_FILES=wolfcrypt\src\wolfmath.c wolfcrypt\src\memory.c wolfcrypt\src\logging.c wolfcrypt\src\wc_port.c wolfcrypt\src\sha256.c wolfcrypt\src\sha512.c wolfcrypt\src\hmac.c wolfcrypt\src\coding.c wolfcrypt\src\error.c wolfcrypt\src\kdf.c wolfcrypt\src\hash.c wolfcrypt\src\asn.c wolfcrypt\src\aes.c wolfcrypt\src\tfm.c wolfcrypt\src\random.c wolfcrypt\src\ecc.c wolfcrypt\src\curve25519.c wolfcrypt\src\fe_operations.c wolfcrypt\src\rsa.c wolfcrypt\src\signature.c src\ssl.c src\internal.c src\tls.c src\wolfio.c src\tls13.c src\keys.c

for %%w in (%WOLFSSL_FILES%) do (
    "%MSVC_BIN%\cl.exe" %CL_OPTS% userlib\wolfssl\%%w
    for %%x in (%%w) do (
        if not exist %%~nx.obj echo FAILED: %%w && goto :fail
        move %%~nx.obj userlib\ >nul 2>&1
    )
)

for %%f in (help version ticks reboot halt meminfo ps colors ls cat pwd write rm mkdir cp mv sleep shell nc https curl nano fastfetch) do (
    "%MSVC_BIN%\cl.exe" %CL_OPTS% %%f.c
    if exist %%f.obj (
        "%MSVC_BIN%\link.exe" %LINK_OPTS% %USERLIB% %%f.obj /out:%%f.exe /map:%%f.map
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