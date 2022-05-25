cmake_minimum_required(VERSION 3.21)
project(serialization)

set(CMAKE_CXX_STANDARD 20)

include_directories(include)

add_executable(serialization src/main.cpp include/binary.hpp)
