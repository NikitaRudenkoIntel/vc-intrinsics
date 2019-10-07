set(LLVM_TARGET_DEFINITIONS ${LLVM_MAIN_INCLUDE_DIR}/llvm/IR/Intrinsics.td)
tablegen(GenX GenXIntrinsicEnum.inc -gen-intrinsic-enums)
tablegen(GenX GenXIntrinsicImpl.inc -gen-intrinsic-impl)
add_public_tablegen_target(GenXIntrinsicsGen)
