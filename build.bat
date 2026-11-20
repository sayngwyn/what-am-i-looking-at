@echo off
rem Build script. Configures and builds Release with MSVC + Ninja + vcpkg.
rem Needs TI_CACHE (folder for the build trees and vcpkg caches). Set it in build.local.bat (git-ignored) or the environment.
rem Optional: TI_VS (Visual Studio install folder), TI_VCVARS_VER (toolset, e.g. 14.44), TI_EXTRA_PATH (extra PATH entries).
rem Usage: build.bat [skyrim ^| fo4 ^| tests] [cmake --build arguments, e.g. --target PickingTests]
rem   skyrim  (the default, so `build.bat` and `build.bat --target X` work as before): the Skyrim plugin and the offline tests, in %TI_CACHE%\build
rem   fo4     the Fallout 4 plugin (one DLL for OG, NG and AE): build tree %TI_CACHE%\build-fo4, mod folder dist\WhatAmILookingAt-FO4
rem   tests   only the offline tests (they build from src\core, no game library or vcpkg), in %TI_CACHE%\build-tests
setlocal
if exist "%~dp0build.local.bat" call "%~dp0build.local.bat"
if "%TI_CACHE%"=="" (echo Set TI_CACHE to a build cache folder. & exit /b 1)
set "TI_GAME=skyrim"
if /i "%~1"=="skyrim" (shift)
if /i "%~1"=="tests" (set "TI_GAME=tests" & shift)
if /i "%~1"=="fo4" (set "TI_GAME=fo4" & shift)
set "TI_ARGS="
:collect
if "%~1"=="" goto collected
set "TI_ARGS=%TI_ARGS% %1"
shift
goto collect
:collected
if "%TI_VS%"=="" set "TI_VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
if "%TI_VCVARS_VER%"=="" set "TI_VCVARS_VER=14.44"
call "%TI_VS%\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=%TI_VCVARS_VER% >nul || exit /b 1
set "PATH=%PATH%;C:\Program Files\CMake\bin;%TI_EXTRA_PATH%"
set "VCPKG_ROOT=%TI_VS%\VC\vcpkg"
set "VCPKG_DOWNLOADS=%TI_CACHE%\vcpkg\downloads"
set "VCPKG_DEFAULT_BINARY_CACHE=%TI_CACHE%\vcpkg\archives"
if not exist "%VCPKG_DEFAULT_BINARY_CACHE%" mkdir "%VCPKG_DEFAULT_BINARY_CACHE%"
cd /d "%~dp0"
if "%TI_GAME%"=="tests" (
  if not exist "%TI_CACHE%\build-tests\CMakeCache.txt" (
    cmake -S . -B "%TI_CACHE%\build-tests" -G Ninja -DCMAKE_BUILD_TYPE=Release -DTI_BUILD_PLUGIN=OFF -DCMAKE_CXX_FLAGS="/EHsc" || exit /b 1
  )
  cmake --build "%TI_CACHE%\build-tests" %TI_ARGS% || exit /b 1
  exit /b 0
)
if "%TI_GAME%"=="fo4" (
  if not exist "%TI_CACHE%\build-fo4\CMakeCache.txt" (
    cmake -S . -B "%TI_CACHE%\build-fo4" -G Ninja -DCMAKE_BUILD_TYPE=Release -DTI_GAME=fo4 -DVCPKG_MANIFEST_FEATURES=fo4 ^
      -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
      -DVCPKG_TARGET_TRIPLET=x64-windows-static-md-release -DVCPKG_HOST_TRIPLET=x64-windows-static-md-release ^
      -DVCPKG_OVERLAY_TRIPLETS="%~dp0cmake\triplets" -DVCPKG_BUILD_TYPE=release ^
      "-DVCPKG_INSTALL_OPTIONS=--x-buildtrees-root=%TI_CACHE%\vcpkg\buildtrees;--x-packages-root=%TI_CACHE%\vcpkg\packages" ^
      -DCMAKE_CXX_FLAGS="/EHsc" || exit /b 1
  )
  cmake --build "%TI_CACHE%\build-fo4" %TI_ARGS% || exit /b 1
  exit /b 0
)
if not exist "%TI_CACHE%\build\CMakeCache.txt" (
  cmake -S . -B "%TI_CACHE%\build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DTI_GAME=skyrim ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-static-md-release -DVCPKG_HOST_TRIPLET=x64-windows-static-md-release ^
    -DVCPKG_OVERLAY_TRIPLETS="%~dp0cmake\triplets" -DVCPKG_BUILD_TYPE=release ^
    "-DVCPKG_INSTALL_OPTIONS=--x-buildtrees-root=%TI_CACHE%\vcpkg\buildtrees;--x-packages-root=%TI_CACHE%\vcpkg\packages" ^
    -DCMAKE_CXX_FLAGS="/EHsc" || exit /b 1
)
cmake --build "%TI_CACHE%\build" %TI_ARGS% || exit /b 1
