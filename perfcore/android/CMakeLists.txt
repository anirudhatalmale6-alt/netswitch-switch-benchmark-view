# Android NDK build for the native perfcore engine.
# Drop into an app module and reference from build.gradle:
#   android { defaultConfig { externalNativeBuild { cmake { cppFlags "-std=c++17 -O2" } } }
#            externalNativeBuild { cmake { path "src/main/cpp/perfcore/android/CMakeLists.txt" } } }
cmake_minimum_required(VERSION 3.18.1)
project(perfcore)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# perfcore_jni.cpp #includes ../ggw_perfcore.cpp with PERFCORE_NO_MAIN, so one .cpp
# builds the whole engine + the JNI surface. Same engine file the iOS target uses.
add_library(perfcore SHARED perfcore_jni.cpp)

# link the Android log lib (available if the JNI later logs)
find_library(log-lib log)
target_link_libraries(perfcore ${log-lib})
