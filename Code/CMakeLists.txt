cmake_minimum_required(VERSION 3.16)
# MSVC: link the C/C++ runtime statically (/MT), for raylib too (CMP0091 forced on for its older CMake files),
# so the exe runs on any Windows PC without the Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
project(StarSwarm CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Android (android/build-apk.ps1 configures this with the NDK toolchain): raylib's Android backend on OpenGL ES 3.0,
# and the game is built as libmain.so for a NativeActivity instead of an executable.
if(ANDROID)
  set(PLATFORM "Android" CACHE STRING "" FORCE)
  set(OPENGL_VERSION "ES 3.0" CACHE STRING "" FORCE)
endif()

# Use an installed raylib if present, otherwise download and build it automatically.
if(NOT ANDROID)
  find_package(raylib 5.0 QUIET)
endif()
if(NOT raylib_FOUND)
  include(FetchContent)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(raylib)
endif()

# main.cpp: platform glue, startup loader and main loop; game.h: struct Game; game_*.cpp: its code, by area
set(GAME_SOURCES main.cpp
  game_audio.cpp game_stage.cpp game_meteors.cpp game_boss.cpp game_ship.cpp game_enemies.cpp game_menus.cpp
  game_update.cpp game_weapons.cpp game_title.cpp game_draw.cpp game_hud.cpp game_save.cpp)

if(ANDROID)
  add_library(starswarm SHARED ${GAME_SOURCES})
  set_target_properties(starswarm PROPERTIES OUTPUT_NAME main)            # NativeActivity loads libmain.so
  target_include_directories(starswarm PRIVATE ${ANDROID_NDK}/sources/android/native_app_glue)
  # keep the glue's entry point; route android_main through main.cpp (one game per process)
  target_link_options(starswarm PRIVATE -u ANativeActivity_onCreate -Wl,--wrap=android_main)
  target_link_libraries(starswarm PRIVATE raylib android log EGL GLESv3 OpenSLES)
else()
  add_executable(starswarm ${GAME_SOURCES})
  target_link_libraries(starswarm PRIVATE raylib)
endif()

# Compiler warnings for the game's own code (not raylib's)
if(MSVC)
  target_compile_options(starswarm PRIVATE /W4)
  target_compile_definitions(starswarm PRIVATE _CRT_SECURE_NO_WARNINGS)   # getenv/fopen are fine here
else()
  # (raylib's raymath.h zero-fills structs with { 0 }, which -Wextra would flag in every file)
  target_compile_options(starswarm PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

# Balance-testing autopilot (bot.h, run with --bot). Off by default: release builds never contain it.
#   cmake -B build-bot -DSTARSWARM_BOT=ON && cmake --build build-bot --config Release
option(STARSWARM_BOT "Build the autoplay bot (--bot) for balance testing" OFF)
if(STARSWARM_BOT)
  target_compile_definitions(starswarm PRIVATE STARSWARM_BOT)
endif()

# Content fingerprint for the startup cache: a hash of the art/sound generator sources, regenerated every build
# (contenthash.h only changes when one of these files does).
set(CONTENT_SOURCES sprites.h monsters.h meteor3d.h sfxsynth.h titlemusic.h gamemusic.h)
list(TRANSFORM CONTENT_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)
string(REPLACE ";" "," CONTENT_FILES "${CONTENT_SOURCES}")
add_custom_target(contenthash ALL
  COMMAND ${CMAKE_COMMAND} -DFILES=${CONTENT_FILES} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/contenthash.h
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/contenthash.cmake
  BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/contenthash.h
  COMMENT "Fingerprinting art/sound generators for the startup cache"
  VERBATIM)
add_dependencies(starswarm contenthash)
target_include_directories(starswarm PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

if(WIN32)
  target_sources(starswarm PRIVATE starswarm.rc)   # exe + window icon (starswarm.ico)
  # No console window in Release builds on Windows
  set_target_properties(starswarm PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
  if(MSVC)
    target_link_options(starswarm PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
  endif()
endif()
if(APPLE)
  target_link_libraries(starswarm PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()
