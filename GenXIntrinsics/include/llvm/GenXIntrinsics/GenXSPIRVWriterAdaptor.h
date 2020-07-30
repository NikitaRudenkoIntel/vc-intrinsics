/*===================== begin_copyright_notice ==================================
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
======================= end_copyright_notice ==================================*/
///
/// GenXSPIRVWriterAdaptor
/// ---------------------------
/// This pass converts metadata to SPIRV format from whichever used in frontend

namespace llvm {
class ModulePass;
class PassRegistry;

void initializeGenXSPIRVWriterAdaptorPass(PassRegistry &);
ModulePass *createGenXSPIRVWriterAdaptorPass();
} // namespace llvm
