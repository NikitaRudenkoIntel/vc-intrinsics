set(LLVM_LINK_COMPONENTS Core)

add_llvm_library(LLVMGenXIntrinsics
  GenXIntrinsics.cpp

  ADDITIONAL_HEADER_DIRS
  ${GENX_INTRINSICS_MAIN_INCLUDE_DIR}/llvm/GenXIntrinsics

  LINK_LIBS
    LLVMCodeGen

  DEPENDS
    GenXIntrinsicsGen
    intrinsics_gen
)
