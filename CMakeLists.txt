cmake_minimum_required(VERSION 3.3)

set(LLVM_GENX_INTRINSICS_VERSION 1.0)

# check if we build inside llvm or not
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(BUILD_EXTERNAL YES)
  project(LLVM_GenXIntrinsics
    VERSION
      ${LLVM_GENX_INTRINSICS_VERSION}
    LANGUAGES
      C CXX
  )

  set(CMAKE_CXX_STANDARD 11)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)

  find_package(LLVM 8.0.0 REQUIRED)
  set(CMAKE_MODULE_PATH
    ${CMAKE_MODULE_PATH}
    ${LLVM_CMAKE_DIR}
  )
  include(AddLLVM)
  include(HandleLLVMOptions)

  include_directories("${LLVM_INCLUDE_DIR}")
  link_directories("${LLVM_LIBRARY_DIR}")

  message(STATUS "Found LLVM: ${LLVM_VERSION}")
else(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(BUILD_EXTERNAL NO)
endif(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)

include(FindPythonInterp)
if( NOT PYTHONINTERP_FOUND )
  message(FATAL_ERROR
"Unable to find Python interpreter, required for builds and testing.

Please install Python or specify the PYTHON_EXECUTABLE CMake variable.")
endif()

if( "${PYTHON_VERSION_STRING}" VERSION_LESS 2.7 )
message(FATAL_ERROR "Python 2.7 or newer is required")
endif()

set(__INTEL_EMBARGO__ ON CACHE BOOL "Build with IP sensitive code")
if( __INTEL_EMBARGO__ )
  message("-- Building with IP sensitive code")
  add_definitions(-D__INTEL_EMBARGO__)
endif()

if(NOT IGNORE_VC_INTRINSICS)
add_subdirectory(GenXIntrinsics)
else()
message("Ignoring intrinsics")
endif()

if(NOT IGNORE_SPIRV_TRANSLATOR)
add_subdirectory(SPIRV-LLVM-Translator)
else()
message("Ignoring SPIRV-Translator")
endif()
