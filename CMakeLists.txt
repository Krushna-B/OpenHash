cmake_minimum_required(VERSION 3.23)

project(FlashKV VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#External Deps
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/releases/download/v1.16.0/googletest-1.16.0.tar.gz
)
FetchContent_MakeAvailable(googletest)

set(CMAKE_CXX_STANDARD 20) #C++ 20



#Executables
# add_executable(FlashKV src/main.cpp)
add_executable(Tests tests/test.cpp)
add_executable(Benchmark benchmark/benchmark.cpp)

#Compiler settings
target_compile_options(Benchmark PRIVATE -Wall -Wextra -O2 -g)

#Target
target_include_directories(Tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(Benchmark PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(Tests PRIVATE GTest::gtest_main)

