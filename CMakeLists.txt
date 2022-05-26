cmake_minimum_required(VERSION 3.21)
project(serialization)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(
        tinyxml2
        GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
        GIT_TAG 9.0.0
)
FetchContent_MakeAvailable(tinyxml2)

include_directories(include)
include_directories(tinyxml2)

add_executable(serialization src/main.cpp include/binary.hpp)

target_link_libraries(serialization PRIVATE tinyxml2)
