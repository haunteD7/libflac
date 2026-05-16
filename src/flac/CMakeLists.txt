cmake_minimum_required(VERSION 4.3.0)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)    
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(flacapp)

add_executable(flacapp
    flac.cpp
)
add_subdirectory(../)
target_link_libraries(flacapp libflac)