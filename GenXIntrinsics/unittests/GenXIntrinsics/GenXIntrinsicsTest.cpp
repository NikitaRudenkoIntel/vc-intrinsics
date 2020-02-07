//===- llvm/unittest/GenXIntrinsics/GenXIntrinsicsTest.cpp - --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

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
