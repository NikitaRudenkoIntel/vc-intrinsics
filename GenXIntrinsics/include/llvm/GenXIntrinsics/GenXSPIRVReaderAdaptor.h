//===-- GenXSPIRVReaderAdaptor.h - converts metadata -----*- C++ -*-===//
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
/// GenXSPIRVReaderAdaptor
/// ---------------------------
/// This pass converts metadata from SPIRV format to whichever used in backend
/// Mostly, spirv format is the same as OCL format
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/Pass.h"

namespace llvm {
namespace genx {
class GenXSPIRVReaderAdaptor final : public ModulePass {

public:
  static char ID;
  explicit GenXSPIRVReaderAdaptor() : ModulePass(ID) {}
  llvm::StringRef getPassName() const override {
    return "GenX SPIRVReader Adaptor";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnModule(Module &M) override;

private:
  bool runOnFunction(Function &F);
};

} // namespace genx
} // namespace llvm

namespace llvm {
void initializeGenXSPIRVReaderAdaptorPass(PassRegistry &);
ModulePass *createGenXSPIRVReaderAdaptorPass();
} // namespace llvm
