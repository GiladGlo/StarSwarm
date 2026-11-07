cmake_minimum_required(VERSION 3.16)
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

if(ANDROID)
  add_library(starswarm SHARED main.cpp)
  set_target_properties(starswarm PROPERTIES OUTPUT_NAME main)            # NativeActivity loads libmain.so
  target_include_directories(starswarm PRIVATE ${ANDROID_NDK}/sources/android/native_app_glue)
  target_link_options(starswarm PRIVATE -u ANativeActivity_onCreate)    # keep the glue's entry point
  target_link_libraries(starswarm PRIVATE raylib android log EGL GLESv3 OpenSLES)
else()
  add_executable(starswarm main.cpp)
  target_link_libraries(starswarm PRIVATE raylib)
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
