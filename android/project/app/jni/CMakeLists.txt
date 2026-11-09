cmake_minimum_required(VERSION 3.16)
project(BreakoutMayhemAndroid C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL2 as a shared library (libSDL2.so), which SDL's Java side loads
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${SDL2_SRC} SDL2)

# SDL2_ttf as a static library with its bundled FreeType (no HarfBuzz needed).
# A static SDL2_ttf looks for the target SDL2::SDL2-static; point it at the shared SDL2 built above
# (SDL2_ttf only needs SDL's headers and symbols, libmain.so links against libSDL2.so).
if(NOT TARGET SDL2::SDL2-static)
    add_library(SDL2::SDL2-static ALIAS SDL2)
endif()
set(BUILD_SHARED_LIBS OFF)
set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${SDL2_TTF_SRC} SDL2_ttf)

# the game itself: libmain.so (SDL calls SDL_main, which is the game's main())
file(GLOB GAME_SOURCES ${GAME_SRC}/*.cpp)
add_library(main SHARED ${GAME_SOURCES})
target_link_libraries(main PRIVATE SDL2::SDL2 SDL2_ttf)
if(ANDROID)
    target_link_libraries(main PRIVATE android log)
endif()
