cmake_minimum_required(VERSION 3.3)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)

add_subdirectory(include/llvm)
add_subdirectory(lib)
add_subdirectory(unittests)
