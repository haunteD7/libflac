cmake_minimum_required(VERSION 4.3.0)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)    
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Release optimizations
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")
#
project(convflac)

add_executable(flac
    main.cpp
)
add_subdirectory(libflac)
target_link_libraries(flac 
    libflac
)