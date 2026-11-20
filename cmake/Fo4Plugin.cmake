# The Fallout 4 plugin (included by CMakeLists.txt when TI_GAME=fo4). One DLL for OG 1.10.163, NG 1.10.984 and AE 1.11.240:
# Dear-Modding-FO4's multi-runtime commonlibf4 (COMMONLIB_RUNTIMECOUNT=3) resolves every address per runtime at load.
# commonlibf4 has no CMake of its own (its commonlib-shared does), so the game library is built here from its sources.
add_compile_definitions(FALLOUT4)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

if(MSVC)
    # Privacy: keep the build machine's folder names out of the DLL (__FILE__ strings): trim the repo folder and everything above it, so paths read src... and extern...
    cmake_path(NATIVE_PATH CMAKE_SOURCE_DIR NORMALIZE TI_REPO_NATIVE)
    add_compile_options("/d1trimfile:${TI_REPO_NATIVE}\\")
endif()

set(CLF4 "${CMAKE_SOURCE_DIR}/extern/CommonLibF4")
if(NOT EXISTS "${CLF4}/lib/commonlib-shared/CMakeLists.txt")
    message(FATAL_ERROR "extern/CommonLibF4 is not checked out with its commonlib-shared: run git submodule update --init extern/CommonLibF4 extern/CommonLibF4/lib/commonlib-shared")
endif()

add_subdirectory("${CLF4}/lib/commonlib-shared" commonlib-shared EXCLUDE_FROM_ALL)

file(GLOB_RECURSE TI_CLF4_SOURCES CONFIGURE_DEPENDS "${CLF4}/src/*.cpp")
add_library(commonlibf4 STATIC ${TI_CLF4_SOURCES})
target_include_directories(commonlibf4 PUBLIC "${CLF4}/include")
target_link_libraries(commonlibf4 PUBLIC commonlib-shared::commonlib-shared)
target_compile_definitions(commonlibf4 PUBLIC COMMONLIB_RUNTIMECOUNT=3)
target_compile_features(commonlibf4 PUBLIC cxx_std_23)
target_precompile_headers(commonlibf4 PRIVATE "${CLF4}/include/F4SE/Impl/PCH.h")
if(MSVC)
    target_compile_options(commonlibf4 PRIVATE /permissive- /Zc:preprocessor /Zc:__cplusplus /EHsc /MP /utf-8 /W3
        /wd4200 /wd4201 /wd4324)
endif()

find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h" REQUIRED)
find_path(ROBIN_HOOD_INCLUDE_DIRS "robin_hood.h" REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)

set(TI_FO4_SOURCES
    src/fo4/Camera.cpp
    src/fo4/Inspector.cpp
    src/fo4/Main.cpp
    src/fo4/OverlayHooks.cpp
    src/fo4/ReportFill.cpp
    src/fo4/Sample.cpp
    src/fo4/TextureHold.cpp
    src/fo4/TextureInfo.cpp
    src/fo4/WorldFill.cpp
)

add_library(${PROJECT_NAME} SHARED ${TI_CORE_SOURCES} ${TI_FO4_SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "WhatAmILookingAt")
# The core sources are compiled without the precompiled header (it pulls in the game's headers), so a game type leaking into
# src/core is a compile error here as it is in the offline tests.
set_source_files_properties(${TI_CORE_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
target_include_directories(${PROJECT_NAME} PRIVATE src/core src/fo4 ${SIMPLEINI_INCLUDE_DIRS} ${ROBIN_HOOD_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE commonlibf4 imgui::imgui Microsoft::DirectXTK d3d11 dxgi windowscodecs ole32 uuid)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/fo4/PCH.h)
target_compile_definitions(${PROJECT_NAME} PRIVATE _UNICODE UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /Zc:preprocessor /Zc:__cplusplus /EHsc /MP /utf-8
        /wd4200 /wd4201 /wd4324)
    target_link_options(${PROJECT_NAME} PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF /INCREMENTAL:NO "/PDBALTPATH:%_PDB%")
endif()

# dist: a ready MO2 mod folder (never installed from here)
set(TI_DIST "${CMAKE_SOURCE_DIR}/dist/WhatAmILookingAt-FO4")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TI_DIST}/F4SE/Plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${TI_DIST}/F4SE/Plugins/WhatAmILookingAt.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/dist-src/fo4/WhatAmILookingAt.ini" "${TI_DIST}/F4SE/Plugins/WhatAmILookingAt.ini"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TI_DIST}/F4SE/Plugins/WhatAmILookingAt/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/dist-src/fonts" "${TI_DIST}/F4SE/Plugins/WhatAmILookingAt/fonts"
)
