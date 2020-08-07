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

namespace llvm {
class ModulePass;
class PassRegistry;

void initializeGenXSPIRVWriterAdaptorPass(PassRegistry &);

// Create spirv writer adaptor pass.
// RewriteTypes -- whether plain types with decorations should be
// rewritten with native SPIRV types. Defaults to false for
// compatibility reasons until backend will be able to handle new
// types.
ModulePass *createGenXSPIRVWriterAdaptorPass(bool RewriteTypes = false);
} // namespace llvm
