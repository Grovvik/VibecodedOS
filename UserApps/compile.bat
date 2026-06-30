cd /d D:\Games\OS\UserApps
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\ml64.exe" /c test_user.asm
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe" /subsystem:native /entry:Start /fixed /base:0x400000 /align:4096 /nodefaultlib test_user.obj /out:test_user.exe