/*
 * Copyright (c) 2017 Trail of Bits, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Disable the "loop not unrolled warnings"
#pragma clang diagnostic ignored "-Wpass-failed"

namespace {

template <typename S>
DEF_SEM(ORR_Vec, V128W dst, S src1, S src2) {
  UWriteV64(dst, UOrV64(UReadV64(src1), UReadV64(src2)));
  return memory;
}

template <typename S>
DEF_SEM(AND_Vec, V128W dst, S src1, S src2) {
  UWriteV64(dst, UAndV64(UReadV64(src1), UReadV64(src2)));
  return memory;
}

template <typename S>
DEF_SEM(BIC_Vec, V128W dst, S src1, S src2) {
  UWriteV64(dst, UAndV64(UReadV64(src1), UNotV64(UReadV64(src2))));
  return memory;
}

template <typename S>
DEF_SEM(EOR_Vec, V128W dst, S src1, S src2) {
  auto operand4 = UReadV64(src1);
  auto operand1 = UReadV64(src2);
  auto operand2 = UClearV64(operand4);
  auto operand3 = UNotV64(operand2);
  UWriteV64(dst,
            UXorV64(operand1, UAndV64(UXorV64(operand2, operand4), operand3)));
  return memory;
}

template <typename S>
DEF_SEM(BIT_Vec, V128W dst, S dst_src, S src1, S src2) {
  auto operand4 = UReadV64(src1);
  auto operand1 = UReadV64(dst_src);
  auto operand3 = UReadV64(src2);
  UWriteV64(dst,
            UXorV64(operand1, UAndV64(UXorV64(operand1, operand4), operand3)));
  return memory;
}

template <typename S>
DEF_SEM(BIF_Vec, V128W dst, S dst_src, S src1, S src2) {
  auto operand4 = UReadV64(src1);
  auto operand1 = UReadV64(dst_src);
  auto operand3 = UNotV64(UReadV64(src2));
  UWriteV64(dst,
            UXorV64(operand1, UAndV64(UXorV64(operand1, operand4), operand3)));
  return memory;
}

template <typename S>
DEF_SEM(BSL_Vec, V128W dst, S dst_src, S src1, S src2) {
  auto operand4 = UReadV64(src1);
  auto operand1 = UReadV64(src2);
  auto operand3 = UReadV64(dst_src);
  UWriteV64(dst,
            UXorV64(operand1, UAndV64(UXorV64(operand1, operand4), operand3)));
  return memory;
}

}  // namespace

DEF_ISEL(ORR_ASIMDSAME_ONLY_8B) = ORR_Vec<V64>;
DEF_ISEL(ORR_ASIMDSAME_ONLY_16B) = ORR_Vec<V128>;

DEF_ISEL(AND_ASIMDSAME_ONLY_8B) = AND_Vec<V64>;
DEF_ISEL(AND_ASIMDSAME_ONLY_16B) = AND_Vec<V128>;

DEF_ISEL(BIC_ASIMDSAME_ONLY_8B) = BIC_Vec<V64>;
DEF_ISEL(BIC_ASIMDSAME_ONLY_16B) = BIC_Vec<V128>;

DEF_ISEL(EOR_ASIMDSAME_ONLY_8B) = EOR_Vec<V64>;
DEF_ISEL(EOR_ASIMDSAME_ONLY_16B) = EOR_Vec<V128>;

DEF_ISEL(BIT_ASIMDSAME_ONLY_8B) = BIT_Vec<V64>;
DEF_ISEL(BIT_ASIMDSAME_ONLY_16B) = BIT_Vec<V128>;

DEF_ISEL(BIF_ASIMDSAME_ONLY_8B) = BIF_Vec<V64>;
DEF_ISEL(BIF_ASIMDSAME_ONLY_16B) = BIF_Vec<V128>;

DEF_ISEL(BSL_ASIMDSAME_ONLY_8B) = BSL_Vec<V64>;
DEF_ISEL(BSL_ASIMDSAME_ONLY_16B) = BSL_Vec<V128>;

namespace {

DEF_SEM(FMOV_VectorToUInt64, R64W dst, V128 src) {
  auto val = UExtractV64(UReadV64(src), 1);
  WriteZExt(dst, val);
  return memory;
}

DEF_SEM(FMOV_UInt64ToVector, V128W dst, R64 src) {
  auto val = Read(src);
  uint64v2_t temp_vec = {};
  temp_vec = UInsertV64(temp_vec, 0, UExtractV64(UReadV64(dst), 0));
  temp_vec = UInsertV64(temp_vec, 1, val);
  UWriteV64(dst, temp_vec);
  return memory;
}
}  // namespace

DEF_ISEL(FMOV_64VX_FLOAT2INT) = FMOV_VectorToUInt64;
DEF_ISEL(FMOV_V64I_FLOAT2INT) = FMOV_UInt64ToVector;

namespace {

#define MAKE_DUP(size) \
  template <typename V> \
  DEF_SEM(DUP_##size, V128W dst, R64 src) { \
    auto val = TruncTo<uint##size##_t>(Read(src)); \
    V vec = {}; \
    _Pragma("unroll") for (auto &element : vec.elems) { \
      element = val; \
    } \
    UWriteV##size(dst, vec); \
    return memory; \
  }

MAKE_DUP(8)
MAKE_DUP(16)
MAKE_DUP(32)
MAKE_DUP(64)

#undef MAKE_DUP

}  // namespace

DEF_ISEL(DUP_ASIMDINS_DR_R_8B) = DUP_8<uint8v8_t>;
DEF_ISEL(DUP_ASIMDINS_DR_R_16B) = DUP_8<uint8v16_t>;
DEF_ISEL(DUP_ASIMDINS_DR_R_4H) = DUP_16<uint16v4_t>;
DEF_ISEL(DUP_ASIMDINS_DR_R_8H) = DUP_16<uint16v8_t>;
DEF_ISEL(DUP_ASIMDINS_DR_R_2S) = DUP_32<uint32v2_t>;
DEF_ISEL(DUP_ASIMDINS_DR_R_4S) = DUP_32<uint32v4_t>;
DEF_ISEL(DUP_ASIMDINS_DR_R_2D) = DUP_64<uint64v2_t>;

namespace {

template <typename T>
ALWAYS_INLINE static T UMin(T lhs, T rhs) {
  return lhs < rhs ? lhs : rhs;
}

template <typename T>
ALWAYS_INLINE static T UMax(T lhs, T rhs) {
  return lhs < rhs ? rhs : lhs;
}

#define SMin UMin
#define SMax UMax

#define MAKE_BROADCAST(op, prefix, binop, size) \
  template <typename S, typename V> \
  DEF_SEM(op##_##size, V128W dst, S src1, S src2) { \
    auto vec1 = prefix##ReadV##size(src1); \
    auto vec2 = prefix##ReadV##size(src2); \
    V sum = {}; \
    _Pragma("unroll") for (size_t i = 0, max_i = NumVectorElems(sum); \
                           i < max_i; ++i) { \
      sum.elems[i] = prefix##binop(prefix##ExtractV##size(vec1, i), \
                                   prefix##ExtractV##size(vec2, i)); \
    } \
    prefix##WriteV##size(dst, sum); \
    return memory; \
  }

MAKE_BROADCAST(ADD, U, Add, 8)
MAKE_BROADCAST(ADD, U, Add, 16)
MAKE_BROADCAST(ADD, U, Add, 32)
MAKE_BROADCAST(ADD, U, Add, 64)

MAKE_BROADCAST(SUB, U, Sub, 8)
MAKE_BROADCAST(SUB, U, Sub, 16)
MAKE_BROADCAST(SUB, U, Sub, 32)
MAKE_BROADCAST(SUB, U, Sub, 64)

MAKE_BROADCAST(UMIN, U, Min, 8)
MAKE_BROADCAST(UMIN, U, Min, 16)
MAKE_BROADCAST(UMIN, U, Min, 32)

MAKE_BROADCAST(SMIN, S, Min, 8)
MAKE_BROADCAST(SMIN, S, Min, 16)
MAKE_BROADCAST(SMIN, S, Min, 32)

MAKE_BROADCAST(UMAX, U, Max, 8)
MAKE_BROADCAST(UMAX, U, Max, 16)
MAKE_BROADCAST(UMAX, U, Max, 32)

MAKE_BROADCAST(SMAX, S, Max, 8)
MAKE_BROADCAST(SMAX, S, Max, 16)
MAKE_BROADCAST(SMAX, S, Max, 32)

#undef MAKE_BROADCAST

}  // namespace

DEF_ISEL(ADD_ASIMDSAME_ONLY_8B) = ADD_8<V64, uint8v8_t>;
DEF_ISEL(ADD_ASIMDSAME_ONLY_16B) = ADD_8<V128, uint8v16_t>;
DEF_ISEL(ADD_ASIMDSAME_ONLY_4H) = ADD_16<V64, uint16v4_t>;
DEF_ISEL(ADD_ASIMDSAME_ONLY_8H) = ADD_16<V128, uint16v8_t>;
DEF_ISEL(ADD_ASIMDSAME_ONLY_2S) = ADD_32<V64, uint32v2_t>;
DEF_ISEL(ADD_ASIMDSAME_ONLY_4S) = ADD_32<V128, uint32v4_t>;
DEF_ISEL(ADD_ASIMDSAME_ONLY_2D) = ADD_64<V128, uint64v2_t>;

DEF_ISEL(SUB_ASIMDSAME_ONLY_8B) = SUB_8<V64, uint8v8_t>;
DEF_ISEL(SUB_ASIMDSAME_ONLY_16B) = SUB_8<V128, uint8v16_t>;
DEF_ISEL(SUB_ASIMDSAME_ONLY_4H) = SUB_16<V64, uint16v4_t>;
DEF_ISEL(SUB_ASIMDSAME_ONLY_8H) = SUB_16<V128, uint16v8_t>;
DEF_ISEL(SUB_ASIMDSAME_ONLY_2S) = SUB_32<V64, uint32v2_t>;
DEF_ISEL(SUB_ASIMDSAME_ONLY_4S) = SUB_32<V128, uint32v4_t>;
DEF_ISEL(SUB_ASIMDSAME_ONLY_2D) = SUB_64<V128, uint64v2_t>;

DEF_ISEL(UMIN_ASIMDSAME_ONLY_8B) = UMIN_8<V64, uint8v8_t>;
DEF_ISEL(UMIN_ASIMDSAME_ONLY_16B) = UMIN_8<V128, uint8v16_t>;
DEF_ISEL(UMIN_ASIMDSAME_ONLY_4H) = UMIN_16<V64, uint16v4_t>;
DEF_ISEL(UMIN_ASIMDSAME_ONLY_8H) = UMIN_16<V128, uint16v8_t>;
DEF_ISEL(UMIN_ASIMDSAME_ONLY_2S) = UMIN_32<V64, uint32v2_t>;
DEF_ISEL(UMIN_ASIMDSAME_ONLY_4S) = UMIN_32<V128, uint32v4_t>;

DEF_ISEL(UMAX_ASIMDSAME_ONLY_8B) = UMAX_8<V64, uint8v8_t>;
DEF_ISEL(UMAX_ASIMDSAME_ONLY_16B) = UMAX_8<V128, uint8v16_t>;
DEF_ISEL(UMAX_ASIMDSAME_ONLY_4H) = UMAX_16<V64, uint16v4_t>;
DEF_ISEL(UMAX_ASIMDSAME_ONLY_8H) = UMAX_16<V128, uint16v8_t>;
DEF_ISEL(UMAX_ASIMDSAME_ONLY_2S) = UMAX_32<V64, uint32v2_t>;
DEF_ISEL(UMAX_ASIMDSAME_ONLY_4S) = UMAX_32<V128, uint32v4_t>;

DEF_ISEL(SMIN_ASIMDSAME_ONLY_8B) = SMIN_8<V64, int8v8_t>;
DEF_ISEL(SMIN_ASIMDSAME_ONLY_16B) = SMIN_8<V128, int8v16_t>;
DEF_ISEL(SMIN_ASIMDSAME_ONLY_4H) = SMIN_16<V64, int16v4_t>;
DEF_ISEL(SMIN_ASIMDSAME_ONLY_8H) = SMIN_16<V128, int16v8_t>;
DEF_ISEL(SMIN_ASIMDSAME_ONLY_2S) = SMIN_32<V64, int32v2_t>;
DEF_ISEL(SMIN_ASIMDSAME_ONLY_4S) = SMIN_32<V128, int32v4_t>;

DEF_ISEL(SMAX_ASIMDSAME_ONLY_8B) = SMAX_8<V64, int8v8_t>;
DEF_ISEL(SMAX_ASIMDSAME_ONLY_16B) = SMAX_8<V128, int8v16_t>;
DEF_ISEL(SMAX_ASIMDSAME_ONLY_4H) = SMAX_16<V64, int16v4_t>;
DEF_ISEL(SMAX_ASIMDSAME_ONLY_8H) = SMAX_16<V128, int16v8_t>;
DEF_ISEL(SMAX_ASIMDSAME_ONLY_2S) = SMAX_32<V64, int32v2_t>;
DEF_ISEL(SMAX_ASIMDSAME_ONLY_4S) = SMAX_32<V128, int32v4_t>;

namespace {

#define MAKE_CMP_BROADCAST(op, prefix, binop, size) \
  template <typename S, typename V> \
  DEF_SEM(op##_##size, V128W dst, S src1, I##size imm) { \
    auto vec1 = prefix##ReadV##size(src1); \
    auto ucmp_val = Read(imm); \
    auto cmp_val = Signed(ucmp_val); \
    decltype(ucmp_val) zeros = 0; \
    decltype(ucmp_val) ones = ~zeros; \
    V res = {}; \
    _Pragma("unroll") for (size_t i = 0, max_i = NumVectorElems(res); \
                           i < max_i; ++i) { \
      res.elems[i] = \
          Select(prefix##binop(prefix##ExtractV##size(vec1, i), cmp_val), \
                 ones, zeros); \
    } \
    UWriteV##size(dst, res); \
    return memory; \
  }

MAKE_CMP_BROADCAST(CMPEQ_IMM, S, CmpEq, 8)
MAKE_CMP_BROADCAST(CMPEQ_IMM, S, CmpEq, 16)
MAKE_CMP_BROADCAST(CMPEQ_IMM, S, CmpEq, 32)
MAKE_CMP_BROADCAST(CMPEQ_IMM, S, CmpEq, 64)

MAKE_CMP_BROADCAST(CMPLT_IMM, S, CmpLt, 8)
MAKE_CMP_BROADCAST(CMPLT_IMM, S, CmpLt, 16)
MAKE_CMP_BROADCAST(CMPLT_IMM, S, CmpLt, 32)
MAKE_CMP_BROADCAST(CMPLT_IMM, S, CmpLt, 64)

MAKE_CMP_BROADCAST(CMPLE_IMM, S, CmpLte, 8)
MAKE_CMP_BROADCAST(CMPLE_IMM, S, CmpLte, 16)
MAKE_CMP_BROADCAST(CMPLE_IMM, S, CmpLte, 32)
MAKE_CMP_BROADCAST(CMPLE_IMM, S, CmpLte, 64)

MAKE_CMP_BROADCAST(CMPGT_IMM, S, CmpGt, 8)
MAKE_CMP_BROADCAST(CMPGT_IMM, S, CmpGt, 16)
MAKE_CMP_BROADCAST(CMPGT_IMM, S, CmpGt, 32)
MAKE_CMP_BROADCAST(CMPGT_IMM, S, CmpGt, 64)

MAKE_CMP_BROADCAST(CMPGE_IMM, S, CmpGte, 8)
MAKE_CMP_BROADCAST(CMPGE_IMM, S, CmpGte, 16)
MAKE_CMP_BROADCAST(CMPGE_IMM, S, CmpGte, 32)
MAKE_CMP_BROADCAST(CMPGE_IMM, S, CmpGte, 64)

#undef MAKE_CMP_BROADCAST

// M12.7: SHL (Advanced SIMD shift left by immediate) — per-lane left shift by a
// broadcast immediate. Modeled on MAKE_CMP_BROADCAST above. Was unimplemented
// (TryDecodeSHL_ASIMDSHF_R stubbed) → __remill_error → the lifted layout solver
// diverged (the Rust auto-vectorizer emits `shl.8b` in is_normal()'s bool reduction).
#define MAKE_SHL_BROADCAST(size) \
  template <typename S, typename V> \
  DEF_SEM(SHL_IMM_##size, V128W dst, S src1, I64 imm) { \
    auto vec1 = UReadV##size(src1); \
    auto shift_amt = static_cast<uint##size##_t>(Read(imm)); \
    V res = {}; \
    _Pragma("unroll") for (size_t i = 0, max_i = NumVectorElems(res); \
                           i < max_i; ++i) { \
      res.elems[i] = UShl(UExtractV##size(vec1, i), shift_amt); \
    } \
    UWriteV##size(dst, res); \
    return memory; \
  }

MAKE_SHL_BROADCAST(8)
MAKE_SHL_BROADCAST(16)
MAKE_SHL_BROADCAST(32)
MAKE_SHL_BROADCAST(64)

#undef MAKE_SHL_BROADCAST

// M12.7: SSHLL/USHLL (Advanced SIMD shift-left-long by immediate). Widen each narrow
// lane to 2x width (S=sign / U=zero extend), then shift left by the immediate. The L
// (Q=0) form takes the LOW half of Vn; the "2" (Q=1) form takes the HIGH half. The Rust
// auto-vectorizer emits `sshll.4s v,v,#0` (i16->i32 sign-extend-widen) in the layout
// positioning pass; it was unimplemented (TryDecode... stubbed) -> __remill_error.
#define MAKE_SHLL_SEM(NAME, RDV, EXV, EXT, ELEMW, WIDEV, WRV, NL, HB) \
  DEF_SEM(NAME, V128W dst, V128 src, I64 imm) { \
    auto vec = RDV(src); \
    auto sh = Read(imm); \
    WIDEV res = {}; \
    _Pragma("unroll") for (size_t i = 0; i < (NL); ++i) { \
      res.elems[i] = static_cast<ELEMW>(EXT<ELEMW>(EXV(vec, (HB) + i)) << sh); \
    } \
    WRV(dst, res); \
    return memory; \
  }

MAKE_SHLL_SEM(SSHLL_8H,  SReadV8,  SExtractV8,  SExtTo, int16_t, int16v8_t, SWriteV16, 8, 0)
MAKE_SHLL_SEM(SSHLL_4S,  SReadV16, SExtractV16, SExtTo, int32_t, int32v4_t, SWriteV32, 4, 0)
MAKE_SHLL_SEM(SSHLL_2D,  SReadV32, SExtractV32, SExtTo, int64_t, int64v2_t, SWriteV64, 2, 0)
MAKE_SHLL_SEM(SSHLL2_8H, SReadV8,  SExtractV8,  SExtTo, int16_t, int16v8_t, SWriteV16, 8, 8)
MAKE_SHLL_SEM(SSHLL2_4S, SReadV16, SExtractV16, SExtTo, int32_t, int32v4_t, SWriteV32, 4, 4)
MAKE_SHLL_SEM(SSHLL2_2D, SReadV32, SExtractV32, SExtTo, int64_t, int64v2_t, SWriteV64, 2, 2)
MAKE_SHLL_SEM(USHLL_8H,  UReadV8,  UExtractV8,  ZExtTo, uint16_t, uint16v8_t, UWriteV16, 8, 0)
MAKE_SHLL_SEM(USHLL_4S,  UReadV16, UExtractV16, ZExtTo, uint32_t, uint32v4_t, UWriteV32, 4, 0)
MAKE_SHLL_SEM(USHLL_2D,  UReadV32, UExtractV32, ZExtTo, uint64_t, uint64v2_t, UWriteV64, 2, 0)
MAKE_SHLL_SEM(USHLL2_8H, UReadV8,  UExtractV8,  ZExtTo, uint16_t, uint16v8_t, UWriteV16, 8, 8)
MAKE_SHLL_SEM(USHLL2_4S, UReadV16, UExtractV16, ZExtTo, uint32_t, uint32v4_t, UWriteV32, 4, 4)
MAKE_SHLL_SEM(USHLL2_2D, UReadV32, UExtractV32, ZExtTo, uint64_t, uint64v2_t, UWriteV64, 2, 2)

#undef MAKE_SHLL_SEM

// M12.7: vector FP arithmetic (FADD/FSUB/FMUL/FDIV ASIMDSAME) — per-lane, modeled on
// the scalar FADD_Scalar32 (BINARY.cpp): CheckedFloatBinOp(state, FAdd32, a, b). The
// layout positioning is FP-vector-heavy (sshll i16->i32, scvtf i32->f32, fmul/fadd).
#define MAKE_FP_VEC(NAME, FOP, RDV, EXV, WRV, DV, NL) \
  DEF_SEM(NAME, V128W dst, V128 src1, V128 src2) { \
    auto v1 = RDV(src1); \
    auto v2 = RDV(src2); \
    DV res = {}; \
    _Pragma("unroll") for (size_t i = 0; i < (NL); ++i) { \
      res.elems[i] = CheckedFloatBinOp(state, FOP, EXV(v1, i), EXV(v2, i)); \
    } \
    WRV(dst, res); \
    return memory; \
  }
MAKE_FP_VEC(FADD_VEC_2S, FAdd32, FReadV32, FExtractV32, FWriteV32, float32v2_t, 2)
MAKE_FP_VEC(FADD_VEC_4S, FAdd32, FReadV32, FExtractV32, FWriteV32, float32v4_t, 4)
MAKE_FP_VEC(FADD_VEC_2D, FAdd64, FReadV64, FExtractV64, FWriteV64, float64v2_t, 2)
MAKE_FP_VEC(FSUB_VEC_2S, FSub32, FReadV32, FExtractV32, FWriteV32, float32v2_t, 2)
MAKE_FP_VEC(FSUB_VEC_4S, FSub32, FReadV32, FExtractV32, FWriteV32, float32v4_t, 4)
MAKE_FP_VEC(FSUB_VEC_2D, FSub64, FReadV64, FExtractV64, FWriteV64, float64v2_t, 2)
MAKE_FP_VEC(FMUL_VEC_2S, FMul32, FReadV32, FExtractV32, FWriteV32, float32v2_t, 2)
MAKE_FP_VEC(FMUL_VEC_4S, FMul32, FReadV32, FExtractV32, FWriteV32, float32v4_t, 4)
MAKE_FP_VEC(FMUL_VEC_2D, FMul64, FReadV64, FExtractV64, FWriteV64, float64v2_t, 2)
MAKE_FP_VEC(FDIV_VEC_2S, FDiv32, FReadV32, FExtractV32, FWriteV32, float32v2_t, 2)
MAKE_FP_VEC(FDIV_VEC_4S, FDiv32, FReadV32, FExtractV32, FWriteV32, float32v4_t, 4)
MAKE_FP_VEC(FDIV_VEC_2D, FDiv64, FReadV64, FExtractV64, FWriteV64, float64v2_t, 2)
#undef MAKE_FP_VEC

// M12.7: vector int->float convert (SCVTF/UCVTF ASIMDMISC) — per-lane CheckedCast,
// modeled on the scalar UCVTF_UInt32ToFloat32 (CONVERT.cpp).
#define MAKE_CVTF_VEC(NAME, RDV, EXV, SRCT, DSTT, WRV, DV, NL) \
  DEF_SEM(NAME, V128W dst, V128 src) { \
    auto v = RDV(src); \
    DV res = {}; \
    _Pragma("unroll") for (size_t i = 0; i < (NL); ++i) { \
      res.elems[i] = CheckedCast<SRCT, DSTT>(state, EXV(v, i)); \
    } \
    WRV(dst, res); \
    return memory; \
  }
MAKE_CVTF_VEC(SCVTF_VEC_2S, SReadV32, SExtractV32, int32_t, float32_t, FWriteV32, float32v2_t, 2)
MAKE_CVTF_VEC(SCVTF_VEC_4S, SReadV32, SExtractV32, int32_t, float32_t, FWriteV32, float32v4_t, 4)
MAKE_CVTF_VEC(SCVTF_VEC_2D, SReadV64, SExtractV64, int64_t, float64_t, FWriteV64, float64v2_t, 2)
MAKE_CVTF_VEC(UCVTF_VEC_2S, UReadV32, UExtractV32, uint32_t, float32_t, FWriteV32, float32v2_t, 2)
MAKE_CVTF_VEC(UCVTF_VEC_4S, UReadV32, UExtractV32, uint32_t, float32_t, FWriteV32, float32v4_t, 4)
MAKE_CVTF_VEC(UCVTF_VEC_2D, UReadV64, UExtractV64, uint64_t, float64_t, FWriteV64, float64v2_t, 2)
#undef MAKE_CVTF_VEC

// M12.7: DUP element (DUP_ASIMDINS_DV_V) — broadcast lane[index] to all lanes.
#define MAKE_DUP_ELT(NAME, RDV, EXV, WRV, DV, NL) \
  DEF_SEM(NAME, V128W dst, V128 src, I64 index) { \
    auto v = RDV(src); \
    auto val = EXV(v, Read(index)); \
    DV res = {}; \
    _Pragma("unroll") for (size_t i = 0; i < (NL); ++i) { res.elems[i] = val; } \
    WRV(dst, res); \
    return memory; \
  }
MAKE_DUP_ELT(DUP_ELT_8B,  UReadV8,  UExtractV8,  UWriteV8,  uint8v8_t,   8)
MAKE_DUP_ELT(DUP_ELT_16B, UReadV8,  UExtractV8,  UWriteV8,  uint8v16_t,  16)
MAKE_DUP_ELT(DUP_ELT_4H,  UReadV16, UExtractV16, UWriteV16, uint16v4_t,  4)
MAKE_DUP_ELT(DUP_ELT_8H,  UReadV16, UExtractV16, UWriteV16, uint16v8_t,  8)
MAKE_DUP_ELT(DUP_ELT_2S,  UReadV32, UExtractV32, UWriteV32, uint32v2_t,  2)
MAKE_DUP_ELT(DUP_ELT_4S,  UReadV32, UExtractV32, UWriteV32, uint32v4_t,  4)
MAKE_DUP_ELT(DUP_ELT_2D,  UReadV64, UExtractV64, UWriteV64, uint64v2_t,  2)
#undef MAKE_DUP_ELT

// M12.7: SCALAR DUP element (DUP_ASISDONE / `MOV <V><d>, <Vn>.<T>[index]`) —
// extract ONE lane[index] of Vn into the scalar FP reg Vd, zeroing the upper
// bits (scalar-write semantics: write a 128-bit vector with lane0=val, rest 0).
#define MAKE_DUP_SCALAR(NAME, RDV, EXV, WRV, DV) \
  DEF_SEM(NAME, V128W dst, V128 src, I64 index) { \
    auto v = RDV(src); \
    auto val = EXV(v, Read(index)); \
    DV res = {}; \
    res.elems[0] = val; \
    WRV(dst, res); \
    return memory; \
  }
MAKE_DUP_SCALAR(DUP_SCALAR_B, UReadV8,  UExtractV8,  UWriteV8,  uint8v16_t)
MAKE_DUP_SCALAR(DUP_SCALAR_H, UReadV16, UExtractV16, UWriteV16, uint16v8_t)
MAKE_DUP_SCALAR(DUP_SCALAR_S, UReadV32, UExtractV32, UWriteV32, uint32v4_t)
MAKE_DUP_SCALAR(DUP_SCALAR_D, UReadV64, UExtractV64, UWriteV64, uint64v2_t)
#undef MAKE_DUP_SCALAR

// M12.7: ZIP1/ZIP2 (ASIMDPERM) — interleave the low (ZIP1) or high (ZIP2) halves of
// two vectors: res[2i]=src1[base+i], res[2i+1]=src2[base+i], base = HI ? n/2 : 0.
#define MAKE_ZIP(NAME, RDV, EXV, WRV, DV, NL, HI) \
  DEF_SEM(NAME, V128W dst, V128 src1, V128 src2) { \
    auto v1 = RDV(src1); \
    auto v2 = RDV(src2); \
    DV res = {}; \
    const size_t half = (NL) / 2; \
    const size_t base = (HI) ? half : 0; \
    _Pragma("unroll") for (size_t i = 0; i < half; ++i) { \
      res.elems[2 * i] = EXV(v1, base + i); \
      res.elems[2 * i + 1] = EXV(v2, base + i); \
    } \
    WRV(dst, res); \
    return memory; \
  }
MAKE_ZIP(ZIP1_8B,  UReadV8,  UExtractV8,  UWriteV8,  uint8v8_t,   8,  0)
MAKE_ZIP(ZIP1_16B, UReadV8,  UExtractV8,  UWriteV8,  uint8v16_t,  16, 0)
MAKE_ZIP(ZIP1_4H,  UReadV16, UExtractV16, UWriteV16, uint16v4_t,  4,  0)
MAKE_ZIP(ZIP1_8H,  UReadV16, UExtractV16, UWriteV16, uint16v8_t,  8,  0)
MAKE_ZIP(ZIP1_2S,  UReadV32, UExtractV32, UWriteV32, uint32v2_t,  2,  0)
MAKE_ZIP(ZIP1_4S,  UReadV32, UExtractV32, UWriteV32, uint32v4_t,  4,  0)
MAKE_ZIP(ZIP1_2D,  UReadV64, UExtractV64, UWriteV64, uint64v2_t,  2,  0)
MAKE_ZIP(ZIP2_8B,  UReadV8,  UExtractV8,  UWriteV8,  uint8v8_t,   8,  1)
MAKE_ZIP(ZIP2_16B, UReadV8,  UExtractV8,  UWriteV8,  uint8v16_t,  16, 1)
MAKE_ZIP(ZIP2_4H,  UReadV16, UExtractV16, UWriteV16, uint16v4_t,  4,  1)
MAKE_ZIP(ZIP2_8H,  UReadV16, UExtractV16, UWriteV16, uint16v8_t,  8,  1)
MAKE_ZIP(ZIP2_2S,  UReadV32, UExtractV32, UWriteV32, uint32v2_t,  2,  1)
MAKE_ZIP(ZIP2_4S,  UReadV32, UExtractV32, UWriteV32, uint32v4_t,  4,  1)
MAKE_ZIP(ZIP2_2D,  UReadV64, UExtractV64, UWriteV64, uint64v2_t,  2,  1)
#undef MAKE_ZIP

// M12.7: XTN/XTN2 — extract narrow (truncate each element to half width). XTN (Q=0)
// writes the low half with the upper 64 bits zeroed; XTN2 (Q=1) writes the high half,
// preserving the low half from dst_in. Used by azul's vectorized box-prop comparison
// (UnresolvedBoxProps::resolve narrows a cmeq result before deinterleaving it).
#define MAKE_XTN(NAME, RDV, EXV, WRV, DV, NL) \
  DEF_SEM(NAME, V128W dst, V128 src) { \
    auto s = RDV(src); \
    DV res = {}; \
    _Pragma("unroll") for (size_t i = 0; i < (NL); ++i) { \
      res.elems[i] = EXV(s, i); /* narrowing assignment truncates */ \
    } \
    WRV(dst, res); \
    return memory; \
  }
MAKE_XTN(XTN_8B, UReadV16, UExtractV16, UWriteV8,  uint8v16_t, 8)
MAKE_XTN(XTN_4H, UReadV32, UExtractV32, UWriteV16, uint16v8_t, 4)
MAKE_XTN(XTN_2S, UReadV64, UExtractV64, UWriteV32, uint32v4_t, 2)

#define MAKE_XTN2(NAME, RDV_W, EXV_W, RDV_N, EXV_N, WRV, DV, NL) \
  DEF_SEM(NAME, V128W dst, V128 dst_in, V128 src) { \
    auto s = RDV_W(src); \
    auto d = RDV_N(dst_in); \
    DV res = {}; \
    _Pragma("unroll") for (size_t i = 0; i < (NL); ++i) { \
      res.elems[i] = EXV_N(d, i); \
      res.elems[i + (NL)] = EXV_W(s, i); /* narrowing assignment truncates */ \
    } \
    WRV(dst, res); \
    return memory; \
  }
MAKE_XTN2(XTN2_16B, UReadV16, UExtractV16, UReadV8,  UExtractV8,  UWriteV8,  uint8v16_t, 8)
MAKE_XTN2(XTN2_8H,  UReadV32, UExtractV32, UReadV16, UExtractV16, UWriteV16, uint16v8_t, 4)
MAKE_XTN2(XTN2_4S,  UReadV64, UExtractV64, UReadV32, UExtractV32, UWriteV32, uint32v4_t, 2)
#undef MAKE_XTN
#undef MAKE_XTN2

// M12.7: UZP1/UZP2 — unzip (deinterleave). UZP1 gathers the EVEN-indexed elements of
// {src1, src2}; UZP2 the ODD-indexed. result = [n[k], n[k+2], ...(half) , m[k], ...]
// where k = ODD (0 for UZP1, 1 for UZP2). Mirrors MAKE_ZIP above.
#define MAKE_UZP(NAME, RDV, EXV, WRV, DV, NL, ODD) \
  DEF_SEM(NAME, V128W dst, V128 src1, V128 src2) { \
    auto v1 = RDV(src1); \
    auto v2 = RDV(src2); \
    DV res = {}; \
    const size_t half = (NL) / 2; \
    _Pragma("unroll") for (size_t i = 0; i < half; ++i) { \
      res.elems[i] = EXV(v1, 2 * i + (ODD)); \
      res.elems[half + i] = EXV(v2, 2 * i + (ODD)); \
    } \
    WRV(dst, res); \
    return memory; \
  }
MAKE_UZP(UZP1_8B,  UReadV8,  UExtractV8,  UWriteV8,  uint8v8_t,   8,  0)
MAKE_UZP(UZP1_16B, UReadV8,  UExtractV8,  UWriteV8,  uint8v16_t,  16, 0)
MAKE_UZP(UZP1_4H,  UReadV16, UExtractV16, UWriteV16, uint16v4_t,  4,  0)
MAKE_UZP(UZP1_8H,  UReadV16, UExtractV16, UWriteV16, uint16v8_t,  8,  0)
MAKE_UZP(UZP1_2S,  UReadV32, UExtractV32, UWriteV32, uint32v2_t,  2,  0)
MAKE_UZP(UZP1_4S,  UReadV32, UExtractV32, UWriteV32, uint32v4_t,  4,  0)
MAKE_UZP(UZP1_2D,  UReadV64, UExtractV64, UWriteV64, uint64v2_t,  2,  0)
MAKE_UZP(UZP2_8B,  UReadV8,  UExtractV8,  UWriteV8,  uint8v8_t,   8,  1)
MAKE_UZP(UZP2_16B, UReadV8,  UExtractV8,  UWriteV8,  uint8v16_t,  16, 1)
MAKE_UZP(UZP2_4H,  UReadV16, UExtractV16, UWriteV16, uint16v4_t,  4,  1)
MAKE_UZP(UZP2_8H,  UReadV16, UExtractV16, UWriteV16, uint16v8_t,  8,  1)
MAKE_UZP(UZP2_2S,  UReadV32, UExtractV32, UWriteV32, uint32v2_t,  2,  1)
MAKE_UZP(UZP2_4S,  UReadV32, UExtractV32, UWriteV32, uint32v4_t,  4,  1)
MAKE_UZP(UZP2_2D,  UReadV64, UExtractV64, UWriteV64, uint64v2_t,  2,  1)
#undef MAKE_UZP

// M12.7: vector FP min/max (FMAX/FMIN/FMAXNM/FMINNM ASIMDSAME) — per-lane FloatMax/
// FloatMin (the helpers FMAXV/FMINV use). FMAXNM/FMINNM use the same op here (the
// NaN-propagation distinction doesn't matter for finite layout values). Used by the
// used-size computation (max(content,min-width), min(content,max-width)).
#define MAKE_FMINMAX(NAME, CMP, RDV, EXV, WRV, DV, NL) \
  DEF_SEM(NAME, V128W dst, V128 src1, V128 src2) { \
    auto v1 = RDV(src1); \
    auto v2 = RDV(src2); \
    DV res = {}; \
    _Pragma("unroll") for (size_t i = 0; i < (NL); ++i) { \
      auto a = EXV(v1, i); \
      auto b = EXV(v2, i); \
      res.elems[i] = (a CMP b) ? a : b; \
    } \
    WRV(dst, res); \
    return memory; \
  }
MAKE_FMINMAX(FMAX_VEC_2S, >, FReadV32, FExtractV32, FWriteV32, float32v2_t, 2)
MAKE_FMINMAX(FMAX_VEC_4S, >, FReadV32, FExtractV32, FWriteV32, float32v4_t, 4)
MAKE_FMINMAX(FMAX_VEC_2D, >, FReadV64, FExtractV64, FWriteV64, float64v2_t, 2)
MAKE_FMINMAX(FMIN_VEC_2S, <, FReadV32, FExtractV32, FWriteV32, float32v2_t, 2)
MAKE_FMINMAX(FMIN_VEC_4S, <, FReadV32, FExtractV32, FWriteV32, float32v4_t, 4)
MAKE_FMINMAX(FMIN_VEC_2D, <, FReadV64, FExtractV64, FWriteV64, float64v2_t, 2)
#undef MAKE_FMINMAX

// M12.7: vector float->uint convert toward zero (FCVTZU ASIMDMISC) — per-lane
// CheckedCast<float,uint>, mirror of SCVTF.
#define MAKE_FCVTZ_VEC(NAME, RDV, EXV, SRCT, DSTT, WRV, DV, NL) \
  DEF_SEM(NAME, V128W dst, V128 src) { \
    auto v = RDV(src); \
    DV res = {}; \
    _Pragma("unroll") for (size_t i = 0; i < (NL); ++i) { \
      res.elems[i] = CheckedCast<SRCT, DSTT>(state, EXV(v, i)); \
    } \
    WRV(dst, res); \
    return memory; \
  }
MAKE_FCVTZ_VEC(FCVTZU_VEC_2S, FReadV32, FExtractV32, float32_t, uint32_t, UWriteV32, uint32v2_t, 2)
MAKE_FCVTZ_VEC(FCVTZU_VEC_4S, FReadV32, FExtractV32, float32_t, uint32_t, UWriteV32, uint32v4_t, 4)
MAKE_FCVTZ_VEC(FCVTZU_VEC_2D, FReadV64, FExtractV64, float64_t, uint64_t, UWriteV64, uint64v2_t, 2)
// M12.7: signed variant (FCVTZS, float->signed int toward zero) — same macro, int dest.
MAKE_FCVTZ_VEC(FCVTZS_VEC_2S, FReadV32, FExtractV32, float32_t, int32_t, SWriteV32, int32v2_t, 2)
MAKE_FCVTZ_VEC(FCVTZS_VEC_4S, FReadV32, FExtractV32, float32_t, int32_t, SWriteV32, int32v4_t, 4)
MAKE_FCVTZ_VEC(FCVTZS_VEC_2D, FReadV64, FExtractV64, float64_t, int64_t, SWriteV64, int64v2_t, 2)
#undef MAKE_FCVTZ_VEC

// M12.7: vector FRINTA (round to nearest, ties away from zero) — float->float, per lane.
// Implemented as floor(x + copysign(0.5, x)) which is exactly round-half-away and lowers
// to wasm-native f32.floor / f32.copysign / f32.add (no libcall). Used by the layout's
// geometry->pixel quantization (LayoutNode::split: frinta -> fcvtzs -> sqxtn).
#define MAKE_FRINTA_VEC(NAME, RDV, EXV, T, WRV, DV, NL, HALF, FLR, CPS) \
  DEF_SEM(NAME, V128W dst, V128 src) { \
    auto v = RDV(src); \
    DV res = {}; \
    _Pragma("unroll") for (size_t i = 0; i < (NL); ++i) { \
      T x = EXV(v, i); \
      res.elems[i] = FLR(x + CPS(static_cast<T>(HALF), x)); \
    } \
    WRV(dst, res); \
    return memory; \
  }
MAKE_FRINTA_VEC(FRINTA_VEC_2S, FReadV32, FExtractV32, float32_t, FWriteV32, float32v2_t, 2, 0.5f, __builtin_floorf, __builtin_copysignf)
MAKE_FRINTA_VEC(FRINTA_VEC_4S, FReadV32, FExtractV32, float32_t, FWriteV32, float32v4_t, 4, 0.5f, __builtin_floorf, __builtin_copysignf)
MAKE_FRINTA_VEC(FRINTA_VEC_2D, FReadV64, FExtractV64, float64_t, FWriteV64, float64v2_t, 2, 0.5, __builtin_floor, __builtin_copysign)
#undef MAKE_FRINTA_VEC

// M12.7: SQXTN / SQXTN2 — signed saturating extract narrow, i32 -> i16 (the .4h/.8h forms
// the layout uses). SQXTN (.4h, Q=0) writes the low 64 bits and zeroes the upper; SQXTN2
// (.8h, Q=1) writes the upper 64 bits (lanes 4-7) preserving the lower. Saturate to i16.
ALWAYS_INLINE static int16_t AzSatI32ToI16(int32_t x) {
  return x > 32767 ? static_cast<int16_t>(32767)
                   : (x < -32768 ? static_cast<int16_t>(-32768)
                                 : static_cast<int16_t>(x));
}
DEF_SEM(SQXTN_4H, V128W dst, V128 src) {
  auto s = SReadV32(src);
  int16v8_t res = {};
  _Pragma("unroll") for (size_t i = 0; i < 4; ++i) {
    res.elems[i] = AzSatI32ToI16(SExtractV32(s, i));
  }
  SWriteV16(dst, res);
  return memory;
}
DEF_SEM(SQXTN_8H, V128W dst, V128 dst_in, V128 src) {
  auto s = SReadV32(src);
  auto d = SReadV16(dst_in);
  int16v8_t res = {};
  _Pragma("unroll") for (size_t i = 0; i < 4; ++i) {
    res.elems[i] = SExtractV16(d, i);
    res.elems[i + 4] = AzSatI32ToI16(SExtractV32(s, i));
  }
  SWriteV16(dst, res);
  return memory;
}

// M12.7: vector shift-right by immediate (SSHR signed-arith / USHR unsigned-logical,
// ASIMDSHF). The shift amount is decoded as an immediate operand.
#define MAKE_SHR(NAME, RDV, EXV, WRV, DV, NL) \
  DEF_SEM(NAME, V128W dst, V128 src, I64 shift) { \
    auto v = RDV(src); \
    auto s = Read(shift); \
    DV res = {}; \
    _Pragma("unroll") for (size_t i = 0; i < (NL); ++i) { \
      res.elems[i] = EXV(v, i) >> s; \
    } \
    WRV(dst, res); \
    return memory; \
  }
MAKE_SHR(SSHR_8B,  SReadV8,  SExtractV8,  SWriteV8,  int8v8_t,   8)
MAKE_SHR(SSHR_16B, SReadV8,  SExtractV8,  SWriteV8,  int8v16_t,  16)
MAKE_SHR(SSHR_4H,  SReadV16, SExtractV16, SWriteV16, int16v4_t,  4)
MAKE_SHR(SSHR_8H,  SReadV16, SExtractV16, SWriteV16, int16v8_t,  8)
MAKE_SHR(SSHR_2S,  SReadV32, SExtractV32, SWriteV32, int32v2_t,  2)
MAKE_SHR(SSHR_4S,  SReadV32, SExtractV32, SWriteV32, int32v4_t,  4)
MAKE_SHR(SSHR_2D,  SReadV64, SExtractV64, SWriteV64, int64v2_t,  2)
MAKE_SHR(USHR_8B,  UReadV8,  UExtractV8,  UWriteV8,  uint8v8_t,   8)
MAKE_SHR(USHR_16B, UReadV8,  UExtractV8,  UWriteV8,  uint8v16_t,  16)
MAKE_SHR(USHR_4H,  UReadV16, UExtractV16, UWriteV16, uint16v4_t,  4)
MAKE_SHR(USHR_8H,  UReadV16, UExtractV16, UWriteV16, uint16v8_t,  8)
MAKE_SHR(USHR_2S,  UReadV32, UExtractV32, UWriteV32, uint32v2_t,  2)
MAKE_SHR(USHR_4S,  UReadV32, UExtractV32, UWriteV32, uint32v4_t,  4)
MAKE_SHR(USHR_2D,  UReadV64, UExtractV64, UWriteV64, uint64v2_t,  2)
#undef MAKE_SHR

// M12.7: unsigned variable shift (USHL ASIMDSAME) — per-lane: the signed low byte of
// src2[i] gives a left shift (>=0) or logical right shift (<0).
#define MAKE_USHL(NAME, RDV, EXV, WRV, DV, ELEMT, NL) \
  DEF_SEM(NAME, V128W dst, V128 src1, V128 src2) { \
    auto v1 = RDV(src1); \
    auto v2 = RDV(src2); \
    DV res = {}; \
    _Pragma("unroll") for (size_t i = 0; i < (NL); ++i) { \
      int8_t sh = static_cast<int8_t>(EXV(v2, i) & 0xff); \
      ELEMT x = EXV(v1, i); \
      res.elems[i] = (sh >= 0) ? (sh >= static_cast<int8_t>(sizeof(ELEMT) * 8) ? 0 : (x << sh)) \
                               : ((-sh) >= static_cast<int8_t>(sizeof(ELEMT) * 8) ? 0 : (x >> (-sh))); \
    } \
    WRV(dst, res); \
    return memory; \
  }
MAKE_USHL(USHL_8B,  UReadV8,  UExtractV8,  UWriteV8,  uint8v8_t,   uint8_t,  8)
MAKE_USHL(USHL_16B, UReadV8,  UExtractV8,  UWriteV8,  uint8v16_t,  uint8_t,  16)
MAKE_USHL(USHL_4H,  UReadV16, UExtractV16, UWriteV16, uint16v4_t,  uint16_t, 4)
MAKE_USHL(USHL_8H,  UReadV16, UExtractV16, UWriteV16, uint16v8_t,  uint16_t, 8)
MAKE_USHL(USHL_2S,  UReadV32, UExtractV32, UWriteV32, uint32v2_t,  uint32_t, 2)
MAKE_USHL(USHL_4S,  UReadV32, UExtractV32, UWriteV32, uint32v4_t,  uint32_t, 4)
MAKE_USHL(USHL_2D,  UReadV64, UExtractV64, UWriteV64, uint64v2_t,  uint64_t, 2)
#undef MAKE_USHL

}  // namespace

DEF_ISEL(CMEQ_ASIMDMISC_Z_8B) = CMPEQ_IMM_8<V64, uint8v8_t>;
DEF_ISEL(CMLT_ASIMDMISC_Z_8B) = CMPLT_IMM_8<V64, uint8v8_t>;
DEF_ISEL(CMLE_ASIMDMISC_Z_8B) = CMPLE_IMM_8<V64, uint8v8_t>;
DEF_ISEL(CMGT_ASIMDMISC_Z_8B) = CMPGT_IMM_8<V64, uint8v8_t>;
DEF_ISEL(CMGE_ASIMDMISC_Z_8B) = CMPGE_IMM_8<V64, uint8v8_t>;

DEF_ISEL(CMEQ_ASIMDMISC_Z_16B) = CMPEQ_IMM_8<V128, uint8v16_t>;
DEF_ISEL(CMLT_ASIMDMISC_Z_16B) = CMPLT_IMM_8<V128, uint8v16_t>;
DEF_ISEL(CMLE_ASIMDMISC_Z_16B) = CMPLE_IMM_8<V128, uint8v16_t>;
DEF_ISEL(CMGT_ASIMDMISC_Z_16B) = CMPGT_IMM_8<V128, uint8v16_t>;
DEF_ISEL(CMGE_ASIMDMISC_Z_16B) = CMPGE_IMM_8<V128, uint8v16_t>;

// M12.7: SHL (ASIMD shift left by immediate), all arrangements.
DEF_ISEL(SHL_ASIMDSHF_R_8B) = SHL_IMM_8<V64, uint8v8_t>;
DEF_ISEL(SHL_ASIMDSHF_R_16B) = SHL_IMM_8<V128, uint8v16_t>;
DEF_ISEL(SHL_ASIMDSHF_R_4H) = SHL_IMM_16<V64, uint16v4_t>;
DEF_ISEL(SHL_ASIMDSHF_R_8H) = SHL_IMM_16<V128, uint16v8_t>;
DEF_ISEL(SHL_ASIMDSHF_R_2S) = SHL_IMM_32<V64, uint32v2_t>;
DEF_ISEL(SHL_ASIMDSHF_R_4S) = SHL_IMM_32<V128, uint32v4_t>;
DEF_ISEL(SHL_ASIMDSHF_R_2D) = SHL_IMM_64<V128, uint64v2_t>;

// M12.7: SSHLL/USHLL (shift-left-long), low half (L, Q=0) + high half ("2", Q=1).
DEF_ISEL(SSHLL_ASIMDSHF_L_8H) = SSHLL_8H;
DEF_ISEL(SSHLL_ASIMDSHF_L_4S) = SSHLL_4S;
DEF_ISEL(SSHLL_ASIMDSHF_L_2D) = SSHLL_2D;
DEF_ISEL(SSHLL_ASIMDSHF_L_8H_2) = SSHLL2_8H;
DEF_ISEL(SSHLL_ASIMDSHF_L_4S_2) = SSHLL2_4S;
DEF_ISEL(SSHLL_ASIMDSHF_L_2D_2) = SSHLL2_2D;
DEF_ISEL(USHLL_ASIMDSHF_L_8H) = USHLL_8H;
DEF_ISEL(USHLL_ASIMDSHF_L_4S) = USHLL_4S;
DEF_ISEL(USHLL_ASIMDSHF_L_2D) = USHLL_2D;
DEF_ISEL(USHLL_ASIMDSHF_L_8H_2) = USHLL2_8H;
DEF_ISEL(USHLL_ASIMDSHF_L_4S_2) = USHLL2_4S;
DEF_ISEL(USHLL_ASIMDSHF_L_2D_2) = USHLL2_2D;

// M12.7: vector FP arith (FADD/FSUB/FMUL/FDIV ASIMDSAME).
DEF_ISEL(FADD_ASIMDSAME_ONLY_2S) = FADD_VEC_2S;
DEF_ISEL(FADD_ASIMDSAME_ONLY_4S) = FADD_VEC_4S;
DEF_ISEL(FADD_ASIMDSAME_ONLY_2D) = FADD_VEC_2D;
DEF_ISEL(FSUB_ASIMDSAME_ONLY_2S) = FSUB_VEC_2S;
DEF_ISEL(FSUB_ASIMDSAME_ONLY_4S) = FSUB_VEC_4S;
DEF_ISEL(FSUB_ASIMDSAME_ONLY_2D) = FSUB_VEC_2D;
DEF_ISEL(FMUL_ASIMDSAME_ONLY_2S) = FMUL_VEC_2S;
DEF_ISEL(FMUL_ASIMDSAME_ONLY_4S) = FMUL_VEC_4S;
DEF_ISEL(FMUL_ASIMDSAME_ONLY_2D) = FMUL_VEC_2D;
DEF_ISEL(FDIV_ASIMDSAME_ONLY_2S) = FDIV_VEC_2S;
DEF_ISEL(FDIV_ASIMDSAME_ONLY_4S) = FDIV_VEC_4S;
DEF_ISEL(FDIV_ASIMDSAME_ONLY_2D) = FDIV_VEC_2D;

// M12.7: vector int->float convert (SCVTF/UCVTF ASIMDMISC).
DEF_ISEL(SCVTF_ASIMDMISC_R_2S) = SCVTF_VEC_2S;
DEF_ISEL(SCVTF_ASIMDMISC_R_4S) = SCVTF_VEC_4S;
DEF_ISEL(SCVTF_ASIMDMISC_R_2D) = SCVTF_VEC_2D;
DEF_ISEL(UCVTF_ASIMDMISC_R_2S) = UCVTF_VEC_2S;
DEF_ISEL(UCVTF_ASIMDMISC_R_4S) = UCVTF_VEC_4S;
DEF_ISEL(UCVTF_ASIMDMISC_R_2D) = UCVTF_VEC_2D;

// M12.7: DUP element (DUP_ASIMDINS_DV_V), all arrangements.
DEF_ISEL(DUP_ASIMDINS_DV_V_8B)  = DUP_ELT_8B;
DEF_ISEL(DUP_ASIMDINS_DV_V_16B) = DUP_ELT_16B;
DEF_ISEL(DUP_ASIMDINS_DV_V_4H)  = DUP_ELT_4H;
DEF_ISEL(DUP_ASIMDINS_DV_V_8H)  = DUP_ELT_8H;
DEF_ISEL(DUP_ASIMDINS_DV_V_2S)  = DUP_ELT_2S;
DEF_ISEL(DUP_ASIMDINS_DV_V_4S)  = DUP_ELT_4S;
DEF_ISEL(DUP_ASIMDINS_DV_V_2D)  = DUP_ELT_2D;

// M12.7: scalar DUP element (DUP_ASISDONE_ONLY) + its MOV alias — both decode to
// the appended size suffix (_B/_H/_S/_D) and share the DUP_SCALAR semantics.
DEF_ISEL(DUP_ASISDONE_ONLY_B) = DUP_SCALAR_B;
DEF_ISEL(DUP_ASISDONE_ONLY_H) = DUP_SCALAR_H;
DEF_ISEL(DUP_ASISDONE_ONLY_S) = DUP_SCALAR_S;
DEF_ISEL(DUP_ASISDONE_ONLY_D) = DUP_SCALAR_D;
DEF_ISEL(MOV_DUP_ASISDONE_ONLY_B) = DUP_SCALAR_B;
DEF_ISEL(MOV_DUP_ASISDONE_ONLY_H) = DUP_SCALAR_H;
DEF_ISEL(MOV_DUP_ASISDONE_ONLY_S) = DUP_SCALAR_S;
DEF_ISEL(MOV_DUP_ASISDONE_ONLY_D) = DUP_SCALAR_D;

// M12.7: ZIP1/ZIP2 (ASIMDPERM), all arrangements.
DEF_ISEL(ZIP1_ASIMDPERM_ONLY_8B)  = ZIP1_8B;
DEF_ISEL(ZIP1_ASIMDPERM_ONLY_16B) = ZIP1_16B;
DEF_ISEL(ZIP1_ASIMDPERM_ONLY_4H)  = ZIP1_4H;
DEF_ISEL(ZIP1_ASIMDPERM_ONLY_8H)  = ZIP1_8H;
DEF_ISEL(ZIP1_ASIMDPERM_ONLY_2S)  = ZIP1_2S;
DEF_ISEL(ZIP1_ASIMDPERM_ONLY_4S)  = ZIP1_4S;
DEF_ISEL(ZIP1_ASIMDPERM_ONLY_2D)  = ZIP1_2D;
DEF_ISEL(ZIP2_ASIMDPERM_ONLY_8B)  = ZIP2_8B;
DEF_ISEL(ZIP2_ASIMDPERM_ONLY_16B) = ZIP2_16B;
DEF_ISEL(ZIP2_ASIMDPERM_ONLY_4H)  = ZIP2_4H;
DEF_ISEL(ZIP2_ASIMDPERM_ONLY_8H)  = ZIP2_8H;
DEF_ISEL(ZIP2_ASIMDPERM_ONLY_2S)  = ZIP2_2S;
DEF_ISEL(ZIP2_ASIMDPERM_ONLY_4S)  = ZIP2_4S;
DEF_ISEL(ZIP2_ASIMDPERM_ONLY_2D)  = ZIP2_2D;

// M12.7: FCVTZS (vector float->signed int), FRINTA (vector round half-away), SQXTN/SQXTN2
// (signed saturating narrow i32->i16) — the layout's geometry->pixel quantization.
DEF_ISEL(FCVTZS_ASIMDMISC_R_2S) = FCVTZS_VEC_2S;
DEF_ISEL(FCVTZS_ASIMDMISC_R_4S) = FCVTZS_VEC_4S;
DEF_ISEL(FCVTZS_ASIMDMISC_R_2D) = FCVTZS_VEC_2D;
DEF_ISEL(FRINTA_ASIMDMISC_R_2S) = FRINTA_VEC_2S;
DEF_ISEL(FRINTA_ASIMDMISC_R_4S) = FRINTA_VEC_4S;
DEF_ISEL(FRINTA_ASIMDMISC_R_2D) = FRINTA_VEC_2D;
DEF_ISEL(SQXTN_ASIMDMISC_N_4H) = SQXTN_4H;
DEF_ISEL(SQXTN_ASIMDMISC_N_8H) = SQXTN_8H;

// M12.7: XTN/XTN2 (extract narrow) — Q=0 -> low half (_8B/_4H/_2S); Q=1 -> high half
// (_16B/_8H/_4S, XTN2). DEF_ISEL suffix is the DEST arrangement.
DEF_ISEL(XTN_ASIMDMISC_N_8B) = XTN_8B;
DEF_ISEL(XTN_ASIMDMISC_N_4H) = XTN_4H;
DEF_ISEL(XTN_ASIMDMISC_N_2S) = XTN_2S;
DEF_ISEL(XTN_ASIMDMISC_N_16B) = XTN2_16B;
DEF_ISEL(XTN_ASIMDMISC_N_8H) = XTN2_8H;
DEF_ISEL(XTN_ASIMDMISC_N_4S) = XTN2_4S;

// M12.7: UZP1/UZP2 (unzip / deinterleave even/odd).
DEF_ISEL(UZP1_ASIMDPERM_ONLY_8B)  = UZP1_8B;
DEF_ISEL(UZP1_ASIMDPERM_ONLY_16B) = UZP1_16B;
DEF_ISEL(UZP1_ASIMDPERM_ONLY_4H)  = UZP1_4H;
DEF_ISEL(UZP1_ASIMDPERM_ONLY_8H)  = UZP1_8H;
DEF_ISEL(UZP1_ASIMDPERM_ONLY_2S)  = UZP1_2S;
DEF_ISEL(UZP1_ASIMDPERM_ONLY_4S)  = UZP1_4S;
DEF_ISEL(UZP1_ASIMDPERM_ONLY_2D)  = UZP1_2D;
DEF_ISEL(UZP2_ASIMDPERM_ONLY_8B)  = UZP2_8B;
DEF_ISEL(UZP2_ASIMDPERM_ONLY_16B) = UZP2_16B;
DEF_ISEL(UZP2_ASIMDPERM_ONLY_4H)  = UZP2_4H;
DEF_ISEL(UZP2_ASIMDPERM_ONLY_8H)  = UZP2_8H;
DEF_ISEL(UZP2_ASIMDPERM_ONLY_2S)  = UZP2_2S;
DEF_ISEL(UZP2_ASIMDPERM_ONLY_4S)  = UZP2_4S;
DEF_ISEL(UZP2_ASIMDPERM_ONLY_2D)  = UZP2_2D;

// M12.7: vector FP min/max (FMAX/FMIN/FMAXNM/FMINNM ASIMDSAME). NM variants reuse the
// same per-lane FloatMax/FloatMin (NaN distinction irrelevant for finite layout values).
DEF_ISEL(FMAX_ASIMDSAME_ONLY_2S) = FMAX_VEC_2S;
DEF_ISEL(FMAX_ASIMDSAME_ONLY_4S) = FMAX_VEC_4S;
DEF_ISEL(FMAX_ASIMDSAME_ONLY_2D) = FMAX_VEC_2D;
DEF_ISEL(FMIN_ASIMDSAME_ONLY_2S) = FMIN_VEC_2S;
DEF_ISEL(FMIN_ASIMDSAME_ONLY_4S) = FMIN_VEC_4S;
DEF_ISEL(FMIN_ASIMDSAME_ONLY_2D) = FMIN_VEC_2D;
DEF_ISEL(FMAXNM_ASIMDSAME_ONLY_2S) = FMAX_VEC_2S;
DEF_ISEL(FMAXNM_ASIMDSAME_ONLY_4S) = FMAX_VEC_4S;
DEF_ISEL(FMAXNM_ASIMDSAME_ONLY_2D) = FMAX_VEC_2D;
DEF_ISEL(FMINNM_ASIMDSAME_ONLY_2S) = FMIN_VEC_2S;
DEF_ISEL(FMINNM_ASIMDSAME_ONLY_4S) = FMIN_VEC_4S;
DEF_ISEL(FMINNM_ASIMDSAME_ONLY_2D) = FMIN_VEC_2D;

// M12.7: FCVTZU (vector float->uint).
DEF_ISEL(FCVTZU_ASIMDMISC_R_2S) = FCVTZU_VEC_2S;
DEF_ISEL(FCVTZU_ASIMDMISC_R_4S) = FCVTZU_VEC_4S;
DEF_ISEL(FCVTZU_ASIMDMISC_R_2D) = FCVTZU_VEC_2D;

// M12.7: SSHR/USHR (vector shift-right by immediate).
DEF_ISEL(SSHR_ASIMDSHF_R_8B)  = SSHR_8B;
DEF_ISEL(SSHR_ASIMDSHF_R_16B) = SSHR_16B;
DEF_ISEL(SSHR_ASIMDSHF_R_4H)  = SSHR_4H;
DEF_ISEL(SSHR_ASIMDSHF_R_8H)  = SSHR_8H;
DEF_ISEL(SSHR_ASIMDSHF_R_2S)  = SSHR_2S;
DEF_ISEL(SSHR_ASIMDSHF_R_4S)  = SSHR_4S;
DEF_ISEL(SSHR_ASIMDSHF_R_2D)  = SSHR_2D;
DEF_ISEL(USHR_ASIMDSHF_R_8B)  = USHR_8B;
DEF_ISEL(USHR_ASIMDSHF_R_16B) = USHR_16B;
DEF_ISEL(USHR_ASIMDSHF_R_4H)  = USHR_4H;
DEF_ISEL(USHR_ASIMDSHF_R_8H)  = USHR_8H;
DEF_ISEL(USHR_ASIMDSHF_R_2S)  = USHR_2S;
DEF_ISEL(USHR_ASIMDSHF_R_4S)  = USHR_4S;
DEF_ISEL(USHR_ASIMDSHF_R_2D)  = USHR_2D;

// M12.7: USHL (vector unsigned variable shift).
DEF_ISEL(USHL_ASIMDSAME_ONLY_8B)  = USHL_8B;
DEF_ISEL(USHL_ASIMDSAME_ONLY_16B) = USHL_16B;
DEF_ISEL(USHL_ASIMDSAME_ONLY_4H)  = USHL_4H;
DEF_ISEL(USHL_ASIMDSAME_ONLY_8H)  = USHL_8H;
DEF_ISEL(USHL_ASIMDSAME_ONLY_2S)  = USHL_2S;
DEF_ISEL(USHL_ASIMDSAME_ONLY_4S)  = USHL_4S;
DEF_ISEL(USHL_ASIMDSAME_ONLY_2D)  = USHL_2D;

DEF_ISEL(CMEQ_ASIMDMISC_Z_4H) = CMPEQ_IMM_16<V64, uint16v4_t>;
DEF_ISEL(CMLT_ASIMDMISC_Z_4H) = CMPLT_IMM_16<V64, uint16v4_t>;
DEF_ISEL(CMLE_ASIMDMISC_Z_4H) = CMPLE_IMM_16<V64, uint16v4_t>;
DEF_ISEL(CMGT_ASIMDMISC_Z_4H) = CMPGT_IMM_16<V64, uint16v4_t>;
DEF_ISEL(CMGE_ASIMDMISC_Z_4H) = CMPGE_IMM_16<V64, uint16v4_t>;

DEF_ISEL(CMEQ_ASIMDMISC_Z_8H) = CMPEQ_IMM_16<V128, uint16v8_t>;
DEF_ISEL(CMLT_ASIMDMISC_Z_8H) = CMPLT_IMM_16<V128, uint16v8_t>;
DEF_ISEL(CMLE_ASIMDMISC_Z_8H) = CMPLE_IMM_16<V128, uint16v8_t>;
DEF_ISEL(CMGT_ASIMDMISC_Z_8H) = CMPGT_IMM_16<V128, uint16v8_t>;
DEF_ISEL(CMGE_ASIMDMISC_Z_8H) = CMPGE_IMM_16<V128, uint16v8_t>;

DEF_ISEL(CMEQ_ASIMDMISC_Z_2S) = CMPEQ_IMM_32<V64, uint32v2_t>;
DEF_ISEL(CMLT_ASIMDMISC_Z_2S) = CMPLT_IMM_32<V64, uint32v2_t>;
DEF_ISEL(CMLE_ASIMDMISC_Z_2S) = CMPLE_IMM_32<V64, uint32v2_t>;
DEF_ISEL(CMGT_ASIMDMISC_Z_2S) = CMPGT_IMM_32<V64, uint32v2_t>;
DEF_ISEL(CMGE_ASIMDMISC_Z_2S) = CMPGE_IMM_32<V64, uint32v2_t>;

DEF_ISEL(CMEQ_ASIMDMISC_Z_4S) = CMPEQ_IMM_32<V128, uint32v4_t>;
DEF_ISEL(CMLT_ASIMDMISC_Z_4S) = CMPLT_IMM_32<V128, uint32v4_t>;
DEF_ISEL(CMLE_ASIMDMISC_Z_4S) = CMPLE_IMM_32<V128, uint32v4_t>;
DEF_ISEL(CMGT_ASIMDMISC_Z_4S) = CMPGT_IMM_32<V128, uint32v4_t>;
DEF_ISEL(CMGE_ASIMDMISC_Z_4S) = CMPGE_IMM_32<V128, uint32v4_t>;

DEF_ISEL(CMEQ_ASIMDMISC_Z_1D) = CMPEQ_IMM_64<V64, uint64v1_t>;
DEF_ISEL(CMLT_ASIMDMISC_Z_1D) = CMPLT_IMM_64<V64, uint64v1_t>;
DEF_ISEL(CMLE_ASIMDMISC_Z_1D) = CMPLE_IMM_64<V64, uint64v1_t>;
DEF_ISEL(CMGT_ASIMDMISC_Z_1D) = CMPGT_IMM_64<V64, uint64v1_t>;
DEF_ISEL(CMGE_ASIMDMISC_Z_1D) = CMPGE_IMM_64<V64, uint64v1_t>;

DEF_ISEL(CMEQ_ASIMDMISC_Z_2D) = CMPEQ_IMM_64<V128, uint64v2_t>;
DEF_ISEL(CMLT_ASIMDMISC_Z_2D) = CMPLT_IMM_64<V128, uint64v2_t>;
DEF_ISEL(CMLE_ASIMDMISC_Z_2D) = CMPLE_IMM_64<V128, uint64v2_t>;
DEF_ISEL(CMGT_ASIMDMISC_Z_2D) = CMPGT_IMM_64<V128, uint64v2_t>;
DEF_ISEL(CMGE_ASIMDMISC_Z_2D) = CMPGE_IMM_64<V128, uint64v2_t>;

namespace {

#define MAKE_CMP_BROADCAST(op, prefix, binop, size) \
  template <typename S, typename V> \
  DEF_SEM(op##_##size, V128W dst, S src1, S src2) { \
    auto vec1 = prefix##ReadV##size(src1); \
    auto vec2 = prefix##ReadV##size(src2); \
    uint##size##_t zeros = 0; \
    uint##size##_t ones = ~zeros; \
    V res = {}; \
    _Pragma("unroll") for (size_t i = 0, max_i = NumVectorElems(res); \
                           i < max_i; ++i) { \
      res.elems[i] = Select(prefix##binop(prefix##ExtractV##size(vec1, i), \
                                          prefix##ExtractV##size(vec2, i)), \
                            ones, zeros); \
    } \
    UWriteV##size(dst, res); \
    return memory; \
  }

template <typename T>
ALWAYS_INLINE static bool UCmpTst(T lhs, T rhs) {
  return UCmpNeq(UAnd(lhs, rhs), T(0));
}

MAKE_CMP_BROADCAST(CMPEQ, S, CmpEq, 8)
MAKE_CMP_BROADCAST(CMPEQ, S, CmpEq, 16)
MAKE_CMP_BROADCAST(CMPEQ, S, CmpEq, 32)
MAKE_CMP_BROADCAST(CMPEQ, S, CmpEq, 64)

MAKE_CMP_BROADCAST(CMPTST, U, CmpTst, 8)
MAKE_CMP_BROADCAST(CMPTST, U, CmpTst, 16)
MAKE_CMP_BROADCAST(CMPTST, U, CmpTst, 32)
MAKE_CMP_BROADCAST(CMPTST, U, CmpTst, 64)

MAKE_CMP_BROADCAST(CMPGT, S, CmpGt, 8)
MAKE_CMP_BROADCAST(CMPGT, S, CmpGt, 16)
MAKE_CMP_BROADCAST(CMPGT, S, CmpGt, 32)
MAKE_CMP_BROADCAST(CMPGT, S, CmpGt, 64)

MAKE_CMP_BROADCAST(CMPGE, S, CmpGte, 8)
MAKE_CMP_BROADCAST(CMPGE, S, CmpGte, 16)
MAKE_CMP_BROADCAST(CMPGE, S, CmpGte, 32)
MAKE_CMP_BROADCAST(CMPGE, S, CmpGte, 64)

#undef MAKE_CMP_BROADCAST

}  // namespace

DEF_ISEL(CMEQ_ASIMDSAME_ONLY_8B) = CMPEQ_8<V64, uint8v8_t>;
DEF_ISEL(CMGT_ASIMDSAME_ONLY_8B) = CMPGT_8<V64, uint8v8_t>;
DEF_ISEL(CMGE_ASIMDSAME_ONLY_8B) = CMPGE_8<V64, uint8v8_t>;
DEF_ISEL(CMTST_ASIMDSAME_ONLY_8B) = CMPTST_8<V64, uint8v8_t>;

DEF_ISEL(CMEQ_ASIMDSAME_ONLY_16B) = CMPEQ_8<V128, uint8v16_t>;
DEF_ISEL(CMGT_ASIMDSAME_ONLY_16B) = CMPGT_8<V128, uint8v16_t>;
DEF_ISEL(CMGE_ASIMDSAME_ONLY_16B) = CMPGE_8<V128, uint8v16_t>;
DEF_ISEL(CMTST_ASIMDSAME_ONLY_16B) = CMPTST_8<V128, uint8v16_t>;

DEF_ISEL(CMEQ_ASIMDSAME_ONLY_4H) = CMPEQ_16<V64, uint16v4_t>;
DEF_ISEL(CMGT_ASIMDSAME_ONLY_4H) = CMPGT_16<V64, uint16v4_t>;
DEF_ISEL(CMGE_ASIMDSAME_ONLY_4H) = CMPGE_16<V64, uint16v4_t>;
DEF_ISEL(CMTST_ASIMDSAME_ONLY_4H) = CMPTST_16<V64, uint16v4_t>;

DEF_ISEL(CMEQ_ASIMDSAME_ONLY_8H) = CMPEQ_16<V128, uint16v8_t>;
DEF_ISEL(CMGT_ASIMDSAME_ONLY_8H) = CMPGT_16<V128, uint16v8_t>;
DEF_ISEL(CMGE_ASIMDSAME_ONLY_8H) = CMPGE_16<V128, uint16v8_t>;
DEF_ISEL(CMTST_ASIMDSAME_ONLY_8H) = CMPTST_16<V128, uint16v8_t>;

DEF_ISEL(CMEQ_ASIMDSAME_ONLY_2S) = CMPEQ_32<V64, uint32v2_t>;
DEF_ISEL(CMGT_ASIMDSAME_ONLY_2S) = CMPGT_32<V64, uint32v2_t>;
DEF_ISEL(CMGE_ASIMDSAME_ONLY_2S) = CMPGE_32<V64, uint32v2_t>;
DEF_ISEL(CMTST_ASIMDSAME_ONLY_2S) = CMPTST_32<V64, uint32v2_t>;

DEF_ISEL(CMEQ_ASIMDSAME_ONLY_4S) = CMPEQ_32<V128, uint32v4_t>;
DEF_ISEL(CMGT_ASIMDSAME_ONLY_4S) = CMPGT_32<V128, uint32v4_t>;
DEF_ISEL(CMGE_ASIMDSAME_ONLY_4S) = CMPGE_32<V128, uint32v4_t>;
DEF_ISEL(CMTST_ASIMDSAME_ONLY_4S) = CMPTST_32<V128, uint32v4_t>;

DEF_ISEL(CMEQ_ASIMDSAME_ONLY_2D) = CMPEQ_64<V128, uint64v2_t>;
DEF_ISEL(CMGT_ASIMDSAME_ONLY_2D) = CMPGT_64<V128, uint64v2_t>;
DEF_ISEL(CMGE_ASIMDSAME_ONLY_2D) = CMPGE_64<V128, uint64v2_t>;
DEF_ISEL(CMTST_ASIMDSAME_ONLY_2D) = CMPTST_64<V128, uint64v2_t>;

namespace {

#define MAKE_PAIRWAISE_BROADCAST(op, prefix, binop, size) \
  template <typename S, typename V> \
  DEF_SEM(op##_##size, V128W dst, S src1, S src2) { \
    auto vec1 = prefix##ReadV##size(src1); \
    auto vec2 = prefix##ReadV##size(src2); \
    V res = {}; \
    size_t max_i = NumVectorElems(res); \
    size_t j = 0; \
    _Pragma("unroll") for (size_t i = 0; i < max_i; i += 2) { \
      res.elems[j++] = prefix##binop(prefix##ExtractV##size(vec1, i), \
                                     prefix##ExtractV##size(vec1, i + 1)); \
    } \
    _Pragma("unroll") for (size_t i = 0; i < max_i; i += 2) { \
      res.elems[j++] = prefix##binop(prefix##ExtractV##size(vec2, i), \
                                     prefix##ExtractV##size(vec2, i + 1)); \
    } \
    prefix##WriteV##size(dst, res); \
    return memory; \
  }

MAKE_PAIRWAISE_BROADCAST(ADDP, U, Add, 8)
MAKE_PAIRWAISE_BROADCAST(ADDP, U, Add, 16)
MAKE_PAIRWAISE_BROADCAST(ADDP, U, Add, 32)
MAKE_PAIRWAISE_BROADCAST(ADDP, U, Add, 64)

MAKE_PAIRWAISE_BROADCAST(UMAXP, U, Max, 8)
MAKE_PAIRWAISE_BROADCAST(UMAXP, U, Max, 16)
MAKE_PAIRWAISE_BROADCAST(UMAXP, U, Max, 32)

MAKE_PAIRWAISE_BROADCAST(SMAXP, S, Max, 8)
MAKE_PAIRWAISE_BROADCAST(SMAXP, S, Max, 16)
MAKE_PAIRWAISE_BROADCAST(SMAXP, S, Max, 32)

MAKE_PAIRWAISE_BROADCAST(UMINP, U, Min, 8)
MAKE_PAIRWAISE_BROADCAST(UMINP, U, Min, 16)
MAKE_PAIRWAISE_BROADCAST(UMINP, U, Min, 32)

MAKE_PAIRWAISE_BROADCAST(SMINP, S, Min, 8)
MAKE_PAIRWAISE_BROADCAST(SMINP, S, Min, 16)
MAKE_PAIRWAISE_BROADCAST(SMINP, S, Min, 32)

#undef MAKE_PAIRWAISE_BROADCAST

}  // namespace

DEF_ISEL(ADDP_ASIMDSAME_ONLY_8B) = ADDP_8<V64, uint8v8_t>;
DEF_ISEL(ADDP_ASIMDSAME_ONLY_16B) = ADDP_8<V128, uint8v16_t>;
DEF_ISEL(ADDP_ASIMDSAME_ONLY_4H) = ADDP_16<V64, uint16v4_t>;
DEF_ISEL(ADDP_ASIMDSAME_ONLY_8H) = ADDP_16<V128, uint16v8_t>;
DEF_ISEL(ADDP_ASIMDSAME_ONLY_2S) = ADDP_32<V64, uint32v2_t>;
DEF_ISEL(ADDP_ASIMDSAME_ONLY_4S) = ADDP_32<V128, uint32v4_t>;
DEF_ISEL(ADDP_ASIMDSAME_ONLY_2D) = ADDP_64<V128, uint64v2_t>;

namespace {

// ADDP scalar-pair: sums the two 64-bit lanes of Vn.2D into Dd[0].
// Only the 2D arrangement is defined by ARM. The destination is a
// scalar D register (lower 64 bits of a V-reg, upper 64 bits zero).
DEF_SEM(ADDP_PAIR_64, V128W dst, V128 src) {
  auto vec = UReadV64(src);
  auto lane0 = UExtractV64(vec, 0);
  auto lane1 = UExtractV64(vec, 1);
  auto sum = UAdd(lane0, lane1);
  auto out = UClearV64(UReadV64(dst));
  out = UInsertV64(out, 0, sum);
  UWriteV64(dst, out);
  return memory;
}

}  // namespace

DEF_ISEL(ADDP_ASISDPAIR_ONLY_2D) = ADDP_PAIR_64;

DEF_ISEL(UMINP_ASIMDSAME_ONLY_8B) = UMINP_8<V64, uint8v8_t>;
DEF_ISEL(UMINP_ASIMDSAME_ONLY_16B) = UMINP_8<V128, uint8v16_t>;
DEF_ISEL(UMINP_ASIMDSAME_ONLY_4H) = UMINP_16<V64, uint16v4_t>;
DEF_ISEL(UMINP_ASIMDSAME_ONLY_8H) = UMINP_16<V128, uint16v8_t>;
DEF_ISEL(UMINP_ASIMDSAME_ONLY_2S) = UMINP_32<V64, uint32v2_t>;
DEF_ISEL(UMINP_ASIMDSAME_ONLY_4S) = UMINP_32<V128, uint32v4_t>;

DEF_ISEL(UMAXP_ASIMDSAME_ONLY_8B) = UMAXP_8<V64, uint8v8_t>;
DEF_ISEL(UMAXP_ASIMDSAME_ONLY_16B) = UMAXP_8<V128, uint8v16_t>;
DEF_ISEL(UMAXP_ASIMDSAME_ONLY_4H) = UMAXP_16<V64, uint16v4_t>;
DEF_ISEL(UMAXP_ASIMDSAME_ONLY_8H) = UMAXP_16<V128, uint16v8_t>;
DEF_ISEL(UMAXP_ASIMDSAME_ONLY_2S) = UMAXP_32<V64, uint32v2_t>;
DEF_ISEL(UMAXP_ASIMDSAME_ONLY_4S) = UMAXP_32<V128, uint32v4_t>;

DEF_ISEL(SMINP_ASIMDSAME_ONLY_8B) = SMINP_8<V64, int8v8_t>;
DEF_ISEL(SMINP_ASIMDSAME_ONLY_16B) = SMINP_8<V128, int8v16_t>;
DEF_ISEL(SMINP_ASIMDSAME_ONLY_4H) = SMINP_16<V64, int16v4_t>;
DEF_ISEL(SMINP_ASIMDSAME_ONLY_8H) = SMINP_16<V128, int16v8_t>;
DEF_ISEL(SMINP_ASIMDSAME_ONLY_2S) = SMINP_32<V64, int32v2_t>;
DEF_ISEL(SMINP_ASIMDSAME_ONLY_4S) = SMINP_32<V128, int32v4_t>;

DEF_ISEL(SMAXP_ASIMDSAME_ONLY_8B) = SMAXP_8<V64, int8v8_t>;
DEF_ISEL(SMAXP_ASIMDSAME_ONLY_16B) = SMAXP_8<V128, int8v16_t>;
DEF_ISEL(SMAXP_ASIMDSAME_ONLY_4H) = SMAXP_16<V64, int16v4_t>;
DEF_ISEL(SMAXP_ASIMDSAME_ONLY_8H) = SMAXP_16<V128, int16v8_t>;
DEF_ISEL(SMAXP_ASIMDSAME_ONLY_2S) = SMAXP_32<V64, int32v2_t>;
DEF_ISEL(SMAXP_ASIMDSAME_ONLY_4S) = SMAXP_32<V128, int32v4_t>;

namespace {

template <typename V, typename B>
ALWAYS_INLINE static auto Reduce2(const V &vec, B binop, size_t base = 0)
    -> decltype(binop(vec.elems[0], vec.elems[1])) {
  return binop(vec.elems[base + 0], vec.elems[base + 1]);
}

template <typename V, typename B>
ALWAYS_INLINE static auto Reduce4(const V &vec, B binop, size_t base = 0)
    -> decltype(binop(vec.elems[0], vec.elems[1])) {
  auto lo = Reduce2(vec, binop, base + 0);
  auto hi = Reduce2(vec, binop, base + 2);
  return binop(lo, hi);
}

template <typename V, typename B>
ALWAYS_INLINE static auto Reduce8(const V &vec, B binop, size_t base = 0)
    -> decltype(binop(vec.elems[0], vec.elems[1])) {
  auto lo = Reduce4(vec, binop, base + 0);
  auto hi = Reduce4(vec, binop, base + 4);
  return binop(lo, hi);
}

template <typename V, typename B>
ALWAYS_INLINE static auto Reduce16(const V &vec, B binop, size_t base = 0)
    -> decltype(binop(vec.elems[0], vec.elems[1])) {
  auto lo = Reduce8(vec, binop, base + 0);
  auto hi = Reduce8(vec, binop, base + 8);
  return binop(lo, hi);
}

template <typename V, typename B>
ALWAYS_INLINE static auto Reduce(const V &vec, B binop)
    -> decltype(Reduce2(vec, binop)) {
  switch (NumVectorElems(vec)) {
    case 2: return Reduce2(vec, binop);
    case 4: return Reduce4(vec, binop);
    case 8: return Reduce8(vec, binop);
    case 16: return Reduce16(vec, binop);
    default: __builtin_unreachable();
  }
}

template <typename S>
DEF_SEM(ADDV_8_Reduce, V128W dst, S src) {
  auto vec = SReadV8(src);
  UWriteV8(dst, Unsigned(Reduce(vec, SAdd8)));
  return memory;
}

template <typename S>
DEF_SEM(ADDV_16_Reduce, V128W dst, S src) {
  auto vec = SReadV16(src);
  UWriteV16(dst, Unsigned(Reduce(vec, SAdd16)));
  return memory;
}

template <typename S>
DEF_SEM(ADDV_32_Reduce, V128W dst, S src) {
  auto vec = SReadV32(src);
  UWriteV32(dst, Unsigned(Reduce(vec, SAdd32)));
  return memory;
}

template <typename S>
DEF_SEM(UMINV_8, V128W dst, S src) {
  auto vec = UReadV8(src);
  auto val = std::numeric_limits<uint8_t>::max();
  _Pragma("unroll") for (auto elem : vec.elems) {
    val = UMin(elem, val);
  }
  UWriteV8(dst, val);
  return memory;
}

template <typename S>
DEF_SEM(UMINV_16, V128W dst, S src) {
  auto vec = UReadV16(src);
  auto val = std::numeric_limits<uint16_t>::max();
  _Pragma("unroll") for (auto elem : vec.elems) {
    val = UMin(elem, val);
  }
  UWriteV16(dst, val);
  return memory;
}

template <typename S>
DEF_SEM(UMINV_32, V128W dst, S src) {
  auto vec = UReadV32(src);
  auto val = std::numeric_limits<uint32_t>::max();
  _Pragma("unroll") for (auto elem : vec.elems) {
    val = UMin(elem, val);
  }
  UWriteV32(dst, val);
  return memory;
}

template <typename S>
DEF_SEM(SMINV_8, V128W dst, S src) {
  auto vec = SReadV8(src);
  auto val = std::numeric_limits<int8_t>::max();
  _Pragma("unroll") for (auto elem : vec.elems) {
    val = SMin(elem, val);
  }
  SWriteV8(dst, val);
  return memory;
}

template <typename S>
DEF_SEM(SMINV_16, V128W dst, S src) {
  auto vec = SReadV16(src);
  auto val = std::numeric_limits<int16_t>::max();
  _Pragma("unroll") for (auto elem : vec.elems) {
    val = SMin(elem, val);
  }
  SWriteV16(dst, val);
  return memory;
}

template <typename S>
DEF_SEM(SMINV_32, V128W dst, S src) {
  auto vec = SReadV32(src);
  auto val = std::numeric_limits<int32_t>::max();
  _Pragma("unroll") for (auto elem : vec.elems) {
    val = SMin(elem, val);
  }
  SWriteV32(dst, val);
  return memory;
}

template <typename S>
DEF_SEM(UMAXV_8, V128W dst, S src) {
  auto vec = UReadV8(src);
  auto val = std::numeric_limits<uint8_t>::min();
  _Pragma("unroll") for (auto elem : vec.elems) {
    val = UMax(elem, val);
  }
  UWriteV8(dst, val);
  return memory;
}

template <typename S>
DEF_SEM(UMAXV_16, V128W dst, S src) {
  auto vec = UReadV16(src);
  auto val = std::numeric_limits<uint16_t>::min();
  _Pragma("unroll") for (auto elem : vec.elems) {
    val = UMax(elem, val);
  }
  UWriteV16(dst, val);
  return memory;
}

template <typename S>
DEF_SEM(UMAXV_32, V128W dst, S src) {
  auto vec = UReadV32(src);
  auto val = std::numeric_limits<uint32_t>::min();
  _Pragma("unroll") for (auto elem : vec.elems) {
    val = UMax(elem, val);
  }
  UWriteV32(dst, val);
  return memory;
}

template <typename S>
DEF_SEM(SMAXV_8, V128W dst, S src) {
  auto vec = SReadV8(src);
  auto val = std::numeric_limits<int8_t>::min();
  _Pragma("unroll") for (auto elem : vec.elems) {
    val = SMax(elem, val);
  }
  SWriteV8(dst, val);
  return memory;
}

template <typename S>
DEF_SEM(SMAXV_16, V128W dst, S src) {
  auto vec = SReadV16(src);
  auto val = std::numeric_limits<int16_t>::min();
  _Pragma("unroll") for (auto elem : vec.elems) {
    val = SMax(elem, val);
  }
  SWriteV16(dst, val);
  return memory;
}

template <typename S>
DEF_SEM(SMAXV_32, V128W dst, S src) {
  auto vec = SReadV32(src);
  auto val = std::numeric_limits<int32_t>::min();
  _Pragma("unroll") for (auto elem : vec.elems) {
    val = SMax(elem, val);
  }
  SWriteV32(dst, val);
  return memory;
}

}  // namespace

DEF_ISEL(ADDV_ASIMDALL_ONLY_8B) = ADDV_8_Reduce<V64>;
DEF_ISEL(ADDV_ASIMDALL_ONLY_16B) = ADDV_8_Reduce<V128>;
DEF_ISEL(ADDV_ASIMDALL_ONLY_4H) = ADDV_16_Reduce<V64>;
DEF_ISEL(ADDV_ASIMDALL_ONLY_8H) = ADDV_16_Reduce<V128>;
DEF_ISEL(ADDV_ASIMDALL_ONLY_4S) = ADDV_32_Reduce<V128>;

DEF_ISEL(UMINV_ASIMDALL_ONLY_8B) = UMINV_8<V64>;
DEF_ISEL(UMINV_ASIMDALL_ONLY_16B) = UMINV_8<V128>;
DEF_ISEL(UMINV_ASIMDALL_ONLY_4H) = UMINV_16<V64>;
DEF_ISEL(UMINV_ASIMDALL_ONLY_8H) = UMINV_16<V128>;
DEF_ISEL(UMINV_ASIMDALL_ONLY_4S) = UMINV_32<V128>;

DEF_ISEL(SMINV_ASIMDALL_ONLY_8B) = SMINV_8<V64>;
DEF_ISEL(SMINV_ASIMDALL_ONLY_16B) = SMINV_8<V128>;
DEF_ISEL(SMINV_ASIMDALL_ONLY_4H) = SMINV_16<V64>;
DEF_ISEL(SMINV_ASIMDALL_ONLY_8H) = SMINV_16<V128>;
DEF_ISEL(SMINV_ASIMDALL_ONLY_4S) = SMINV_32<V128>;

DEF_ISEL(UMAXV_ASIMDALL_ONLY_8B) = UMAXV_8<V64>;
DEF_ISEL(UMAXV_ASIMDALL_ONLY_16B) = UMAXV_8<V128>;
DEF_ISEL(UMAXV_ASIMDALL_ONLY_4H) = UMAXV_16<V64>;
DEF_ISEL(UMAXV_ASIMDALL_ONLY_8H) = UMAXV_16<V128>;
DEF_ISEL(UMAXV_ASIMDALL_ONLY_4S) = UMAXV_32<V128>;

DEF_ISEL(SMAXV_ASIMDALL_ONLY_8B) = SMAXV_8<V64>;
DEF_ISEL(SMAXV_ASIMDALL_ONLY_16B) = SMAXV_8<V128>;
DEF_ISEL(SMAXV_ASIMDALL_ONLY_4H) = SMAXV_16<V64>;
DEF_ISEL(SMAXV_ASIMDALL_ONLY_8H) = SMAXV_16<V128>;
DEF_ISEL(SMAXV_ASIMDALL_ONLY_4S) = SMAXV_32<V128>;

namespace {

template <typename T, typename I>
ALWAYS_INLINE static T FloatMin(T lhs, T rhs) {
  if (__builtin_isunordered(lhs, rhs)) {
    return NAN;
  } else if (__builtin_isless(lhs, rhs)) {
    return lhs;
  } else {
    return rhs;
  }

  //  if (lhs < rhs) {
  //    return lhs;
  //
  //  } else if (lhs > rhs) {
  //    return rhs;
  //
  //  // Use integer comparisons; we need to return the "most negative" value
  //  // (e.g. in the case of +0 and -0).
  //  } else {
  //    auto a = reinterpret_cast<I &>(lhs);
  //    auto b = reinterpret_cast<I &>(rhs);
  //    auto res = SMin(a, b);
  //    return reinterpret_cast<T &>(res);
  //  }
}

template <typename T, typename I>
ALWAYS_INLINE static T FloatMax(T lhs, T rhs) {
  if (__builtin_isunordered(lhs, rhs)) {
    return NAN;
  } else if (__builtin_isgreater(lhs, rhs)) {
    return lhs;
  } else {
    return rhs;
  }
  //
  //  if (lhs < rhs) {
  //    return rhs;
  //
  //  } else if (lhs > rhs) {
  //    return lhs;
  //
  //  // Use integer comparisons; we need to return the "most negative" value
  //  // (e.g. in the case of +0 and -0).
  //  } else {
  //    auto a = reinterpret_cast<I &>(lhs);
  //    auto b = reinterpret_cast<I &>(rhs);
  //    auto res = SMax(a, b);
  //    return reinterpret_cast<T &>(res);
  //  }
}

// NOTE(pag): These aren't quite right w.r.t. NaN propagation.
DEF_SEM(FMINV_32_Reduce, V128W dst, V128 src) {
  auto vec = FReadV32(src);
  FWriteV32(dst, Reduce4(vec, FloatMin<float32_t, int32_t>));
  return memory;
}

DEF_SEM(FMAXV_32_Reduce, V128W dst, V128 src) {
  auto vec = FReadV32(src);
  FWriteV32(dst, Reduce4(vec, FloatMax<float32_t, int32_t>));
  return memory;
}

}  // namespace

DEF_ISEL(FMINV_ASIMDALL_ONLY_SD_4S) = FMINV_32_Reduce;
DEF_ISEL(FMAXV_ASIMDALL_ONLY_SD_4S) = FMAXV_32_Reduce;

namespace {

template <typename S>
DEF_SEM(NOT_8, V128W dst, S src) {
  auto vec = UReadV8(src);
  auto res = UNotV8(vec);
  UWriteV8(dst, res);
  return memory;
}

}  // namespace

DEF_ISEL(NOT_ASIMDMISC_R_8B) = NOT_8<V64>;
DEF_ISEL(NOT_ASIMDMISC_R_16B) = NOT_8<V128>;

namespace {

template <typename T, size_t count>
DEF_SEM(EXT, V128W dst, T src1, T src2, I32 src3) {
  auto lsb = Read(src3);
  auto vn = UReadV8(src1);
  auto vm = UReadV8(src2);
  uint8v16_t result = {};
  _Pragma("unroll") for (size_t i = 0, max_i = count; i + lsb < max_i; ++i) {
    result.elems[count - 1 - i] = UExtractV8(vm, i + lsb);
  }
  _Pragma("unroll") for (size_t i = lsb; i < count; ++i) {
    result.elems[count - 1 - i] = UExtractV8(vn, i - lsb);
  }
  UWriteV8(dst, result);
  return memory;
}

}  //  namespace

DEF_ISEL(EXT_ASIMDEXT_ONLY_8B) = EXT<V64, 8>;
DEF_ISEL(EXT_ASIMDEXT_ONLY_16B) = EXT<V128, 16>;


// TODO(pag):
// FMINV_ASIMDALL_ONLY_H
// FMAXV_ASIMDALL_ONLY_H
// FMINNMV_ASIMDALL_ONLY_H
// FMINNMV_ASIMDALL_ONLY_SD
// FMAXNMV_ASIMDALL_ONLY_H
// FMAXNMV_ASIMDALL_ONLY_SD

DEF_SEM(USHR_64B, V128W dst, V128W src, I64 shift) {
  auto vec = UExtractV64(UReadV64(src), 0);
  auto sft = Read(shift);
  auto shifted = UShr128(vec, sft);
  uint64v2_t temp_vec = {};
  temp_vec = UInsertV64(temp_vec, 1, 0);
  temp_vec = UInsertV64(temp_vec, 0, (uint64_t) shifted);
  UWriteV64(dst, temp_vec);
  return memory;
}

DEF_ISEL(USHR_ASISDSHF_R) = USHR_64B;
