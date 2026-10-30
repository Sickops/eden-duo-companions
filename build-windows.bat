@echo off
rem Build one native companion as a Windows DLL (MSVC + Ninja).
rem   build-windows.bat            -> dsmod-smo
rem   build-windows.bat dsmod-p5r  -> Persona 5 Royal
rem   build-windows.bat all        -> every companion module
rem Override EDEN_ROOT if your Eden Duo checkout lives elsewhere.
setlocal enabledelayedexpansion
if "%EDEN_ROOT%"=="" set EDEN_ROOT=D:\Projet IA\Dual screen\eden-duo
set SRC=%~dp0eden-duo-companions-super-mario-odyssey-1.0.0\native\modules
set BLD=%~dp0build-native-windows
set TARGET=%~1
if "%TARGET%"=="" set TARGET=dsmod-smo

rem Every module target declared by the companion tree (see native/modules/CMakeLists.txt and
rem chained_echoes.cmake). Links Awakening and VA-11 Hall-A ship no module: they are declarative.
set ALL_TARGETS=dsmod-smo dsmod-mk8d dsmod-ctr dsmod-wonder dsmod-dread dsmod-p5r dsmod-echoes dsmod-acnh dsmod-fe3h dsmod-isaac dsmod-dq3 dsmod-lp dsmod-lp-pearl dsmod-yoru dsmod-d3 dsmod-farming-simulator dsmod-breath-of-the-wild dsmod-belmont dsmod-ce dsmod-za

call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 (
    echo [erreur] vcvarsall.bat introuvable : adapter le chemin dans ce script.
    exit /b 1
)

if not exist "%BLD%\build.ninja" (
    cmake -S "%SRC%" -B "%BLD%" -G Ninja ^
        -DEDEN_SOURCE_ROOT="%EDEN_ROOT%" ^
        -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
    if errorlevel 1 exit /b 1
)

if /i "%TARGET%"=="all" (
    set FAILED=
    for %%T in (%ALL_TARGETS%) do (
        echo === %%T
        cmake --build "%BLD%" --target %%T --parallel 8
        if errorlevel 1 set FAILED=!FAILED! %%T
    )
    if not "!FAILED!"=="" (
        echo [erreur] cibles en echec :!FAILED!
        exit /b 1
    )
) else (
    cmake --build "%BLD%" --target %TARGET% --parallel 8
    if errorlevel 1 exit /b 1
)

for %%F in ("%BLD%\*.dll") do echo DLL: %%F
endlocal
