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
  // ALU type conversion intrinsics

  // llvm.genx.fptosi.sat : convert floating point to signed integer
  //      with saturate
  //
  // arg0: value to saturate, any scalar or vector floating point type
  //
  // Return value: converted value, any scalar or vector integer type
  //               (treated as signed) with same vector width as arg0
  //
  def int_genx_fptosi_sat :
              Intrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty], [IntrNoMem]>;

  // llvm.genx.fptoui.sat : convert floating point to unsigned integer
  //      with saturate
  //
  // arg0: value to saturate, any scalar or vector floating point type
  //
  // Return value: converted value, any scalar or vector integer type
  //               (treated as unsigned) with same vector width as arg0
  //
  def int_genx_fptoui_sat :
              Intrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty], [IntrNoMem]>;

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

  // llvm.genx.*trunc.sat : integer truncation with saturation
  //    llvm.genx.sstrunc.sat : signed result, signed operand
  //    llvm.genx.sutrunc.sat : signed result, unsigned operand
  //    llvm.genx.ustrunc.sat : unsigned result, signed operand
  //    llvm.genx.uutrunc.sat : unsigned result, unsigned operand
  //
  // arg0: value to truncate, any scalar or vector integer type
  //
  // Return value: truncated value, any scalar or vector integer type
  //               with same vector width as arg0
  //
  def int_genx_sstrunc_sat : Intrinsic<[llvm_anyint_ty], [llvm_anyint_ty], [IntrNoMem]>;
  def int_genx_sutrunc_sat : Intrinsic<[llvm_anyint_ty], [llvm_anyint_ty], [IntrNoMem]>;
  def int_genx_ustrunc_sat : Intrinsic<[llvm_anyint_ty], [llvm_anyint_ty], [IntrNoMem]>;
  def int_genx_uutrunc_sat : Intrinsic<[llvm_anyint_ty], [llvm_anyint_ty], [IntrNoMem]>;

  //--------------------------------------------------------------------
  // ALU intrinsics

  // llvm.genx.acos : acos instruction
  //
  // arg0: input value, scalar/vector half/float 
  //
  // Return value: result, same type
  //
  def int_genx_acos :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.*add : integer add instruction, no saturation
  //    llvm.genx.ssadd : result signed, operands signed
  //    llvm.genx.suadd : result signed, operands unsigned
  //    llvm.genx.usadd : result unsigned, operands signed
  //    llvm.genx.uuadd : result unsigned, operands unsigned
  //
  // arg0: first input, any scalar/vector integer type, even i64
  // arg1: second input, same type as arg0
  //
  // Return value: result, any scalar or vector integer type with same
  //               vector width
  //
  // The signedness of a non-saturating add only makes a difference if the
  // element type of the result is bigger than the element type of the args.
  //
  // For an integer add where the result and the sources are the same type,
  // and is not saturating, use the LLVM IR Add instruction.
  //
  // For an fp add, use the LLVM IR FAdd instruction, followed by
  // llvm.genx.sat if saturation is required.
  //
  def int_genx_ssadd : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_suadd : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_usadd : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_uuadd : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;

  // llvm.genx.*add.sat : add instruction with saturation
  //    llvm.genx.ssadd.sat : result signed, operands signed
  //    llvm.genx.suadd.sat : result signed, operands unsigned
  //    llvm.genx.usadd.sat : result unsigned, operands signed
  //    llvm.genx.uuadd.sat : result unsigned, operands unsigned
  //
  // arg0: first input, any scalar/vector integer type, even i64
  // arg1: second input, same type as arg0
  //
  // Return value: result, any scalar or vector integer type with same
  //               vector width
  //
  def int_genx_ssadd_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_suadd_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_usadd_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_uuadd_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;

  // No intrinsic for addc as it has two results.

  // llvm.genx.asin : asin instruction
  //
  // arg0: input value, any scalar/vector half/float type
  //
  // Return value: result, same type
  //
  def int_genx_asin :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // asr intrinsic is not needed. Because asr cannot overflow, an asr that
  // saturates with a smaller result type than the execution type can be
  // represented by an LLVM IR Asr instruction then an llvm.genx.sstrunc.sat.

  // llvm.genx.atan : atan instruction
  //
  // arg0: input value, any scalar/vector half/float type
  //
  // Return value: result, same type
  //
  def int_genx_atan :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.*avg : integer averaging, no saturation
  //    llvm.genx.ssavg : result signed, operands signed
  //    llvm.genx.suavg : result signed, operands unsigned
  //    llvm.genx.usavg : result unsigned, operands signed
  //    llvm.genx.uuavg : result unsigned, operands unsigned
  //
  // arg0: first input, any scalar/vector integer type (not i64)
  // arg1: second input, same type as arg0
  //
  // Return value: result, any scalar/vector integer type (not i64)
  //               with same vector width
  //
  def int_genx_ssavg : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_suavg : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_usavg : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_uuavg : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;

  // llvm.genx.*avg.sat : integer averaging with saturation
  //    llvm.genx.ssavg.sat : result signed, operands signed
  //    llvm.genx.suavg.sat : result signed, operands unsigned
  //    llvm.genx.usavg.sat : result unsigned, operands signed
  //    llvm.genx.uuavg.sat : result unsigned, operands unsigned
  //
  // arg0: first input, any scalar/vector integer type (not i64)
  // arg1: second input, same type as arg0
  //
  // Return value: result, any scalar/vector integer type (not i64)
  //               with same vector width
  //
  def int_genx_ssavg_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_suavg_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_usavg_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_uuavg_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;

  // llvm.genx.sbfe : bitfield extract, signed result
  // llvm.genx.ubfe : bitfield extract, unsigned result
  //
  // arg0: first input, any scalar/vector i32 type
  // arg1: second input, same type as arg0
  // arg2: third input, same type as arg0
  //
  // Return value: result, same type as arg0
  //
  def int_genx_sbfe : Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>,
                LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem]>;
  def int_genx_ubfe : Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>,
                LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.bfi : bitfield insert
  //
  // arg0: first input, any scalar/vector i32 type
  // arg1: second input, same type as arg0
  // arg2: third input, same type as arg0
  // arg3: fourth input, same type as arg0
  //
  // Return value: result, same type as arg0
  //
  def int_genx_bfi : Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>,
        LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.bfrev : reverse bits
  //
  // arg0: first input, any scalar/vector i32 type
  //
  // Return value: result, same type as arg0
  //
  def int_genx_bfrev : Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.cbit : count set bits
  //
  // arg0: first input, any scalar/vector i32 type
  //
  // Return value: result, same type as arg0
  //
  def int_genx_cbit : Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // cmp : no intrinsic needed as the LLVM IR ICmp and FCmp instructions cover
  // vISA functionality

  // llvm.genx.cos : cos instruction
  //
  // arg0: input value, any scalar/vector half/float type
  //
  // Return value: result, same type
  //
  def int_genx_cos :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // div : no intrinsic needed as the LLVM IR SDiv, UDiv and FDiv instructions
  // cover vISA functionality

  // llvm.genx.dp2 : dp2 instruction (dot product on groups of 4 elements)
  //
  // arg0: first input value, any vector float with a multiple of 4 elements
  // arg1: second input value, same type as arg0
  //
  // Return value: result, same type
  //
  def int_genx_dp2 :
              Intrinsic<[llvm_anyfloat_ty],
                    [LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.dp3 : dp3 instruction (dot product on groups of 4 elements)
  //
  // arg0: first input value, any vector float with a multiple of 4 elements
  // arg1: second input value, same type as arg0
  //
  // Return value: result, same type
  //
  def int_genx_dp3 :
              Intrinsic<[llvm_anyfloat_ty],
                    [LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.dp4 : dp4 instruction (dot product on groups of 4 elements)
  //
  // arg0: first input value, any vector float with a multiple of 4 elements
  // arg1: second input value, same type as arg0
  //
  // Return value: result, same type
  //
  def int_genx_dp4 :
              Intrinsic<[llvm_anyfloat_ty],
                    [LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.dph : dph instruction (dot product homogenous)
  //
  // arg0: first input value, any vector float with a multiple of 4 elements
  // arg1: second input value, same type as arg0
  //
  // Return value: result, same type
  //
  def int_genx_dph :
              Intrinsic<[llvm_anyfloat_ty],
                    [LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.exp : base 2 exponent
  //
  // arg0: input value, any scalar/vector half/float type
  //
  // Return value: result, same type
  //
  def int_genx_exp :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.sfbh : find bit high, signed operand
  // llvm.genx.ufbh : find bit high, unsigned operand
  //
  // arg0: input value, any scalar/vector i32 type
  //
  // Return value: result, same type
  //
  def int_genx_sfbh :
              Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>], [IntrNoMem]>;
  def int_genx_ufbh :
              Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.fbl : find bit low
  //
  // arg0: input value, any scalar/vector i32 type
  //
  // Return value: result, same type
  //
  def int_genx_fbl :
              Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.frc : fractional part
  //
  // arg0: input value, any scalar/vector float type
  //
  // Return value: result, same type
  //
  def int_genx_frc :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.inv : reciprocal
  //
  // arg0: input value, any scalar/vector half/float type
  //
  // Return value: result, same type
  //
  def int_genx_inv :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.inv.ieee : reciprocal, IEEE result
  //
  // arg0: input value, any scalar/vector half/float type
  //
  // Return value: result, same type
  //
  def int_genx_inv_ieee :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.line : linear equation
  //
  // arg0: first input value, vector float with exactly 4 elements
  // arg1: second input value, vector float with a multiple of 4 elements
  //
  // Return value: result, same type as arg1
  //
  def int_genx_line : Intrinsic<[llvm_anyfloat_ty],
                    [LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.log : base 2 logarithm
  //
  // arg0: input value, any scalar/vector half/float type
  //
  // Return value: result, same type
  //
  def int_genx_log :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.lrp : linear interpolation
  //
  // arg0: first input value, any vector float with a multiple of 4 elements
  // arg1: second input value, same type as arg0
  // arg2: third input value, same type as arg0
  //
  // Return value: result, same type
  //
  def int_genx_lrp : Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>,
              LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.lzd : leading zero detection
  //
  // arg0: input value, any scalar/vector i32 type
  //
  // Return value: result, same type
  //
  def int_genx_lzd :
              Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.*mad : mad instruction, no saturation
  //    llvm.genx.ssmad : result signed, operands signed
  //    llvm.genx.sumad : result signed, operands unsigned
  //    llvm.genx.usmad : result unsigned, operands signed
  //    llvm.genx.uumad : result unsigned, operands unsigned
  //
  // arg0: first input, any scalar/vector integer type (not i64)
  // arg1: second input, same type as arg0
  //
  // Return value: result, any scalar or vector integer type with same
  //               vector width
  //
  def int_genx_ssmad : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_sumad : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_usmad : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_uumad : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;

  // llvm.genx.*mad.sat : mad instruction with saturation
  //    llvm.genx.ssmad.sat : result signed, operands signed
  //    llvm.genx.sumad.sat : result signed, operands unsigned
  //    llvm.genx.usmad.sat : result unsigned, operands signed
  //    llvm.genx.uumad.sat : result unsigned, operands unsigned
  //
  // arg0: first input, any scalar/vector integer type (not i64)
  // arg1: second input, same type as arg0
  //
  // Return value: result, any scalar or vector integer type with same
  //               vector width
  //
  def int_genx_ssmad_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_sumad_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_usmad_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_uumad_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;

  // llvm.genx.*max : max instruction
  //    llvm.genx.smax : result and operands signed
  //    llvm.genx.umax : result and operands unsigned
  //
  // arg0: first input, any scalar/vector integer type, even i64
  // arg1: second input, same type as arg0
  //
  // Return value: result, any scalar or vector integer type with same
  //               vector width
  //
  // There is no need for a saturating variant of this intrinsic.
  // Because max cannot overflow, a saturating max can be represented
  // by this non-saturating max followed by the applicable one of the
  // saturating trunc intrinsics.
  //
  def int_genx_smax : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_umax : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;

  // llvm.genx.*min : min instruction
  //    llvm.genx.smin : result and operands signed
  //    llvm.genx.umin : result and operands unsigned
  //
  // arg0: first input, any scalar/vector integer type, even i64
  // arg1: second input, same type as arg0
  //
  // Return value: result, any scalar or vector integer type with same
  //               vector width
  //
  // There is no need for a saturating variant of this intrinsic.
  // Because min cannot overflow, a saturating min can be represented
  // by this non-saturating min followed by the applicable one of the
  // saturating trunc intrinsics.
  //
  def int_genx_smin : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_umin : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;

  // mod : no intrinsic needed as the LLVM IR SRem, URem and FRem instructions
  // cover vISA functionality

  // llvm.genx.*mul : mul instruction, no saturation
  //    llvm.genx.ssmul : result signed, operands signed
  //    llvm.genx.sumul : result signed, operands unsigned
  //    llvm.genx.usmul : result unsigned, operands signed
  //    llvm.genx.uumul : result unsigned, operands unsigned
  //
  // arg0: first input, any scalar/vector integer type (not i64)
  // arg1: second input, same type as arg0
  //
  // Return value: result, any scalar or vector integer type with same
  //               vector width, even i64
  //
  // For an integer mul where the result and the sources are the same type,
  // and is not saturating, use the LLVM IR Mul instruction.
  //
  // For an fp mul, use the LLVM IR FMul instruction, followed by
  // llvm.genx.sat if saturation is required.
  //
  def int_genx_ssmul : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_sumul : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_usmul : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_uumul : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;

  // llvm.genx.*mul.sat : mul instruction with saturation
  //    llvm.genx.ssmul.sat : result signed, operands signed
  //    llvm.genx.sumul.sat : result signed, operands unsigned
  //    llvm.genx.usmul.sat : result unsigned, operands signed
  //    llvm.genx.uumul.sat : result unsigned, operands unsigned
  //
  // arg0: first input, any scalar/vector integer type (not i64)
  // arg1: second input, same type as arg0
  //
  // Return value: result, any scalar/vector integer type with same
  //               vector width, even i64
  //
  def int_genx_ssmul_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_sumul_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_usmul_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_uumul_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;

  // llvm.genx.*mulh : mulh instruction, no saturation
  //    llvm.genx.smulh : signed
  //    llvm.genx.umulh : unsigned
  //
  // arg0: first input, any scalar/vector i32 type
  // arg1: second input, same type as arg0
  //
  // Return value: result, same type as arg0
  //
  def int_genx_smulh : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_umulh : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;

  // not : intrinsic not needed; use LLVM IR Xor instruction with -1

  // or : intrinsic not needed; use LLVM IR Or instruction

  // llvm.genx.pln : plane equation
  //
  // arg0: first input value, vector of 4 float
  // arg1: second input value, vector float with a multiple of 16 elements
  //
  // Return value: result, vector float with half the vector width of arg1
  //
  def int_genx_pln : Intrinsic<[llvm_anyfloat_ty],
                    [LLVMMatchType<0>, llvm_anyfloat_ty], [IntrNoMem]>;

  // llvm.genx.pow : power
  //
  // arg0: first input, any scalar/vector half/float type
  // arg1: second input, same type
  //
  // Return value: result, same type
  //
  def int_genx_pow : Intrinsic<[llvm_anyfloat_ty],
              [LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.rndd : round down
  //
  // arg0: input value, any scalar/vector float type
  //
  // Return value: result, same type
  //
  def int_genx_rndd :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.rnde : round to even
  //
  // arg0: input value, any scalar/vector float type
  //
  // Return value: result, same type
  //
  def int_genx_rnde :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.rndu : round up
  //
  // arg0: input value, any scalar/vector float type
  //
  // Return value: result, same type
  //
  def int_genx_rndu :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.rndz : round to zero
  //
  // arg0: input value, any scalar/vector float type
  //
  // Return value: result, same type
  //
  def int_genx_rndz :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.rsqrt : reciprocal square root
  //
  // arg0: input value, any scalar/vector half/float type
  //
  // Return value: result, same type
  //
  def int_genx_rsqrt :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.sad2 : two-wide sum of absolute differences
  //
  // arg0: first input, vector of i8, multiple of 2 wide
  // arg1: second input, same type
  //
  // Return value: result, vector of i16 of same vector width
  //
  // The vISA spec says that the args and the result can be signed or
  // unsigned, and saturation is an option. I can't see that any of that makes
  // any difference to the result, so I have provided just this one intrinsic.
  //
  def int_genx_sad2 : Intrinsic<[llvm_anyint_ty], [llvm_anyint_ty,
              LLVMMatchType<1>], [IntrNoMem]>;

  // llvm.genx.sad2add : two-wide sum of absolute differences and add
  //
  // arg0: first input, vector of i8, multiple of 2 wide
  // arg1: second input, same type
  // arg2: third input, vector of i16 of same vector width
  //
  // Return value: result, same type as arg2
  //
  // The vISA spec says that the args and the result can be signed or
  // unsigned, and saturation is an option. I can't see that any of that makes
  // any difference to the result, so I have provided just this one intrinsic.
  //
  def int_genx_sad2add : Intrinsic<[llvm_anyint_ty], [llvm_anyint_ty,
              LLVMMatchType<1>], [IntrNoMem]>;

  // llvm.genx.*sad2add.sat : two-wide sum of absolute differences and add, saturated
  //    llvm.genx.ssad2add.sat : signed arg2 and result
  //    llvm.genx.usad2add.sat : unsigned arg2 and result
  //
  // arg0: first input, vector of i8, multiple of 2 wide
  // arg1: second input, same type
  // arg2: third input, vector of i16 of same vector width
  //
  // Return value: result, same type as arg2
  //
  def int_genx_ssad2add_sat : Intrinsic<[llvm_anyint_ty], [llvm_anyint_ty,
              LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_usad2add_sat : Intrinsic<[llvm_anyint_ty], [llvm_anyint_ty,
              LLVMMatchType<1>], [IntrNoMem]>;

  // llvm.genx.*shl : shl instruction, no saturation
  //    llvm.genx.ssshl : result signed, operands signed
  //    llvm.genx.sushl : result signed, operands unsigned
  //    llvm.genx.usshl : result unsigned, operands signed
  //    llvm.genx.uushl : result unsigned, operands unsigned
  //
  // arg0: first input, any scalar/vector integer type, even i64
  // arg1: second input, same type as arg0
  //
  // Return value: result, any scalar or vector integer type with same
  //               vector width, even i64
  //
  def int_genx_ssshl : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_sushl : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_usshl : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_uushl : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;

  // llvm.genx.*shl.sat : shl instruction with saturation
  //    llvm.genx.ssshl.sat : result signed, operands signed
  //    llvm.genx.sushl.sat : result signed, operands unsigned
  //    llvm.genx.usshl.sat : result unsigned, operands signed
  //    llvm.genx.uushl.sat : result unsigned, operands unsigned
  //
  // arg0: first input, any scalar/vector integer type, even i64
  // arg1: second input, same type as arg0
  //
  // Return value: result, any scalar/vector integer type with same
  //               vector width, even i64
  //
  def int_genx_ssshl_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_sushl_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_usshl_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;
  def int_genx_uushl_sat : Intrinsic<[llvm_anyint_ty],
                [llvm_anyint_ty, LLVMMatchType<1>], [IntrNoMem]>;

  // shr intrinsic is not needed. Because shr cannot overflow, an shr that
  // saturates with a smaller result type than the execution type can be
  // represented by an LLVM IR Shr instruction then an llvm.genx.sstrunc.sat.

  // llvm.genx.sin : reciprocal square root
  //
  // arg0: input value, any scalar/vector half/float type
  //
  // Return value: result, same type
  //
  def int_genx_sin :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.sqrt : reciprocal square root
  //
  // arg0: input value, any scalar/vector half/float type
  //
  // Return value: result, same type
  //
  def int_genx_sqrt :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // llvm.genx.sqrt_ieee : reciprocal square root
  //
  // arg0: input value, any scalar/vector half/float type
  //
  // Return value: result, same type
  //
  def int_genx_sqrt_ieee :
              Intrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  // No intrinsic for subb as it has two results.

  // xor : intrinsic not needed; use LLVM IR Xor instruction


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
