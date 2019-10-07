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
// GenXIntrinsic::getIntrinsicID.
//
//===----------------------------------------------------------------------===//

#ifndef GENX_INTRINSIC_INTERFACE_H
#define GENX_INTRINSIC_INTERFACE_H

#include "llvm/IR/Module.h"

#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Intrinsics.h"

namespace llvm {

namespace GenXIntrinsic {
enum ID : unsigned {
  not_genx_intrinsic = Intrinsic::num_intrinsics,
#define GET_INTRINSIC_ENUM_VALUES
#include "llvm/GenXIntrinsics/GenXIntrinsicEnum.inc"
#undef GET_INTRINSIC_ENUM_VALUES
  ,
  num_genx_intrinsics
};

/// Intrinsic::getName(ID) - Return the LLVM name for an intrinsic, such as
/// "llvm.ppc.altivec.lvx".
std::string getName(ID id, ArrayRef<Type *> Tys = None);

/// Intrinsic::getDeclaration(M, ID) - Create or insert an LLVM Function
/// declaration for an intrinsic, and return it.
///
/// The Tys parameter is for intrinsics with overloaded types (e.g., those
/// using iAny, fAny, vAny, or iPTRAny).  For a declaration of an overloaded
/// intrinsic, Tys must provide exactly one type for each overloaded type in
/// the intrinsic.
Function *getDeclaration(Module *M, ID id, ArrayRef<Type *> Tys = None);

AttributeList getAttributes(LLVMContext &C, ID id);

// Override of isIntrinsic method defined in Function.h
inline const char *getGenXIntrinsicPrefix() { return "llvm.genx."; }
inline bool isIntrinsic(const Function *CF) {
  return CF->getName().startswith(getGenXIntrinsicPrefix());
}
ID getIntrinsicID(const Function *F);

ID lookupGenXIntrinsicID(StringRef Name);

} // namespace GenXIntrinsic

} // namespace llvm

#endif
