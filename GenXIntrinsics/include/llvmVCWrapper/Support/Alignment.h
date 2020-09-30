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

#ifndef VCINTR_IR_ALIGNMENT_H
#define VCINTR_IR_ALIGNMENT_H

#include <llvm/IR/Instructions.h>

namespace VCINTR {

namespace Align {

#if VC_INTR_LLVM_VERSION_MAJOR <= 9
template <class TValue> unsigned getAlign(TValue *Val) {
  return Val->getAlignment();
}
#elif VC_INTR_LLVM_VERSION_MAJOR <= 10
template <class TValue> llvm::MaybeAlign getAlign(TValue *Val) {
  // LLVM 10 instructions accept MaybeAlign but do not provide
  // getMaybeAlignMethod
  return llvm::MaybeAlign(Val->getAlignment());
}
#else
template <class TValue> llvm::Align getAlign(TValue *Val) {
  return Val->getAlign();
}
#endif

} // namespace Align

} // namespace VCINTR

#endif // VCINTR_IR_ALIGNMENT_H
