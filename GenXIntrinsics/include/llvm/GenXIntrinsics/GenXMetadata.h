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

//===----------------------------------------------------------------------===//
//
// This file defines GenX kernel metadata operand numbers and other module
// metadata.
//
//===----------------------------------------------------------------------===//

#ifndef GENX_METADATA_H
#define GENX_METADATA_H

namespace llvm {
namespace genx {

namespace FunctionMD {
static constexpr const char GenXKernels[] = "genx.kernels";
static constexpr const char GenXByteOffset[] = "genx_byte_offset";
static constexpr const char GenXVolatile[] = "genx_volatile";
static constexpr const char CMGenXMain[] = "CMGenxMain";
static constexpr const char CMStackCall[] = "CMStackCall";
static constexpr const char CMFloatControl[] = "CMFloatControl";
static constexpr const char CMGenxSIMT[] = "CMGenxSIMT";
static constexpr const char CMGenxReplicateMask[] = "CMGenxReplicateMask";
static constexpr const char OCLRuntime[] = "oclrt";
static constexpr const char ReferencedIndirectly[] = "referenced-indirectly";
} // namespace FunctionMD

namespace VCModuleMD {
static constexpr const char VCGlobalVariable[] = "VCGlobalVariable";
static constexpr const char VCVolatile[] = "VCVolatile";
static constexpr const char VCByteOffset[] = "VCByteOffset";
} // namespace VCModuleMD

namespace VCFunctionMD {
static constexpr const char VCFunction[] = "VCFunction";
static constexpr const char VCStackCall[] = "VCStackCall";
static constexpr const char VCArgumentIOKind[] = "VCArgumentIOKind";
static constexpr const char VCFloatControl[] = "VCFloatControl";
static constexpr const char VCSLMSize[] = "VCSLMSize";
static constexpr const char VCArgumentKind[] = "VCArgumentKind";
static constexpr const char VCArgumentDesc[] = "VCArgumentDesc";
static constexpr const char VCSIMTCall[] = "VCSIMTCall";
#ifdef __INTEL_EMBARGO__
static constexpr const char VCNamedBarrierCount[] = "VCNamedBarrierCount";
#endif // __INTEL_EMBARGO__
} // namespace VCFunctionMD

enum KernelMDOp {
  FunctionRef,  // Reference to Function
  Name,         // Kernel name
  ArgKinds,     // Reference to metadata node containing kernel arg kinds
  SLMSize,      // SLM-size in bytes
  ArgOffsets,   // Kernel argument offsets
  ArgIOKinds,   // Reference to metadata node containing kernel argument
                // input/output kinds
  ArgTypeDescs, // Kernel argument type descriptors
#ifdef __INTEL_EMBARGO__
  NBarrierCnt,  // Named barrier count
#else // __INTEL_EMBARGO__
  Reserved_0,
#endif // __INTEL_EMBARGO__
  BarrierCnt    // Barrier count
};

static MDNode *GetOldStyleKernelMD(Function const &F) {
  auto *KernelMD = static_cast<MDNode *>(nullptr);
  auto *KernelMDs = F.getParent()->getNamedMetadata(FunctionMD::GenXKernels);
  if (!KernelMDs)
    return KernelMD;

  for (unsigned I = 0, E = KernelMDs->getNumOperands(); I < E; ++I) {
    auto *Kernel = mdconst::dyn_extract<Function>(
        KernelMDs->getOperand(I)->getOperand(KernelMDOp::FunctionRef));
    if (Kernel == &F) {
      KernelMD = KernelMDs->getOperand(I);
      break;
    }
  }
  return KernelMD;
}

} // namespace genx
} // namespace llvm

#endif
