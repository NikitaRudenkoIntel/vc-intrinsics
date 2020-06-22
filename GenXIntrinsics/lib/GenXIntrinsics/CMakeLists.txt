if(BUILD_EXTERNAL)
  add_library(LLVMGenXIntrinsics 
              GenXIntrinsics.cpp
              GenXRestoreIntrAttr.cpp
              GenXSimdCFLowering.cpp
              GenXSPIRVReaderAdaptor.cpp
              GenXSPIRVWriterAdaptor.cpp
             )
  llvm_update_compile_flags(LLVMGenXIntrinsics)
  add_dependencies(LLVMGenXIntrinsics GenXIntrinsicsGen)

  # if LLVM_LINK_LLVM_DYLIB is ON, it means that we use dynamic llvm library
  # and thus we don't have libLLVMCodeGen.a, thus we don't set the dependency
  # in this case
  if(NOT LLVM_LINK_LLVM_DYLIB)
    add_dependencies(LLVMGenXIntrinsics LLVMCodeGen)
    target_link_libraries(LLVMGenXIntrinsics LLVMCodeGen)
  endif()

  target_include_directories(LLVMGenXIntrinsics PUBLIC
    $<BUILD_INTERFACE:${GENX_INTRINSICS_MAIN_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
  )
else()
  # when we are building in LLVM infra, we need to conform
  set(LLVM_LINK_COMPONENTS Core)

  add_llvm_library(LLVMGenXIntrinsics
    GenXIntrinsics.cpp
    GenXRestoreIntrAttr.cpp
    GenXSimdCFLowering.cpp
    GenXSPIRVReaderAdaptor.cpp
    GenXSPIRVWriterAdaptor.cpp
    ADDITIONAL_HEADER_DIRS
    ${GENX_INTRINSICS_MAIN_INCLUDE_DIR}/llvm/GenXIntrinsics

    LINK_LIBS
      LLVMCodeGen
      LLVMSupport

    DEPENDS
      GenXIntrinsicsGen
      intrinsics_gen
  )
endif()
