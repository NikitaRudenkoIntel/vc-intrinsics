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

constexpr Intrinsic::ID BeginLLVMGenXID = Intrinsic::genx_3d_load;
constexpr Intrinsic::ID EndLLVMGenXID = static_cast<Intrinsic::ID>(Intrinsic::genx_zzzzend + 1);
constexpr GenXIntrinsic::ID BeginGenXID = GenXIntrinsic::genx_3d_load;
constexpr GenXIntrinsic::ID EndGenXID = static_cast<GenXIntrinsic::ID>(GenXIntrinsic::genx_zzzzend + 1);

static_assert(EndLLVMGenXID - BeginLLVMGenXID == EndGenXID - BeginGenXID, "Mismatch in intrinsic number");

TEST(GenXIntrinsics, IdenticalNames) {
  unsigned LLVMID = BeginLLVMGenXID;
  unsigned GenXID = BeginGenXID;
  while (LLVMID < EndLLVMGenXID) {
    StringRef LLVMName = Intrinsic::getName(static_cast<Intrinsic::ID>(LLVMID), {});
    StringRef GenXName = GenXIntrinsic::getName(static_cast<GenXIntrinsic::ID>(GenXID), {});
    EXPECT_EQ(LLVMName, GenXName);
    ++LLVMID;
    ++GenXID;
  }
}

TEST(GenXIntrinsics, IdenticalAttrs) {
  LLVMContext Ctx;
  unsigned LLVMID = BeginLLVMGenXID;
  unsigned GenXID = BeginGenXID;
  while (LLVMID < EndLLVMGenXID) {
    auto LLVMAttrs = Intrinsic::getAttributes(Ctx, static_cast<Intrinsic::ID>(LLVMID));
    auto GenXAttrs = GenXIntrinsic::getAttributes(Ctx, static_cast<GenXIntrinsic::ID>(GenXID));
    EXPECT_EQ(LLVMAttrs, GenXAttrs);
    ++LLVMID;
    ++GenXID;
  }
}

// Currently returns some fixed types.
Type *generateAnyType(Intrinsic::IITDescriptor::ArgKind AK, LLVMContext &Ctx) {
  using namespace Intrinsic;

  switch (AK) {
  case IITDescriptor::AK_Any:
  case IITDescriptor::AK_AnyInteger:
    return Type::getInt32Ty(Ctx);
  case IITDescriptor::AK_AnyFloat:
    return Type::getDoubleTy(Ctx);
  case IITDescriptor::AK_AnyPointer:
    return Type::getInt32PtrTy(Ctx);
  case IITDescriptor::AK_AnyVector:
    return VectorType::get(Type::getInt32Ty(Ctx), 8);
  }
  llvm_unreachable("All types should be handled");
}

void generateOverloadedTypes(Intrinsic::ID Id, LLVMContext &Ctx, SmallVectorImpl<Type *> &Tys) {
  using namespace Intrinsic;

  SmallVector<IITDescriptor, 8> Table;
  getIntrinsicInfoTableEntries(Id, Table);

  for (unsigned i = 0, e = Table.size(); i != e; ++i) {
    auto Desc = Table[i];
    if (Desc.Kind != IITDescriptor::Argument)
      continue;

    auto ArgNum = Desc.getArgumentNumber();
    Tys.resize(std::max(ArgNum + 1, Tys.size()));

    Tys[ArgNum] = generateAnyType(Desc.getArgumentKind(), Ctx);
  }
}

TEST(GenXIntrinsics, IdenticalDecls) {
  LLVMContext Ctx;
  Module *M = new Module("test_module", Ctx);
  unsigned LLVMID = BeginLLVMGenXID;
  unsigned GenXID = BeginGenXID;
  SmallVector<Type *, 8> Tys;
  while (LLVMID < EndLLVMGenXID) {
    generateOverloadedTypes(static_cast<Intrinsic::ID>(LLVMID), Ctx, Tys);
    auto LLVMDecl = Intrinsic::getDeclaration(M, static_cast<Intrinsic::ID>(LLVMID), Tys);
    auto GenXDecl = GenXIntrinsic::getDeclaration(M, static_cast<GenXIntrinsic::ID>(GenXID), Tys);
    EXPECT_EQ(LLVMDecl, GenXDecl);
    ++LLVMID;
    ++GenXID;
    Tys.clear();
  }
}

TEST(GenXIntrinsics, DeclToID) {
  LLVMContext Ctx;
  Module *M = new Module("test_module", Ctx);
  SmallVector<Type *, 8> Tys;
  for (unsigned LLVMID = BeginLLVMGenXID; LLVMID < EndLLVMGenXID; ++LLVMID) {
    generateOverloadedTypes(static_cast<Intrinsic::ID>(LLVMID), Ctx, Tys);
    auto LLVMDecl = Intrinsic::getDeclaration(M, static_cast<Intrinsic::ID>(LLVMID), Tys);
    unsigned GenXID = GenXIntrinsic::getIntrinsicID(LLVMDecl);
    EXPECT_EQ(LLVMID - BeginLLVMGenXID, GenXID - BeginGenXID);
    ++LLVMID;
    Tys.clear();
  }
}

TEST(GenXIntrinsics, DeclToIDGenX) {
  LLVMContext Ctx;
  Module *M = new Module("test_module", Ctx);
  SmallVector<Type *, 8> Tys;
  for (unsigned GenXID = BeginGenXID; GenXID < EndGenXID; ++GenXID) {
    generateOverloadedTypes(static_cast<Intrinsic::ID>(GenXID - BeginGenXID + BeginLLVMGenXID), Ctx, Tys);
    auto GenXDecl = GenXIntrinsic::getDeclaration(M, static_cast<GenXIntrinsic::ID>(GenXID), Tys);
    unsigned LLVMID = GenXDecl->getIntrinsicID();
    EXPECT_EQ(LLVMID - BeginLLVMGenXID, GenXID - BeginGenXID);
    ++GenXID;
    Tys.clear();
  }
}

TEST(GenXIntrinsics, NameMatch) {
  for (unsigned GenXID = BeginGenXID; GenXID < EndGenXID; ++GenXID) {
    std::string Name = GenXIntrinsic::getName(static_cast<GenXIntrinsic::ID>(GenXID), {});
    unsigned FromNameID = GenXIntrinsic::lookupGenXIntrinsicID(Name);
    EXPECT_EQ(GenXID, FromNameID);
  }
}

} // namespace
