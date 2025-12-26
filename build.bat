@echo off
setlocal enabledelayedexpansion

set "PROJECT_ROOT=%~dp0"
set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
set "BUILD_DIR=%PROJECT_ROOT%\build"
set "BUILD_TYPE=Release"
set "VERBOSE="
set "PRESET=windows-release"

if "%1"=="" goto usage
if "%1"=="help" goto usage
if "%1"=="-h" goto usage
if "%1"=="--help" goto usage

if "%1"=="debug" (
    set "BUILD_TYPE=Debug"
    set "PRESET=windows-debug"
    goto configure_build
)

if "%1"=="release" (
    set "BUILD_TYPE=Release"
    set "PRESET=windows-release"
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
echo Usage: build.bat [command]
echo.
echo Commands:
echo   debug             Configure + build Debug version
echo   release           Configure + build Release version
echo   clean             Run CMake's built-in clean target
echo   clean-all         Run clean_all (clears C++ + Rust targets)
echo   verbose           Build with verbose output (implies last configured type)
echo   help              Show this help
echo.
echo Examples:
echo   build.bat debug
echo   build.bat release
echo   build.bat clean
echo   build.bat clean-all
echo   build.bat verbose
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
