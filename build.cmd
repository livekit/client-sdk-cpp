@echo off
setlocal enabledelayedexpansion

set "PROJECT_ROOT=%~dp0"
set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
set "BUILD_TYPE=Release"
set "PRESET=windows-release"
set "LIVEKIT_VERSION="
set "CMAKE_EXTRA_ARGS="

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
if /I "%1"=="help" goto usage
if "%1"=="-h" goto usage
if "%1"=="--help" goto usage

set "CMD="
set "LIVEKIT_VERSION="

:parse_all
if "%~1"=="" goto after_parse

:: 1. Capture the command if we don't have one yet
if not defined CMD (
    set "CMD=%~1"
    echo Command set to: %~1
    shift
    goto parse_all
)

:: 2. Check for the version flag
if /I "%~1"=="--version" (
    shift
    goto :get_version_value
)

:: 3. Handle unknown arguments
echo ERROR: Unknown option: %~1
exit /b 1

:get_version_value
echo 1 after shift is : %~1
set "LIVEKIT_VERSION=%~1"
shift
goto parse_all

:after_parse
if not defined CMD (
    echo ERROR: No command specified
    goto usage
)

if defined LIVEKIT_VERSION (
    set "CMAKE_EXTRA_ARGS=-DLIVEKIT_VERSION=%LIVEKIT_VERSION%"
    echo ==^> Injecting LIVEKIT_VERSION=%LIVEKIT_VERSION%
)
goto dispatch

:dispatch
if "%CMD%"=="debug" (
    set "BUILD_TYPE=Debug"
    set "PRESET=windows-debug"
    set "BUILD_DIR=%PROJECT_ROOT%\build-debug"
    goto configure_build
)

if "%CMD%"=="debug-examples" (
    set "BUILD_TYPE=Debug"
    set "PRESET=windows-debug-examples"
    set "BUILD_DIR=%PROJECT_ROOT%\build-debug"
    goto configure_build
)

if "%CMD%"=="release" (
    set "BUILD_TYPE=Release"
    set "PRESET=windows-release"
    set "BUILD_DIR=%PROJECT_ROOT%\build-release"
    goto configure_build
)

if "%CMD%"=="release-examples" (
    set "BUILD_TYPE=Release"
    set "PRESET=windows-release-examples"
    set "BUILD_DIR=%PROJECT_ROOT%\build-release"
    goto configure_build
)

if "%CMD%"=="clean" goto clean
if "%CMD%"=="clean-all" goto clean_all

echo Unknown command: %CMD%
goto usage

:usage
echo Usage: build.cmd [command]
echo.
echo Commands:
echo   debug             Configure + build Debug version (build-debug/)
echo   debug-examples    Configure + build Debug version with examples
echo   release           Configure + build Release version (build-release/)
echo   release-examples  Configure + build Release version with examples
echo   clean             Clean both Debug and Release build directories
echo   clean-all         Full clean (build dirs + Rust targets)
echo   help              Show this help
echo.
echo Examples:
echo   build.cmd debug
echo   build.cmd release
echo   build.cmd release-examples
echo   build.cmd clean
echo   build.cmd clean-all
goto :eof

:configure_build
echo ==^> Configuring CMake (%BUILD_TYPE%)...
if not defined VCPKG_ROOT (
    echo Warning: VCPKG_ROOT is not set. Attempting to configure without vcpkg...
    cmake -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=%BUILD_TYPE% %CMAKE_EXTRA_ARGS%
) else (
    cmake --preset "%PRESET%" %CMAKE_EXTRA_ARGS%
)
if errorlevel 1 (
    echo Configuration failed!
    exit /b 1
)
goto build_only

:build_only
echo ==^> Building (%BUILD_TYPE%)...
cmake --build "%BUILD_DIR%" --config %BUILD_TYPE%
if errorlevel 1 (
    echo Build failed!
    exit /b 1
)
echo ==^> Build complete!
goto :eof

:clean
echo ==^> Cleaning build directories...
set "BUILD_DIR_DEBUG=%PROJECT_ROOT%\build-debug"
set "BUILD_DIR_RELEASE=%PROJECT_ROOT%\build-release"

if exist "%BUILD_DIR_DEBUG%\CMakeCache.txt" (
    echo    Cleaning build-debug...
    cmake --build "%BUILD_DIR_DEBUG%" --target clean --config Debug 2>nul
) else (
    echo    ^(skipping^) build-debug does not exist or is not configured.
)

if exist "%BUILD_DIR_RELEASE%\CMakeCache.txt" (
    echo    Cleaning build-release...
    cmake --build "%BUILD_DIR_RELEASE%" --target clean --config Release 2>nul
) else (
    echo    ^(skipping^) build-release does not exist or is not configured.
)
echo ==^> Clean complete.
goto :eof

:clean_all
echo ==^> Running full clean-all ^(C++ + Rust^)...

echo Removing build-debug directory...
if exist "%PROJECT_ROOT%\build-debug" (
    rmdir /s /q "%PROJECT_ROOT%\build-debug" 2>nul
)

echo Removing build-release directory...
if exist "%PROJECT_ROOT%\build-release" (
    rmdir /s /q "%PROJECT_ROOT%\build-release" 2>nul
)

echo Removing Rust debug artifacts...
if exist "%PROJECT_ROOT%\client-sdk-rust\target\debug" (
    rmdir /s /q "%PROJECT_ROOT%\client-sdk-rust\target\debug" 2>nul
)

echo Removing Rust release artifacts...
if exist "%PROJECT_ROOT%\client-sdk-rust\target\release" (
    rmdir /s /q "%PROJECT_ROOT%\client-sdk-rust\target\release" 2>nul
)

echo ==^> Clean-all complete.
goto :eof
