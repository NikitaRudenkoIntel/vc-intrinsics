cmake_minimum_required(VERSION 3.3)

set(LLVM_GENX_INTRINSICS_VERSION 1.0)

if( NOT PYTHON_EXECUTABLE )
  include(FindPythonInterp)
  if( NOT PYTHONINTERP_FOUND )
    message(FATAL_ERROR
  "Unable to find Python interpreter, required for builds and testing.

  Please install Python or specify the PYTHON_EXECUTABLE CMake variable.")
  endif()

  if( ${PYTHON_VERSION_STRING} VERSION_LESS 2.7 )
    message(FATAL_ERROR "Python 2.7 or newer is required")
  endif()
endif()

add_subdirectory(GenXIntrinsics)
