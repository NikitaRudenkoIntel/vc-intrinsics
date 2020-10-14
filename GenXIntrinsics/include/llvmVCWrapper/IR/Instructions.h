/*===================== begin_copyright_notice ==================================

INTEL CONFIDENTIAL
Copyright 2000-2020
Intel Corporation All Rights Reserved.

The source code contained or described herein and all documents related to the
source code ("Material") are owned by Intel Corporation or its suppliers or
licensors. Title to the Material remains with Intel Corporation or its suppliers
and licensors. The Material contains trade secrets and proprietary and confidential
information of Intel or its suppliers and licensors. The Material is protected by
worldwide copyright and trade secret laws and treaty provisions. No part of the
Material may be used, copied, reproduced, modified, published, uploaded, posted,
transmitted, distributed, or disclosed in any way without Intel's prior express
written permission.

No license under any patent, copyright, trade secret or other intellectual
property right is granted to or conferred upon you by disclosure or delivery
of the Materials, either expressly, by implication, inducement, estoppel or
otherwise. Any license under such intellectual property rights must be express
and approved by Intel in writing.

======================= end_copyright_notice ==================================*/

#ifndef VCINTR_IR_INSTRUCTIONS_H
#define VCINTR_IR_INSTRUCTIONS_H

#include <algorithm>
#include <llvm/IR/Instructions.h>

namespace VCINTR {
namespace ShuffleVectorInst {
auto static constexpr UndefMaskElem = -1;

// LLVM <= 10 does not have ShuffleVectorInst ctor which accepts ArrayRef<int>
// This method returns mask with appropriate type for ShuffleVectorInst ctor
#if VC_INTR_LLVM_VERSION_MAJOR <= 10
static llvm::Constant *getShuffleMask(llvm::ArrayRef<int> Mask,
                                      llvm::LLVMContext &Context) {
  using namespace llvm;
  auto Indices = SmallVector<llvm::Constant *, 8>{};
  auto *Int32Ty = IntegerType::getInt32Ty(Context);
  std::transform(Mask.begin(), Mask.end(), std::back_inserter(Indices),
                 [&](int El) -> llvm::Constant * {
                   if (El == UndefMaskElem)
                     return UndefValue::get(Int32Ty);
                   else
                     return ConstantInt::get(Int32Ty, El);
                 });
  return ConstantVector::get(Indices);
}
#else
static llvm::ArrayRef<int> getShuffleMask(llvm::ArrayRef<int> Mask,
                                          llvm::LLVMContext &Context) {
  return Mask;
}
#endif

} // namespace VCINTR

} // namespace VCINTR

#endif // VCINTR_IR_INSTRUCTIONS_H
