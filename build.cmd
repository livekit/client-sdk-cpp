@echo off
setlocal enabledelayedexpansion

set "PROJECT_ROOT=%~dp0"
set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
set "BUILD_DIR=%PROJECT_ROOT%\build"
set "BUILD_TYPE=Release"
set "VERBOSE="
set "PRESET=windows-release"

REM ============================================================
REM Auto-detect LIBCLANG_PATH if not already set
REM ============================================================
if not defined LIBCLANG_PATH (
    echo ==^> Detecting LIBCLANG_PATH...
    
    REM Define common VS installation paths to check
    REM VS 2022 editions
    set "VS_PATHS[0]=D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin"
    set "VS_PATHS[1]=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin"
    set "VS_PATHS[2]=D:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin"
    set "VS_PATHS[3]=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin"
    set "VS_PATHS[4]=D:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\x64\bin"
    set "VS_PATHS[5]=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\x64\bin"
    set "VS_PATHS[6]=D:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin"
    set "VS_PATHS[7]=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin"
    REM VS 2019 editions
    set "VS_PATHS[8]=D:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\Llvm\x64\bin"
    set "VS_PATHS[9]=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\Llvm\x64\bin"
    set "VS_PATHS[10]=D:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Tools\Llvm\x64\bin"
    set "VS_PATHS[11]=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Tools\Llvm\x64\bin"
    set "VS_PATHS[12]=D:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Tools\Llvm\x64\bin"
    set "VS_PATHS[13]=C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Tools\Llvm\x64\bin"
    REM Standalone LLVM installations
    set "VS_PATHS[14]=C:\Program Files\LLVM\bin"
    set "VS_PATHS[15]=D:\Program Files\LLVM\bin"
    
    set "LIBCLANG_FOUND="
    
    for /L %%i in (0,1,15) do (
        if not defined LIBCLANG_FOUND (
            if exist "!VS_PATHS[%%i]!\libclang.dll" (
                set "LIBCLANG_PATH=!VS_PATHS[%%i]!"
                set "LIBCLANG_FOUND=1"
                echo    Found libclang at: !LIBCLANG_PATH!
            )
        )
    )
    
    if not defined LIBCLANG_FOUND (
        echo    Warning: Could not auto-detect LIBCLANG_PATH.
        echo    Please set LIBCLANG_PATH manually to the directory containing libclang.dll
        echo    Example: set LIBCLANG_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin
    )
) else (
    echo ==^> Using existing LIBCLANG_PATH: %LIBCLANG_PATH%
)

if "%1"=="" goto usage
if "%1"=="help" goto usage
if "%1"=="-h" goto usage
if "%1"=="--help" goto usage

if "%1"=="debug" (
    set "BUILD_TYPE=Debug"
    set "PRESET=windows-debug"
    goto configure_build
)

if "%1"=="debug-examples" (
    set "BUILD_TYPE=Debug"
    set "PRESET=windows-debug-examples"
    goto configure_build
)

if "%1"=="release" (
    set "BUILD_TYPE=Release"
    set "PRESET=windows-release"
    goto configure_build
)

if "%1"=="release-examples" (
    set "BUILD_TYPE=Release"
    set "PRESET=windows-release-examples"
    goto configure_build
)

if "%1"=="verbose" (
    set "VERBOSE=--verbose"
    goto build_only
)

if "%1"=="clean" goto clean
if "%1"=="clean-all" goto clean_all

echo Unknown command: %1
goto usage

:usage
echo Usage: build.cmd [command]
echo.
echo Commands:
echo   debug             Configure + build Debug version
echo   debug-examples    Configure + build Debug version with examples
echo   release           Configure + build Release version
echo   release-examples  Configure + build Release version with examples
echo   clean             Run CMake's built-in clean target
echo   clean-all         Run clean_all (clears C++ + Rust targets)
echo   verbose           Build with verbose output (implies last configured type)
echo   help              Show this help
echo.
echo Examples:
echo   build.cmd debug
echo   build.cmd release
echo   build.cmd release-examples
echo   build.cmd clean
echo   build.cmd clean-all
echo   build.cmd verbose
goto :eof

:configure_build
echo ==^> Configuring CMake (%BUILD_TYPE%)...
if not defined VCPKG_ROOT (
    echo Warning: VCPKG_ROOT is not set. Attempting to configure without vcpkg...
    cmake -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DCMAKE_CONFIGURATION_TYPES="Debug;Release"
) else (
    cmake --preset %PRESET%
)
if errorlevel 1 (
    echo Configuration failed!
    exit /b 1
)
goto build_only

:build_only
echo ==^> Building (%BUILD_TYPE%)...
cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% -j %VERBOSE%
if errorlevel 1 (
    echo Build failed!
    exit /b 1
)
echo ==^> Build complete!
goto :eof

:clean
echo ==^> Cleaning CMake targets...
if exist "%BUILD_DIR%" (
    cmake --build "%BUILD_DIR%" --target clean
) else (
    echo    (skipping) %BUILD_DIR% does not exist.
)
goto :eof

:clean_all
echo ==^> Running full clean-all (C++ + Rust)...
if exist "%BUILD_DIR%" (
    cmake --build "%BUILD_DIR%" --target clean_all
) else (
    echo    (info) %BUILD_DIR% does not exist; doing manual deep clean...
)

if exist "%PROJECT_ROOT%\client-sdk-rust\target\debug" (
    echo Removing Rust debug artifacts...
    rmdir /s /q "%PROJECT_ROOT%\client-sdk-rust\target\debug" 2>nul
)
if exist "%PROJECT_ROOT%\client-sdk-rust\target\release" (
    echo Removing Rust release artifacts...
    rmdir /s /q "%PROJECT_ROOT%\client-sdk-rust\target\release" 2>nul
)
if exist "%BUILD_DIR%" (
    echo Removing build directory...
    rmdir /s /q "%BUILD_DIR%" 2>nul
)
echo ==^> Clean-all complete.
goto :eof
