cmake_minimum_required(VERSION 3.24)

project(WhatAmILookingAt VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Layout: src/core has no game headers (card, layout, settings, report, output, overlay drawing); src/skyrim is the
# Skyrim game layer; src/fo4 is the Fallout 4 game layer (cmake/Fo4Plugin.cmake). build.bat picks the game.
set(TI_GAME "skyrim" CACHE STRING "Game to build the plugin for: skyrim or fo4")
set_property(CACHE TI_GAME PROPERTY STRINGS skyrim fo4)
option(TI_BUILD_PLUGIN "Build the game plugin (needs the game's CommonLib and vcpkg)" ON)
option(TI_BUILD_TESTS "Build the offline tests (they build from src/core only)" ON)

if(TI_BUILD_PLUGIN AND NOT (TI_GAME STREQUAL "skyrim" OR TI_GAME STREQUAL "fo4"))
    message(FATAL_ERROR "TI_GAME=${TI_GAME}: expected skyrim or fo4.")
endif()

set(TI_CORE_SOURCES
    src/core/Ba2.cpp
    src/core/Inflate.cpp
    src/core/Bsa.cpp
    src/core/Card.cpp
    src/core/CardLayout.cpp
    src/core/CardState.cpp
    src/core/Config.cpp
    src/core/Diagnostics.cpp
    src/core/Fo4Names.cpp
    src/core/IniRead.cpp
    src/core/Output.cpp
    src/core/Overlay.cpp
    src/core/PickScan.cpp
    src/core/Report.cpp
)

# ---- offline tests: the picking maths has no game dependency -------------------------------
if(TI_BUILD_TESTS)
    enable_testing()
    add_executable(PickingTests tests/PickingTests.cpp src/core/Output.cpp src/core/Report.cpp)
    target_include_directories(PickingTests PRIVATE src/core tests)
    if(MSVC)
        target_compile_options(PickingTests PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME PickingTests COMMAND PickingTests)

    add_executable(ReportTests tests/ReportTests.cpp src/core/Output.cpp src/core/Report.cpp src/core/Diagnostics.cpp src/core/CardLayout.cpp src/core/Bsa.cpp)
    target_include_directories(ReportTests PRIVATE src/core tests)
    if(MSVC)
        target_compile_options(ReportTests PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME ReportTests COMMAND ReportTests)

    add_executable(Ba2Tests tests/Ba2Tests.cpp src/core/Ba2.cpp src/core/Inflate.cpp)
    target_include_directories(Ba2Tests PRIVATE src/core tests)
    if(MSVC)
        target_compile_options(Ba2Tests PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME Ba2Tests COMMAND Ba2Tests)

    add_executable(PickScanTests tests/PickScanTests.cpp src/core/PickScan.cpp src/core/Fo4Names.cpp)
    target_include_directories(PickScanTests PRIVATE src/core tests)
    if(MSVC)
        target_compile_options(PickScanTests PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME PickScanTests COMMAND PickScanTests)

    add_executable(ConfigTests tests/ConfigTests.cpp src/core/IniRead.cpp)
    target_include_directories(ConfigTests PRIVATE src/core tests)
    if(MSVC)
        target_compile_options(ConfigTests PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME ConfigTests COMMAND ConfigTests)

    add_executable(CardLayoutTests tests/CardLayoutTests.cpp src/core/CardLayout.cpp)
    target_include_directories(CardLayoutTests PRIVATE src/core tests)
    if(MSVC)
        target_compile_options(CardLayoutTests PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME CardLayoutTests COMMAND CardLayoutTests)

    add_executable(SettingsTests tests/SettingsTests.cpp src/core/CardState.cpp)
    target_include_directories(SettingsTests PRIVATE src/core tests)
    if(MSVC)
        target_compile_options(SettingsTests PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME SettingsTests COMMAND SettingsTests)
endif()

if(NOT TI_BUILD_PLUGIN)
    return()
endif()

# ---- plugin ---------------------------------------------------------------------------------
if(TI_GAME STREQUAL "fo4")
    include(cmake/Fo4Plugin.cmake)
    return()
endif()

add_compile_definitions(SKYRIM)

set(COMMONLIB_PREBUILT ON CACHE BOOL "Use prebuilt CommonLibSSE when it matches" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

if(MSVC)
    # Privacy: keep the build machine's folder names out of the DLL (__FILE__ strings): trim the repo folder and everything above it, so paths read src... and extern...
    cmake_path(NATIVE_PATH CMAKE_SOURCE_DIR NORMALIZE TI_REPO_NATIVE)
    add_compile_options("/d1trimfile:${TI_REPO_NATIVE}\\")
endif()

add_subdirectory(extern/CommonLibSSE-NG CommonLibSSE EXCLUDE_FROM_ALL)
include(extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake)

find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h" REQUIRED)
find_path(ROBIN_HOOD_INCLUDE_DIRS "robin_hood.h" REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    NAME "WhatAmILookingAt"
    AUTHOR "sayngwyn"
    VERSION ${PROJECT_VERSION}
    USE_ADDRESS_LIBRARY
    SOURCES
        ${TI_CORE_SOURCES}
        src/skyrim/Main.cpp
        src/skyrim/Inspector.cpp
        src/skyrim/Camera.cpp
        src/skyrim/TextureInfo.cpp
        src/skyrim/ReportFill.cpp
        src/skyrim/WorldFill.cpp
        src/skyrim/TextureHold.cpp
        src/skyrim/OverlayHooks.cpp
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "WhatAmILookingAt")
# The core sources are compiled without the precompiled header (it pulls in the game's headers), so a game type leaking into
# src/core is a compile error here as it is in the offline tests.
set_source_files_properties(${TI_CORE_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
target_include_directories(${PROJECT_NAME} PRIVATE src/core src/skyrim ${SIMPLEINI_INCLUDE_DIRS} ${ROBIN_HOOD_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE imgui::imgui Microsoft::DirectXTK d3d11 dxgi windowscodecs ole32 uuid)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/skyrim/PCH.h)
target_compile_definitions(${PROJECT_NAME} PRIVATE _UNICODE UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /Zc:preprocessor /Zc:__cplusplus /EHsc /MP
        /wd4200 /wd4201 /wd4324)
    target_link_options(${PROJECT_NAME} PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF /INCREMENTAL:NO "/PDBALTPATH:%_PDB%")
endif()

# dist: a ready MO2 mod folder (never installed from here)
set(TI_DIST "${CMAKE_SOURCE_DIR}/dist/WhatAmILookingAt")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TI_DIST}/SKSE/Plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${TI_DIST}/SKSE/Plugins/WhatAmILookingAt.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/dist-src/WhatAmILookingAt.ini" "${TI_DIST}/SKSE/Plugins/WhatAmILookingAt.ini"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TI_DIST}/SKSE/Plugins/WhatAmILookingAt/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/dist-src/fonts" "${TI_DIST}/SKSE/Plugins/WhatAmILookingAt/fonts"
)
