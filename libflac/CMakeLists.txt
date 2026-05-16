cmake_minimum_required(VERSION 4.3.0)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)    
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(libflac)

add_library(libflac
    src/bit_reader.cpp
    src/bit_writer.cpp
)
target_include_directories(libflac PUBLIC include)