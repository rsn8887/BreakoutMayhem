cmake_minimum_required(VERSION 3.16)

# Breakout Mayhem
#   Desktop (Linux / macOS / Windows MSYS2):  cmake -S BreakoutMayhem -B build && cmake --build build
#   macOS app bundle:                         cmake -S BreakoutMayhem -B build-mac -DMACOS_BUNDLE=ON
#   Nintendo Switch (devkitPro):              cmake -S BreakoutMayhem -B build-switch -DPLATFORM=SWITCH
# Always build out of the source tree.

set(PLATFORM "DESKTOP" CACHE STRING "DESKTOP or SWITCH")

if(PLATFORM STREQUAL "SWITCH" AND NOT CMAKE_TOOLCHAIN_FILE)
    if(NOT DEFINED ENV{DEVKITPRO})
        set(ENV{DEVKITPRO} /opt/devkitpro)
    endif()
    set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/Switch.cmake")
endif()

project(BreakoutMayhem VERSION 1.4.0 LANGUAGES C CXX)

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "Please build out of the source tree, e.g. cmake -S BreakoutMayhem -B build")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(SOURCES
    src/main.cpp
    src/game.cpp
    src/physics.cpp
    src/levels.cpp
    src/render.cpp
    src/audio.cpp
    src/platform.cpp)

set(DATA_FILES
    ${CMAKE_SOURCE_DIR}/data/Poppins-Bold.ttf
    ${CMAKE_SOURCE_DIR}/data/Poppins-OFL.txt)

option(MACOS_BUNDLE "Build a macOS .app bundle" OFF)

if(PLATFORM STREQUAL "SWITCH")
    # ---------------------------------------------------------------- Nintendo Switch
    add_executable(BreakoutMayhem ${SOURCES})
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SWLIBS REQUIRED sdl2 SDL2_ttf)
    target_include_directories(BreakoutMayhem PRIVATE ${SWLIBS_STATIC_INCLUDE_DIRS})
    target_compile_options(BreakoutMayhem PRIVATE ${SWLIBS_STATIC_CFLAGS_OTHER})
    target_link_libraries(BreakoutMayhem PRIVATE ${SWLIBS_STATIC_LDFLAGS})

    set(ROMFS_DIR ${CMAKE_BINARY_DIR}/romfs)
    file(MAKE_DIRECTORY ${ROMFS_DIR}/data)
    file(COPY ${DATA_FILES} DESTINATION ${ROMFS_DIR}/data)

    nx_generate_nacp(BreakoutMayhem.nacp
        NAME "Breakout Mayhem"
        AUTHOR "rsn8887"
        VERSION "${PROJECT_VERSION}")
    nx_create_nro(BreakoutMayhem
        NACP BreakoutMayhem.nacp
        ICON ${CMAKE_SOURCE_DIR}/res/icon256.jpg
        ROMFS ${ROMFS_DIR})
else()
    # ---------------------------------------------------------------- Desktop
    find_package(SDL2 CONFIG QUIET)
    find_package(SDL2_ttf CONFIG QUIET)
    if(MACOS_BUNDLE AND APPLE)
        # generate the .icns from res/icon1024.png into the build folder
        set(ICON_FILE ${CMAKE_BINARY_DIR}/BreakoutMayhem.icns)
        set(ICONSET ${CMAKE_BINARY_DIR}/BreakoutMayhem.iconset)
        if(NOT EXISTS ${ICON_FILE})
            file(MAKE_DIRECTORY ${ICONSET})
            foreach(S 16 32 128 256 512)
                math(EXPR S2 "${S} * 2")
                execute_process(COMMAND sips -z ${S} ${S} ${CMAKE_SOURCE_DIR}/res/icon1024.png --out ${ICONSET}/icon_${S}x${S}.png OUTPUT_QUIET)
                execute_process(COMMAND sips -z ${S2} ${S2} ${CMAKE_SOURCE_DIR}/res/icon1024.png --out ${ICONSET}/icon_${S}x${S}@2x.png OUTPUT_QUIET)
            endforeach()
            execute_process(COMMAND iconutil -c icns ${ICONSET} -o ${ICON_FILE})
        endif()
        set(BUNDLE_RES ${DATA_FILES})
        if(EXISTS ${ICON_FILE})
            list(APPEND BUNDLE_RES ${ICON_FILE})
            set_source_files_properties(${ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        endif()
        set_source_files_properties(${DATA_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/data)
        add_executable(BreakoutMayhem MACOSX_BUNDLE ${SOURCES} ${BUNDLE_RES})
        set_target_properties(BreakoutMayhem PROPERTIES
            OUTPUT_NAME "Breakout Mayhem"
            MACOSX_BUNDLE_BUNDLE_NAME "Breakout Mayhem"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.rsn8887.breakoutmayhem"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_ICON_FILE "BreakoutMayhem.icns")
    else()
        add_executable(BreakoutMayhem WIN32 ${SOURCES})
        add_custom_command(TARGET BreakoutMayhem POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:BreakoutMayhem>/data
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DATA_FILES} $<TARGET_FILE_DIR:BreakoutMayhem>/data)
    endif()

    if(TARGET SDL2::SDL2 AND TARGET SDL2_ttf::SDL2_ttf)
        if(TARGET SDL2::SDL2main)
            target_link_libraries(BreakoutMayhem PRIVATE SDL2::SDL2main)
        endif()
        target_link_libraries(BreakoutMayhem PRIVATE SDL2::SDL2 SDL2_ttf::SDL2_ttf)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDLLIBS REQUIRED IMPORTED_TARGET sdl2 SDL2_ttf)
        target_link_libraries(BreakoutMayhem PRIVATE PkgConfig::SDLLIBS)
    endif()

    if(MSVC)
        target_compile_options(BreakoutMayhem PRIVATE /W3)
    else()
        target_compile_options(BreakoutMayhem PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endif()
