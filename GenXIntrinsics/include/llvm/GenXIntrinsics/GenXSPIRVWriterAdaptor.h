//===-- GenXSPIRVWriterAdaptor.h - converts metadata -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//  Copyright  (C) 2014 Intel Corporation. All rights reserved.
//
// The information and source code contained herein is the exclusive
// property of Intel Corporation and may not be disclosed, examined
// or reproduced in whole or in part without explicit written authorization
// from the company.
//
//===----------------------------------------------------------------------===//
//
/// GenXSPIRVWriterAdaptor
/// ---------------------------
/// This pass converts metadata to SPIRV format from whichever used in frontend
/// Mostly, spirv format is the same as OCL format
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/Pass.h"

namespace llvm {
namespace genx {
class GenXSPIRVWriterAdaptor final : public ModulePass {

public:
  static char ID;
  explicit GenXSPIRVWriterAdaptor() : ModulePass(ID) {}
  llvm::StringRef getPassName() const override {
    return "GenX SPIRVWriter Adaptor";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnModule(Module &M) override;

private:
  bool runOnFunction(Function &F);
};

} // namespace genx
} // namespace llvm

namespace llvm {
void initializeGenXSPIRVWriterAdaptorPass(PassRegistry &);
ModulePass *createGenXSPIRVWriterAdaptorPass();
} // namespace llvm
