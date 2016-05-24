//===- CMSimdCFLowering.cpp - Lower CM SIMD control flow ------------------===//
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
/// CMSimdCFLowering
/// ----------------
///
/// This pass lowers CM SIMD control flow into a form where the IR reflects
/// the semantics.
///
/// On entry, any SIMD control flow conditional branch is a br instruction with
/// a scalar condition that is the result of an llvm.genx.simdcf.any intrinsic.
/// The IR in this state does not reflect the real semantics, and would thus be
/// prone to LLVM optimizations misanalyzing and misoptimizing it.
///
/// This pass runs very early, straight after Clang codegen has generated the
/// IR.
///
/// After this pass, the LLVM IR reflects the semantics using a model of Gen
/// unstructured SIMD control flow (goto/join instructions). The idea is that
/// the IR generates code that works but is suboptimal, but we can then have a
/// pass late in the GenX backend that spots this code and turns it into real
/// goto/join instructions.
///
/// The model is as follows:
///
/// * There is a vXi1 execution mask (EM) (for SIMD width X). Within SIMD
///   control flow, vector instructions that affect state are predicated by
///   EM. (SIMD control flow of different widths cannot be mixed, although
///   it can appear separately in the same function, so there is a separate
///   EM for each width used in the function.)
///
/// * Each SIMD control flow join point has a vXi1 re-enable mask (RM)
///   variable. It is initialized to 0.
///
/// * A SIMD conditional branch is always forward, and does the following:
///
///   - For a channel that is enabled (bit set in EM) and wants to take the
///     branch, its bit is cleared in EM and set in the branch target's RM.
///
///   - If all bits in EM are now zero, it branches to
///     the next join point where any currently disabled channel could be
///     re-enabled. For structured control flow, this is the join point of
///     the current or next outer construct.
///
/// * A join point does the following:
///
///   - re-enables channels from its RM variable by ORing RM into EM
///
///   - resets its RM to 0
///
///   - if EM is still all zero, it branches to the next join point where any
///     currently disabled channel could be re-enabled.
///
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "cmsimdcflowering"

#include "llvm/ADT/MapVector.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsGenX.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Transforms/Scalar.h"
#include <algorithm>
#include <set>

using namespace llvm;

namespace {

// Grouping : utility class to maintain a grouping, a partition of a set of
// items into disjoint groups. The initial state is that each item is in its
// own group, then you call joinGroups to join two groups together.
template<typename T> class Grouping {
  std::map<T, T> Group;
public:
  // joinGroups : join the groups that Block1 and Block2 are in
  void joinGroups(T Block1, T Block2) {
    auto G1 = getGroup(Block1);
    auto G2 = getGroup(Block2);
    if (G1 != G2)
      Group[G2] = G1;
  }
  // getGroup : get the group for Block
  // The chain of blocks between Block and its group are modified to point
  // directly to the group at the end of the chain.
  T getGroup(T Block) {
    SmallVector<T, 4> Chain;
    T G;
    for (;;) {
      G = Group[Block];
      if (!G)
        Group[Block] = G = Block; // never seen before, initialize
      if (G == Block)
        break;
      Chain.push_back(Block);
      Block = G;
    }
    for (auto i = Chain.begin(), e = Chain.end(); i != e; ++i)
      *i = G;
    return G;
  }
};

// Diagnostic information for error/warning relating to SIMD control flow.
class DiagnosticInfoSimdCF : public DiagnosticInfoOptimizationBase {
private:
  static int KindID;
  static int getKindID() {
    if (KindID == 0)
      KindID = llvm::getNextAvailablePluginDiagnosticKind();
    return KindID;
  }
public:
  static void emit(Instruction *Inst, const Twine &Msg, DiagnosticSeverity Severity = DS_Error);
  DiagnosticInfoSimdCF(DiagnosticSeverity Severity, const Function &Fn,
      const DebugLoc &DLoc, const Twine &Msg)
      : DiagnosticInfoOptimizationBase((DiagnosticKind)getKindID(), Severity,
          /*PassName=*/nullptr, Fn, DLoc, Msg) {}
  // This kind of message is always enabled, and not affected by -rpass.
  virtual bool isEnabled() const override { return true; }
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == getKindID();
  }
};
int DiagnosticInfoSimdCF::KindID = 0;

// Call graph node
struct CGNode {
  Function *F;
  std::set<CGNode *> UnvisitedCallers;
  std::set<CGNode *> Callees;
};

// The CM SIMD CF lowering pass (a function pass)
class CMSimdCFLowering : public FunctionPass {
  static const unsigned MAX_SIMD_CF_WIDTH = 32;
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
  static char ID;

  CMSimdCFLowering() : FunctionPass(ID), EMVar(nullptr) {
    initializeCMSimdCFLoweringPass(*PassRegistry::getPassRegistry());
  }
  void getAnalysisUsage(AnalysisUsage &AU) const {
    FunctionPass::getAnalysisUsage(AU);
  }

  virtual bool doInitialization(Module &M);
  virtual bool runOnFunction(Function &F) { return false; }
  static CallInst *isSimdCFAny(Value *V);
  static Use *getSimdConditionUse(Value *Cond);
private:
  void calculateVisitOrder(Module *M, std::vector<Function *> *VisitOrder);
  void processFunction(Function *F);
  void findSimdBranches(unsigned CMWidth);
  void determinePredicatedBlocks();
  void markPredicatedBranches();
  void fixSimdBranches();
  void findAndSplitJoinPoints();
  void determineJIPs();
  void determineJIP(BasicBlock *BB, std::map<BasicBlock *, unsigned> *Numbers, bool IsJoin);

  // Methods to add predication to the code
  void predicateCode(unsigned CMWidth);
  void predicateBlock(BasicBlock *BB, unsigned SimdWidth, bool PredicateStores);
  void predicateInst(Instruction *Inst, unsigned SimdWidth, bool PredicateStores);
  void rewritePredication(CallInst *CI, unsigned SimdWidth);
  void predicateStore(StoreInst *SI, unsigned SimdWidth);
  CallInst *convertScatterGather(CallInst *CI, unsigned IID);
  void predicateScatterGather(CallInst *CI, unsigned SimdWidth, unsigned PredOperandNum);
  CallInst *predicateWrRegion(CallInst *WrR, unsigned SimdWidth);
  void predicateCall(CallInst *CI, unsigned SimdWidth);

  void lowerSimdCF();
  Instruction *loadExecutionMask(Instruction *InsertBefore, unsigned SimdWidth);
  Value *getRMAddr(BasicBlock *JP, unsigned SimdWidth);
};
} // namespace

char CMSimdCFLowering::ID = 0;
INITIALIZE_PASS_BEGIN(CMSimdCFLowering, "cmsimdcflowering", "Lower CM SIMD control flow", false, false)
INITIALIZE_PASS_END(CMSimdCFLowering, "cmsimdcflowering", "Lower CM SIMD control flow", false, false)

Pass *llvm::createCMSimdCFLoweringPass() { return new CMSimdCFLowering(); }

/***********************************************************************
 * doInitialization : per-module initialization for CM simd CF lowering
 *
 * Really we want a module pass for CM simd CF lowering. But, without modifying
 * llvm's PassManagerBuilder, the earliest place to insert a pass is
 * EP_EarlyAsPossible, which must be a function pass. So, we do our
 * per-module processing here in doInitialization.
 */
bool CMSimdCFLowering::doInitialization(Module &M)
{
  // See if simd CF is used anywhere in this module.
  // We have to try each overload of llvm.genx.simdcf.any separately.
  bool HasSimdCF = false;
  for (unsigned Width = 2; Width <= MAX_SIMD_CF_WIDTH; Width <<= 1) {
    auto VT = VectorType::get(Type::getInt1Ty(M.getContext()), Width);
    Function *SimdCFAny = Intrinsic::getDeclaration(
        &M, Intrinsic::genx_simdcf_any, VT);
    if (!SimdCFAny->use_empty()) {
      HasSimdCF = true;
      break;
    }
  }

  if (HasSimdCF) {
    // Create the global variable for the execution mask.
    auto EMTy = VectorType::get(Type::getInt1Ty(M.getContext()),
        MAX_SIMD_CF_WIDTH);
    EMVar = new GlobalVariable(M, EMTy, false/*isConstant*/,
        GlobalValue::InternalLinkage, Constant::getAllOnesValue(EMTy), "EM");
    // Derive an order to process functions such that a function is visited
    // after anything that calls it.
    std::vector<Function *> VisitOrder;
    calculateVisitOrder(&M, &VisitOrder);
    // Process functions in that order.
    for (auto i = VisitOrder.begin(), e = VisitOrder.end(); i != e; ++i)
      processFunction(*i);
  }

  // Any predication calls which remain are not in SIMD CF regions,
  // so can be deleted.
  for (auto mi = M.begin(), me = M.end(); mi != me; ++ mi) {
    Function *F = &*mi;
    unsigned IntrinsicID = F->getIntrinsicID();
    if (IntrinsicID != Intrinsic::genx_simdcf_predicate)
      continue;
    while (!F->use_empty()) {
      auto CI = cast<CallInst>(F->use_begin()->getUser());
      auto EnabledValues = CI->getArgOperand(0);
      CI->replaceAllUsesWith(EnabledValues);
      CI->eraseFromParent();
    }
  }

  return HasSimdCF;
}

/***********************************************************************
 * calculateVisitOrder : calculate the order we want to visit functions,
 *    such that a function is not visited until all its callers have been
 */
void CMSimdCFLowering::calculateVisitOrder(Module *M,
    std::vector<Function *> *VisitOrder)
{
  // First build the call graph.
  // We roll our own call graph here, because it is simpler than the general
  // case supported by LLVM's call graph analysis (CM does not support
  // recursion or function pointers), and we want to modify it (using the
  // UnvisitedCallers set) when we traverse it.
  std::map<Function *, CGNode> CallGraph;
  for (auto mi = M->begin(), me = M->end(); mi != me; ++mi) {
    Function *F = &*mi;
    if (F->empty())
      continue;
    // For each defined function: for each use (a call), add it to our
    // UnvisitedCallers set, and add us to its Callees set.
    // We are ignoring an illegal non-call use of a function; someone
    // else can spot and diagnose that later.
    // If the function has no callers, then add it straight in to VisitOrder.
    CGNode *CGN = &CallGraph[F];
    CGN->F = F;
    if (F->use_empty()) {
      VisitOrder->push_back(F);
      continue;
    }
    for (auto ui = F->use_begin(), ue = F->use_end(); ui != ue; ++ui) {
      if (auto CI = dyn_cast<CallInst>(ui->getUser())) {
        Function *Caller = CI->getParent()->getParent();
        CGNode *CallerNode = &CallGraph[Caller];
        CallerNode->F = Caller;
        CGN->UnvisitedCallers.insert(CallerNode);
        CallerNode->Callees.insert(CGN);
      }
    }
  }
  // Run through the visit order. For each function, remove it from each
  // callee's UnvisitedCallers set, and, if now empty, add the callee to
  // the end of the visit order.
  for (unsigned i = 0; i != VisitOrder->size(); ++i) {
    CGNode *CGN = &CallGraph[(*VisitOrder)[i]];
    for (auto ci = CGN->Callees.begin(), ce = CGN->Callees.end();
        ci != ce; ++ci) {
      CGNode *Callee = *ci;
      Callee->UnvisitedCallers.erase(CGN);
      if (Callee->UnvisitedCallers.empty())
        VisitOrder->push_back(Callee->F);
    }
  }
}

/***********************************************************************
 * processFunction : process CM SIMD CF in a function
 */
void CMSimdCFLowering::processFunction(Function *ArgF)
{
  F = ArgF;
  DEBUG(dbgs() << "CMSimdCFLowering::processFunction:\n" << *F << "\n");
  DEBUG(F->print(dbgs()));
  unsigned CMWidth = PredicatedSubroutines[F];
  // Find the simd branches.
  findSimdBranches(CMWidth);
  // Determine which basic blocks need to be predicated.
  determinePredicatedBlocks();
  // Mark the branch at the end of any to-be-predicated block as a simd branch.
  markPredicatedBranches();
  // Fix simd branches:
  //  - remove backward simd branches
  //  - ensure that the false leg is fallthrough
  fixSimdBranches();
  // Find the join points, and split out any join point into its own basic
  // block.
  findAndSplitJoinPoints();
  // Determine the JIPs for the gotos and joins.
  determineJIPs();
  // Predicate the code.
  predicateCode(CMWidth);
  // Lower the control flow.
  lowerSimdCF();

  SimdBranches.clear();
  PredicatedBlocks.clear();
  JoinPoints.clear();
  RMAddrs.clear();
  AlreadyPredicated.clear();
}

/***********************************************************************
 * findSimdBranches : find the simd branches in the function
 *
 * Enter:   CMWidth = 0 normally, or call mask width if in predicated subroutine
 *
 * This adds blocks to SimdBranches.
 */
void CMSimdCFLowering::findSimdBranches(unsigned CMWidth)
{
  for (auto fi = F->begin(), fe = F->end(); fi != fe; ++fi) {
    BasicBlock *BB = &*fi;
    auto Br = dyn_cast<BranchInst>(BB->getTerminator());
    if (!Br || !Br->isConditional())
      continue;
    if (auto SimdCondUse = getSimdConditionUse(Br->getCondition())) {
      unsigned SimdWidth = (*SimdCondUse)->getType()->getVectorNumElements();
      if (CMWidth && SimdWidth != CMWidth)
        DiagnosticInfoSimdCF::emit(Br, "mismatching SIMD CF width inside SIMD call");
      SimdBranches[BB] = SimdWidth;
    }
  }
}

/***********************************************************************
 * determinePredicatedBlocks : determine which blocks need to be predicated
 *
 * We need to find blocks that are control dependent on a simd branch.
 *
 * This adds blocks to PredicatedBlocks. It also errors when a block is control
 * dependent on more than one simd branch with disagreeing simd width.
 *
 * See Muchnick section 9.5 Program-Dependence Graphs. For each edge m->n in
 * the control flow graph where n does not post-dominate m, find l, the
 * closest common ancestor in the post-dominance tree of m and n. All nodes
 * in the post-dominance tree from l to n except l itself are control dependent
 * on m.
 */
void CMSimdCFLowering::determinePredicatedBlocks()
{
  PostDominatorTree *PDT = nullptr;
  for (auto sbi = SimdBranches.begin(), sbe = SimdBranches.end();
      sbi != sbe; ++sbi) {
    BasicBlock *BlockM = sbi->first;
    auto Br = cast<BranchInst>(BlockM->getTerminator());
    unsigned SimdWidth = sbi->second;
    DEBUG(dbgs() << "simd branch (width " << SimdWidth << ") at " << BlockM->getName() << "\n");
    if (SimdWidth < 2 || SimdWidth > MAX_SIMD_CF_WIDTH || !isPowerOf2_32(SimdWidth))
      DiagnosticInfoSimdCF::emit(Br, "illegal SIMD CF width");
    // BlockM has a simd conditional branch. Get the postdominator tree if we
    // do not already have it.
    if (!PDT) {
      PDT = (PostDominatorTree *)createPostDomTree();
      PDT->runOnFunction(*F);
    }
    // For each successor BlockN of BlockM...
    for (unsigned si = 0, se = Br->getNumSuccessors(); si != se; ++si) {
      auto BlockN = Br->getSuccessor(si);
      // Get BlockL, the closest common postdominator.
      auto BlockL = PDT->findNearestCommonDominator(BlockM, BlockN);
      // Trace up the postdominator tree from BlockN (inclusive) to BlockL
      // (exclusive) to find blocks control dependent on BlockM. This also
      // handles the case that BlockN does postdominate BlockM; no blocks
      // are control dependent on BlockM.
      for (auto Node = PDT->getNode(BlockN); Node && Node->getBlock() != BlockL;
            Node = Node->getIDom()) {
        auto BB = Node->getBlock();
        DEBUG(dbgs() << "  " << BB->getName() << " needs predicating\n");
        auto PBEntry = &PredicatedBlocks[BB];
        if (*PBEntry && *PBEntry != SimdWidth)
          DiagnosticInfoSimdCF::emit(Br, "mismatching SIMD CF width");
        *PBEntry = SimdWidth;
      }
    }
  }
  delete PDT;
}

/***********************************************************************
 * markPredicatedBranches : mark the branch in any to-be-predicated block
 *    as a simd branch, even if it is unconditional
 *
 * This errors if it finds anything other than a BranchInst. Using switch or
 * return inside simd control flow is not allowed.
 */
void CMSimdCFLowering::markPredicatedBranches()
{
  for (auto pbi = PredicatedBlocks.begin(), pbe = PredicatedBlocks.end();
      pbi != pbe; ++pbi) {
    auto BB = pbi->first;
    unsigned SimdWidth = pbi->second;
    auto Term = BB->getTerminator();
    if (!isa<BranchInst>(Term))
      DiagnosticInfoSimdCF::emit(Term, "return or switch not allowed in SIMD control flow");
    if (!SimdBranches[BB])
      DEBUG(dbgs() << "branch at " << BB->getName() << " becomes simd\n");
    SimdBranches[BB] = SimdWidth;
  }
}

/***********************************************************************
 * fixSimdBranches : fix simd branches ready for JIP determination
 *
 * - remove backward simd branches
 * - ensure that the false leg is fallthrough
 */
void CMSimdCFLowering::fixSimdBranches()
{
  // Scan through all basic blocks, remembering which ones we have seen.
  std::set<BasicBlock *> Seen;
  for (auto fi = F->begin(), fe = F->end(); fi != fe; ++fi) {
    BasicBlock *BB = &*fi;
    Seen.insert(BB);
    if (!SimdBranches.count(BB))
      continue;
    // This is a simd branch.
    auto Br = cast<BranchInst>(BB->getTerminator());
    // Check for backward branch in either leg.
    for (unsigned si = 0, se = Br->getNumSuccessors(); si != se; ++si) {
      BasicBlock *Succ = Br->getSuccessor(si);
      if (Seen.find(Succ) != Seen.end()) {
        DEBUG(dbgs() << "simd branch at " << BB->getName() << " succ " << si << " is backward\n");
        if (!Br->isConditional()) {
          // Unconditional simd backward branch. We can just remove its simdness.
          DEBUG(dbgs() << " unconditional, so unsimding\n");
          SimdBranches.erase(SimdBranches.find(BB));
        } else {
          // Conditional simd branch where a leg is backward. Insert an extra
          // block.
          auto NextBB = BB->getNextNode();
          auto NewBB = BasicBlock::Create(BB->getContext(),
                BB->getName() + ".backward", BB->getParent(), NextBB);
          BranchInst::Create(Succ, NewBB)->setDebugLoc(Br->getDebugLoc());
          Br->setSuccessor(si, NewBB);
        }
      }
    }
    if (Br->isConditional()) {
      // Ensure that the false leg is fallthrough.
      auto NextBB = BB->getNextNode();
      if (Br->getSuccessor(1) != NextBB) {
        if (Br->getSuccessor(0) != NextBB) {
          // Neither leg is fallthrough. Add an extra basic block to make the
          // false one fallthrough.
          DEBUG(dbgs() << "simd branch at " << BB->getName() << ": inserted fallthrough\n");
          auto NewBB = BasicBlock::Create(BB->getContext(),
                BB->getName() + ".fallthrough", BB->getParent(), NextBB);
          PredicatedBlocks[NewBB] = PredicatedBlocks[Br->getSuccessor(0)];
          BranchInst::Create(Br->getSuccessor(1), NewBB)
              ->setDebugLoc(Br->getDebugLoc());
          Br->setSuccessor(1, NewBB);
        } else {
          // The true leg is fallthrough. Invert the branch.
          DEBUG(dbgs() << "simd branch at " << BB->getName() << ": inverting\n");
          Use *U = getSimdConditionUse(Br->getCondition());
          if (!U)
            U = &Br->getOperandUse(0);
          Value *Cond = *U;
          auto Xor = BinaryOperator::Create(Instruction::Xor, *U,
              Constant::getAllOnesValue(Cond->getType()),
              "invert", cast<Instruction>(U->getUser()));
          Xor->setDebugLoc(Br->getDebugLoc());
          *U = Xor;
          Br->setSuccessor(0, Br->getSuccessor(1));
          Br->setSuccessor(1, NextBB);
        }
      }
    }
  }
}

/***********************************************************************
 * findAndSplitJoinPoints : find the join points, and split out any join point
 *      into its own basic block
 */
void CMSimdCFLowering::findAndSplitJoinPoints()
{
  for (auto sbi = SimdBranches.begin(), sbe = SimdBranches.end();
      sbi != sbe; ++sbi) {
    auto Br = sbi->first->getTerminator();
    unsigned SimdWidth = sbi->second;
    DEBUG(dbgs() << *Br << "\n");
    auto JP = Br->getSuccessor(0);
    if (JoinPoints.count(JP))
      continue;
    // This is a new join point.
    DEBUG(dbgs() << "new join point " << JP->getName() << "\n");
    // We need to split it into its own basic block, so later we can modify
    // the join to do a branch to its JIP.
    auto SplitBB = BasicBlock::Create(JP->getContext(),
        JP->getName() + ".joinpoint", JP->getParent(), JP);
    if (PredicatedBlocks.find(JP) != PredicatedBlocks.end())
      PredicatedBlocks[SplitBB] = PredicatedBlocks[JP];
    JP->replaceAllUsesWith(SplitBB);
    BranchInst::Create(JP, SplitBB)->setDebugLoc(JP->front().getDebugLoc());
    DEBUG(dbgs() << "split join point " << JP->getName() << " out to " << SplitBB->getName() << "\n");
    JP = SplitBB;
    JoinPoints[JP] = SimdWidth;
  }
}

/***********************************************************************
 * determineJIPs : determine the JIPs for the gotos and joins
 */
void CMSimdCFLowering::determineJIPs()
{
  DEBUG(dbgs() << "determineJIPs: " << F->getName() << "\n");
  // Number the basic blocks.
  std::map<BasicBlock *, unsigned> Numbers;
  unsigned Num = 0;
  for (auto fi = F->begin(), fe = F->end(); fi != fe; ++fi) {
    BasicBlock *BB = &*fi;
    Numbers[BB] = Num++;
  }
  // Work out which joins do not need a JIP at all. Doing that helps avoid
  // problems in the GenX backend where a join that turns out to be a branching
  // join label needs to be in a basic block by itself, so other code has to be
  // moved out, which is not always possible.
  //
  // A join does not need a JIP if we can guarantee that any path reaching the
  // join will result in at least one channel being enabled.
  //
  // As a proxy for that, which is sufficient but maybe not necessary, we
  // divide the control flow up into groups. Two groups are either disjoint, or
  // one is nested inside the other. Then the join at the end of a group does
  // not need a JIP.
  //
  // We find the groups as follows: any edge that is not a fallthrough edge
  // causes the target block and the block after the branch block to be in the
  // same group.
  Grouping<BasicBlock *> Groups;
  for (auto NextBB = &F->front(), EndBB = &F->back(); NextBB;) {
    auto BB = NextBB;
    NextBB = BB == EndBB ? nullptr : BB->getNextNode();
    auto Term = BB->getTerminator();
    for (unsigned si = 0, se = Term->getNumSuccessors(); si != se; ++si) {
      BasicBlock *Succ = Term->getSuccessor(si);
      if (Succ == NextBB)
        continue;
      // We have a non-fallthrough edge BB -> Succ. Thus NextBB and Succ need
      // to be in the same group.
      DEBUG(dbgs() << "joinGroups " << NextBB->getName() << " " << Succ->getName() << "\n");
      Groups.joinGroups(NextBB, Succ);
    }
  }
  // Repeat until we stop un-simding branches...
  for (;;) {
    // Determine the JIPs for the SIMD branches.
    for (auto sbi = SimdBranches.begin(), sbe = SimdBranches.end();
        sbi != sbe; ++sbi)
      determineJIP(sbi->first, &Numbers, /*IsJoin=*/false);
    // Determine the JIPs for the joins. A join does not need a JIP if it is the
    // last block in its group.
    std::set<BasicBlock *> SeenGroup;
    for (auto BB = &F->back();;) {
      DEBUG(dbgs() << "  " << BB->getName() << " is group " << Groups.getGroup(BB)->getName() << "\n");
      if (JoinPoints.count(BB)) {
        if (!SeenGroup.insert(Groups.getGroup(BB)).second)
          determineJIP(BB, &Numbers, /*IsJoin=*/true);
        else
          DEBUG(dbgs() << BB->getName() << " does not need JIP\n");
      }
      if (BB == &F->front())
        break;
      BB = BB->getPrevNode();
    }

    // See if we have any unconditional branch with UIP == JIP or no JIP. If so,
    // it can stay as a scalar unconditional branch.
    SmallVector<BasicBlock *, 4> BranchesToUnsimd;
    std::set<BasicBlock *> UIPs;
    for (auto sbi = SimdBranches.begin(), sbe = SimdBranches.end();
        sbi != sbe; ++sbi) {
      BasicBlock *BB = sbi->first;
      auto Br = cast<BranchInst>(BB->getTerminator());
      BasicBlock *UIP = Br->getSuccessor(0);
      BasicBlock *JIP = JIPs[BB];
      if (!Br->isConditional() && (!JIP || UIP == JIP)) {
        DEBUG(dbgs() << BB->getName() << ": converting back to unconditional branch to " << UIP->getName() << "\n");
        BranchesToUnsimd.push_back(BB);
      } else
        UIPs.insert(UIP);
    }
    // If we did not un-simd any branch, we are done.
    if (BranchesToUnsimd.empty())
      break;
    for (auto i = BranchesToUnsimd.begin(), e = BranchesToUnsimd.end(); i != e; ++i)
      SimdBranches.erase(SimdBranches.find(*i));

    // For each join, see if it is still the UIP of any goto. If not, remove it.
    SmallVector<BasicBlock *, 4> JoinsToRemove;
    for (auto i = JoinPoints.begin(), e = JoinPoints.end(); i != e; ++i)
      if (UIPs.find(i->first) == UIPs.end())
        JoinsToRemove.push_back(i->first);
    for (auto i = JoinsToRemove.begin(), e = JoinsToRemove.end(); i != e; ++i) {
      DEBUG(dbgs() << (*i)->getName() << ": removing now unreferenced join\n");
      JoinPoints.erase(JoinPoints.find(*i));
    }
  }
}

/***********************************************************************
 * determineJIP : determine the JIP for a goto or join
 */
void CMSimdCFLowering::determineJIP(BasicBlock *BB,
      std::map<BasicBlock *, unsigned> *Numbers, bool IsJoin)
{
  BasicBlock *UIP = nullptr;
  auto Br = cast<BranchInst>(BB->getTerminator());
  if (!IsJoin)
    UIP = Br->getSuccessor(0); // this is a goto with a UIP, not a join
  DEBUG(dbgs() << BB->getName() << ": UIP is " << (UIP ? UIP->getName() : "(none)") << "\n");
  // Scan forwards to find the next join point that could be resumed by any
  // code before or at BB.
  unsigned BBNum = (*Numbers)[BB];
  bool NeedNextJoin = false;
  BasicBlock *JP = BB->getNextNode();
  unsigned JPNum = BBNum + 1;
  for (;; JP = JP->getNextNode(), ++JPNum) {
    assert(JP);
    if ((*Numbers)[JP] != JPNum)
      DEBUG(dbgs() << JP->getName() << " number " << (*Numbers)[JP] << " does not match " << JPNum << " for " << JP->getName() << "\n");
    assert((*Numbers)[JP] == JPNum);
    // If we have reached UIP, then that is also JIP.
    if (JP == UIP)
      break;
    // See if JP is a basic block with a branch from before BB.
    for (auto ui = JP->use_begin(), ue = JP->use_end(); ui != ue; ++ui) {
      auto BranchBlock = cast<Instruction>(ui->getUser())->getParent();
      if ((*Numbers)[BranchBlock] < BBNum) {
        NeedNextJoin = true;
        break;
      }
    }
    if (NeedNextJoin && JoinPoints.count(JP))
      break; // found join point
    // See if JP finishes with a branch to BB or before.
    auto Term = JP->getTerminator();
    for (unsigned si = 0, se = Term->getNumSuccessors(); si != se; ++si) {
      auto Succ = Term->getSuccessor(si);
      if ((*Numbers)[Succ] <= BBNum) {
        NeedNextJoin = true;
        break;
      }
    }
    assert(JP != &BB->getParent()->back() && "reached end");
  }
  DEBUG(dbgs() << BB->getName() << ": JIP is " << JP->getName() << "\n");
  JIPs[BB] = JP;
}

/***********************************************************************
 * predicateCode : predicate the instructions in the code
 */
void CMSimdCFLowering::predicateCode(unsigned CMWidth)
{
  if (CMWidth) {
    // Inside a predicated call, also predicate all other blocks, but without
    // predicating the stores. We do this first so the entry block gets done
    // before any other block, avoiding a problem that code we insert to set up
    // the EMs and RMs accidentally gets predicated.
    for (auto fi = F->begin(), fe = F->end(); fi != fe; ++fi) {
      BasicBlock *BB = &*fi;
      if (PredicatedBlocks.find(BB) == PredicatedBlocks.end())
        predicateBlock(BB, CMWidth, /*PredicateStores=*/false);
    }
  }
  // Predicate all basic blocks that need it.
  for (auto pbi = PredicatedBlocks.begin(), pbe = PredicatedBlocks.end();
      pbi != pbe; ++pbi) {
    BasicBlock *BB = pbi->first;
    unsigned SimdWidth = pbi->second;
    predicateBlock(BB, SimdWidth, /*PredicateStores=*/true);
  }
}

/***********************************************************************
 * predicateBlock : add predication to a basic block
 *
 * Enter:   BB = basic block
 *          R = outermost simd CF region containing it
 */
void CMSimdCFLowering::predicateBlock(BasicBlock *BB, unsigned SimdWidth,
    bool PredicateStores)
{
  for (auto bi = BB->begin(), be = BB->end(); bi != be; ) {
    Instruction *Inst = &*bi;
    ++bi; // Increment here in case Inst is removed
    predicateInst(Inst, SimdWidth, PredicateStores);
  }
}

/***********************************************************************
 * createWrRegion : create wrregion instruction
 *
 * Enter:   Args = the args for wrregion
 *          Name
 *          InsertBefore
 */
static CallInst *createWrRegion(ArrayRef<Value *> Args, const Twine &Name,
    Instruction *InsertBefore)
{
  Type *OverloadedTypes[] = { Args[0]->getType(), Args[1]->getType(),
      Args[5]->getType(), Args[7]->getType() };
  Module *M = InsertBefore->getParent()->getParent()->getParent();
  Function *Decl = Intrinsic::getDeclaration(M,
      OverloadedTypes[0]->isFPOrFPVectorTy()
        ? llvm::Intrinsic::genx_wrregionf : llvm::Intrinsic::genx_wrregioni,
      OverloadedTypes);
  auto WrRegion = CallInst::Create(Decl, Args, Name, InsertBefore);
  WrRegion->setDebugLoc(InsertBefore->getDebugLoc());
  return WrRegion;
}

/***********************************************************************
 * predicateInst : add predication to an Instruction if necessary
 *
 * Enter:   Inst = the instruction
 *          SimdWidth = simd cf width in force
 *          PredicateStores = whether to predicate store instructions
 */
void CMSimdCFLowering::predicateInst(Instruction *Inst, unsigned SimdWidth,
    bool PredicateStores)
{
  if (auto CI = dyn_cast<CallInst>(Inst)) {
    unsigned IntrinsicID = Intrinsic::not_intrinsic;
    auto Callee = CI->getCalledFunction();
    if (Callee)
      IntrinsicID = Callee->getIntrinsicID();
    switch (IntrinsicID) {
      case Intrinsic::genx_rdregioni:
      case Intrinsic::genx_rdregionf:
      case Intrinsic::genx_wrregioni:
      case Intrinsic::genx_wrregionf:
      case Intrinsic::genx_simdcf_any:
        return; // ignore these intrinsics
      case Intrinsic::genx_simdcf_predicate:
        rewritePredication(CI, SimdWidth);
        return;
      case Intrinsic::genx_gather_orig:
      case Intrinsic::genx_gather4_orig:
      case Intrinsic::genx_scatter_orig:
      case Intrinsic::genx_scatter4_orig:
        CI = convertScatterGather(CI, IntrinsicID);
        predicateScatterGather(CI, SimdWidth, 0);
        return;
      case Intrinsic::not_intrinsic:
        // Call to real subroutine.
        predicateCall(CI, SimdWidth);
        return;
    }
    // An IntrNoMem intrinsic is an ALU intrinsic and can be ignored.
    if (Callee->doesNotAccessMemory())
      return;
    // Look for a predicate operand in operand 2, 1 or 0.
    unsigned PredNum = std::max(2U, CI->getNumArgOperands());
    for (;;) {
      if (auto VT = dyn_cast<VectorType>(CI->getArgOperand(PredNum)->getType()))
      {
        if (VT->getElementType()->isIntegerTy(1)) {
          // We have a predicate operand.
          predicateScatterGather(CI, SimdWidth, PredNum);
          return;
        }
      }
      if (!PredNum)
        break;
      --PredNum;
    }
    DiagnosticInfoSimdCF::emit(CI, "illegal instruction inside SIMD control flow");
    return;
  }
  if (PredicateStores)
    if (auto SI = dyn_cast<StoreInst>(Inst))
      predicateStore(SI, SimdWidth);
}

/***********************************************************************
 * rewritePredication : convert a predication intrinsic call into a
 * selection based on the region's SIMD predicate mask.
 *
 * Enter:   Inst = the predication intrinsic call instruction
 *          SimdWidth = simd cf width in force
 */
void CMSimdCFLowering::rewritePredication(CallInst *CI, unsigned SimdWidth)
{
  auto EnabledValues = CI->getArgOperand(0);
  auto DisabledDefaults = CI->getArgOperand(1);

  assert(isa<VectorType>(EnabledValues->getType()) &&
         EnabledValues->getType() == DisabledDefaults->getType() &&
         "malformed predication intrinsic");

  if (cast<VectorType>(EnabledValues->getType())->getNumElements() != SimdWidth) {
    DiagnosticInfoSimdCF::emit(CI, "mismatching SIMD width inside SIMD control flow");
    return;
  }
  auto EM = loadExecutionMask(CI, SimdWidth);
  auto Select = SelectInst::Create(EM, EnabledValues, DisabledDefaults,
      EnabledValues->getName() + ".simdcfpred", CI);
  Select->setDebugLoc(CI->getDebugLoc());
  CI->replaceAllUsesWith(Select);
  CI->eraseFromParent();
}

/***********************************************************************
 * predicateStore : add predication to a StoreInst
 *
 * Enter:   Inst = the instruction
 *          SimdWidth = simd cf width in force
 *
 * This code avoids using the utility functions and classes for the wrregion
 * intrinsic that are in the GenX backend because this pass is not part of the
 * GenX backend.
 */
void CMSimdCFLowering::predicateStore(StoreInst *SI, unsigned SimdWidth)
{
  auto V = SI->getValueOperand();
  auto StoreVT = dyn_cast<VectorType>(V->getType());
  if (!StoreVT || StoreVT->getNumElements() == 1)
    return; // Scalar store not predicated
  // See if the value to store is a wrregion (possibly predicated) of the
  // right width. If so, we predicate that instead. This also handles
  // the case that the value to store is wider than the simd CF width,
  // but there is a wrregion with the right width.
  // Also allow for a chain of multiple wrregions, as clang can generate
  // two, one for the columns and one for the rows.
  // Also skip any bitcasts.
  CallInst *WrRegionToPredicate = nullptr;
  Use *U = &SI->getOperandUse(0);
  for (;;) {
    if (auto BC = dyn_cast<BitCastInst>(V)) {
      U = &BC->getOperandUse(0);
      V = *U;
      continue;
    }
    auto WrRegion = dyn_cast<CallInst>(V);
    if (!WrRegion)
      break;
    auto Callee = WrRegion->getCalledFunction();
    if (!Callee)
      break;
    unsigned IID = Callee->getIntrinsicID();
    if (IID != Intrinsic::genx_wrregioni
         && Callee->getIntrinsicID() != Intrinsic::genx_wrregionf) {
      // Not wrregion. See if it is an intrinsic that has already been
      // predicated; if so do not attempt to predicate the store.
      if (AlreadyPredicated.find(WrRegion) != AlreadyPredicated.end())
        return;
      // Otherwise break out of the wrregion-and-bitcast-traversing loop.
      break;
    }
    // We have a wrregion. Check its input width.
    unsigned Width = 0;
    Value *Input = WrRegion->getArgOperand(
        Intrinsic::GenXRegion::NewValueOperandNum);
    if (auto VT = dyn_cast<VectorType>(Input->getType()))
      Width = VT->getNumElements();
    if (Width == SimdWidth) {
      // This wrregion has the right width input. We could predicate it.
      if (WrRegionToPredicate)
        U = &WrRegionToPredicate->getOperandUse(
            Intrinsic::GenXRegion::NewValueOperandNum);
      WrRegionToPredicate = WrRegion;
      V = WrRegionToPredicate->getArgOperand(
          Intrinsic::GenXRegion::NewValueOperandNum);
      // See if it is already predicated, other than by an all true constant.
      Value *Pred = WrRegion->getArgOperand(
          Intrinsic::GenXRegion::PredicateOperandNum);
      if (auto C = dyn_cast<Constant>(Pred))
        if (C->isAllOnesValue())
          Pred = nullptr;
      if (Pred) {
        // Yes it is predicated. Stop here and further predicate it.
        break;
      }
    } else if (Width == 1) {
      // Single element wrregion. This is a scalar operation, so we do not
      // want to predicate it at all.
      return;
    } else if (Width < SimdWidth) {
      // Too narrow. Predicate the last correctly sized wrregion or the store.
      break;
    }
  }
  if (WrRegionToPredicate) {
    // We found a wrregion to predicate. Replace it with a predicated one.
    *U = predicateWrRegion(WrRegionToPredicate, SimdWidth);
    if (WrRegionToPredicate->use_empty())
      WrRegionToPredicate->eraseFromParent();
    return;
  }
  if (StoreVT->getNumElements() != SimdWidth) {
    DiagnosticInfoSimdCF::emit(SI, "mismatching SIMD width inside SIMD control flow");
    return;
  }
  // Predicate the store by creating a select.
  auto Load = new LoadInst(SI->getPointerOperand(),
      SI->getPointerOperand()->getName() + ".simdcfpred.load", SI);
  Load->setDebugLoc(SI->getDebugLoc());
  auto EM = loadExecutionMask(SI, SimdWidth);
  auto Select = SelectInst::Create(EM, V, Load,
      V->getName() + ".simdcfpred", SI);
  SI->setOperand(0, Select);
}

/***********************************************************************
 * convertScatterGather : convert old unpredicable scatter/gather
 *
 * This converts an old-style gather, gather4, scatter, scatter4 into a
 * new-style gather_scaled, gather4_scaled, scatter_scaled, scatter4_scaled
 * so it can be predicated.
 */
CallInst *CMSimdCFLowering::convertScatterGather(CallInst *CI, unsigned IID)
{
  bool IsScatter = IID == Intrinsic::genx_scatter_orig
                || IID == Intrinsic::genx_scatter4_orig;
  bool Is4 = IID == Intrinsic::genx_gather4_orig || IID == Intrinsic::genx_scatter4_orig;
  unsigned NumArgs = CI->getNumArgOperands();
  auto GlobalOffset = CI->getArgOperand(NumArgs - 3);
  auto EltOffsets = CI->getArgOperand(NumArgs - 2);
  // Gather the overloaded types and get the intrinsic declaration.
  SmallVector<Type *, 4> Tys;
  if (!IsScatter)
    Tys.push_back(CI->getType()); // return type
  auto PredTy = VectorType::get(Type::getInt1Ty(
            CI->getContext()), EltOffsets->getType()->getVectorNumElements());
  Tys.push_back(PredTy); // predicate type
  Tys.push_back(CI->getArgOperand(NumArgs - 2)->getType()); // offsets type
  if (IsScatter)
    Tys.push_back(CI->getArgOperand(NumArgs - 1)->getType()); // data type
  unsigned NewIID = 0;
  switch (IID) {
    case Intrinsic::genx_gather_orig: NewIID = Intrinsic::genx_gather_scaled; break;
    case Intrinsic::genx_gather4_orig: NewIID = Intrinsic::genx_gather4_scaled; break;
    case Intrinsic::genx_scatter_orig: NewIID = Intrinsic::genx_scatter_scaled; break;
    case Intrinsic::genx_scatter4_orig: NewIID = Intrinsic::genx_scatter4_scaled; break;
    default: llvm_unreachable("invalid intrinsic ID"); break;
  }
  Function *Decl = Intrinsic::getDeclaration(
      CI->getParent()->getParent()->getParent(), (Intrinsic::ID)NewIID, Tys);
  // Get the element size.
  unsigned EltSize = 4;
  if (!Is4)
    EltSize = CI->getArgOperand(0)->getType()->getScalarType()
      ->getPrimitiveSizeInBits() / 8U;
  // Scale the global and element offsets.
  if (EltSize != 1) {
    auto EltSizeC = ConstantInt::get(GlobalOffset->getType(), EltSize);
    auto NewInst = BinaryOperator::Create(Instruction::Mul, GlobalOffset,
        EltSizeC, "", CI);
    NewInst->setDebugLoc(CI->getDebugLoc());
    GlobalOffset = NewInst;
    NewInst = BinaryOperator::Create(Instruction::Mul, EltOffsets,
        ConstantVector::getSplat(
          EltOffsets->getType()->getVectorNumElements(), EltSizeC),
        "", CI);
    NewInst->setDebugLoc(CI->getDebugLoc());
    EltOffsets = NewInst;
  }
  // Gather the args for the new intrinsic. First the all ones predicate.
  SmallVector<Value *, 8> Args;
  Args.push_back(Constant::getAllOnesValue(PredTy));
  // Block size for non-4 variants, channel mask (inverted) for 4 variants.
  if (!Is4)
    Args.push_back(ConstantInt::get(GlobalOffset->getType(),
        countTrailingZeros(EltSize, ZB_Undefined)));
  else {
    unsigned Mask = cast<ConstantInt>(CI->getArgOperand(0))->getSExtValue();
    Mask ^= 0xf;
    Args.push_back(ConstantInt::get(CI->getArgOperand(0)->getType(), Mask));
  }
  // Scale -- always 0.
  Args.push_back(ConstantInt::get(Type::getInt16Ty(CI->getContext()), 0));
  // Surface index.
  Args.push_back(CI->getArgOperand(NumArgs - 4));
  // Global and element offsets.
  Args.push_back(GlobalOffset);
  Args.push_back(EltOffsets);
  // Data.
  Args.push_back(CI->getArgOperand(NumArgs - 1));
  // Create the new intrinsic and replace the old one.
  auto NewCI = CallInst::Create(Decl, Args, "", CI);
  NewCI->takeName(CI);
  NewCI->setDebugLoc(CI->getDebugLoc());
  CI->replaceAllUsesWith(NewCI);
  CI->eraseFromParent();
  return NewCI;
}

/***********************************************************************
 * predicateScatterGather : predicate a scatter/gather intrinsic call
 *
 * This works on the scatter/gather intrinsics with a predicate operand.
 */
void CMSimdCFLowering::predicateScatterGather(CallInst *CI, unsigned SimdWidth,
      unsigned PredOperandNum)
{
  Value *OldPred = CI->getArgOperand(PredOperandNum);
  assert(OldPred->getType()->getScalarType()->isIntegerTy(1));
  if (SimdWidth != OldPred->getType()->getVectorNumElements()) {
    DiagnosticInfoSimdCF::emit(CI, "mismatching SIMD width of scatter/gather inside SIMD control flow");
    return;
  }
  Instruction *NewPred = loadExecutionMask(CI, SimdWidth);
  if (auto C = dyn_cast<Constant>(OldPred))
    if (C->isAllOnesValue())
      OldPred = nullptr;
  if (OldPred) {
    auto And = BinaryOperator::Create(Instruction::And, OldPred, NewPred,
        OldPred->getName() + ".and." + NewPred->getName(), CI);
    And->setDebugLoc(CI->getDebugLoc());
    NewPred = And;
  }
  CI->setArgOperand(PredOperandNum, NewPred);
  AlreadyPredicated.insert(CI);
}

/***********************************************************************
 * predicateWrRegion : create a predicated version of a wrregion
 *
 * Enter:   WrR = the wrregion, whose value width must be equal to the
 *                simd CF width
 *          SimdWidth = simd cf width in force
 *
 * Return:  the new predicated wrregion
 *
 * If the wrregion is already predicated, the new one has a predicated that
 * is an "and" of the original predicate and our EM.
 */
CallInst *CMSimdCFLowering::predicateWrRegion(CallInst *WrR, unsigned SimdWidth)
{
  // First gather the args of the original wrregion.
  SmallVector<Value *, 8> Args;
  for (unsigned i = 0, e = WrR->getNumArgOperands(); i != e; ++i)
    Args.push_back(WrR->getArgOperand(i));
  // Modify the predicate in Args.
  Value *Pred = Args[Intrinsic::GenXRegion::PredicateOperandNum];
  if (auto C = dyn_cast<Constant>(Pred))
    if (C->isAllOnesValue())
      Pred = nullptr;
  auto EM = loadExecutionMask(WrR, SimdWidth);
  if (!Pred)
    Pred = EM;
  else {
    auto And = BinaryOperator::Create(Instruction::And, EM, Pred,
        Pred->getName() + ".and." + EM->getName(), WrR);
    And->setDebugLoc(WrR->getDebugLoc());
    Pred = And;
  }
  Args[Intrinsic::GenXRegion::PredicateOperandNum] = Pred;
  return createWrRegion(Args, WrR->getName(), WrR);
}

/***********************************************************************
 * predicateCall : predicate a real call to a subroutine
 */
void CMSimdCFLowering::predicateCall(CallInst *CI, unsigned SimdWidth)
{
  Function *F = CI->getCalledFunction();
  assert(F);
  auto PSEntry = &PredicatedSubroutines[F];
  if (!*PSEntry)
    *PSEntry = SimdWidth;
  else if (*PSEntry != SimdWidth)
    DiagnosticInfoSimdCF::emit(CI, "mismatching SIMD width of called subroutine");
}

/***********************************************************************
 * lowerSimdCF : lower the simd control flow
 */
void CMSimdCFLowering::lowerSimdCF()
{
  // First lower the simd branches.
  for (auto sbi = SimdBranches.begin(), sbe = SimdBranches.end();
      sbi != sbe; ++sbi) {
    BasicBlock *BB = sbi->first;
    auto Br = cast<BranchInst>(BB->getTerminator());
    BasicBlock *UIP = Br->getSuccessor(0);
    BasicBlock *JIP = JIPs[BB];
    DEBUG(dbgs() << "lower branch at " << BB->getName() << ", UIP=" << UIP->getName() << ", JIP=" << JIP->getName() << "\n");
    if (!Br->isConditional()) {
      // Unconditional branch.  Turn it into a conditional branch on true,
      // adding a fallthrough on false.
      auto NewBr = BranchInst::Create(UIP, BB->getNextNode(),
          Constant::getAllOnesValue(Type::getInt1Ty(BB->getContext())), BB);
      NewBr->setDebugLoc(Br->getDebugLoc());
      Br->eraseFromParent();
      Br = NewBr;
    }
    Value *Cond = Br->getCondition();
    Use *CondUse = getSimdConditionUse(Cond);
    DebugLoc DL = Br->getDebugLoc();
    if (CondUse)
      Cond = *CondUse;
    else {
      // Branch is currently scalar. Splat to a vector condition.
      unsigned SimdWidth = PredicatedBlocks[BB];
      if (auto C = dyn_cast<Constant>(Cond))
        Cond = ConstantVector::getSplat(SimdWidth, C);
      else {
        Cond = Br->getCondition();
        Type *VecTy = VectorType::get(Cond->getType(), 1);
        Value *Undef = UndefValue::get(VecTy);
        Type *I32Ty = Type::getInt32Ty(Cond->getContext());
        auto Insert = InsertElementInst::Create(Undef, Cond,
            Constant::getNullValue(I32Ty), Cond->getName() + ".splat", Br);
        Insert->setDebugLoc(DL);
        auto Splat = new ShuffleVectorInst(Insert, Undef,
            Constant::getNullValue(VectorType::get(I32Ty, SimdWidth)),
            Insert->getName(), Br);
        Splat->setDebugLoc(DL);
        Cond = Splat;
      }
    }
    // Insert {NewEM,NewRM,BranchCond} = llvm.genx.simdcf.goto(OldEM,OldRM,~Cond)
    unsigned SimdWidth = Cond->getType()->getVectorNumElements();
    auto NotCond = BinaryOperator::Create(Instruction::Xor, Cond,
        Constant::getAllOnesValue(Cond->getType()), Cond->getName() + ".not",
        Br);
    Value *RMAddr = getRMAddr(UIP, SimdWidth);
    Instruction *OldEM = new LoadInst(EMVar, EMVar->getName(), Br);
    OldEM->setDebugLoc(DL);
    auto OldRM = new LoadInst(RMAddr, RMAddr->getName(), Br);
    OldRM->setDebugLoc(DL);
    Type *Tys[] = { OldEM->getType(), OldRM->getType() };
    auto GotoFunc = Intrinsic::getDeclaration(BB->getParent()->getParent(),
          Intrinsic::genx_simdcf_goto, Tys);
    Value *Args[] = { OldEM, OldRM, NotCond };
    auto Goto = CallInst::Create(GotoFunc, Args, "goto", Br);
    Goto->setDebugLoc(DL);
    Instruction *NewEM = ExtractValueInst::Create(Goto, 0, "goto.extractem", Br);
    (new StoreInst(NewEM, EMVar, Br))->setDebugLoc(DL);
    auto NewRM = ExtractValueInst::Create(Goto, 1, "goto.extractrm", Br);
    (new StoreInst(NewRM, RMAddr, Br))->setDebugLoc(DL);
    auto BranchCond = ExtractValueInst::Create(Goto, 2, "goto.extractcond", Br);
    // Change the branch condition.
    auto OldCond = dyn_cast<Instruction>(Br->getCondition());
    Br->setCondition(BranchCond);
    // Change the branch target to JIP.
    Br->setSuccessor(0, JIP);
    // Erase the old llvm.genx.simdcf.any.
    if (OldCond && OldCond->use_empty())
      OldCond->eraseFromParent();
  }
  // Then lower the join points.
  for (auto jpi = JoinPoints.begin(), jpe = JoinPoints.end();
      jpi != jpe; ++jpi) {
    BasicBlock *JP = jpi->first;
    unsigned SimdWidth = jpi->second;
    DEBUG(dbgs() << "lower join point " << JP->getName() << "\n");
    DebugLoc DL = JP->front().getDebugLoc();
    Instruction *InsertBefore = JP->getFirstNonPHI();
    // Insert {NewEM,BranchCond} = llvm.genx.simdcf.join(OldEM,RM)
    Value *RMAddr = getRMAddr(JP, SimdWidth);
    Instruction *OldEM = new LoadInst(EMVar, EMVar->getName(), InsertBefore);
    OldEM->setDebugLoc(DL);
    auto RM = new LoadInst(RMAddr, RMAddr->getName(), InsertBefore);
    RM->setDebugLoc(DL);
    Type *Tys[] = { OldEM->getType(), RM->getType() };
    auto JoinFunc = Intrinsic::getDeclaration(JP->getParent()->getParent(),
          Intrinsic::genx_simdcf_join, Tys);
    Value *Args[] = { OldEM, RM };
    auto Join = CallInst::Create(JoinFunc, Args, "join", InsertBefore);
    Join->setDebugLoc(DL);
    auto NewEM = ExtractValueInst::Create(Join, 0, "join.extractem", InsertBefore);
    (new StoreInst(NewEM, EMVar, InsertBefore))->setDebugLoc(DL);
    auto BranchCond = ExtractValueInst::Create(Join, 1, "join.extractcond", InsertBefore);
    // Zero RM.
    (new StoreInst(Constant::getNullValue(RM->getType()), RMAddr, InsertBefore))
          ->setDebugLoc(DL);
    BasicBlock *JIP = JIPs[JP];
    if (JIP) {
      // This join point is in predicated code, so it was separated into its
      // own block. It needs to be turned into a conditional branch to JIP,
      // with the condition from llvm.genx.simdcf.join.
      auto Br = cast<BranchInst>(JP->getTerminator());
      assert(!Br->isConditional());
      auto NewBr = BranchInst::Create(JIP, JP->getNextNode(), BranchCond, Br);
      NewBr->setDebugLoc(DL);
      Br->eraseFromParent();
      // Get the JIP's RM, just to ensure that it knows its SIMD width in case
      // nothing else references it.
      getRMAddr(JIP, RM->getType()->getVectorNumElements());
    }
  }
}

/***********************************************************************
 * getSimdConditionUse : given a branch condition, if it is
 *    llvm.genx.simdcf.any, get the vector condition
 */
Use *CMSimdCFLowering::getSimdConditionUse(Value *Cond)
{
  if (auto CI = isSimdCFAny(Cond))
    return &CI->getOperandUse(0);
  return nullptr;
}

/***********************************************************************
 * isSimdCFAny : given a value (or nullptr), see if it is a call to
 *    llvm.genx.simdcf.any
 *
 * Return:  the instruction (cast to CallInst) if it is such a call
 *          else nullptr
 */
CallInst *CMSimdCFLowering::isSimdCFAny(Value *V)
{
  if (auto CI = dyn_cast_or_null<CallInst>(V))
    if (Function *Callee = CI->getCalledFunction())
      if (Callee->getIntrinsicID() == Intrinsic::genx_simdcf_any)
        return CI;
  return nullptr;
}

/***********************************************************************
 * loadExecutionMask : create instruction to load EM
 */
Instruction *CMSimdCFLowering::loadExecutionMask(Instruction *InsertBefore,
    unsigned SimdWidth)
{
  Instruction *EM = new LoadInst(EMVar, EMVar->getName(), InsertBefore);
  EM->setDebugLoc(InsertBefore->getDebugLoc());
  // If the simd width is not MAX_SIMD_CF_WIDTH, extract the part of EM we want.
  if (SimdWidth == MAX_SIMD_CF_WIDTH)
    return EM;
  if (ShuffleMask.empty()) {
    auto I32Ty = Type::getInt32Ty(F->getContext());
    for (unsigned i = 0; i != 32; ++i)
      ShuffleMask.push_back(ConstantInt::get(I32Ty, i));
  }
  EM = new ShuffleVectorInst(EM, UndefValue::get(EM->getType()),
      ConstantVector::get(ArrayRef<Constant *>(ShuffleMask).slice(0, SimdWidth)),
      Twine("EM") + Twine(SimdWidth), InsertBefore);
  EM->setDebugLoc(InsertBefore->getDebugLoc());
  return EM;
}

/***********************************************************************
 * getRMAddr : get address of resume mask variable for a particular join
 *        point, creating the variable if necessary
 *
 * Enter:   JP = the join point
 *          SimdWidth = the simd width for the join point, used for creating
 *              the RM variable. Can be 0 as long as the RM variable already
 *              exists.
 */
Value *CMSimdCFLowering::getRMAddr(BasicBlock *JP, unsigned SimdWidth)
{
  DEBUG(dbgs() << "getRMAddr(" << JP->getName() << ", " << SimdWidth << ")\n");
  auto RMAddr = &RMAddrs[JP];
  if (!*RMAddr) {
    assert(SimdWidth);
    // Create an RM variable for this join point. Insert an alloca at the start
    // of the function.
    Type *RMTy = VectorType::get(Type::getInt1Ty(F->getContext()), SimdWidth);
    Instruction *InsertBefore = &F->front().front();
    *RMAddr = new AllocaInst(RMTy, Twine("RM.") + JP->getName(), InsertBefore);
    // Initialize to all zeros.
    new StoreInst(Constant::getNullValue(RMTy), *RMAddr, InsertBefore);
  }
  assert(!SimdWidth
      || (*RMAddr)->getType()->getPointerElementType()->getVectorNumElements()
        == SimdWidth);
  return *RMAddr;
}

/***********************************************************************
 * DiagnosticInfoSimdCF::emit : emit an error or warning
 */
void DiagnosticInfoSimdCF::emit(Instruction *Inst, const Twine &Msg,
        DiagnosticSeverity Severity)
{
  DiagnosticInfoSimdCF Err(Severity, *Inst->getParent()->getParent(),
      Inst->getDebugLoc(), Msg);
  Inst->getContext().diagnose(Err);
}

