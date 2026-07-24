cmake_minimum_required(VERSION 3.16)
project(ggw_server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

add_executable(ggw_server ggw_server.cpp)
target_link_libraries(ggw_server PRIVATE OpenSSL::Crypto ZLIB::ZLIB Threads::Threads)
