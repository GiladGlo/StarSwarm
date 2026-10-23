cmake_minimum_required(VERSION 3.16)
project(StarSwarm CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Use an installed raylib if present, otherwise download and build it automatically.
find_package(raylib 5.0 QUIET)
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

add_executable(starswarm main.cpp)
target_link_libraries(starswarm PRIVATE raylib)

if(WIN32)
  # No console window in Release builds on Windows
  set_target_properties(starswarm PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
  if(MSVC)
    target_link_options(starswarm PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
  endif()
endif()
if(APPLE)
  target_link_libraries(starswarm PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()
