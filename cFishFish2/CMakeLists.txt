# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
# 3.15+ is needed for CMAKE_MSVC_RUNTIME_LIBRARY (VS 2019 ships 3.20).
cmake_minimum_required (VERSION 3.15)

project ("cFishFish2" LANGUAGES CXX)

# ---- Language standard -------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Build type --------------------------------------------------------------
# Single-config generators (Ninja, Makefiles) default to Release if none given.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---- Options -----------------------------------------------------------------
# ON  : AVX2 + BMI2 + POPCNT (/arch:AVX2 on MSVC, -march=native elsewhere)
# OFF : baseline x86-64, runs on any 64-bit CPU
option(CFISH_NATIVE "Build for modern CPUs (AVX2/BMI2/POPCNT)" ON)

# Link the C++ runtime statically so the .exe runs on machines without the
# VC++ redistributable (handy when loading the engine into a chess GUI elsewhere).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Include sub-projects.
add_subdirectory ("cFishFish2")
