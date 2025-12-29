@echo off

if "%~1"=="" (
    echo please set the app version same as in debian/changelog
    exit /B 1
)

set APP_VERSION=%~1
echo set APP_VERSION: %APP_VERSION%

REM Set architecture - default x64
if "%~2"=="" (
    set ARCH=x64
) else (
    set ARCH=%~2
)

echo Build architecture: %ARCH%

REM Validate architecture
if /i "%ARCH%" neq "x64" if /i "%ARCH%" neq "x86" (
    echo Error: ARCH must be x64 or x86
    exit /B 1
)

REM Setup Visual Studio environment based on architecture
set VCINSTALLDIR=C:\Program Files\Microsoft Visual Studio\2022\Community\VC
echo VCINSTALLDIR: %VCINSTALLDIR%

if /i "%ARCH%"=="x64" (
    call "%VCINSTALLDIR%\Auxiliary\Build\vcvars64.bat"
) else (
    call "%VCINSTALLDIR%\Auxiliary\Build\vcvars32.bat"
)

@REM projects
set COO_PROJECT=dde-cooperation
set DT_PROJECT=data-transfer

REM defaults - override them by creating a build_env.bat file
set B_BUILD_TYPE=Release
set B_QT_ROOT=D:\Qt
set B_QT_VER=5.15.2

REM Setup Qt and OpenSSL based on architecture
if /i "%ARCH%"=="x64" (
    set B_QT_MSVC=msvc2019_64
    set "OPENSSL_DIR=C:\Program Files\OpenSSL-Win64"
    set CMAKE_ARCH=-A x64
    set OPENSSL_DLL_ARCH=x64
) else (
    set B_QT_MSVC=msvc2019_86
    set "OPENSSL_DIR=C:\Program Files (x86)\OpenSSL-Win32"
    set CMAKE_ARCH=-A Win32
    set OPENSSL_DLL_ARCH=x86
)

set B_BONJOUR=%~dp0\3rdparty\ext\BonjourSDK

REM Set OPENSSL_ROOT_DIR from OPENSSL_DIR (avoid parentheses in variable name)
set "OPENSSL_ROOT_DIR=%OPENSSL_DIR%"

set savedir=%cd%
cd /d %~dp0

REM cmake generator name for the target build system
if "%VisualStudioVersion%"=="15.0" (
    set cmake_gen=Visual Studio 15 2017
) else if "%VisualStudioVersion%"=="16.0" (
    set cmake_gen=Visual Studio 16 2019
) else (
    echo Visual Studio version was not detected: %VisualStudioVersion%
    echo Did you forget to run inside a VS developer prompt?
    echo Using the default cmake generator.
    set cmake_gen=Visual Studio 17 2022
)

if exist build_env.bat call build_env.bat

REM full path to Qt stuff we need
set B_QT_FULLPATH=%B_QT_ROOT%\%B_QT_VER%\%B_QT_MSVC%

echo Qt: %B_QT_FULLPATH%

@REM rmdir /q /s build
@REM mkdir build
@REM if ERRORLEVEL 1 goto failed
cd build
mkdir _CPack_Packages

echo ------------starting cmake------------

cmake -G "%cmake_gen%" %CMAKE_ARCH% -D CMAKE_BUILD_TYPE=%B_BUILD_TYPE% -D CMAKE_PREFIX_PATH="%B_QT_FULLPATH%" -D QT_VERSION=%B_QT_VER% -D APP_VERSION=%APP_VERSION% ..
if ERRORLEVEL 1 goto failed
cmake --build . --config %B_BUILD_TYPE%
if ERRORLEVEL 1 goto failed

REM Copy OpenSSL 3.x DLLs to build output
if exist output\%B_BUILD_TYPE% (
    copy output\%B_BUILD_TYPE%\* output\%COO_PROJECT%\%B_BUILD_TYPE%\ > NUL
    
    REM OpenSSL 3.x DLLs
    if exist "%OPENSSL_ROOT_DIR%\libcrypto-3-%OPENSSL_DLL_ARCH%.dll" (
        copy "%OPENSSL_ROOT_DIR%\libcrypto-3-%OPENSSL_DLL_ARCH%.dll" output\%COO_PROJECT%\%B_BUILD_TYPE%\ > NUL
    ) else if exist "%OPENSSL_ROOT_DIR%\libcrypto-3-x64.dll" (
        copy "%OPENSSL_ROOT_DIR%\libcrypto-3-x64.dll" output\%COO_PROJECT%\%B_BUILD_TYPE%\ > NUL
    ) else if exist "%OPENSSL_ROOT_DIR%\libcrypto-1_1-x64.dll" (
        echo Warning: OpenSSL 1.1 found, please use OpenSSL 3.x
        copy "%OPENSSL_ROOT_DIR%\libcrypto-1_1-x64.dll" output\%COO_PROJECT%\%B_BUILD_TYPE%\ > NUL
    ) else (
        echo Error: OpenSSL libcrypto DLL not found in %OPENSSL_ROOT_DIR%
        goto failed
    )
    
    if exist "%OPENSSL_ROOT_DIR%\libssl-3-%OPENSSL_DLL_ARCH%.dll" (
        copy "%OPENSSL_ROOT_DIR%\libssl-3-%OPENSSL_DLL_ARCH%.dll" output\%COO_PROJECT%\%B_BUILD_TYPE%\ > NUL
    ) else if exist "%OPENSSL_ROOT_DIR%\libssl-3-x64.dll" (
        copy "%OPENSSL_ROOT_DIR%\libssl-3-x64.dll" output\%COO_PROJECT%\%B_BUILD_TYPE%\ > NUL
    ) else if exist "%OPENSSL_ROOT_DIR%\libssl-1_1-x64.dll" (
        echo Warning: OpenSSL 1.1 found, please use OpenSSL 3.x
        copy "%OPENSSL_ROOT_DIR%\libssl-1_1-x64.dll" output\%COO_PROJECT%\%B_BUILD_TYPE%\ > NUL
    ) else (
        echo Error: OpenSSL libssl DLL not found in %OPENSSL_ROOT_DIR%
        goto failed
    )

    REM Same for data-transfer
    if exist "%OPENSSL_ROOT_DIR%\libcrypto-3-%OPENSSL_DLL_ARCH%.dll" (
        copy "%OPENSSL_ROOT_DIR%\libcrypto-3-%OPENSSL_DLL_ARCH%.dll" output\%DT_PROJECT%\%B_BUILD_TYPE%\ > NUL
    ) else if exist "%OPENSSL_ROOT_DIR%\libcrypto-3-x64.dll" (
        copy "%OPENSSL_ROOT_DIR%\libcrypto-3-x64.dll" output\%DT_PROJECT%\%B_BUILD_TYPE%\ > NUL
    ) else if exist "%OPENSSL_ROOT_DIR%\libcrypto-1_1-x64.dll" (
        copy "%OPENSSL_ROOT_DIR%\libcrypto-1_1-x64.dll" output\%DT_PROJECT%\%B_BUILD_TYPE%\ > NUL
    )
    
    if exist "%OPENSSL_ROOT_DIR%\libssl-3-%OPENSSL_DLL_ARCH%.dll" (
        copy "%OPENSSL_ROOT_DIR%\libssl-3-%OPENSSL_DLL_ARCH%.dll" output\%DT_PROJECT%\%B_BUILD_TYPE%\ > NUL
    ) else if exist "%OPENSSL_ROOT_DIR%\libssl-3-x64.dll" (
        copy "%OPENSSL_ROOT_DIR%\libssl-3-x64.dll" output\%DT_PROJECT%\%B_BUILD_TYPE%\ > NUL
    ) else if exist "%OPENSSL_ROOT_DIR%\libssl-1_1-x64.dll" (
        copy "%OPENSSL_ROOT_DIR%\libssl-1_1-x64.dll" output\%DT_PROJECT%\%B_BUILD_TYPE%\ > NUL
    )
) else (
    echo Remember to copy supporting binaries and configuration files!
)

echo Build completed successfully

REM ========================================
REM Check WiX Toolset
REM ========================================
where wix >nul 2>&1
if ERRORLEVEL 1 (
    echo Error: WiX Toolset 6+ not found in PATH
    echo Please install from: https://wixtoolset.org/releases/
    echo Or use: winget install WiX.Toolset
    goto failed
)

REM ========================================
REM Verify WiX template files
REM ========================================
echo Verifying WiX template files...
if not exist "output\%COO_PROJECT%.wxs" (
    echo Error: %COO_PROJECT%.wxs not found in output
    goto failed
)
if not exist "output\deepin-%DT_PROJECT%.wxs" (
    echo Error: %DT_PROJECT%.wxs not found in output
    goto failed
)

REM ========================================
REM Package with WiX
REM ========================================
echo Building %ARCH% Windows MSI installer...

REM Package dde-cooperation
echo.
echo Packaging %COO_PROJECT%...
if exist wix-temp-coop rmdir /q /s wix-temp-coop
mkdir wix-temp-coop
xcopy /E /I /Y output\%COO_PROJECT%\%B_BUILD_TYPE%\* wix-temp-coop\ > NUL

@REM   -loc "..\..\dist\wix\scripts\lang\dde-cooperation-en.wxl" ^
cd wix-temp-coop
wix build ..\output\%COO_PROJECT%.wxs ^
  -ext WixToolset.UI.wixext ^
  -loc "..\..\dist\wix\scripts\lang\dde-cooperation-chs.wxl" ^
  -out "..\_CPack_Packages\%COO_PROJECT%-%APP_VERSION%-win-%ARCH%.msi"
if ERRORLEVEL 1 (
  cd ..
  goto failed
)
cd ..
rmdir /q /s wix-temp-coop
echo %COO_PROJECT% MSI created successfully!

REM Package data-transfer
echo.
echo Packaging %DT_PROJECT%...
if exist wix-temp-dt rmdir /q /s wix-temp-dt
mkdir wix-temp-dt
xcopy /E /I /Y output\%DT_PROJECT%\%B_BUILD_TYPE%\* wix-temp-dt\ > NUL

cd wix-temp-dt
wix build ..\output\%DT_PROJECT%.wxs ^
  -loc "..\..\dist\wix\scripts\lang\data-transfer-en.wxl" ^
  -loc "..\..\dist\wix\scripts\lang\data-transfer-chs.wxl" ^
  -out "..\_CPack_Packages\deepin-%DT_PROJECT%-%APP_VERSION%-win-%ARCH%.msi"
if ERRORLEVEL 1 (
  cd ..
  goto failed
)
cd ..
rmdir /q /s wix-temp-dt
echo deepin-%DT_PROJECT% MSI created successfully!

echo.
echo ========================================
echo Build all Windows MSI installer successfully!!!
echo Architecture: %ARCH%
echo Output directory: _CPack_Packages
echo ========================================
echo.
dir /b _CPack_Packages\*.msi
echo.

set BUILD_FAILED=0
goto done

:failed
set BUILD_FAILED=1
echo Build failed

:done
cd /d %savedir%

set B_BUILD_TYPE=
set B_QT_ROOT=
set B_QT_VER=
set B_QT_MSVC=
set B_BONJOUR=
set B_QT_FULLPATH=
set savedir=
set cmake_gen=
set ARCH=
set CMAKE_ARCH=
set OPENSSL_DLL_ARCH=

EXIT /B %BUILD_FAILED%

