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

#include "llvm/ADT/StringRef.h"
#include "llvm/GenXIntrinsics/GenXIntrinsics.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "gtest/gtest.h"

using namespace llvm;

namespace {

TEST(GenXIntrinsics, OverloadedTypes) {
  EXPECT_EQ(GenXIntrinsic::isOverloadedArg(Intrinsic::fma, 0), false);
  EXPECT_EQ(GenXIntrinsic::isOverloadedArg(Intrinsic::fma, 1), false);
  EXPECT_EQ(GenXIntrinsic::isOverloadedArg(GenXIntrinsic::genx_3d_sample, 7),
            true);
  EXPECT_EQ(GenXIntrinsic::isOverloadedArg(GenXIntrinsic::genx_raw_send, 1),
            true);
  EXPECT_EQ(GenXIntrinsic::isOverloadedArg(GenXIntrinsic::genx_simdcf_any, 0),
            true);
#ifdef __INTEL_EMBARGO__
  EXPECT_EQ(GenXIntrinsic::isOverloadedArg(GenXIntrinsic::genx_ssdp4a, 0), true);
  EXPECT_EQ(GenXIntrinsic::isOverloadedArg(GenXIntrinsic::genx_ssdp4a, 1), true);
  EXPECT_EQ(GenXIntrinsic::isOverloadedArg(GenXIntrinsic::genx_ssdp4a, 2), true);
  EXPECT_EQ(GenXIntrinsic::isOverloadedArg(GenXIntrinsic::genx_dpasw_nosrc0, 2),
            false);
  EXPECT_EQ(
      GenXIntrinsic::isOverloadedArg(GenXIntrinsic::genx_lsc_store_slm, 10),
      true);
  EXPECT_EQ(
      GenXIntrinsic::isOverloadedArg(GenXIntrinsic::genx_lsc_store_slm, 11),
      true);
  EXPECT_EQ(
      GenXIntrinsic::isOverloadedArg(GenXIntrinsic::genx_lsc_store_slm, 12),
      false);
#endif // __INTEL_EMBARGO__
}
} // namespace
