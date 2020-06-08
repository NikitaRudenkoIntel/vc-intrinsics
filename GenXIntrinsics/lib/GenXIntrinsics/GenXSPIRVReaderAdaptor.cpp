//===-- GenXSPIRVReaderAdaptor.cpp - converts metadata -----*- C++ -*-===//
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

#include "llvm/GenXIntrinsics/GenXSPIRVReaderAdaptor.h"
#include "llvm/GenXIntrinsics/GenXMetadata.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace genx;

char GenXSPIRVReaderAdaptor::ID = 0;

INITIALIZE_PASS_BEGIN(GenXSPIRVReaderAdaptor, "GenXSPIRVReaderAdaptor",
                      "GenXSPIRVReaderAdaptor", false, false)
INITIALIZE_PASS_END(GenXSPIRVReaderAdaptor, "GenXSPIRVReaderAdaptor",
                    "GenXSPIRVReaderAdaptor", false, false)

ModulePass *llvm::createGenXSPIRVReaderAdaptorPass() {
  return new GenXSPIRVReaderAdaptor();
}

void GenXSPIRVReaderAdaptor::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
}

bool GenXSPIRVReaderAdaptor::runOnModule(Module &M) {
  auto TargetTriple = StringRef(M.getTargetTriple());
  if (TargetTriple.startswith("spir")) {
    if (TargetTriple.startswith("spir64"))
      M.setTargetTriple("genx64");
    else
      M.setTargetTriple("genx");
  }

  auto &&x = M.getGlobalList();
  for (auto &&GV : M.getGlobalList()) {
    if (!GV.hasAttribute(VCModuleMD::VCGlobalVariable))
      continue;
    if (GV.hasAttribute(VCModuleMD::VCVolatile))
      GV.addAttribute(FunctionMD::GenXVolatile);
    if (GV.hasAttribute(VCModuleMD::VCByteOffset)) {
      auto Offset =
          GV.getAttribute(VCModuleMD::VCByteOffset).getValueAsString();
      GV.addAttribute(FunctionMD::GenXByteOffset, Offset);
    }
  }

  for (auto &&F : M)
    runOnFunction(F);

  return true;
}

bool GenXSPIRVReaderAdaptor::runOnFunction(Function &F) {
  auto Attrs = F.getAttributes();
  if (!Attrs.hasFnAttribute(VCFunctionMD::VCFunction))
    return true;

  if (Attrs.hasFnAttribute(VCFunctionMD::VCStackCall)) {
    F.addFnAttr(FunctionMD::CMStackCall);
    F.addFnAttr(Attribute::NoInline);
  }
#ifdef __INTEL_EMBARGO__
  if (Attrs.hasFnAttribute(VCFunctionMD::VCSIMTCall))
    F.addFnAttr(FunctionMD::CMGenxSIMT);
#endif // __INTEL_EMBARGO__

  auto &&Context = F.getContext();
  if (Attrs.hasFnAttribute(VCFunctionMD::VCFloatControl)) {
    unsigned FloatControl = 0;
    Attrs
        .getAttribute(AttributeList::FunctionIndex,
                      VCFunctionMD::VCFloatControl)
        .getValueAsString()
        .getAsInteger(0, FloatControl);

    auto Attr = Attribute::get(Context, FunctionMD::CMFloatControl,
                               std::to_string(FloatControl));
    F.addAttribute(AttributeList::FunctionIndex, Attr);
  }

  if (auto *ReqdSubgroupSize =
          F.getMetadata(SPIRVParams::SPIRVSIMDSubgroupSize)) {
    auto SIMDSize =
        mdconst::dyn_extract<ConstantInt>(ReqdSubgroupSize->getOperand(0))
            ->getZExtValue();
    Attribute Attr = Attribute::get(Context, FunctionMD::OCLRuntime,
                                    std::to_string(SIMDSize));
    F.addAttribute(AttributeList::FunctionIndex, Attr);
  }

  if (!(F.getCallingConv() == CallingConv::SPIR_KERNEL))
    return true;
  F.addFnAttr(FunctionMD::CMGenXMain);
  F.setDLLStorageClass(llvm::GlobalVariable::DLLExportStorageClass);

  auto *FunctionRef = ValueAsMetadata::get(&F);
  auto KernelName = F.getName();
  auto ArgKinds = llvm::SmallVector<llvm::Metadata *, 8>();
  auto SLMSize = unsigned(0);
  auto ArgOffset = unsigned(0);
  auto ArgIOKinds = llvm::SmallVector<llvm::Metadata *, 8>();
  auto ArgDescs = llvm::SmallVector<llvm::Metadata *, 8>();
#ifdef __INTEL_EMBARGO__
  auto NBarrierCnt = unsigned(0);
#endif // __INTEL_EMBARGO__

  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Context);

  for (Function::arg_iterator I = F.arg_begin(), E = F.arg_end(); I != E; ++I) {
    auto ArgNo = I->getArgNo();
    auto ArgKind = unsigned(0);
#ifdef __INTEL_EMBARGO__
    if (Attrs.hasAttribute(ArgNo + 1, VCFunctionMD::VCArgumentKind)) {
      Attrs.getAttribute(ArgNo + 1, VCFunctionMD::VCArgumentKind)
          .getValueAsString()
          .getAsInteger(0, ArgKind);
    }
#endif // __INTEL_EMBARGO__
    ArgKinds.push_back(
        llvm::ValueAsMetadata::get(llvm::ConstantInt::get(I32Ty, ArgKind)));
  }

  if (Attrs.hasFnAttribute(VCFunctionMD::VCSLMSize)) {
    Attrs.getAttribute(AttributeList::FunctionIndex, VCFunctionMD::VCSLMSize)
        .getValueAsString()
        .getAsInteger(0, SLMSize);
  }

  for (Function::arg_iterator I = F.arg_begin(), E = F.arg_end(); I != E; ++I) {
    auto ArgNo = I->getArgNo();
    unsigned ArgKind = 0;
    if (Attrs.hasAttribute(ArgNo + 1, VCFunctionMD::VCArgumentIOKind)) {
      Attrs.getAttribute(ArgNo + 1, VCFunctionMD::VCArgumentIOKind)
          .getValueAsString()
          .getAsInteger(0, ArgKind);
    }
    ArgIOKinds.push_back(
        llvm::ValueAsMetadata::get(llvm::ConstantInt::get(I32Ty, ArgKind)));
  }

  for (Function::arg_iterator I = F.arg_begin(), E = F.arg_end(); I != E; ++I) {
    auto ArgNo = I->getArgNo();
    auto ArgDesc = std::string();
#ifdef __INTEL_EMBARGO__
    if (Attrs.hasAttribute(ArgNo + 1, VCFunctionMD::VCArgumentDesc)) {
      ArgDesc = Attrs.getAttribute(ArgNo + 1, VCFunctionMD::VCArgumentDesc)
                    .getValueAsString().str();
    }
#endif // __INTEL_EMBARGO__
    ArgDescs.push_back(llvm::MDString::get(Context, ArgDesc));
  }

#ifdef __INTEL_EMBARGO__
  if (Attrs.hasFnAttribute(VCFunctionMD::VCNamedBarrierCount)) {
    Attrs
        .getAttribute(AttributeList::FunctionIndex,
                      VCFunctionMD::VCNamedBarrierCount)
        .getValueAsString()
        .getAsInteger(0, NBarrierCnt);
  }
#endif // __INTEL_EMBARGO__

  auto KernelMD = std::vector<llvm::Metadata *>();
  KernelMD.push_back(FunctionRef);
  KernelMD.push_back(llvm::MDString::get(Context, KernelName));
  KernelMD.push_back(llvm::MDNode::get(Context, ArgKinds));
  KernelMD.push_back(ConstantAsMetadata::get(ConstantInt::get(I32Ty, SLMSize)));
  KernelMD.push_back(
      llvm::ValueAsMetadata::get(llvm::ConstantInt::get(I32Ty, ArgOffset)));
  KernelMD.push_back(llvm::MDNode::get(Context, ArgIOKinds));
  KernelMD.push_back(llvm::MDNode::get(Context, ArgDescs));
#ifdef __INTEL_EMBARGO__
  KernelMD.push_back(
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, NBarrierCnt)));
#else
  KernelMD.push_back(ConstantAsMetadata::get(ConstantInt::get(I32Ty, 0)));
#endif // __INTEL_EMBARGO__

  NamedMDNode *KernelMDs =
      F.getParent()->getOrInsertNamedMetadata(FunctionMD::GenXKernels);
  llvm::MDNode *Node = MDNode::get(F.getContext(), KernelMD);
  KernelMDs->addOperand(Node);
  return true;
}