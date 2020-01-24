//===-- GenXKernelMDOps.h - Kernel information support ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//  Copyright  (C) 2015 Intel Corporation. All rights reserved.
//
// The information and source code contained herein is the exclusive
// property of Intel Corporation and may not be disclosed, examined
// or reproduced in whole or in part without explicit written authorization
// from the company.
//
//===----------------------------------------------------------------------===//
//
// This file defines operand numbers to work with kernel metadata.
//
//===----------------------------------------------------------------------===//

#ifndef GENX_KERNEL_MD_H
#define GENX_KERNEL_MD_H

namespace llvm {
namespace genx {
    /*
    * The metadata node has the following operands:
    *  0: reference to Function
    *  1: kernel name
    *  2: asm name
    *  3: reference to metadata node containing kernel arg kinds
    *  4: slm-size in bytes
    *  5: kernel argument offsets
    *  6: reference to metadata node containing kernel argument input/output kinds
    *  7: kernel argument type descriptors
    *  8: named barrier count
    *  9: barrier count
    */
    enum KernelMDOp
    {
        FunctionRef,
        Name,
        AsmName,
        ArgKinds,
        SLMSize,
        ArgOffsets,
        ArgIOKinds,
        ArgTypeDescs,
        NBarrierCnt,
        BarrierCnt
    };
} // namespace genx
} // namespace llvm

#endif