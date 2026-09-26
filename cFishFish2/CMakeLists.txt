# CMakeList.txt : CMake project for cFishFish2, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.15)

# Add source to this project's executable.
add_executable (cFishFish2 "cFishFish2.cpp" "cFishFish2.h" "uci.h" "Chess.h" "Search.h" "Eval.h" "TT.h" "Book.h" "BookData.h")

target_compile_features(cFishFish2 PRIVATE cxx_std_20)

# True for optimized builds (Release and RelWithDebInfo)
set(OPTIMIZED "$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>")

if(MSVC)  # MSVC and clang-cl
    target_compile_options(cFishFish2 PRIVATE
        /permissive-            # strict standards conformance
        /Zc:__cplusplus         # report the real __cplusplus value
        /W4
        "$<${OPTIMIZED}:/O2;/Ob2;/Oi;/Ot;/GS-;/Gw;/DNDEBUG>"
    )
    if(CFISH_NATIVE)
        target_compile_options(cFishFish2 PRIVATE /arch:AVX2)
    endif()
    target_link_options(cFishFish2 PRIVATE
        "$<${OPTIMIZED}:/OPT:REF;/OPT:ICF>"
    )
else()    # GCC / Clang
    target_compile_options(cFishFish2 PRIVATE
        -Wall -Wextra
        "$<${OPTIMIZED}:-O3;-DNDEBUG>"
    )
    if(CFISH_NATIVE)
        target_compile_options(cFishFish2 PRIVATE -march=native)
    endif()
endif()

# Lets code check "#if CFISH_NATIVE" before using pext/pdep, AVX2 intrinsics, etc.
if(CFISH_NATIVE)
    target_compile_definitions(cFishFish2 PRIVATE CFISH_NATIVE=1)
endif()

# Link-time optimization (/GL + /LTCG on MSVC, -flto elsewhere) for optimized builds
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_OK OUTPUT IPO_MSG LANGUAGES CXX)
if(IPO_OK)
    set_property(TARGET cFishFish2 PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET cFishFish2 PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
else()
    message(STATUS "LTO not supported: ${IPO_MSG}")
endif()

# Copy EPD test suites next to the executable so "testsuite wac.epd" works
# when the engine is run from its build folder.
configure_file(wac.epd ${CMAKE_CURRENT_BINARY_DIR}/wac.epd COPYONLY)

# TODO: Add tests and install targets if needed.
