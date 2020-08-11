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
const static char GenXKernels[] = "genx.kernels";
const static char GenXByteOffset[] = "genx_byte_offset";
const static char GenXVolatile[] = "genx_volatile";
const static char CMGenXMain[] = "CMGenxMain";
const static char CMStackCall[] = "CMStackCall";
const static char CMFloatControl[] = "CMFloatControl";
const static char CMGenxSIMT[] = "CMGenxSIMT";
const static char OCLRuntime[] = "oclrt";
const static char ReferencedIndirectly[] = "referenced-indirectly";
} // namespace FunctionMD

namespace VCModuleMD {
const static char VCGlobalVariable[] = "VCGlobalVariable";
const static char VCVolatile[] = "VCVolatile";
const static char VCByteOffset[] = "VCByteOffset";
} // namespace VCModuleMD

namespace VCFunctionMD {
const static char VCFunction[] = "VCFunction";
const static char VCStackCall[] = "VCStackCall";
const static char VCArgumentIOKind[] = "VCArgumentIOKind";
const static char VCFloatControl[] = "VCFloatControl";
const static char VCSLMSize[] = "VCSLMSize";
const static char VCArgumentKind[] = "VCArgumentKind";
const static char VCArgumentDesc[] = "VCArgumentDesc";
const static char VCSIMTCall[] = "VCSIMTCall";
#ifdef __INTEL_EMBARGO__
const static char VCNamedBarrierCount[] = "VCNamedBarrierCount";
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
} // namespace genx
} // namespace llvm

#endif
