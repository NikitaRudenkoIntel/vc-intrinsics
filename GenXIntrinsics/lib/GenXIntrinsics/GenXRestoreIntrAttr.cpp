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

//===-- GenXRestoreIntrAttr.cpp - GenX Restore Intrinsics' attributes pass --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// GenXRestoreIntrAttr
/// -------------------
///
/// This is a module pass that restores attributes for intrinsics:
///
/// * Since SPIR-V doesn't save intrinsics' attributes, some important
///   information can be lost. This pass restores it.
///
/// * Only GenX intrinsics are handled.
///
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "GENX_RESTOREINTRATTR"

#include "llvm/GenXIntrinsics/GenXIntrOpts.h"
#include "llvm/GenXIntrinsics/GenXIntrinsics.h"
#include "llvm/Support/Debug.h"
#include "llvm/Pass.h"

using namespace llvm;

namespace {

// GenXRestoreIntrAttr : restore intrinsics' attributes
class GenXRestoreIntrAttr : public ModulePass {
public:
  GenXRestoreIntrAttr();

  StringRef getPassName() const override {
    return "GenX Restore Intrinsics' Attributes";
  }

  bool runOnModule(Module &M) override;

private:
  bool restoreAttributes(Function *F);

public:
  static char ID;
};
} // namespace

namespace llvm {
void initializeGenXRestoreIntrAttrPass(PassRegistry &);
}
INITIALIZE_PASS_BEGIN(GenXRestoreIntrAttr, "GenXRestoreIntrAttr",
                      "GenXRestoreIntrAttr", false, false)
INITIALIZE_PASS_END(GenXRestoreIntrAttr, "GenXRestoreIntrAttr",
                    "GenXRestoreIntrAttr", false, false)

char GenXRestoreIntrAttr::ID = 0;

Pass *llvm::createGenXRestoreIntrAttrPass() {
  return new GenXRestoreIntrAttr;
}

GenXRestoreIntrAttr::GenXRestoreIntrAttr() : ModulePass(ID) {
  initializeGenXRestoreIntrAttrPass(*PassRegistry::getPassRegistry());
}

bool GenXRestoreIntrAttr::restoreAttributes(Function *F) {
  LLVM_DEBUG(dbgs() << "Restoring attributes for: " << F->getName() << "\n");
  F->setAttributes(GenXIntrinsic::getAttributes(F->getContext(), GenXIntrinsic::getGenXIntrinsicID(F)));
  return true;
}

bool GenXRestoreIntrAttr::runOnModule(Module &M) {
  bool Modified = false;

  for (auto &F : M.getFunctionList()) {
    if (GenXIntrinsic::isGenXIntrinsic(&F))
      Modified |= restoreAttributes(&F);
  }

  return Modified;
}
