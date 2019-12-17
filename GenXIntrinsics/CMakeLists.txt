cmake_minimum_required(VERSION 3.3)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)

set(GENX_INTRINSICS_DESCRIPTION "GenXIntrinsicDescription.gen")

add_subdirectory(include/llvm)
add_subdirectory(lib)

# LLVM doesn't install gtest which is required for our tests
# so we cannot simply enable current unittests with prebuilt LLVM
if( NOT BUILD_EXTERNAL )
  add_subdirectory(unittests)
endif()

install(DIRECTORY include/llvm
  DESTINATION include
  COMPONENT genx-intrinsics-headers
  FILES_MATCHING
  PATTERN "*.h"
)
# cmake creates too many subdirectories in build directory
# and then "install(DIRECTORY" installs them even if they are empty
# so generated file has to be installed separetely
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/llvm/GenXIntrinsics/${GENX_INTRINSICS_DESCRIPTION}
  DESTINATION include/llvm/GenXIntrinsics
  COMPONENT genx-intrinsics-headers
)
