@echo off
setlocal EnableDelayedExpansion
REM Kill Launcher - forced static single-file build (Win7 compatible)
REM Uses static Qt (with OpenSSL TLS backend) + static OpenSSL.
REM Output: single exe depending only on system DLLs.
REM Post-build: patch import GetSystemTimePreciseAsFileTime -> GetSystemTimeAsFileTime

set LC_ALL=C
set LANG=C

REM 1. Init MSVC x64 environment
call "D:\program\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 goto VCVARS_FAIL

set PATH=D:\program\qt\Tools\Ninja;D:\program\qt\Tools\CMake_64\bin;%PATH%

REM 2. Static Qt (OpenSSL backend) path
set QT_STATIC_DIR=D:/program/qt-static-ssl/6.8.4/msvc2022_64
if not exist "%QT_STATIC_DIR%/lib/cmake/Qt6/Qt6Config.cmake" goto QT_NOTFOUND

set BUILD_DIR=build_ssl
set DIST_DIR=%~dp0dist

REM 3. Configure if needed
if not exist "%BUILD_DIR%\CMakeCache.txt" goto CONFIGURE
 goto BUILD

:CONFIGURE
echo [INFO] Configuring CMake (static Qt + OpenSSL)...
cmake -S . -B %BUILD_DIR% -G Ninja -DCMAKE_BUILD_TYPE=Release -DKILL_STATIC_QT=ON -DCMAKE_PREFIX_PATH="%QT_STATIC_DIR%" -DOPENSSL_ROOT_DIR="D:/program/qt-src/openssl-static"
if errorlevel 1 goto CONFIG_FAIL

:BUILD
REM 4. Remove stale output to avoid false positives
if exist "%DIST_DIR%\Kill.exe" del /f /q "%DIST_DIR%\Kill.exe" >nul 2>&1

REM 5. Build (CMake POST_BUILD runs patch script)
echo [INFO] Building...
cmake --build %BUILD_DIR% --parallel
if errorlevel 1 goto BUILD_FAIL

REM 6. Verify output
if not exist "%DIST_DIR%\Kill.exe" goto NO_EXE
for %%A in ("%DIST_DIR%\Kill.exe") do set SIZE=%%~zA
if !SIZE! LSS 8388608 goto TOO_SMALL

REM 7. Ensure patch applied (idempotent; prints no change if already patched)
python "%~dp0scripts\patch_win7_import.py" "%DIST_DIR%\Kill.exe"

set /a SIZE_MB=!SIZE! / 1048576
echo [OK] Static single-file build complete: dist\Kill.exe  (!SIZE! bytes, ~!SIZE_MB! MB)
exit /b 0

:VCVARS_FAIL
echo [ERROR] vcvars64.bat init failed
exit /b 1

:QT_NOTFOUND
echo [ERROR] static Qt not found: %QT_STATIC_DIR%
exit /b 1

:CONFIG_FAIL
echo [ERROR] CMake configure failed
exit /b 1

:BUILD_FAIL
echo [ERROR] build failed
exit /b 1

:NO_EXE
echo [ERROR] dist\Kill.exe not produced
exit /b 1

:TOO_SMALL
echo [ERROR] output too small (!SIZE! bytes) - looks dynamically linked, not static single-file!
exit /b 1
