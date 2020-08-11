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
/// GenXSPIRVWriterAdaptor
/// ---------------------------
/// This pass converts metadata to SPIRV format from whichever used in frontend

#include "AdaptorsCommon.h"

#include "llvm/GenXIntrinsics/GenXSPIRVWriterAdaptor.h"
#include "llvm/GenXIntrinsics/GenXIntrinsics.h"
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

} // namespace

char GenXSPIRVWriterAdaptor::ID = 0;

INITIALIZE_PASS_BEGIN(GenXSPIRVWriterAdaptor, "GenXSPIRVWriterAdaptor",
                      "GenXSPIRVWriterAdaptor", false, false)
INITIALIZE_PASS_END(GenXSPIRVWriterAdaptor, "GenXSPIRVWriterAdaptor",
                    "GenXSPIRVWriterAdaptor", false, false)

ModulePass *llvm::createGenXSPIRVWriterAdaptorPass() {
  return new GenXSPIRVWriterAdaptor();
}

void GenXSPIRVWriterAdaptor::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
}

bool GenXSPIRVWriterAdaptor::runOnModule(Module &M) {

  auto TargetTriple = StringRef(M.getTargetTriple());
  if (!M.getNamedMetadata(SPIRVParams::SPIRVMemoryModel)) {
    // TODO: remove this block when finally transferred to open source
    auto &&Context = M.getContext();
    auto AddressingModel = TargetTriple.startswith("genx64")
                               ? SPIRVParams::SPIRVAddressingModel64
                               : SPIRVParams::SPIRVAddressingModel32;
    auto *MemoryModelMD =
        M.getOrInsertNamedMetadata(SPIRVParams::SPIRVMemoryModel);
    auto ValueVec = std::vector<llvm::Metadata *>();
    ValueVec.push_back(ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt32Ty(Context), AddressingModel)));
    ValueVec.push_back(ConstantAsMetadata::get(ConstantInt::get(
        Type::getInt32Ty(Context), SPIRVParams::SPIRVMemoryModelSimple)));
    MemoryModelMD->addOperand(MDNode::get(Context, ValueVec));
  }
  if (TargetTriple.startswith("genx")) {
    if (TargetTriple.startswith("genx32"))
      M.setTargetTriple("spir");
    else
      M.setTargetTriple("spir64");
  }

  for (auto &&GV : M.getGlobalList()) {
    GV.addAttribute(VCModuleMD::VCGlobalVariable);
    if (GV.hasAttribute(FunctionMD::GenXVolatile))
      GV.addAttribute(VCModuleMD::VCVolatile);
    if (GV.hasAttribute(FunctionMD::GenXByteOffset)) {
      auto Offset =
          GV.getAttribute(FunctionMD::GenXByteOffset).getValueAsString();
      GV.addAttribute(VCModuleMD::VCByteOffset, Offset);
    }
  }

  for (auto &&F : M)
    runOnFunction(F);

  // Old metadata is not needed anymore at this point.
  M.eraseNamedMetadata(M.getNamedMetadata(FunctionMD::GenXKernels));

  return true;
}

bool GenXSPIRVWriterAdaptor::runOnFunction(Function &F) {
  if (F.isIntrinsic() && !GenXIntrinsic::isGenXIntrinsic(&F))
    return true;
  F.addFnAttr(VCFunctionMD::VCFunction);

  auto Attrs = F.getAttributes();
  if (Attrs.hasFnAttribute(FunctionMD::CMStackCall)) {
    F.addFnAttr(VCFunctionMD::VCStackCall);
  }

  if (Attrs.hasFnAttribute(FunctionMD::CMGenxSIMT)) {
    auto SIMTMode = StringRef();
    SIMTMode =
        Attrs.getAttribute(AttributeList::FunctionIndex, FunctionMD::CMGenxSIMT)
            .getValueAsString();
    F.addFnAttr(VCFunctionMD::VCSIMTCall, SIMTMode);
  }

  auto &&Context = F.getContext();
  if (Attrs.hasFnAttribute(FunctionMD::CMFloatControl)) {
    auto FloatControl = unsigned(0);
    Attrs.getAttribute(AttributeList::FunctionIndex, FunctionMD::CMFloatControl)
        .getValueAsString()
        .getAsInteger(0, FloatControl);

    auto Attr = Attribute::get(Context, VCFunctionMD::VCFloatControl,
                               std::to_string(FloatControl));
    F.addAttribute(AttributeList::FunctionIndex, Attr);
  }

  auto *KernelMDs = F.getParent()->getNamedMetadata(FunctionMD::GenXKernels);
  if (!KernelMDs)
    return true;

  if (Attrs.hasFnAttribute(FunctionMD::OCLRuntime)) {
    auto SIMDSize = unsigned(0);
    Attrs.getAttribute(AttributeList::FunctionIndex, FunctionMD::OCLRuntime)
        .getValueAsString()
        .getAsInteger(0, SIMDSize);
    auto SizeMD = ConstantAsMetadata::get(
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(Context), SIMDSize));
    F.setMetadata(SPIRVParams::SPIRVSIMDSubgroupSize,
                  MDNode::get(Context, SizeMD));
  }

  auto *KernelMD = static_cast<MDNode *>(nullptr);
  for (unsigned I = 0, E = KernelMDs->getNumOperands(); I < E; ++I) {
    auto *Kernel = mdconst::dyn_extract<Function>(
        KernelMDs->getOperand(I)->getOperand(KernelMDOp::FunctionRef));
    if (Kernel == &F) {
      KernelMD = KernelMDs->getOperand(I);
      break;
    }
  }
  if (!KernelMD)
    return true;

  F.setCallingConv(CallingConv::SPIR_KERNEL);

  auto MDName =
      cast<MDString>(KernelMD->getOperand(KernelMDOp::Name).get())->getString();
  if (MDName != F.getName())
    F.setName(MDName);

  if (KernelMD->getNumOperands() > KernelMDOp::ArgKinds) {
    if (auto *KindsNode =
            dyn_cast<MDNode>(KernelMD->getOperand(KernelMDOp::ArgKinds))) {
      for (unsigned ArgNo = 0, e = KindsNode->getNumOperands(); ArgNo != e;
           ++ArgNo) {
        if (auto *VM = dyn_cast<ValueAsMetadata>(KindsNode->getOperand(ArgNo)))
          if (auto *V = dyn_cast<ConstantInt>(VM->getValue())) {
            auto ArgKind = V->getZExtValue();
            auto Attr = Attribute::get(Context, VCFunctionMD::VCArgumentKind,
                                       std::to_string(ArgKind));
            F.addAttribute(ArgNo + 1, Attr);
          }
      }
    }
  }

  if (KernelMD->getNumOperands() > KernelMDOp::SLMSize) {
    if (auto *VM = dyn_cast<ValueAsMetadata>(
            KernelMD->getOperand(KernelMDOp::SLMSize)))
      if (auto *V = dyn_cast<ConstantInt>(VM->getValue())) {
        auto SLMSize = V->getZExtValue();
        auto Attr = Attribute::get(Context, VCFunctionMD::VCSLMSize,
                                   std::to_string(SLMSize));
        F.addAttribute(AttributeList::FunctionIndex, Attr);
      }
  }

  if (KernelMD->getNumOperands() > KernelMDOp::ArgIOKinds) {
    if (auto *KindsNode =
            dyn_cast<MDNode>(KernelMD->getOperand(KernelMDOp::ArgIOKinds))) {
      for (unsigned ArgNo = 0, e = KindsNode->getNumOperands(); ArgNo != e;
           ++ArgNo) {
        if (auto *VM = dyn_cast<ValueAsMetadata>(KindsNode->getOperand(ArgNo)))
          if (auto *V = dyn_cast<ConstantInt>(VM->getValue())) {
            auto ArgKind = V->getZExtValue();
            auto Attr = Attribute::get(Context, VCFunctionMD::VCArgumentIOKind,
                                       std::to_string(ArgKind));
            F.addAttribute(ArgNo + 1, Attr);
          }
      }
    }
  }

  if (KernelMD->getNumOperands() > KernelMDOp::ArgTypeDescs) {
    if (auto Node =
            dyn_cast<MDNode>(KernelMD->getOperand(KernelMDOp::ArgTypeDescs))) {
      for (unsigned ArgNo = 0, e = Node->getNumOperands(); ArgNo != e;
           ++ArgNo) {
        if (auto *MS = dyn_cast<MDString>(Node->getOperand(ArgNo))) {
          auto &&Desc = MS->getString();
          auto Attr =
              Attribute::get(Context, VCFunctionMD::VCArgumentDesc, Desc);
          F.addAttribute(ArgNo + 1, Attr);
        }
      }
    }
  }

#ifdef __INTEL_EMBARGO__
  if (KernelMD->getNumOperands() > KernelMDOp::NBarrierCnt) {
    if (auto VM = dyn_cast<ValueAsMetadata>(
            KernelMD->getOperand(KernelMDOp::NBarrierCnt)))
      if (auto V = dyn_cast<ConstantInt>(VM->getValue())) {
        auto NBarrierCnt = V->getZExtValue();
        auto Attr = Attribute::get(Context, VCFunctionMD::VCNamedBarrierCount,
                                   std::to_string(NBarrierCnt));
        F.addAttribute(AttributeList::FunctionIndex, Attr);
      }
  }

#endif // __INTEL_EMBARGO__
  return true;
}
