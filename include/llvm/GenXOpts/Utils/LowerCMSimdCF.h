//===---- LowerCMSimdCFLow.h - Lower CM SIMD control flow -----------------===//
//
//  INTEL CONFIDENTIAL
//  Copyright 2016 Intel Corporation All Rights Reserved.
//
//  The source code contained or described herein and all documents related to
//  the source code ("Material") are owned by Intel Corporation or its suppliers
//  or licensors. Title to the Material remains with Intel Corporation or its
//  suppliers and licensors. The Material contains trade secrets and proprietary
//  and confidential information of Intel or its suppliers and licensors. The
//  Material is protected by worldwide copyright and trade secret laws and
//  treaty provisions. No part of the Material may be used, copied, reproduced,
//  modified, published, uploaded, posted, transmitted, distributed, or
//  disclosed in any way without Intel's prior express written permission.
//
//  No license under any patent, copyright, trade secret or other intellectual
//  property right is granted to or conferred upon you by disclosure or
//  delivery of the Materials, either expressly, by implication, inducement,
//  estoppel or otherwise. Any license under such intellectual property rights
//  must be express and approved by Intel in writing.
//
//===----------------------------------------------------------------------===//
//
/// LowerCMSimdCF
/// -------------
///
/// This is the worker class to lowers CM SIMD control flow into a form where
/// the IR reflects the semantics. See CMSimdCFLowering.cpp for details.
///
//===----------------------------------------------------------------------===//

#ifndef CMSIMDCF_LOWER_H
#define CMSIMDCF_LOWER_H

#include "llvm/ADT/MapVector.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/ValueHandle.h"
#include <algorithm>
#include <set>

namespace llvm {

// The worker class for lowering CM SIMD CF
class CMSimdCFLower {
  Function *F;
  // A map giving the basic blocks ending with a simd branch, and the simd
  // width of each one.
  MapVector<BasicBlock *, unsigned> SimdBranches;
  // A map giving the basic blocks to be predicated, and the simd width of
  // each one.
  MapVector<BasicBlock *, unsigned> PredicatedBlocks;
  // The join points, together with the simd width of each one.
  MapVector<BasicBlock *, unsigned> JoinPoints;
  // The JIP for each simd branch and join point.
  std::map<BasicBlock *, BasicBlock *> JIPs;
  // Subroutines that are predicated, mapping to the simd width.
  std::map<Function *, unsigned> PredicatedSubroutines;
  // Execution mask variable.
  GlobalVariable *EMVar;
  // Resume mask for each join point.
  std::map<BasicBlock *, AllocaInst *> RMAddrs;
  // Set of intrinsic calls (other than wrregion) that have been predicated.
  std::set<AssertingVH<Value>> AlreadyPredicated;
  // Mask for shufflevector to extract part of EM.
  SmallVector<Constant *, 32> ShuffleMask;
public:
  static const unsigned MAX_SIMD_CF_WIDTH = 32;

  CMSimdCFLower(GlobalVariable *EMask) : EMVar(EMask) {}

  static CallInst *isSimdCFAny(Value *V);
  static Use *getSimdConditionUse(Value *Cond);

  void processFunction(Function *F);

private:
  bool findSimdBranches(unsigned CMWidth);
  void determinePredicatedBlocks();
  void markPredicatedBranches();
  void fixSimdBranches();
  void findAndSplitJoinPoints();
  void determineJIPs();
  void determineJIP(BasicBlock *BB, std::map<BasicBlock *, unsigned> *Numbers, bool IsJoin);

  // Methods to add predication to the code
  void predicateCode(unsigned CMWidth);
  void predicateBlock(BasicBlock *BB, unsigned SimdWidth);
  void predicateInst(Instruction *Inst, unsigned SimdWidth);
  void rewritePredication(CallInst *CI, unsigned SimdWidth);
  void predicateStore(Instruction *SI, unsigned SimdWidth);
  void predicateSend(CallInst *CI, unsigned IntrinsicID, unsigned SimdWidth);
  void predicateScatterGather(CallInst *CI, unsigned SimdWidth, unsigned PredOperandNum);
  CallInst *predicateWrRegion(CallInst *WrR, unsigned SimdWidth);
  void predicateCall(CallInst *CI, unsigned SimdWidth);

  void lowerSimdCF();
  void lowerUnmaskOps();
  Instruction *loadExecutionMask(Instruction *InsertBefore, unsigned SimdWidth);
  Value *getRMAddr(BasicBlock *JP, unsigned SimdWidth);
};

} // namespace

#endif
