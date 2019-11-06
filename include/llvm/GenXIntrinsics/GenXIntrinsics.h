//===-- llvm/Instrinsics.h - LLVM Intrinsic Function Handling ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a set of enums which allow processing of intrinsic
// functions.  Values of these enum types are returned by
// GenXIntrinsic::getGenXIntrinsicID.
//
//===----------------------------------------------------------------------===//

#ifndef GENX_INTRINSIC_INTERFACE_H
#define GENX_INTRINSIC_INTERFACE_H

#include "llvm/IR/Module.h"

#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Instructions.h"

namespace llvm {

namespace GenXIntrinsic {
enum ID : unsigned {
  not_genx_intrinsic = Intrinsic::num_intrinsics,
#define GET_INTRINSIC_ENUM_VALUES
#include "llvm/GenXIntrinsics/GenXIntrinsicEnum.inc"
#undef GET_INTRINSIC_ENUM_VALUES
  ,
  num_genx_intrinsics,
  // note that Intrinsic::not_intrinsic means that it is not a LLVM intrinsic
  not_any_intrinsic
};

// todo: delete this
static inline unsigned llvm2any(unsigned id);
static inline Intrinsic::ID any2llvm(unsigned id);

static inline const char *getGenXIntrinsicPrefix() { return "llvm.genx."; }

ID getGenXIntrinsicID(const Function *F);

/// Utility function to get the genx_intrinsic ID if V is a GenXIntrinsic call.
/// V is allowed to be 0.
static inline ID getGenXIntrinsicID(const Value *V) {
  if (V)
    if (const CallInst *CI = dyn_cast<CallInst>(V))
      if (Function *Callee = CI->getCalledFunction())
        return getGenXIntrinsicID(Callee);
  return GenXIntrinsic::not_genx_intrinsic;
}

/// GenXIntrinsic::isGenXIntrinsic(ID) - Is GenX intrinsic
/// NOTE that this is include not_genx_intrinsic
/// BUT DOES NOT include not_any_intrinsic
static inline bool isGenXIntrinsic(unsigned ID) {
  return ID >= not_genx_intrinsic && ID < num_genx_intrinsics;
}

/// GenXIntrinsic::isGenXIntrinsic(CF) - Returns true if
/// the function's name starts with "llvm.genx.".
/// It's possible for this function to return true while getGenXIntrinsicID()
/// returns GenXIntrinsic::not_genx_intrinsic!
static inline bool isGenXIntrinsic(const Function *CF) {
  return CF->getName().startswith(getGenXIntrinsicPrefix());
}

/// GenXIntrinsic::isGenXIntrinsic(V) - Returns true if
/// the function's name starts with "llvm.genx.".
/// It's possible for this function to return true while getGenXIntrinsicID()
/// returns GenXIntrinsic::not_genx_intrinsic!
static inline bool isGenXIntrinsic(const Value *V) {
  if (V)
    if (const CallInst *CI = dyn_cast<CallInst>(V))
      if (Function *Callee = CI->getCalledFunction())
        return isGenXIntrinsic(Callee);
  return false;
}

/// GenXIntrinsic::isGenXNonTrivialIntrinsic(ID) - Is GenX intrinsic,
/// which is not equal to not_genx_intrinsic or not_any_intrinsic
static inline bool isGenXNonTrivialIntrinsic(unsigned ID) {
  return ID > not_genx_intrinsic && ID < num_genx_intrinsics;
}

/// GenXIntrinsic::isGenXNonTrivialIntrinsic(CF) - Returns true if
/// CF is genx intrinsic, not equal to not_any_intrinsic or not_genx_intrinsic
static inline bool isGenXNonTrivialIntrinsic(const Function *CF) {
  return isGenXNonTrivialIntrinsic(getGenXIntrinsicID(CF));
}

/// GenXIntrinsic::isGenXNonTrivialIntrinsic(V) - Returns true if
/// V is genx intrinsic, not equal to not_any_intrinsic or not_genx_intrinsic
static inline bool isGenXNonTrivialIntrinsic(const Value *V) {
  return isGenXNonTrivialIntrinsic(getGenXIntrinsicID(V));
}

/// GenXIntrinsic::getGenXName(ID) - Return the LLVM name for a GenX intrinsic,
/// such as "llvm.genx.lane.id".
std::string getGenXName(ID id, ArrayRef<Type *> Tys = None);

ID lookupGenXIntrinsicID(StringRef Name);

AttributeList getAttributes(LLVMContext &C, ID id);

/// GenXIntrinsic::getGenXType(ID) - Return the function type for an intrinsic.
FunctionType *getGenXType(LLVMContext &Context, GenXIntrinsic::ID id,
                          ArrayRef<Type *> Tys = None);

/// GenXIntrinsic::getGenXDeclaration(M, ID) - Create or insert a GenX LLVM
/// Function declaration for an intrinsic, and return it.
///
/// The Tys parameter is for intrinsics with overloaded types (e.g., those
/// using iAny, fAny, vAny, or iPTRAny).  For a declaration of an overloaded
/// intrinsic, Tys must provide exactly one type for each overloaded type in
/// the intrinsic.
Function *getGenXDeclaration(Module *M, ID id, ArrayRef<Type *> Tys = None);





/// GenXIntrinsic::getAnyIntrinsicID(F) - Return LLVM or GenX intrinsic ID
/// If is not intrinsic returns not_any_intrinsic
/// Note that Function::getIntrinsicID returns ONLY LLVM intrinsics
static inline unsigned getAnyIntrinsicID(const Function *F) {
  if (isGenXNonTrivialIntrinsic(F))
    return getGenXIntrinsicID(F);
  else {
    unsigned IID = F->getIntrinsicID();
    if (IID == Intrinsic::not_intrinsic)
      return GenXIntrinsic::not_any_intrinsic;
    else
      return IID;
  }
}

/// Utility function to get the LLVM or GenX intrinsic ID if V is an intrinsic
/// call.
/// V is allowed to be 0.
static inline unsigned getAnyIntrinsicID(const Value *V) {
  if (V)
    if (const CallInst *CI = dyn_cast<CallInst>(V))
      if (Function *Callee = CI->getCalledFunction())
        return getAnyIntrinsicID(Callee);
  return GenXIntrinsic::not_any_intrinsic;
}

/// GenXIntrinsic::isAnyIntrinsic(ID) - Is any intrinsic
/// including not_any_intrinsic
static inline bool isAnyIntrinsic(unsigned id) {
  assert(id != not_genx_intrinsic && id != Intrinsic::not_intrinsic &&
         "Do not use this method with getGenXIntrinsicID or getIntrinsicID!");
  return id < num_genx_intrinsics || id == not_any_intrinsic;
}

/// GenXIntrinsic::isAnyNonTrivialIntrinsic(id) - Is GenX or LLVM intrinsic,
/// which is not equal to not_any_intrinsic
static inline bool isAnyNonTrivialIntrinsic(unsigned id) {
  assert(id != not_genx_intrinsic && id != Intrinsic::not_intrinsic &&
         "Do not use this method with getGenXIntrinsicID or getIntrinsicID!");
  return id <  num_genx_intrinsics &&
         id != not_any_intrinsic;
}

/// GenXIntrinsic::isAnyNonTrivialIntrinsic(ID) - Is GenX or LLVM intrinsic,
/// which is not equal to not_genx_intrinsic, not_any_intrinsic or not_intrinsic
static inline bool isAnyNonTrivialIntrinsic(const Function *CF) {
  return isAnyNonTrivialIntrinsic(getAnyIntrinsicID(CF));
}

/// Utility function to check if V is LLVM or GenX intrinsic call,
/// which is not not_intrinsic, not_genx_intrinsic or not_any_intrinsic
/// V is allowed to be 0.
static inline bool isAnyNonTrivialIntrinsic(const Value *V) {
  return isAnyNonTrivialIntrinsic(getAnyIntrinsicID(V));
}

/// GenXIntrinsic::getAnyName(ID) - Return the LLVM name for LLVM or GenX
/// intrinsic, such as "llvm.genx.lane.id".
std::string getAnyName(unsigned id, ArrayRef<Type *> Tys = None);

/// GenXIntrinsic::getAnyType(ID) - Return the function type for an intrinsic.
static inline FunctionType *getAnyType(LLVMContext &Context, unsigned id,
                                       ArrayRef<Type *> Tys = None) {
  assert(isAnyNonTrivialIntrinsic(id));
  if (isGenXIntrinsic(id))
    return getGenXType(Context, (ID)id, Tys);
  else
    return Intrinsic::getType(Context, (Intrinsic::ID)id, Tys);
}

/// GenXIntrinsic::getAnyDeclaration(M, ID) - Create or insert a LLVM
/// Function declaration for an intrinsic, and return it.
///
/// The Tys parameter is for intrinsics with overloaded types (e.g., those
/// using iAny, fAny, vAny, or iPTRAny).  For a declaration of an overloaded
/// intrinsic, Tys must provide exactly one type for each overloaded type in
/// the intrinsic.
static inline Function *getAnyDeclaration(Module *M, unsigned id,
                                          ArrayRef<Type *> Tys = None) {
  assert(isAnyNonTrivialIntrinsic(id));
  if (isGenXIntrinsic(id)) {
    return getGenXDeclaration(M, (ID)id, Tys);
  } else {
    return Intrinsic::getDeclaration(M, (Intrinsic::ID)id, Tys);
  }
}



static inline bool isRdRegion(unsigned IntrinID) {
  IntrinID = llvm2any(IntrinID);
  switch (IntrinID) {
  case GenXIntrinsic::genx_rdregioni:
  case GenXIntrinsic::genx_rdregionf:
    return true;
  default:
    return false;
  }
}

static inline bool isRdRegion(const Function *F) {
  return isRdRegion(getGenXIntrinsicID(F));
}

static inline bool isRdRegion(const Value *V) {
  return isRdRegion(getGenXIntrinsicID(V));
}

static inline bool isWrRegion(unsigned IntrinID) {
  IntrinID = llvm2any(IntrinID);
  switch (IntrinID) {
  case GenXIntrinsic::genx_wrregioni:
  case GenXIntrinsic::genx_wrregionf:
  case GenXIntrinsic::genx_wrconstregion:
    return true;
  default:
    return false;
  }
}

static inline bool isWrRegion(const Function *F) {
  return isWrRegion(getGenXIntrinsicID(F));
}

static inline bool isWrRegion(const Value *V) {
  return isWrRegion(getGenXIntrinsicID(V));
}

static inline bool isAbs(unsigned IntrinID) {
    IntrinID = llvm2any(IntrinID);
    if (IntrinID == GenXIntrinsic::genx_absf || IntrinID == GenXIntrinsic::genx_absi)
        return true;
    return false;
}

static inline bool isAbs(const Function *F) {
    return isAbs(getGenXIntrinsicID(F));
}

static inline bool isAbs(const Value *V) {
    return isAbs(getGenXIntrinsicID(V));
}

static inline bool isIntegerSat(unsigned IID) {
    IID = llvm2any(IID);
    switch (IID) {
    case GenXIntrinsic::genx_sstrunc_sat:
    case GenXIntrinsic::genx_sutrunc_sat:
    case GenXIntrinsic::genx_ustrunc_sat:
    case GenXIntrinsic::genx_uutrunc_sat:
        return true;
    default:
        return false;
    }
}

static inline bool isIntegerSat(const Function *F) {
  return isIntegerSat(getGenXIntrinsicID(F));
}

static inline bool isIntegerSat(const Value *V) {
  return isIntegerSat(getGenXIntrinsicID(V));
}

static inline bool isVLoad(unsigned IntrinID) {
  IntrinID = llvm2any(IntrinID);
  return IntrinID == GenXIntrinsic::genx_vload;
}

static inline bool isVLoad(const Function *F) {
  return isVLoad(getGenXIntrinsicID(F));
}

static inline bool isVLoad(const Value *V) {
  return isVLoad(getGenXIntrinsicID(V));
}

static inline bool isVStore(unsigned IntrinID) {
  IntrinID = llvm2any(IntrinID);
  return IntrinID == GenXIntrinsic::genx_vstore;
}

static inline bool isVStore(const Function *F) {
  return isVStore(getGenXIntrinsicID(F));
}

static inline bool isVStore(const Value *V) {
  return isVStore(getGenXIntrinsicID(V));
}

static inline bool isVLoadStore(unsigned IntrinID) {
  return isVLoad(IntrinID) || isVStore(IntrinID);
}

static inline bool isVLoadStore(const Function *F) {
  return isVLoadStore(getGenXIntrinsicID(F));
}

static inline bool isVLoadStore(const Value *V) {
  return isVLoadStore(getGenXIntrinsicID(V));
}

} // namespace GenXIntrinsic

// todo: delete this
namespace GenXIntrinsic {
AttributeList getAttributes(LLVMContext &C, ID id);

/// migration helper method
/// llvm.genx -> genx
/// llvm      -> llvm
/// genx      -> genx
static inline unsigned llvm2any(unsigned ID) {
  if (isGenXIntrinsic(ID) || ID == GenXIntrinsic::not_any_intrinsic)
    return ID;
  if (ID == Intrinsic::not_intrinsic)
    return GenXIntrinsic::not_any_intrinsic;
  auto Name = Intrinsic::getName((Intrinsic::ID)ID, None);
  if (StringRef(Name).startswith(getGenXIntrinsicPrefix()))
    return lookupGenXIntrinsicID(Name);
  return ID;
}

/// migration helper method
/// llvm.genx -> llvm
/// llvm      -> llvm
/// genx      -> llvm
static inline Intrinsic::ID any2llvm(unsigned ID) {
  assert(ID != GenXIntrinsic::not_genx_intrinsic &&
         "Do not use this with getGenX... methods!");
  if (isGenXIntrinsic(ID) || ID == GenXIntrinsic::not_any_intrinsic) {
    if (ID == GenXIntrinsic::not_any_intrinsic)
      return Intrinsic::not_intrinsic;
    auto Name = GenXIntrinsic::getGenXName((GenXIntrinsic::ID)ID);
    return Function::lookupIntrinsicID(Name);
  }
  return (Intrinsic::ID)ID;
}

} // namespace GenXIntrinsic

} // namespace llvm

#endif
