//===- IntrinsicsGenX.td - Defines GenX intrinsics -----------*- tablegen -*-===//
//
//      Copyright (c) 2014 Intel Corporation.
//      All rights reserved.
//
//        INTEL CORPORATION PROPRIETARY INFORMATION
//
// This software is supplied under the terms of a license
// agreement or nondisclosure agreement with Intel Corp.
// and may not be copied or disclosed except in accordance
// with the terms of that agreement.
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the GenX-specific intrinsics, which correspond to
// vISA instructions.
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "genx" in {  // All intrinsics start with "llvm.genx.".

  def int_genx_add_sat : GCCBuiltin<"__builtin_genx_add_sat">,
              Intrinsic<[llvm_anyvector_ty], [LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem]>;

  //--------------------------------------------------------------------
  // Region/element access intrinsics


  // llvm.genx.rdregion : read a region, direct or single-indirect
  //
  // arg0: vector to read region out of
  // arg1: i32 vstride in elements, constant
  // arg2: i32 width in elements, constant
  // arg3: i32 stride in elements, constant
  // arg4: i16 offset in bytes
  //
  // Return value: the region extracted
  //
  // The return type must be a vector with the same element type as the input
  // vector, and number of elements giving the total size of the region.
  // The width must be non-zero and must divide the total size evenly.
  //
  // There is no requirement on vstride, width, stride or total size being
  // a power of two or having any maximum.
  //
  // After lowering, the return type can be a scalar of the same type as the
  // element type of the input, indicating that the region has one element.
  // (Lowering lowers an extractelement to this type of rdregion.)
  //
  def int_genx_rdregion :
              Intrinsic<[llvm_anyvector_ty], [llvm_anyvector_ty, llvm_i32_ty,
                  llvm_i32_ty, llvm_i32_ty, llvm_i16_ty], [IntrNoMem]>;

  // llvm.genx.wrregion : write a region, direct or single-indirect
  //
  // arg0: vector to write region in to
  // arg1: subvector to write into the region
  // arg2: i32 vstride in elements, constant
  // arg3: i32 width in elements, constant
  // arg4: i32 stride in elements, constant
  // arg5: i16 offset in bytes
  // arg6: vector of i1 mask, or scalar i1
  //
  // Return value: the updated vector with the region modified
  //
  // The return type must be a vector with the same type as the arg0 vector.
  // The arg1 subvector must have the same element type as the arg0 vector
  // and be no larger.
  // The width must be non-zero and must divide the total size evenly.
  //
  // The arg6 mask is a vector of booleans, at least as wide as the
  // arg1 subvector, such that an element of the subvector is written into
  // its place in the vector only if the corresponding element of the mask
  // is true.
  // Alternatively, arg6 can be a single i1 whose value is considered to be
  // the value of every element of the mask.
  //
  // There is no requirement on vstride, width, stride or total size being
  // a power of two or having any maximum.
  //
  // After lowering, the arg1 subvector to write can be a scalar of the same
  // type as an element of arg0, indicating that the region has one element.
  // (Lowering lowers an insertelement to this type of wrregion.)
  //
  def int_genx_wrregion :
              Intrinsic<[llvm_anyvector_ty], [LLVMMatchType<0>,
                  llvm_anyvector_ty, llvm_i32_ty,
                  llvm_i32_ty, llvm_i32_ty, llvm_i16_ty,
                  llvm_anyint_ty], [IntrNoMem]>;

  //--------------------------------------------------------------------
  // ALU intrinsics

  // llvm.genx.sat : floating point saturate
  //
  // arg0: value to saturate, any scalar or vector floating point type
  //
  // Return value: saturated value, same type as arg0
  //
  // We represent floating point saturation by simply calling this intrinsic
  // on the result of a floating point operation. This works because the
  // value before saturation fits in the same type.
  //
  // We do not have an equivalent for integer saturation, because the
  // before-saturation value needs a bigger integer type than the result.
  // Instead, any integer operation that supports saturation needs an
  // intrinsic for the saturating variant.
  //
  def int_genx_sat :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.fptoui.sat : convert floating point to integer with saturate
  //
  // arg0: value to saturate, any scalar or vector floating point type
  //
  // Return value: converted value, any scalar or vector type (treated as
  //               unsigned) with same vector width as arg0
  //
  def int_genx_fptoui_sat :
              Intrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty], [IntrNoMem]>;

  // llvm.genx.dp4 : dp4 instruction (dot product on groups of 4 elements)
  //
  // arg0: first input value, any vector floating point with a multiple of 4 elements
  // arg1: second input value, same type as arg0
  //
  // Return value: result, same type
  //
  def int_genx_dp4 :
              Intrinsic<[llvm_anyfloat_ty],
                    [LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem]>;


  //--------------------------------------------------------------------
  // Shared function intrinsics


  // llvm.genx.media_ld : vISA MEDIA_LD instruction
  //
  // arg0: i32 modifiers, constant
  // arg1: i32 surface index
  // arg2: i32 plane, constant
  // arg3: i32 block width in bytes, constant
  // arg4: i32 block height, constant
  // arg5: i32 x byte offset
  // arg6: i32 y byte offset
  // arg7: ignored (type same as return value, used for Clang intrinsic resolving)
  //
  // Return value: the data read.
  //
  // The return type must be of the size that the instruction reads, that is,
  // stride * height bytes, where stride is block width * elementsize rounded
  // up to the next power of 2 but no less than 4. If block height is 1, it is
  // valid to have a size calculated without the rounding up.
  //
  def int_genx_media_ld : GCCBuiltin<"__builtin_genx_media_ld">,
              Intrinsic<[llvm_anyvector_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                  llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, LLVMMatchType<0>], []>;

  // llvm.genx.media_st : vISA MEDIA_ST instruction
  //
  // arg0: i32 modifiers, constant
  // arg1: i32 surface index
  // arg2: i32 plane, constant
  // arg3: i32 block width in bytes, constant
  // arg4: i32 block height, constant
  // arg5: i32 x byte offset
  // arg6: i32 y byte offset
  // arg7: data to write
  //
  // The data to write type must be at least the size that the instruction
  // writes, that is, stride * height bytes, where stride is block width *
  // elementsize rounded up to the next power of 2 but no less than 4. If block
  // height is 1, it is valid to have a size calculated without the rounding
  // up.
  //
  def int_genx_media_st : GCCBuiltin<"__builtin_genx_media_st">,
              Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                  llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_anyvector_ty], []>;

  // llvm.genx.oword_st : vISA OWORD_ST instruction
  //
  // arg0: i32 size (log2 number of owords, 0..3)
  // arg1: i32 surface index
  // arg2: i32 offset (in owords)
  // arg3: data to write
  //
  // The data to write type must be at least the size that the instruction
  // writes.
  //
  def int_genx_oword_st : GCCBuiltin<"__builtin_genx_oword_st">,
              Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                  llvm_anyvector_ty], []>;

  //--------------------------------------------------------------------
  // GenX backend internal intrinsics


  // llvm.genx.convert : convert register category (non address)
  //
  // arg0: input value (i32 or vector of i32)
  //
  // Return value: converted value (same type)
  //
  // This intrinsic is inserted by the GenXCatgoryConversion pass to represent
  // a value being converted between two register categories. The input and
  // result categories are not represented; they are implied by the other
  // def/uses of the value. Address conversion is not covered by this
  // intrinsic.
  //
  def int_genx_convert :
              Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.convert.addr : convert to address register category
  //
  // arg0: input value (i16 or vector of i16)
  //
  // Return value: converted value (same type)
  //
  // This intrinsic is inserted by the GenXCatgoryConversion pass to represent
  // a value being converted from a general value to an address, used as the
  // variable index in an element or region access.
  //
  def int_genx_convert_addr :
              Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.convert.pred : convert to predicate register category
  //
  // arg0: input value (i32)
  //
  // Return value: converted value (v32i1)
  //
  // This intrinsic is inserted by GenXConstantMaterialization where a
  // wrregion has a constant mask other than 1. We could just use a bitcast,
  // except that EarlyCSE follows GenXConstantMaterialization and it has
  // a habit of putting the constant back in the wrregion.
  def int_genx_convert_pred :
              Intrinsic<[llvm_anyint_ty], [llvm_i32_ty], [IntrNoMem]>;

  // llvm.genx.add.addr : add an offset onto an address register
  //
  // arg0: lhs input (i16 or vector of i16)
  // arg1: rhs input (same type)
  //
  // Return value: result of add (same type)
  //
  // When the result of a constant add/sub is used as a variable index in
  // a region access, GenXCategoryConversion converts it into this intrinsic
  // so that it will be considered an add to an address register.
  //
  def int_genx_add_addr :
            Intrinsic<[llvm_anyint_ty],
                [LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem]>;

}
