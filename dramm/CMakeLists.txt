# 6GGW / NetSwitch — dramm tight compute kernel.
#
# Honest version note: C++17 needs CMake >= 3.8 (that's when CMAKE_CXX_STANDARD 17 landed). There is
# no usable "CMake 1.4 / 1.9" for this — those are ~2000-era releases that predate C++17 by a decade.
# 3.8 is the real floor; 3.18+ is what you want for the CUDA half (first-class CUDA language support).
cmake_minimum_required(VERSION 3.8)
project(ggw_dramm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- CPU kernel: always builds, no dependencies ---
add_executable(ggw_dramm ggw_dramm.cpp)
target_compile_options(ggw_dramm PRIVATE -O2)

# --- CUDA twin: built only if a CUDA compiler is present (needs an NVIDIA box + nvcc) ---
include(CheckLanguage)
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
  enable_language(CUDA)
  add_executable(ggw_dramm_cuda ggw_dramm.cu)
  set_target_properties(ggw_dramm_cuda PROPERTIES CUDA_STANDARD 17)
  # Set your card's arch if CMake can't detect it, e.g. set(CMAKE_CUDA_ARCHITECTURES 86) for Ampere.
  message(STATUS "CUDA found (${CMAKE_CUDA_COMPILER}) — building ggw_dramm_cuda")
else()
  message(STATUS "No CUDA compiler — skipping ggw_dramm_cuda (CPU kernel still builds).")
endif()
