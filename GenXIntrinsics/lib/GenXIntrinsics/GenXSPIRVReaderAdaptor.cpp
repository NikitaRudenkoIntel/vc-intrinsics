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
///
/// GenXSPIRVReaderAdaptor
/// ---------------------------
/// This pass converts metadata from SPIRV format to whichever used in backend

#include "llvm/GenXIntrinsics/GenXSPIRVReaderAdaptor.h"
#include "llvm/GenXIntrinsics/GenXMetadata.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

using namespace llvm;
using namespace genx;

namespace {

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

} // namespace

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

  if (Attrs.hasFnAttribute(VCFunctionMD::VCSIMTCall)) {
    auto SIMTMode = StringRef();
    SIMTMode = Attrs
                   .getAttribute(AttributeList::FunctionIndex,
                                 VCFunctionMD::VCSIMTCall)
                   .getValueAsString();
    F.addFnAttr(FunctionMD::CMGenxSIMT, SIMTMode);
  }

  auto &&Context = F.getContext();
  if (Attrs.hasFnAttribute(VCFunctionMD::VCFloatControl)) {
    auto FloatControl = unsigned(0);
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

  if (Attrs.hasFnAttribute(VCFunctionMD::VCSLMSize)) {
    Attrs.getAttribute(AttributeList::FunctionIndex, VCFunctionMD::VCSLMSize)
        .getValueAsString()
        .getAsInteger(0, SLMSize);
  }

  for (Function::arg_iterator I = F.arg_begin(), E = F.arg_end(); I != E; ++I) {
    auto ArgNo = I->getArgNo();
    auto ArgKind = unsigned(0);
    auto ArgIOKind = unsigned(0);
    auto ArgDesc = std::string();
    if (Attrs.hasAttribute(ArgNo + 1, VCFunctionMD::VCArgumentKind)) {
      Attrs.getAttribute(ArgNo + 1, VCFunctionMD::VCArgumentKind)
          .getValueAsString()
          .getAsInteger(0, ArgKind);
    }
    if (Attrs.hasAttribute(ArgNo + 1, VCFunctionMD::VCArgumentIOKind)) {
      Attrs.getAttribute(ArgNo + 1, VCFunctionMD::VCArgumentIOKind)
          .getValueAsString()
          .getAsInteger(0, ArgIOKind);
    }
    if (Attrs.hasAttribute(ArgNo + 1, VCFunctionMD::VCArgumentDesc)) {
      ArgDesc = Attrs.getAttribute(ArgNo + 1, VCFunctionMD::VCArgumentDesc)
                    .getValueAsString()
                    .str();
    }
    ArgKinds.push_back(
        llvm::ValueAsMetadata::get(llvm::ConstantInt::get(I32Ty, ArgKind)));
    ArgIOKinds.push_back(
        llvm::ValueAsMetadata::get(llvm::ConstantInt::get(I32Ty, ArgIOKind)));
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
      ConstantAsMetadata::get(ConstantInt::get(I32Ty, ArgOffset)));
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
