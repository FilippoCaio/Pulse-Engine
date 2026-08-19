@echo off
setlocal
rem Pulse Engine native build (MSVC)
rem NOTE: the vswhere path contains "(x86)". The quotes must sit ON THE EXPANSION
rem inside the for /f command, otherwise that ")" closes the for block early and
rem cmd ends up trying to run a bare "vswhere.exe".
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio Installer not found: "%VSWHERE%"
    exit /b 1
)
rem Si cerca il 2022 per nome, non "il piu' recente": dove convivono piu'
rem Visual Studio affiancati -latest restituisce l'ultimo installato, che porta
rem un altro toolset. Compilare con quello riesce spesso e sbaglia a sorpresa.
rem Il ripiego su -latest resta, per le macchine dove il 2022 non c'e'.
set "VSPATH="
for %%e in (Community Professional Enterprise BuildTools) do (
    if not defined VSPATH if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvars64.bat" (
        set "VSPATH=%ProgramFiles%\Microsoft Visual Studio\2022\%%e"
    )
)
if not defined VSPATH (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
)
if not defined VSPATH (
    echo Visual Studio with the C++ toolchain was not found.
    exit /b 1
)
rem vcvars64.bat itself shells out to a bare "vswhere.exe" for some lookups, so
rem put the VS Installer directory on PATH first ? otherwise it prints
rem "'vswhere.exe' is not recognized" on stderr and silently uses a fallback.
for %%d in ("%VSWHERE%") do set "VSINSTDIR=%%~dpd"
set "PATH=%VSINSTDIR%;%PATH%"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo vcvars64.bat failed.
    exit /b 1
)

cd /d "%~dp0"
if not exist build mkdir build
set OUT_EXE=impulso_live.exe
if not "%~1"=="" set OUT_EXE=%~1

rem L'icona non si collega da sola: va prima compilata in una risorsa .res.
rem rc.exe fa parte del Windows SDK e vcvars64 lo ha gia' messo su PATH.
rc /nologo /fo build\icon.res icon.rc
if errorlevel 1 (
    echo COMPILAZIONE DELL'ICONA FALLITA
    exit /b 1
)

rem SQLite serve a leggere la libreria di Pulse Asset Manager, che e' un database.
rem Sono nove megabyte di C che non cambiano mai: si compilano una volta sola e
rem l'oggetto viene riusato, altrimenti ogni build pagherebbe mezzo minuto per un
rem file che nessuno tocca. Aggiornando l'amalgamazione, cancellare build\sqlite3.obj.
if not exist build\sqlite3.obj (
    echo Compilazione di SQLite ^(una volta sola^)...
    cl /nologo /O2 /W3 /c /D_CRT_SECURE_NO_WARNINGS ^
       /DSQLITE_THREADSAFE=0 /DSQLITE_OMIT_LOAD_EXTENSION /DSQLITE_OMIT_DEPRECATED /DSQLITE_DEFAULT_MEMSTATUS=0 ^
       third_party\sqlite\sqlite3.c /Fo:build\sqlite3.obj
    if errorlevel 1 (
        echo COMPILAZIONE DI SQLITE FALLITA
        exit /b 1
    )
)

cl /nologo /O2 /EHsc /std:c++17 /W3 /D_CRT_SECURE_NO_WARNINGS ^
   src\app.cpp src\physics.cpp src\render.cpp src\glext.cpp src\ui.cpp src\scene.cpp src\dock.cpp src\blueprint.cpp src\curve.cpp src\animation.cpp src\audio.cpp src\material.cpp src\widget.cpp src\live_session.cpp src\engine_update.cpp src\asset_library.cpp ^
   /Fo:build\ /Fe:build\%OUT_EXE% ^
   /link build\icon.res build\sqlite3.obj opengl32.lib gdi32.lib user32.lib comdlg32.lib shell32.lib ole32.lib windowscodecs.lib winmm.lib ws2_32.lib winhttp.lib bcrypt.lib /SUBSYSTEM:WINDOWS

if %errorlevel% neq 0 (
    echo BUILD FALLITA
    exit /b 1
)
copy /Y update.cfg build\update.cfg >nul
echo BUILD OK: build\%OUT_EXE%
