@echo off
rem =============================================================================
rem build.bat - Build the GM voice extractor with CMake (Windows cmd / PowerShell)
rem
rem If cmake is not on PATH, this script auto-detects the cmake.exe bundled
rem with Visual Studio 2026 (or 2022). You can also pass -CMakePath explicitly.
rem
rem Usage (cmd):           build.bat   [args]
rem Usage (PowerShell):  .\build.bat   [args]   (the .\ prefix is required)
rem
rem Arguments:
rem   -Run  <path-to-sf2>      build, then run extractor on this SF2
rem   -Out  <dir>              output directory (default: generated)
rem   -Programs "0,16,56"      restrict to these GM programs (debug)
rem   -Config Release|Debug    build config (default: Release)
rem   -Generator "<name>"      force CMake generator, e.g. "Ninja"
rem   -CMakePath "<exe>"       full path to cmake.exe (skip auto-detect)
rem   -ToolchainFile "<path>"  -DCMAKE_TOOLCHAIN_FILE=<path> (vcpkg etc.)
rem   -Clean                   wipe build\ and reconfigure
rem
rem Example matching the existing wave_fft workflow:
rem   $cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
rem   .\build.bat -CMakePath $cmake -ToolchainFile "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" -Run .\GeneralUser-GS.sf2
rem =============================================================================
setlocal EnableDelayedExpansion
pushd "%~dp0"

set "RUN="
set "OUTDIR=generated"
set "PROGRAMS="
set "CONFIG=Release"
set "GENERATOR="
set "CMAKE="
set "TOOLCHAIN="
set "CLEAN=0"

:argloop
if "%~1"=="" goto args_done
if /I "%~1"=="-Run"           ( set "RUN=%~2"        & shift & shift & goto argloop )
if /I "%~1"=="-Out"           ( set "OUTDIR=%~2"     & shift & shift & goto argloop )
if /I "%~1"=="-Programs"      ( set "PROGRAMS=%~2"   & shift & shift & goto argloop )
if /I "%~1"=="-Config"        ( set "CONFIG=%~2"     & shift & shift & goto argloop )
if /I "%~1"=="-Generator"     ( set "GENERATOR=%~2"  & shift & shift & goto argloop )
if /I "%~1"=="-CMakePath"     ( set "CMAKE=%~2"      & shift & shift & goto argloop )
if /I "%~1"=="-ToolchainFile" ( set "TOOLCHAIN=%~2"  & shift & shift & goto argloop )
if /I "%~1"=="-Clean"         ( set "CLEAN=1"        & shift & goto argloop )
echo unknown argument: %~1
popd & endlocal & exit /b 2
:args_done

rem --- resolve cmake: -CMakePath > PATH > VS auto-detect ---
if not defined CMAKE (
    where cmake >nul 2>&1
    if not errorlevel 1 (
        set "CMAKE=cmake"
    ) else (
        for /d %%V in ("C:\Program Files\Microsoft Visual Studio\18\*" "C:\Program Files\Microsoft Visual Studio\2022\*") do (
            if not defined CMAKE if exist "%%V\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
                set "CMAKE=%%V\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
                echo auto-detected cmake: !CMAKE!
            )
        )
    )
)
if not defined CMAKE (
    echo ERROR: cmake not found on PATH or in VS install.
    echo Pass -CMakePath, or open "Developer PowerShell for VS 2026" before running.
    popd & endlocal & exit /b 1
)

if "%CLEAN%"=="1" if exist build (
    echo cleaning build\ ...
    rmdir /s /q build
)

rem configure if not already configured (check the CMake cache, not just the dir)
if not exist "build\CMakeCache.txt" (
    set "CFG_ARGS=-S . -B build"
    if defined GENERATOR set "CFG_ARGS=!CFG_ARGS! -G "%GENERATOR%""
    if defined TOOLCHAIN set "CFG_ARGS=!CFG_ARGS! -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%""
    echo configuring ^("%CMAKE%" !CFG_ARGS!^) ...
    "%CMAKE%" !CFG_ARGS!
    if errorlevel 1 (
        echo cmake configure failed
        rem wipe partial build dir so the next run will retry configure
        if exist build rmdir /s /q build
        popd & endlocal & exit /b 1
    )
)

echo building ^(%CONFIG%^) ...
"%CMAKE%" --build build --config %CONFIG%
if errorlevel 1 ( echo build failed & popd & endlocal & exit /b 1 )

rem accept both multi-config (MSVC: build\Release\exe) and single-config (Ninja/MinGW: build\exe)
set "EXE="
if exist "build\%CONFIG%\gm_extract.exe" set "EXE=build\%CONFIG%\gm_extract.exe"
if not defined EXE if exist "build\gm_extract.exe" set "EXE=build\gm_extract.exe"
if not defined EXE (
    echo executable not found in build\%CONFIG%\ or build\
    popd & endlocal & exit /b 1
)
echo built: !EXE!

if defined RUN (
    if not exist "%RUN%" (
        echo SF2 not found: %RUN%
        popd & endlocal & exit /b 1
    )
    set "RUNARGS=--out %OUTDIR%"
    if defined PROGRAMS set "RUNARGS=!RUNARGS! --programs %PROGRAMS%"
    echo running: !EXE! "%RUN%" !RUNARGS!
    "!EXE!" "%RUN%" !RUNARGS!
    if errorlevel 1 (
        echo gm_extract failed
        popd & endlocal & exit /b 1
    )
    echo generated: %OUTDIR%\voice_table.cpp, %OUTDIR%\voices\voice_table.h, %OUTDIR%\voices\voice_harm_data.h
)

popd
endlocal
