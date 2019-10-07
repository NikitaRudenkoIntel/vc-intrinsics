set(LLVM_LINK_COMPONENTS Core)

add_llvm_library(LLVMGenXIntrinsics
  GenXIntrinsics.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/GenXIntrinsics

  DEPENDS
    GenXIntrinsicsGen
    intrinsics_gen
)
