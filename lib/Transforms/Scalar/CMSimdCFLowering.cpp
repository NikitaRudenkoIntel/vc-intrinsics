//===- CMSimdCFLowering.cpp - Lower CM SIMD control flow ------------------===//
//
//  INTEL CONFIDENTIAL
//  Copyright 2015 Intel Corporation All Rights Reserved.
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
// This pass lowers CM SIMD control flow into a form where the IR reflects
// the semantics.
//
// On entry, any SIMD control flow conditional branch is a br instruction with
// a scalar condition that is the result of an llvm.genx.simdcf.any intrinsic.
// The IR in this state does not reflect the real semantics, and would thus be
// prone to LLVM optimizations misanalyzing and misoptimizing it.
//
// This pass runs very early, straight after Clang codegen has generated the
// IR.
//
// After this pass, the LLVM IR reflects the semantics using a model of Gen
// unstructured SIMD control flow (goto/join instructions). The idea is that
// the IR generates code that works but is suboptimal, but we can then have a
// pass late in the GenX backend that spots this code and turns it into real
// goto/join instructions.
//
// The model is as follows:
//
//  * There is a vXi1 execution mask (EM) (for SIMD width X). Within SIMD
//    control flow, vector instructions that affect state are predicated by
//    EM. (SIMD control flow of different widths cannot be mixed, although
//    it can appear separately in the same function, so there is a separate
//    EM for each width used in the function.)
//
//  * Each SIMD control flow join point has a vXi1 re-enable mask (RM)
//    variable. It is initialized to 0.
//
//  * A SIMD conditional branch does the following:
//
//    - For a channel that is enabled (bit set in EM) and wants to take the
//      branch, its bit is cleared in EM and set in the branch target's RM.
//
//    - For a forward branch, if all bits in EM are now zero, it branches to
//      the next join point where any currently disabled channel could be
//      re-enabled. For structured control flow, this is the join point of
//      the current or next outer construct.
//
//    - For a backward branch, if any bit in EM is 1, it branches to the
//      earliest point where any currently disabled channel could be
//      re-enabled. For structured control flow, this is the top of the
//      innermost loop.
//
//   * A join point does the following:
//
//    - re-enables channels from its RM variable: EM |= RM. (If we know from
//      the control flow structure that EM==0, as we do at an "else" and a
//      loop exit, then we can instead use EM = RM, which makes the resulting
//      optimized IR considerably simpler.)
//
//    - resets its RM to 0
//
//    - if EM is still all zero, it branches to the next join point where any
//      currently disabled channel could be re-enabled.
//
// Currently this pass assumes and attempts to enforce the CM restrictions on
// SIMD control flow, in that it must be structured and must not be mixed with
// scalar control flow.
//
// Because the pass runs so early, we can assume that basic blocks are in the
// same order as in the source, and so find the structured SIMD control flow
// with a simple linear scan through the blocks.
//
// Knowing the structure allows us to determine which join point to branch to
// in the model above, avoiding code where a completely false outer if has to
// skip through the join points of inner ifs.
//
// Code inside SIMD control flow, or all code in a subroutine with at least one
// call inside SIMD control flow, needs to be predicated. Predication assumes
// that this pass runs so early that all variables are in allocas. It works
// as follows:
//
//    * A store of a vector of the same width as the SIMD CF width has a load
//      and a select (switching on the EM) inserted, so only enabled channels
//      of the variable get updated.
//
//    * For a store of a vector that is wider than the SIMD CF width, we need
//      to trace back through wrregions to find the one that writes a region
//      of the right size. That wrregion is then predicated by the EM.
//
//    * An intrinsic with a predicate argument is modified to use EM as the
//      predicate ("and"ing it with the original predicate if any). This covers
//      scatter/gather.
//
// A possible future enhancement is to allow arbitrary SIMD control flow in CM.
// The pass would need to do something like this:
//
//    * construct an assumed code order using a pre-order DFS of the control
//      flow graph
//
//    * construct a control dependence graph
//
//    * use that to determine which join point a SIMD control flow branch or join
//      point needs to branch to, and to determine when scalar control flow
//      needs to be vectorized.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "cmsimdcflowering"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Transforms/Scalar.h"
#include <algorithm>
#include <set>

using namespace llvm;

namespace {

// Operand numbers for wrregion
enum {
  ValueToWriteOperandNum = 1,
  PredicateOperandNum = 7,
};

/// Diagnostic information for error/warning relating to SIMD control flow.
class DiagnosticInfoSimdCF : public DiagnosticInfo {
private:
  const Twine &Description;
  StringRef Filename;
  unsigned Line;
  unsigned Col;
  static int KindID;
  static int getKindID() {
    if (KindID == 0)
      KindID = llvm::getNextAvailablePluginDiagnosticKind();
    return KindID;
  }
public:
  // Initialize from an Instruction, possibly an llvm.genx.simdcf.any.
  DiagnosticInfoSimdCF(Instruction *Inst, const Twine &Desc, DiagnosticSeverity Severity = DS_Error);
  void print(DiagnosticPrinter &DP) const override;

  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == getKindID();
  }
};
int DiagnosticInfoSimdCF::KindID = 0;

/// Call graph node
struct CGNode {
  Function *F;
  std::set<CGNode *> UnvisitedCallers;
  std::set<CGNode *> Callees;
};

// Region tree node
// This region tree represents only simd control flow, with a root node
// that has all outermost simd control flow constructs as its children.
class Region {
public:
  enum { ROOT, IF, ELSE, DO, BREAK, CONTINUE };
private:
  bool Errored;
  unsigned Kind;
  Region *Parent;
  BasicBlock *Entry; // if block, or loop header block
  BasicBlock *Exit; // endif block, or loop exit block
  unsigned SimdWidth;
  SmallVector<Region *, 4> Children;
  unsigned NumBreaks :16;
  unsigned NumContinues :16;
  Region(unsigned Kind, Region *Parent, BasicBlock *Entry, BasicBlock *Exit)
    : Errored(false), Kind(Kind), Parent(Parent), Entry(Entry), Exit(Exit),
      SimdWidth(!Parent ? 0 : Parent->SimdWidth), NumBreaks(0),
      NumContinues(0) {}
public:
  ~Region();
  static Region *createRegionTree(Function *F, unsigned CMWidth);
  bool hasError() const { return Errored; }
  unsigned getKind() const { return Kind; }
  BasicBlock *getEntry() const { return Entry; }
  BasicBlock *getExit() const { return Exit; }
  BasicBlock *getElse() const;
  unsigned getSimdWidth() const { return SimdWidth; }
  Region *getRoot();
  Region *getParent() const { return Parent; }
  unsigned getNumContinues() const { return NumContinues; }
  // iterator iterates through children.
  typedef SmallVectorImpl<Region *>::iterator iterator;
  typedef SmallVectorImpl<Region *>::const_iterator const_iterator;
  iterator begin() { return Children.begin(); }
  const_iterator begin() const { return Children.begin(); }
  iterator end() { return Children.end(); }
  const_iterator end() const { return Children.end(); }
  size_t size() const { return Children.size(); }
  // postorder_iterator does a post-order depth first traversal of the tree.
  // I could have used LLVM's po_iterator, but that seemed like overkill given
  // that this is a tree, rather than a general graph, so there is no need for
  // the iterator to keep track of visited nodes.
  class PostOrderIterator {
    // If the stack is empty and Root is not 0, the iterator is in a special
    // case state of pointing at the Root. Incrementing it from there stores
    // 0 in Root, which is the end() state.
    Region *Root;
    // Each stack entry has an iterator in .first and the end() in .second.
    SmallVector<std::pair<iterator, iterator>, 8> Stack;
    void push(Region *R) {
      while (R->begin() != R->end()) {
        Stack.push_back(std::pair<iterator, iterator>(R->begin(), R->end()));
        R = *R->begin();
      }
    }
  public:
    // Constructor for end()
    PostOrderIterator() : Root(nullptr) {}
    // Constructor for begin()
    PostOrderIterator(Region *R) : Root(R) { push(R); }
    // Pre-increment
    PostOrderIterator &operator++() {
      if (!Stack.size()) {
        assert(Root);
        Root = nullptr;
      } else {
        if (++Stack.back().first == Stack.back().second)
          Stack.pop_back();
        else
          push(*Stack.back().first);
      }
      return *this;
    }
    // Deref
    Region &operator *() const { if (!Stack.size()) return *Root; return **Stack.back().first; }
    // Comparison
    bool operator==(const PostOrderIterator &Rhs) const {
      if (Stack.size() != Rhs.Stack.size())
        return false;
      if (!Stack.size())
        return Root == Rhs.Root;
      return *Stack.back().first == *Rhs.Stack.back().first;
    }
    bool operator!=(const PostOrderIterator &Rhs) const {
      return !(*this == Rhs);
    }
  };
  typedef PostOrderIterator postorder_iterator;
  postorder_iterator postorder_begin() { return PostOrderIterator(this); }
  postorder_iterator postorder_end() { return PostOrderIterator(); }
  // Error reporting
  void reportIllegal(Instruction *Inst);
  void reportError(const char *Text, Instruction *Inst);
  // Debug dump/print
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const;
#endif
  void print(raw_ostream &OS, bool Deep = true, unsigned Depth = 0) const;
private:
  // Create a new region and push it into its parent's child list.
  Region *push(unsigned Kind, BasicBlock *Entry, BasicBlock *Exit) {
    Region *NewR = new Region(Kind, this, Entry, Exit);
    Children.push_back(NewR);
    return NewR;
  }
  void setSimdWidth(Value *Predicate);
};

/// Pending join point.
struct PendingJoin {
  BasicBlock *BB;
  BasicBlock *Join;
  bool IsEMZero; // whether EM is guaranteed to be 0 on reaching this point
  PendingJoin(BasicBlock *BB, BasicBlock *Join, bool IsEMZero = false)
      : BB(BB), Join(Join), IsEMZero(IsEMZero) {}
};

/// The CM SIMD CF lowering pass (a function pass)
class CMSimdCFLowering : public FunctionPass {
  bool Illegal;
  // An EM (execution mask) variable for each possible SIMD width.
  SmallVector<AllocaInst *, 6> EMs;
  // A map giving the RM (re-enable mask) variable for each join point.
  std::map<BasicBlock *, AllocaInst *> ReenableMasks;
  // Pending joins to action after the CFG analysis.
  SmallVector<PendingJoin, 4> PendingJoins;
  // Subroutines that are predicated, mapping to the replacement one that
  // has an extra arg for the call mask.
  std::map<Function *, Function *> PredicatedSubroutines;
public:
  static char ID;

  CMSimdCFLowering() : FunctionPass(ID) {
    initializeCMSimdCFLoweringPass(*PassRegistry::getPassRegistry());
  }
  void getAnalysisUsage(AnalysisUsage &AU) const {
    FunctionPass::getAnalysisUsage(AU);
  }

  virtual bool doInitialization(Module &M);
  virtual bool runOnFunction(Function &F) { return false; }
  static CallInst *isSimdCFAny(Value *V);
  static Value *getSimdCondition(Value *Cond);
private:
  void calculateVisitOrder(Module *M, std::vector<Function *> *VisitOrder);
  void processFunction(Function *F, unsigned SimdWidth);
  void processRegion(Region *R);
  void lowerIf(Region *R);
  void lowerDo(Region *R);
  BasicBlock *getJoinPoint(Region *R);
  void addJoins();
  void addBranch(Value *Cond, bool BranchOn, BasicBlock *Target, BasicBlock *Join, BasicBlock *BB);
  CallInst *createAny(Value *In, const Twine &Name, Instruction *InsertBefore, DebugLoc DL);
  void predicateBlock(BasicBlock *BB, Region *R);
  void predicateInst(Instruction *Inst, Region *R);
  void predicateStore(StoreInst *SI, Region *R);
  CallInst *predicateWrRegion(CallInst *WrR, Region *R);
  void predicateCall(CallInst *CI, Region *R);
  CallInst *addCallMaskToCall(CallInst *CI, Function *Callee, Value *CM);
  Instruction *loadExecutionMask(Instruction *InsertBefore, unsigned Width);
  AllocaInst *getExecutionMask(Function *F, unsigned Width);
  AllocaInst *getReenableMask(BasicBlock *BB, unsigned Width = 0);
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
  for (unsigned Width = 2; Width <= 32; Width <<= 1) {
    auto VT = VectorType::get(Type::getInt1Ty(M.getContext()), Width);
    Function *SimdCFAny = Intrinsic::getDeclaration(
        &M, Intrinsic::genx_simdcf_any, VT);
    if (!SimdCFAny->use_empty()) {
      HasSimdCF = true;
      break;
    }
  }
  if (!HasSimdCF)
    return false;
  // Derive an order to process functions such that a function is visited
  // after anything that calls it.
  std::vector<Function *> VisitOrder;
  calculateVisitOrder(&M, &VisitOrder);
  // Process functions in that order.
  for (auto i = VisitOrder.begin(), e = VisitOrder.end(); i != e; ++i)
    processFunction(*i, 0);
  return true;
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
 *
 * Because this pass runs so early, we can rely on the basic blocks being
 * in the same order as the source code, and we can just scan linearly
 * through them to find structured CM SIMD control flow.
 */
void CMSimdCFLowering::processFunction(Function *F, unsigned CMWidth)
{
  if (!CMWidth) {
    // Check if this is a function that was called in predicated code, so needs
    // to be replaced by its new version with an extra call mask arg.
    auto PSIterator = PredicatedSubroutines.find(F);
    if (PSIterator != PredicatedSubroutines.end()) {
      Function *NewF = PSIterator->second;
      // Process the new function, passing the simd width, which we get from the
      // vector width of the final arg, which is the call mask arg added on when
      // this new function was created.
      processFunction(NewF, cast<VectorType>(
            NewF->getArgumentList().back().getType())->getNumElements());
      // Change remaining call sites of the old function to the new function.
      SmallVector<CallInst *, 4> CallSites;
      for (auto ui = F->use_begin(), ue = F->use_end(); ui != ue; ++ui)
        CallSites.push_back(cast<CallInst>(ui->getUser()));
      unsigned NumArgs = NewF->getArgumentList().size();
      Type *CMTy = NewF->getArgumentList().back().getType();
      for (auto i = CallSites.begin(), e = CallSites.end(); i != e; ++i) {
        CallInst *CI = *i;
        if (CI->getNumArgOperands() != NumArgs) {
          // CI is not predicated. It needs an extra call mask arg adding.
          assert(CI->getNumArgOperands() + 1 == NumArgs);
          addCallMaskToCall(CI, NewF, Constant::getAllOnesValue(CMTy));
        }
      }
      // Remove the old function.
      assert(F->use_empty());
      F->eraseFromParent();
      return;
    }
  }

  DEBUG(F->print(dbgs()));
  Region *Root = Region::createRegionTree(F, CMWidth);
  if (!Root->hasError()) {
    DEBUG(
      dbgs() << "CMSimdCFLowering: simd region tree:\n";
      Root->print(dbgs())
    );
    // Add predication as required.
    DEBUG(dbgs() << "CMSimdCFLowering: adding predication " << F->getName() << "\n");
    EMs.resize(6, nullptr); // space for EMs up to 2^(6-1) in size.
    if (CMWidth) {
      // This is a subroutine with predicated calls. First create the alloca for
      // the EM, and initialize it from the CM arg. Do not insert the store into
      // code yet so it does not itself get predicated.
      auto EM = getExecutionMask(F, CMWidth);
      auto Store = new StoreInst(&F->getArgumentList().back(), EM);
      // Add predication to the whole function.
      for (auto i = F->begin(), e = F->end(); i != e; ++i)
        predicateBlock(&*i, Root);
      // Now insert the store just after the alloca of the execution mask.
      Store->insertAfter(EM);
    } else {
      // Add predication to the whole range of each top-level simd CF construct.
      for (auto i = Root->begin(), e = Root->end(); i != e; ++i) {
        Region *R = *i;
        auto BB = R->getEntry();
        if (R->getKind() == Region::IF)
          BB = BB->getNextNode();
        for (auto BBE = R->getExit(); BB != BBE; BB = BB->getNextNode())
          predicateBlock(BB, R);
      }
    }
    // Lower the simd CF constructs themselves.
    DEBUG(dbgs() << "CMSimdCFLowering: lowering CF " << F->getName() << "\n");
    for (auto pi = Root->postorder_begin(), pe = Root->postorder_end();
        pi != pe; ++pi) {
      Region *R = &*pi;
      processRegion(R);
    }
    addJoins();
  }
  delete Root;
  ReenableMasks.clear();
  EMs.clear();
}

/***********************************************************************
 * processRegion : process one simd CF region
 */
void CMSimdCFLowering::processRegion(Region *R)
{
  switch (R->getKind()) {
    case Region::IF:
      lowerIf(R);
      break;
    case Region::DO:
      lowerDo(R);
      break;
  }
}

/***********************************************************************
 * lowerIf : lower the simd "if" on the top of the stack
 *
 * At the conditional branch, we have already checked that the true successor
 * is the following block and the false successor is the else/endif.
 */
void CMSimdCFLowering::lowerIf(Region *R)
{
  BasicBlock *If = R->getEntry();
  BasicBlock *Endif = R->getExit();
  BasicBlock *Else = R->getElse();
  auto OldAny = isSimdCFAny(cast<BranchInst>(
        If->getTerminator())->getCondition());
  auto Cond = OldAny->getOperand(0);
  // Change the if code and erase the old simdcf.any.
  BasicBlock *Target = Else ? Else : Endif;
  addBranch(Cond, /*BranchOn=*/false, /*Target=*/Target, /*Join=*/Target, If);
  if (OldAny->use_empty())
    OldAny->eraseFromParent();
  if (Else) {
    // Change the branch at the end of the then leg. This is a simd branch
    // always to Endif with the next join at Else, which means that it disables
    // all channels for re-enabling at Endif, and then unconditionally branches
    // to Else.
    addBranch(Constant::getNullValue(Cond->getType()), /*BranchOn*/false,
        /*Target=*/Endif, /*Join=*/Else, /*BB=*/Else->getPrevNode());
    // Add a join point at the start of Else, skipping to Endif if all channels
    // disabled. We can guarantee EM==0 on reaching the else join point, making
    // the code a little better.
    PendingJoins.push_back(PendingJoin(Else, Endif, /*IsEMZero=*/true));
  }
  // Add a join point at Endif, skipping to the next outer control flow end.
  PendingJoins.push_back(PendingJoin(Endif, getJoinPoint(R->getParent())));
}

/***********************************************************************
 * lowerDo : lower the simd do..while on the top of the stack
 *
 * At the conditional branch, we have already checked that the true successor
 * is the backedge and the false successor is the following block.
 */
void CMSimdCFLowering::lowerDo(Region *R)
{
  BasicBlock *Do = R->getEntry();
  BasicBlock *Exit = R->getExit();
  BasicBlock *While = Exit->getPrevNode();
  auto OldAny = isSimdCFAny(cast<BranchInst>(
        While->getTerminator())->getCondition());
  auto Cond = OldAny->getOperand(0);
  // Change the while code and erase the old simdcf.any.
  // Passing Join=nullptr to addBranch makes it generate a simd backward branch.
  addBranch(Cond, /*BranchOn=*/true, /*Target=*/Do, /*Join=*/nullptr, While);
  if (OldAny->use_empty())
    OldAny->eraseFromParent();
  // Add a simd branch for each break/continue. We need to do it here, rather
  // than when we encounter the break/continue in the post-order depth first
  // traversal in processFunction(), because we need to do if after any
  // enclosing if..endif is lowered.
  bool GotContinue = false;
  for (auto i = R->postorder_begin(), e = R->postorder_end(); i != e; ++i) {
    Region *R2 = &*i;
    switch (R2->getKind()) {
      default:
        continue;
      case Region::CONTINUE:
        GotContinue = true;
        break;
      case Region::BREAK:
        break;
    }
    // We want a branch !never to the target, so all currently enabled
    // channels get disabled until the target (just before or after the
    // while).
    addBranch(Constant::getNullValue(Cond->getType()), /*BranchOn=*/false,
        /*Target=*/R2->getEntry()->getTerminator()->getSuccessor(0),
        getJoinPoint(R2->getParent()), R2->getEntry());
  }
  if (GotContinue) {
    // If we had any continues, the while block needs a join point.
    // Ensure it has an RM for addJoins to find.
    getReenableMask(While, Cond->getType()->getVectorNumElements());
    PendingJoins.push_back(PendingJoin(While, Exit));
  }
  // Add a join point after the loop, skipping to the next outer control
  // flow end.
  // We can guarantee EM==0 on reaching the join point, making the code a
  // little better.
  PendingJoins.push_back(PendingJoin(Exit, getJoinPoint(R->getParent()),
        /*IsEMZero=*/true));
}

/***********************************************************************
 * getJoinPoint : get the join point for code in the given region
 *
 * Return:  nullptr if not in simd CF
 *          the else block if in the then leg of an if with an else
 *          the exit block if R is otherwise in an if
 *          the while block if R is a do..while with continues
 *          the exit block if R is a do..while without continues
 */
BasicBlock *CMSimdCFLowering::getJoinPoint(Region *R)
{
  for (;;) {
    switch (R->getKind()) {
      case Region::BREAK:
      case Region::CONTINUE:
        R = R->getParent();
        continue;
      case Region::ROOT:
        return nullptr;
      case Region::ELSE:
        return R->getParent()->getExit();
      case Region::IF: {
          auto BB = R->getElse();
          if (!BB)
            BB = R->getExit();
          return BB;
        }
      case Region::DO:
        if (R->getNumContinues())
          return R->getExit()->getPrevNode();
        return R->getExit();
      default:
        assert(0);
        return nullptr;
    }
  }
}

/***********************************************************************
 * addJoins : add pending simd join points
 */
void CMSimdCFLowering::addJoins()
{
  for (auto i = PendingJoins.begin(), e = PendingJoins.end(); i != e; ++i) {
    BasicBlock *BB = i->BB;
    BasicBlock *JIP = i->Join;
    bool IsEMZero = i->IsEMZero;
    assert(!isa<PHINode>(&BB->front()));
    auto RM = getReenableMask(BB);
    // Get execution mask of the same size as the re-enable mask for this
    // join point.
    auto EM = getExecutionMask(BB->getParent(),
        RM->getType()->getPointerElementType()->getPrimitiveSizeInBits());
    if (!JIP) {
      // Handle the case that this is the end of the outermost simd CF.
      // Just set EM to all ones and clear the RM.
      auto EMTy = EM->getType()->getPointerElementType();
      auto InsertBefore = &BB->front();
      new StoreInst(Constant::getAllOnesValue(EMTy), EM, InsertBefore);
      new StoreInst(Constant::getNullValue(EMTy), getReenableMask(BB), InsertBefore);
    } else {
      // Split the block such that the instructions are all after the split.
      BB->splitBasicBlock(BB->begin(), BB->getName() + ".simdcfjoin");
      auto InsertBefore = &BB->front();
      // Re-enable channels in EM. (If the PendingJoin has IsEMZero true,
      // we know that EM is zero on reaching the join point, so we don't
      // need an Or instruction.
      Instruction *LoadedEM = nullptr;
      auto LoadedRM = new LoadInst(RM, RM->getName(), InsertBefore);
      if (IsEMZero)
        LoadedEM = LoadedRM;
      else {
        LoadedEM = new LoadInst(EM, EM->getName(), InsertBefore);
        LoadedEM = BinaryOperator::Create(Instruction::Or, LoadedEM,
            LoadedRM, LoadedEM->getName(), InsertBefore);
      }
      new StoreInst(LoadedEM, EM, InsertBefore);
      new StoreInst(Constant::getNullValue(LoadedEM->getType()), RM, InsertBefore);
      // Add an any(EM).
      auto Any = createAny(LoadedEM, LoadedEM->getName() + ".any",
          InsertBefore, DebugLoc());
      // Change BB's branch to BB2 to a conditional branch using Any.
      auto OldTerm = BB->getTerminator();
      BranchInst::Create(BB->getNextNode(), JIP, Any, InsertBefore);
      OldTerm->eraseFromParent();
    }
  }
  PendingJoins.clear();
}

/***********************************************************************
 * addBranch : add a simd branch
 *
 * Enter:   Cond = vector condition to branch on
 *          BranchOn = true to branch when cond true, or false when false
 *          Target = target of branch (falls through if branch not taken)
 *          Join = join point to branch to if all channels off, nullptr for
 *                backward branch which joins at the immediately following
 *                block
 *          BB = block to put branch at the end of, replacing whatever
 *              branch it had there before
 */

void CMSimdCFLowering::addBranch(Value *Cond, bool BranchOn,
    BasicBlock *Target, BasicBlock *Join, BasicBlock *BB)
{
  Instruction *InsertBefore = BB->getTerminator();
  DebugLoc DL = InsertBefore->getDebugLoc();
  unsigned Width = Cond->getType()->getPrimitiveSizeInBits();
  auto EM = getExecutionMask(BB->getParent(), Width);
  auto RM = getReenableMask(Join ? Target : BB->getNextNode(), Width);
  auto C = dyn_cast<Constant>(Cond);
  if (C && C->isNullValue()) {
    // Handle the constant case first.
    if (!BranchOn) {
      // If BranchOn is false, this is "branch always", which does this:
      //  RM |= EM
      //  EM = 0
      //  unconditional branch to Join
      Instruction *LoadedEM = new LoadInst(EM, EM->getName(), InsertBefore);
      LoadedEM->setDebugLoc(DL);
      Instruction *LoadedRM = new LoadInst(EM, RM->getName(), InsertBefore);
      LoadedRM->setDebugLoc(DL);
      LoadedRM = BinaryOperator::Create(Instruction::Or, LoadedRM,
          LoadedEM, RM->getName(), InsertBefore);
      LoadedRM->setDebugLoc(DL);
      auto Store = new StoreInst(LoadedRM, RM, InsertBefore);
      Store->setDebugLoc(DL);
      Store = new StoreInst(Constant::getNullValue(LoadedEM->getType()),
          EM, InsertBefore);
      Store->setDebugLoc(DL);
      // Change the branch to branch unconditionally to Join.
      auto OldTerm = BB->getTerminator();
      auto NewBr = BranchInst::Create(Join, InsertBefore);
      NewBr->setDebugLoc(DL);
      OldTerm->eraseFromParent();
      return;
    }
    // If BranchOn is true, this is "branch never", which is pointless
    // so we don't use it.
    assert(0);
    return;
  }
  // Insert code:
  //    RM |= EM & ~Cond
  //    EM &= Cond
  // assuming BranchOn is false; if it is true, then Cond and ~Cond are
  // swapped. This sets bits in RM for channels that want to take the branch,
  // and leaves bits set in EM for channels that do not want to take the
  // branch.
  // For a backward branch, the sense of BranchOn is the other way up here.
  auto NotCondInst = BinaryOperator::Create(Instruction::Xor, Cond,
      Constant::getAllOnesValue(Cond->getType()), Cond->getName() + ".not",
      InsertBefore);
  NotCondInst->setDebugLoc(DL);
  Value *NotCond = NotCondInst;
  if (BranchOn)
    std::swap(Cond, NotCond);
  if (!Join)
    std::swap(Cond, NotCond);
  Instruction *LoadedEM = new LoadInst(EM, EM->getName(), InsertBefore);
  LoadedEM->setDebugLoc(DL);
  auto DisablingBits = BinaryOperator::Create(Instruction::And, LoadedEM,
      NotCond, "disablechannels.simdcf", InsertBefore);
  DisablingBits->setDebugLoc(DL);
  Instruction *LoadedRM = new LoadInst(RM, RM->getName(), InsertBefore);
  LoadedRM->setDebugLoc(DL);
  LoadedRM = BinaryOperator::Create(Instruction::Or, LoadedRM,
      DisablingBits, RM->getName(), InsertBefore);
  LoadedRM->setDebugLoc(DL);
  auto Store = new StoreInst(LoadedRM, RM, InsertBefore);
  Store->setDebugLoc(DL);
  LoadedEM = BinaryOperator::Create(Instruction::And, LoadedEM, Cond,
      EM->getName(), InsertBefore);
  LoadedEM->setDebugLoc(DL);
  Store = new StoreInst(LoadedEM, EM, InsertBefore);
  Store->setDebugLoc(DL);
  // Modify the branch.
  auto OldTerm = BB->getTerminator();
  auto Any = createAny(LoadedEM, LoadedEM->getName() + ".any",
      InsertBefore, DL);
  BranchInst *NewBr = nullptr;
  if (Join) {
    // Forward branch. Change the branch to
    //  branch !any(EM), Join
    NewBr = BranchInst::Create(BB->getNextNode(), Join, Any, InsertBefore);
  } else {
    // Backward branch. Change the branch to
    //  branch any(EM), Target
    NewBr = BranchInst::Create(Target, BB->getNextNode(), Any, InsertBefore);
  }
  NewBr->setDebugLoc(DL);
  OldTerm->eraseFromParent();
}

/***********************************************************************
 * createAny : create an "any" instruction
 */
CallInst *CMSimdCFLowering::createAny(Value *In, const Twine &Name,
    Instruction *InsertBefore, DebugLoc DL)
{
  Module *M = InsertBefore->getParent()->getParent()->getParent();
  auto Decl = Intrinsic::getDeclaration(M, Intrinsic::genx_any, In->getType());
  auto CI = CallInst::Create(Decl, In, Name, InsertBefore);
  CI->setDebugLoc(DL);
  return CI;
}

/***********************************************************************
 * predicateBlock : add predication to a basic block
 *
 * Enter:   BB = basic block
 *          R = outermost simd CF region containing it
 */
void CMSimdCFLowering::predicateBlock(BasicBlock *BB, Region *R)
{
  for (auto bi = BB->begin(), be = BB->end(); bi != be; ) {
    Instruction *Inst = &*bi;
    ++bi; // Increment here in case Inst is removed
    predicateInst(Inst, R);
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
  Function *Decl = Intrinsic::getDeclaration(M, Intrinsic::genx_wrregion,
      OverloadedTypes);
  auto WrRegion = CallInst::Create(Decl, Args, Name, InsertBefore);
  WrRegion->setDebugLoc(InsertBefore->getDebugLoc());
  return WrRegion;
}

/***********************************************************************
 * predicateInst : add predication to an Instruction if necessary
 *
 * Enter:   Inst = the instruction
 *          R = outermost simd CF region containing it
 */
void CMSimdCFLowering::predicateInst(Instruction *Inst, Region *R)
{
  if (auto CI = dyn_cast<CallInst>(Inst)) {
    unsigned IntrinsicID = Intrinsic::not_intrinsic;
    auto Callee = CI->getCalledFunction();
    if (Callee)
      IntrinsicID = Callee->getIntrinsicID();
    switch (IntrinsicID) {
      case Intrinsic::genx_rdregion:
      case Intrinsic::genx_wrregion:
      case Intrinsic::genx_simdcf_any:
        return; // ignore these intrinsics
      default:
        // An IntrNoMem intrinsic is an ALU intrinsic and can be ignored.
        if (Callee->doesNotAccessMemory())
          return;
        assert(0 && "predicating non-ALU intrinsic not implemented yet");
        return;
      case Intrinsic::not_intrinsic:
        break;
    }
    // Call to real subroutine.
    predicateCall(CI, R);
    return;
  }
  if (auto SI = dyn_cast<StoreInst>(Inst)) {
    predicateStore(SI, R);
    return;
  }
}

/***********************************************************************
 * predicateStore : add predication to a StoreInst
 *
 * Enter:   Inst = the instruction
 *          R = outermost simd CF region containing it
 *
 * This code avoids using the utility functions and classes for the wrregion
 * intrinsic that are in the GenX backend because this pass is not part of the
 * GenX backend.
 */
void CMSimdCFLowering::predicateStore(StoreInst *SI, Region *R)
{
  auto V = SI->getValueOperand();
  auto StoreVT = dyn_cast<VectorType>(V->getType());
  if (!StoreVT)
    return; // Scalar store not predicated
  // See if the value to store is a wrregion (possibly predicated) of the
  // right width. If so, we predicate that instead. This also handles
  // the case that the value to store is wider than the simd CF width,
  // but there is a wrregion with the right width.
  // Also allow for a chain of multiple wrregions, as clang can generate
  // two, one for the columns and one for the rows.
  CallInst *WrRegionToPredicate = nullptr;
  Use *U = &SI->getOperandUse(0);
  for (;;) {
    auto WrRegion = dyn_cast<CallInst>(V);
    if (!WrRegion)
      break;
    auto Callee = WrRegion->getCalledFunction();
    if (!Callee || Callee->getIntrinsicID() != Intrinsic::genx_wrregion)
      break;
    // We have a wrregion. Check its input width.
    unsigned Width = 0;
    Value *Input = WrRegion->getArgOperand(ValueToWriteOperandNum);
    if (auto VT = dyn_cast<VectorType>(Input->getType()))
      Width = VT->getNumElements();
    if (Width == R->getSimdWidth()) {
      // This wrregion has the right width input. We could predicate it.
      if (WrRegionToPredicate)
        U = &WrRegionToPredicate->getOperandUse(ValueToWriteOperandNum);
      WrRegionToPredicate = WrRegion;
      V = WrRegionToPredicate->getArgOperand(ValueToWriteOperandNum);
      // See if it is already predicated, other than by an all true constant.
      Value *Pred = WrRegion->getArgOperand(PredicateOperandNum);
      if (auto C = dyn_cast<Constant>(Pred))
        if (C->isAllOnesValue())
          Pred = nullptr;
      if (Pred) {
        // Yes it is predicated. Stop here and further predicate it.
        break;
      }
    } else if (Width < R->getSimdWidth()) {
      // Too narrow. Predicate the last correctly sized wrregion or the store.
      break;
    }
  }
  if (WrRegionToPredicate) {
    // We found a wrregion to predicate. Replace it with a predicated one.
    *U = predicateWrRegion(WrRegionToPredicate, R);
    if (WrRegionToPredicate->use_empty())
      WrRegionToPredicate->eraseFromParent();
    return;
  }
  // Predicate the store by creating a select.
  auto Load = new LoadInst(SI->getPointerOperand(),
      SI->getPointerOperand()->getName() + ".simdcfpred.load", SI);
  Load->setDebugLoc(SI->getDebugLoc());
  auto EM = loadExecutionMask(SI, R->getSimdWidth());
  auto Select = SelectInst::Create(EM, V, Load,
      V->getName() + ".simdcfpred", SI);
  SI->setOperand(0, Select);
}

/***********************************************************************
 * predicateWrRegion : create a predicated version of a wrregion
 *
 * Enter:   WrR = the wrregion, whose value width must be equal to the
 *                simd CF width
 *          R = some simd CF region enclosing this wrregion
 *
 * Return:  the new predicated wrregion
 *
 * If the wrregion is already predicated, the new one has a predicated that
 * is an "and" of the original predicate and our EM.
 */
CallInst *CMSimdCFLowering::predicateWrRegion(CallInst *WrR, Region *R)
{
  // First gather the args of the original wrregion.
  SmallVector<Value *, 8> Args;
  for (unsigned i = 0, e = WrR->getNumArgOperands(); i != e; ++i)
    Args.push_back(WrR->getArgOperand(i));
  // Modify the predicate in Args.
  Value *Pred = Args[PredicateOperandNum];
  if (auto C = dyn_cast<Constant>(Pred))
    if (C->isAllOnesValue())
      Pred = nullptr;
  auto EM = loadExecutionMask(WrR, R->getSimdWidth());
  if (!Pred)
    Pred = EM;
  else {
    auto And = BinaryOperator::Create(Instruction::And, EM, Pred,
        Pred->getName() + ".and." + EM->getName(), WrR);
    And->setDebugLoc(WrR->getDebugLoc());
    Pred = And;
  }
  Args[PredicateOperandNum] = Pred;
  return createWrRegion(Args, WrR->getName(), WrR);
}

/***********************************************************************
 * predicateCall : predicate a real call to a subroutine
 */
void CMSimdCFLowering::predicateCall(CallInst *CI, Region *R)
{
  Function *F = CI->getCalledFunction();
  assert(F);
  auto PSEntry = &PredicatedSubroutines[F];
  Function *NewF = *PSEntry;
  if (!NewF) {
    // This is the first time that a call to Callee has been predicated.
    // Create a clone of the function, with an added arg for the call mask.
    // First get the new function type and create the new function, copying
    // attributes across.
    SmallVector<AttributeSet, 8> AttributesVec;
    const AttributeSet &PAL = F->getAttributes();
    auto FT = cast<FunctionType>(F->getType()->getPointerElementType());
    SmallVector<Type *, 4> ParamTypes;
    unsigned ArgIndex = 0;
    for (auto i = FT->param_begin(), e = FT->param_end();
        i != e; ++i, ++ArgIndex) {
      ParamTypes.push_back(*i);
      AttributeSet attrs = PAL.getParamAttributes(ArgIndex);
      if (attrs.hasAttributes(ArgIndex)) {
        AttrBuilder B(attrs, ArgIndex);
        AttributesVec.push_back(AttributeSet::get(F->getContext(), ArgIndex, B));
      }
    }
    auto CMTy = VectorType::get(Type::getInt1Ty(F->getContext()),
        R->getSimdWidth());
    ParamTypes.push_back(CMTy); // extra arg for call mask
    auto NewFTy = FunctionType::get(FT->getReturnType(), ParamTypes, false);
    NewF = Function::Create(NewFTy, F->getLinkage(), "", F->getParent());
    NewF->takeName(F);
    NewF->setAttributes(AttributeSet::get(NewF->getContext(), AttributesVec));
    AttributesVec.clear();
    // Since we have now created the new function, splice the body of the old
    // function right into the new function.
    NewF->getBasicBlockList().splice(NewF->begin(), F->getBasicBlockList());
    // Loop over the argument list, transferring uses of the old arguments over to
    // the new arguments, also transferring over the names as well.
    for (Function::arg_iterator I = F->arg_begin(), E = F->arg_end(),
                                I2 = NewF->arg_begin();
         I != E; ++I, ++I2) {
      I->replaceAllUsesWith(I2);
      I2->takeName(I);
    }
    // Store the new Function in the PredicatedSubroutines map.
    *PSEntry = NewF;
  } else {
    // Get the replacement function and check that the simd width agrees.
    if (NewF->getArgumentList().back().getType()->getPrimitiveSizeInBits()
        != R->getSimdWidth()) {
      R->reportError("mismatched SIMD width of called subroutine", CI);
      return;
    }
  }
  // Construct a replacement call with an extra arg: the execution mask.
  addCallMaskToCall(CI, NewF, loadExecutionMask(CI, R->getSimdWidth()));
}

/***********************************************************************
 * addCallMaskToCall : replace a CallInst with another one with the extra
 *    call mask arg
 *
 * Enter:   CI = the CallInst
 *          Callee = new called function
 *          CM = call mask to use as argument
 *
 * Return:  the new CallInst
 */
CallInst *CMSimdCFLowering::addCallMaskToCall(CallInst *CI,
    Function *Callee, Value *CM)
{
  SmallVector<Value *, 8> Args;
  for (unsigned i = 0, e = CI->getNumArgOperands(); i != e; ++i)
    Args.push_back(CI->getArgOperand(i));
  Args.push_back(CM);
  auto NewCall = CallInst::Create(Callee, Args, "", CI);
  NewCall->takeName(CI);
  NewCall->setDebugLoc(CI->getDebugLoc());
  CI->replaceAllUsesWith(NewCall);
  CI->eraseFromParent();
  return NewCall;
}

/***********************************************************************
 * loadExecutionMask : load the vXi1 EM (execution mask) for the current
 *      SIMD width
 *
 * Enter:   InsertBefore = insert the load before this instruction
 *          Width = width of EM to get
 */
Instruction *CMSimdCFLowering::loadExecutionMask(Instruction *InsertBefore,
    unsigned Width)
{
  auto EM = getExecutionMask(InsertBefore->getParent()->getParent(), Width);
  auto Load = new LoadInst(EM, EM->getName(), InsertBefore);
  Load->setDebugLoc(InsertBefore->getDebugLoc());
  return Load;
}

/***********************************************************************
 * getExecutionMask : get the vXi1 EM (execution mask) for the current
 *      SIMD width
 *
 * Enter:   F = Function
 *          Width = width of EM to get
 *
 * This returns the alloca instruction for the variable, creating it if
 * necessary at the start of the function.
 */
AllocaInst *CMSimdCFLowering::getExecutionMask(Function *F, unsigned Width)
{
  unsigned LogWidth = 31 - countLeadingZeros(Width, ZB_Undefined);
  if (!EMs[LogWidth]) {
    // Need to create a new one and initialize it to all 1s at the start
    // of the function.
    auto EMTy = VectorType::get(Type::getInt1Ty(F->getContext()), Width);
    auto InsertBefore = F->front().getFirstNonPHI();
    EMs[LogWidth] = new AllocaInst(EMTy,
        "EM" + Twine(Width) + ".simdcf", InsertBefore);
    new StoreInst(Constant::getAllOnesValue(EMTy), EMs[LogWidth], InsertBefore);
  }
  return EMs[LogWidth];
}

/***********************************************************************
 * getReenableMask : get the vXi1 RM (re-enable mask) for the join point
 *      at the start of the specified block
 *
 * Enter:   BB = block to get RM for
 *          Width = width of mask, only needed if we need to create it
 *
 * This returns the alloca instruction for the variable, creating it if
 * necessary at the start of the function.
 */
AllocaInst *CMSimdCFLowering::getReenableMask(BasicBlock *BB, unsigned Width)
{
  auto Entry = &ReenableMasks[BB];
  if (!*Entry) {
    // Need to create a new one and initialize it to all 0s at the start
    // of the function.
    assert(Width);
    auto RMTy = VectorType::get(Type::getInt1Ty(BB->getContext()), Width);
    auto InsertBefore = BB->getParent()->front().getFirstNonPHI();
    *Entry = new AllocaInst(RMTy, BB->getName() + ".RM.simdcf", InsertBefore);
    new StoreInst(Constant::getNullValue(RMTy), *Entry, InsertBefore);
  }
  return *Entry;
}

/***********************************************************************
 * getSimdCondition : given a branch condition, if it is llvm.genx.simdcf.any,
 *    get the vector condition
 */
Value *CMSimdCFLowering::getSimdCondition(Value *Cond)
{
  if (auto CI = isSimdCFAny(Cond))
    return CI->getOperand(0);
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
 * DiagnosticInfoSimdCF initializer from Instruction
 *
 * If the Instruction has a DebugLoc, then that is used for the error
 * location.
 * Otherwise, if the Instruction is an llvm.genx.simdcf.any, then its
 * filename and line args are used for the error location.
 * Otherwise, if the Instruction is a branch whose condition is an
 * llvm.genx.simdcf.any, then it works as above.
 * Otherwise, the location is unknown.
 */
DiagnosticInfoSimdCF::DiagnosticInfoSimdCF(Instruction *Inst,
    const Twine &Desc, DiagnosticSeverity Severity)
    : DiagnosticInfo(getKindID(), Severity),
      Description(Desc), Line(0), Col(0)
{
  auto DL = Inst->getDebugLoc();
  if (!DL.isUnknown()) {
    Filename = DIScope(DL.getScope(Inst->getContext())).getFilename();
    Line = DL.getLine();
    Col = DL.getCol();
    return;
  }
  // See if Inst is a conditional branch whose condition might be
  // llvm.genx.simdcf.any.
  if (auto Br = dyn_cast<BranchInst>(Inst))
    if (Br->isConditional())
      Inst = dyn_cast<Instruction>(Br->getCondition());
  // See if Inst is a call to llvm.genx.simdcf.any where operand 1 is a
  // constant GEP of a global variable of type char[N] with an initializer
  // that is a 0 terminated string, giving us the filename.
  if (auto CI = CMSimdCFLowering::isSimdCFAny(Inst)) {
    if (auto CE = dyn_cast<ConstantExpr>(CI->getOperand(1))) {
      if (CE->getOpcode() == Instruction::GetElementPtr) {
        if (auto Glob = dyn_cast<GlobalVariable>(CE->getOperand(0))) {
          if (auto Init = dyn_cast_or_null<ConstantDataArray>(Glob->getInitializer())) {
            if (Init->isCString())
              Filename = Init->getAsCString();
          }
        }
      }
    }
    // See if operand 2 is a constant int giving the line number.
    if (auto C = dyn_cast<ConstantInt>(CI->getOperand(2)))
      Line = C->getZExtValue();
  }
}

/***********************************************************************
 * DiagnosticInfoSimdCF::print : print the error/warning message
 */
void DiagnosticInfoSimdCF::print(DiagnosticPrinter &DP) const
{
  std::string Loc(
        (Twine(!Filename.empty() ? Filename : "<unknown>")
        + ":" + Twine(Line)
        + (!Col ? Twine() : Twine(":") + Twine(Col))
        + ": ")
      .str());
  DP << Loc << Description;
}

/***********************************************************************
 * Region destructor. This frees all its descendants.
 */
Region::~Region()
{
  if (!Children.size())
    return;
  for (auto pi = this->postorder_begin(); ; ++pi) {
    Region *R = &*pi;
    // Children have now all been deleted. Clear the Children vector
    // so they don't get deleted again.
    R->Children.clear();
    if (R == this)
      break; // Don't delete the root as we are in its destructor.
    delete R;
  }
}

/***********************************************************************
 * getElse : get the else block in an if..else..endif region
 *
 * Return:  the else block, nullptr if this is not an if..else..endif region
 *
 * An if that has an else has a final child whose kind is ELSE.
 */
BasicBlock *Region::getElse() const
{
  if (getKind() != IF)
    return nullptr;
  if (!size())
    return nullptr;
  Region *R = Children[size() - 1];
  if (R->getKind() != ELSE)
    return nullptr;
  return R->Entry;
}

/***********************************************************************
 * getNumPredecessors : count the predecessors of a basic block
 */
static unsigned getNumPredecessors(BasicBlock *BB)
{
  unsigned Count = 0;
  for (auto ui = BB->use_begin(), ue = BB->use_end(); ui != ue; ++ui)
    ++Count;
  return Count;
}

/***********************************************************************
 * createRegionTree : create a simd CF region tree for a function
 *
 * Enter:   F = the Function
 *          CMWidth = 0 normally, the call mask width if this function is
 *                called from predicated sites and so needs completely
 *                predicating
 *
 * Return:  the root region
 *
 * Because this pass runs so early, we can rely on the basic blocks being
 * in the same order as the source code, and we can just scan linearly
 * through them to find structured CM SIMD control flow.
 *
 * Setting CMWidth here means that we get an error if there is simd CF
 * with a different width.
 */
Region *Region::createRegionTree(Function *F, unsigned CMWidth)
{
  unsigned IllegalCount = 0;
  Region *Root = new Region(ROOT, nullptr, nullptr, nullptr);
  Root->SimdWidth = CMWidth;
  Region *R = Root;
  for (auto fi = F->begin(), fe = F->end(); fi != fe; ++fi) {
    BasicBlock *BB = &*fi;
    if (BB == R->Exit) {
      if (R->Kind == DO) {
        // This is the exit from a do..while.
        DEBUG(dbgs() << BB->getName() << ": exit from while (do=" << R->Entry->getName() << ")\n");
        if (getNumPredecessors(BB) != 1 + (unsigned)R->NumBreaks) {
          DEBUG(dbgs() << BB->getName() << ": illegal (loop exit has too many predecessors)\n");
          if (!IllegalCount++)
            R->reportIllegal(BB->getFirstNonPHI());
        }
      } else {
        if (R->Kind == ELSE)
          R = R->getParent();
        assert(R->Kind == IF);
        // This is an endif.
        DEBUG(dbgs() << BB->getName() << ": endif\n");
      }
      R = R->getParent();
    } else {
      unsigned NumPred = getNumPredecessors(BB);
      bool Illegal = false;
      if (R->Kind != ROOT && NumPred != 1) {
        Illegal = true;
        // Check for this being the while block in a do..while loop
        // with continues.
        if (R->Kind == DO && (unsigned)R->NumContinues + 1 == NumPred)
          Illegal = false;
      }
      // Check for this being the loop header of a simd do..while.
      if (NumPred == 2) {
        auto ui = BB->use_begin();
        BasicBlock *Pred1 = cast<Instruction>(ui->getUser())->getParent();
        BasicBlock *Pred2 = cast<Instruction>((++ui)->getUser())->getParent();
        BasicBlock *WhileBlock = nullptr;
        if (Pred1 == BB->getPrevNode())
          WhileBlock = Pred2;
        else if (Pred2 == BB->getPrevNode())
          WhileBlock = Pred1;
        if (WhileBlock) {
          if (auto Br = dyn_cast<BranchInst>(WhileBlock->getTerminator())) {
            Value *Predicate = nullptr;
            if (Br->isConditional()
                && (Predicate = CMSimdCFLowering::getSimdCondition(Br->getCondition()))
                && Br->getSuccessor(0) == BB
                && Br->getSuccessor(1) == WhileBlock->getNextNode()) {
              // This is the loop header of a simd do..while.
              R = R->push(DO, BB, WhileBlock->getNextNode());
              DEBUG(dbgs() << BB->getName() << ": do (while=" << WhileBlock->getName() << ")\n");
              Illegal = false;
              R->setSimdWidth(Predicate);
            }
          }
        }
      }
      if (Illegal) {
        DEBUG(dbgs() << BB->getName() << ": illegal (unrecognized join block)\n");
        if (!IllegalCount++)
          R->reportIllegal(BB->getFirstNonPHI());
      }
    }
    // Then process the edges out of the block.
    if (auto Br = dyn_cast<BranchInst>(BB->getTerminator())) {
      if (!Br->isConditional()) {
        // Unconditional branch. Could be else, endif, break, continue,
        // or just a branch into the next block for the start of a do..while.
        auto *Succ = Br->getSuccessor(0);
        if (R->getKind() != ROOT) {
          if (Succ == BB->getNextNode()) {
            // Fall through to next block.
            // Ignore. It is either an endif, or illegally unstructured,
            // and that all gets handled at the top of the next block.
          } else {
            // To check for break or continue, we need to find the innermost
            // break/continue.
            Region *Loop = R;
            while (Loop && Loop->getKind() != DO)
              Loop = Loop->getParent();
            if (Loop && Loop->Exit == Succ->getNextNode()) {
              // This is a continue. (Note that we push a new region into the
              // tree, but we do not make it the current region.)
              DEBUG(dbgs() << BB->getName() << ": continue (to " << Succ->getName() << ")\n");
              R->push(CONTINUE, BB, nullptr);
              ++Loop->NumContinues;
            } else if (Loop && Loop->Exit == Succ) {
              // This is a break. (Note that we push a new region into the
              // tree, but we do not make it the current region.)
              DEBUG(dbgs() << BB->getName() << ": break (to " << Succ->getName() << ")\n");
              R->push(BREAK, BB, nullptr);
              ++Loop->NumBreaks;
            } else if (R->getKind() == IF && BB->getNextNode() == R->Exit) {
              // This is the end of the then leg when there is an else.
              R->Exit = Succ;
              R = R->push(ELSE, BB->getNextNode(), Succ);
              DEBUG(dbgs() << BB->getNextNode()->getName() << ": else (endif=" << Succ->getName() << ")\n");
            } else {
              // Illegal scalar CF inside simd CF.
              DEBUG(dbgs() << BB->getName() << ": illegal (unconditional branch)\n");
              if (!IllegalCount++)
                R->reportIllegal(BB->getTerminator());
            }
          }
        }
      } else if (auto Predicate
            = CMSimdCFLowering::getSimdCondition(Br->getCondition())) {
        // Simd conditional branch. Could be if, while.
        BasicBlock *True = Br->getSuccessor(0);
        BasicBlock *False = Br->getSuccessor(1);
        if (R->Kind != DO || BB->getNextNode() != R->Exit) {
          // This is not the while in a do..while. It must be an if (or illegal).
          if (True != BB->getNextNode()) {
            DEBUG(dbgs() << BB->getName() << ": illegal (conditional branch)\n");
            if (!IllegalCount++)
              R->reportIllegal(BB->getTerminator());
          }
          R = R->push(IF, BB, False);
          DEBUG(dbgs() << BB->getName() << ": if (else/endif=" << False->getName() << ")\n");
          R->setSimdWidth(Predicate);
        }
      } else if (R->getKind() != ROOT) {
        DEBUG(dbgs() << BB->getName() << ": illegal (non vector condition)\n");
        if (!IllegalCount++)
          R->reportIllegal(BB->getTerminator());
      }
    } else if (R->getKind() != ROOT) {
      DEBUG(dbgs() << BB->getName() << ": illegal (not cond/uncond branch)\n");
      if (!IllegalCount++)
        R->reportIllegal(BB->getTerminator());
    }
  }
  if (R->getKind() != ROOT) {
    DEBUG(dbgs() << F->getName() << ": illegal (stack not empty)\n");
    if (!IllegalCount++)
      R->reportIllegal(F->back().getTerminator());
  }
  return Root;
}

/***********************************************************************
 * getRoot : get the root region
 */
Region *Region::getRoot()
{
  Region *Root = this;
  while (Root->Parent)
    Root = Root->Parent;
  return Root;
}

/***********************************************************************
 * reportIllegal : report illegal unstructured control flow
 *
 * We only do this once per function, otherwise we would get loads of
 * spurious messages after the first one.
 */
void Region::reportIllegal(Instruction *Inst)
{
  if (getRoot()->Errored)
    return;
  reportError(
      "illegal unstructured SIMD control flow (did you mix it with scalar control flow?)",
      Inst);
}

/***********************************************************************
 * reportError : report SIMD control flow error
 *
 * Enter:   Text = text of error message
 *          Inst = instruction to report at, or 0
 *
 * If no debug info is available, it reports at the innermost simd branch
 * if possible.
 */
void Region::reportError(const char *Text, Instruction *Inst)
{
  getRoot()->Errored = true;
  if (!Inst || Inst->getDebugLoc().isUnknown()) {
    // If there is no debug info, attempt to pass the innermost simd branch
    // so we can get the source location from that.
    Region *R = this;
    switch (R->getKind()) {
      case ELSE:
        R = R->getParent();
        // fall through...
      case IF:
        Inst = R->Entry->getTerminator();
        break;
      case DO:
        Inst = R->Exit->getPrevNode()->getTerminator();
        break;
    }
  }
  DiagnosticInfoSimdCF Err(Inst, Text);
  Inst->getContext().diagnose(Err);
}

/***********************************************************************
 * setSimdWidth : set the simd with of the simd control flow region
 *
 * The region inherits its simd width from its parent, or 0 if it is
 * an outermost simd CF region. If it is an inner region, this function
 * checks that the inherited and the new widths agree.
 */
void Region::setSimdWidth(Value *Predicate)
{
  unsigned Width = Predicate->getType()->getPrimitiveSizeInBits();
  if (SimdWidth && SimdWidth != Width)
    reportError("mismatched SIMD width in inner SIMD control flow", nullptr);
  else
    SimdWidth = Width;
}

/***********************************************************************
 * debug dump/print for Region
 */
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void Region::dump() const
{
  print(dbgs(), true);
}
#endif

void Region::print(raw_ostream &OS, bool Deep, unsigned Depth) const
{
  const char *Name = "???";
  switch (getKind()) {
    case ROOT: Name = "ROOT"; break;
    case IF: Name = "IF"; break;
    case ELSE: Name = "ELSE"; break;
    case DO: Name = "DO"; break;
    case BREAK: Name = "BREAK"; break;
    case CONTINUE: Name = "CONTINUE"; break;
  }
  OS.indent(2 * Depth) << Name << ": [";
  if (Entry)
    OS << Entry->getName();
  OS << ",";
  if (Exit)
    OS << Exit->getName();
  OS << ")  (width " << SimdWidth << ")\n";
  if (!Deep)
    return;
  for (auto i = begin(), e = end(); i != e; ++i)
    (*i)->print(OS, true, Depth + 1);
}


