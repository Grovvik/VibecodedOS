@echo off
setlocal enabledelayedexpansion

set MICRONT=D:\Games\OS
set DOOM_SRC=%MICRONT%\doomgeneric\doomgeneric
set DOOMPORT=%MICRONT%\DoomPort
set MSVC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
set MSVC_BIN=%MSVC%\bin\Hostx64\x64
set OUTDIR=%DOOMPORT%\build

if not exist "%DOOM_SRC%\doomgeneric.c" (
    echo ERROR: doomgeneric source not found at %DOOM_SRC%
    echo Please clone: git clone https://github.com/ozkl/doomgeneric %MICRONT%\doomgeneric
    exit /b 1
)

if not exist "%OUTDIR%" mkdir "%OUTDIR%"

set CL_OPTS=/c /O2 /GS- /TC /Gm- /fp:precise /Oi- /Gs4096 /DNDEBUG /DHAVE_CONFIG_H /DDOOMGENERIC /DFEATURE_SOUND /FI"%DOOMPORT%\include\micront_preface.h" /I"%DOOMPORT%\include" /I"%DOOM_SRC%" /I"%DOOMPORT%" /W0
set LINK_OPTS=/subsystem:native /entry:_start /fixed /base:0x400000 /align:4096 /nodefaultlib

echo === Building MicroNT DOOM Port ===

echo [1/3] Assembling entry point and syscalls...
"%MSVC_BIN%\ml64.exe" /c /Fo"%OUTDIR%\start.obj" "%MICRONT%\UserApps\userlib\start.asm"
if errorlevel 1 echo FAILED: start.asm && exit /b 1
"%MSVC_BIN%\ml64.exe" /c /Fo"%OUTDIR%\syscall.obj" "%MICRONT%\UserApps\userlib\syscall.asm"
if errorlevel 1 echo FAILED: syscall.asm && exit /b 1

echo [2/3] Compiling MicroNT CRT and platform...
"%MSVC_BIN%\cl.exe" %CL_OPTS% /Fo"%OUTDIR%\micront_crt.obj" "%DOOMPORT%\micront_crt.c"
if errorlevel 1 echo FAILED: micront_crt.c && exit /b 1
"%MSVC_BIN%\cl.exe" %CL_OPTS% /Fo"%OUTDIR%\doomgeneric_micront.obj" "%DOOMPORT%\doomgeneric_micront.c"
if errorlevel 1 echo FAILED: doomgeneric_micront.c && exit /b 1
"%MSVC_BIN%\cl.exe" %CL_OPTS% /Fo"%OUTDIR%\i_sound.obj" "%DOOMPORT%\i_sound.c"
if errorlevel 1 echo FAILED: i_sound.c && exit /b 1

echo [3/3] Compiling doomgeneric engine...
set OBJ_LIST=%OUTDIR%\start.obj %OUTDIR%\syscall.obj %OUTDIR%\micront_crt.obj %OUTDIR%\doomgeneric_micront.obj %OUTDIR%\i_sound.obj

for %%f in (
    am_map d_event d_items d_iwad d_loop d_main d_mode d_net
    doomdef doomstat dstrings f_finale f_wipe g_game
    hu_lib hu_stuff i_input     i_system i_timer i_video
    m_argv m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc m_random
    p_ceilng p_doors p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj
    p_plats p_pspr p_saveg p_setup p_sight p_spec p_switch p_telept p_tick p_user
    r_bsp r_data r_draw r_main r_plane r_segs r_sky r_things
    s_sound sounds st_lib st_stuff tables v_video
    w_file w_file_stdc w_main w_wad w_checksum wi_stuff z_zone
    info doomgeneric
    deh_str deh_main deh_misc deh_mapping deh_ptr deh_sound deh_thing deh_weapon
    memio mus2mid sha1 statdump i_endoom i_cdmus i_joystick gusconf i_scale dummy
) do (
    if exist "%DOOM_SRC%\%%f.c" (
        "%MSVC_BIN%\cl.exe" %CL_OPTS% /Fo"%OUTDIR%\%%f.obj" "%DOOM_SRC%\%%f.c"
        if not errorlevel 1 (
            set "OBJ_LIST=!OBJ_LIST! %OUTDIR%\%%f.obj"
        ) else (
            echo WARNING: %%f.c failed, continuing...
        )
    )
)

echo Linking doom.exe...
"%MSVC_BIN%\link.exe" %LINK_OPTS% !OBJ_LIST! /out:%OUTDIR%\doom.exe
if errorlevel 1 (
    echo FAILED: linking doom.exe
    exit /b 1
)

echo.
echo === Build successful! ===
echo Output: %OUTDIR%\doom.exe
copy /y %OUTDIR%\doom.exe ..\image\bin
echo Run with: doom -iwad /bin/doom1.wad
