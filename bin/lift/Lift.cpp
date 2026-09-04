/*
 * Copyright (c) 2019 Trail of Bits, Inc.
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

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <remill/Arch/Arch.h>
#include <remill/Arch/Instruction.h>
#include <remill/Arch/Name.h>
#include <remill/BC/ABI.h>
#include <remill/BC/IntrinsicTable.h>
#include <remill/BC/Lifter.h>
#include <remill/BC/Optimizer.h>
#include <remill/BC/Util.h>
#include <remill/BC/Version.h>
#include <remill/OS/OS.h>
#include <remill/Version/Version.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <set>
#include <unordered_set>

DEFINE_string(os, REMILL_OS,
              "Operating system name of the code being "
              "translated. Valid OSes: linux, macos, windows, solaris.");
DEFINE_string(arch, "",
              "Architecture of the code being translated. "
              "Valid architectures: x86, amd64 (with or without "
              "`_avx` or `_avx512` appended), aarch64, aarch32");

DEFINE_uint64(address, -1,
              "Address at which we should assume the bytes are "
              "located in virtual memory.");

DEFINE_uint64(entry_address, -1,
              "Address of instruction that should be "
              "considered the entrypoint of this code. "
              "Defaults to the value of -address.");

DEFINE_string(bytes, "", "Hex-encoded byte string to lift.");

DEFINE_string(
    ir_pre_out, "",
    "Path to the file where the LLVM IR (before optimization) should be saved");

DEFINE_string(ir_out, "", "Path to file where the LLVM IR should be saved.");
DEFINE_string(bc_out, "",
              "Path to file where the LLVM bitcode should be "
              "saved.");

DEFINE_string(signature, "", "Function signature \"reg_out(reg_in,...)\"");
DEFINE_bool(mute_state_escape, false, "Mute state escape");
DEFINE_bool(symbolic_regs, false, "Set registers to a symbolic value");

// M12.7: extra non-contiguous memory regions for the lifter (e.g. jump-table
// .rodata), so ForEachDevirtualizedTarget can read EXACT jump-table targets
// instead of over-sweeping a window. Format: "<addrhex>:<bytehex>;<addrhex>:<bytehex>".
DEFINE_string(extra_data, "", "Extra memory regions: <addrhex>:<bytehex>;...");

// Lift many functions in ONE process. Each line: "<entry_hex> <ir_out>
// <bytes_hex> [<extra_data>|-]".
DEFINE_string(batch_manifest, "",
              "Path to a batch manifest; lifts every entry in one process.");

using Memory = std::map<uint64_t, uint8_t>;

// Unhexlify the data passed to `-bytes`, and fill in `memory` with each
// such byte.
static Memory UnhexlifyInputBytes(uint64_t addr_mask) {
  Memory memory;

  for (size_t i = 0; i < FLAGS_bytes.size(); i += 2) {
    char nibbles[] = {FLAGS_bytes[i], FLAGS_bytes[i + 1], '\0'};
    char *parsed_to = nullptr;
    auto byte_val = strtol(nibbles, &parsed_to, 16);

    if (parsed_to != &(nibbles[2])) {
      std::cerr << "Invalid hex byte value '" << nibbles
                << "' specified in -bytes." << std::endl;
      exit(EXIT_FAILURE);
    }

    auto byte_addr = FLAGS_address + (i / 2);
    auto masked_addr = byte_addr & addr_mask;

    // Make sure that if a really big number is specified for `-address`,
    // that we don't accidentally wrap around and start filling out low
    // byte addresses.
    if (masked_addr < byte_addr) {
      std::cerr
          << "Too many bytes specified to -bytes, would result in a 32-bit overflow.";
      exit(EXIT_FAILURE);

    } else if (masked_addr < FLAGS_address) {
      std::cerr
          << "Too many bytes specified to -bytes, would result in a 64-bit overflow.";
      exit(EXIT_FAILURE);
    }

    memory[byte_addr] = static_cast<uint8_t>(byte_val);
  }

  return memory;
}

struct SimpleTraceManager : remill::TraceManager {
  const remill::Arch *arch = nullptr;
  llvm::Module *module = nullptr;
  Memory &memory;
  uint64_t entry = 0;
  std::unordered_map<uint64_t, llvm::Function *> traces;

  SimpleTraceManager(const remill::Arch *arch, llvm::Module *module,
                     Memory &memory, uint64_t entry)
      : arch(arch),
        module(module),
        memory(memory),
        entry(entry) {}

  // Valid instruction-BOUNDARY set for a contiguous code block, built by a
  // linear decode from `lo`. Cached because a function with N jump tables would
  // otherwise re-decode its whole body N times.
  //
  // Needed because the x86 jump-table reader below cannot know a table's LENGTH
  // from the table itself, and MSVC/LLVM emit the per-switch tables BACK TO BACK
  // in .rdata. Reading past table T therefore walks into table T+1, whose entries
  // are offsets relative to T+1's base — evaluated against T's base they land a
  // constant distance off, i.e. MID-INSTRUCTION. Emitting such an address as a
  // devirtualized target makes the lifter decode garbage there (e.g. the second
  // byte of `f3 48 0f 2a ..` cvtsi2ss decodes as the MMX CVTPI2PS) and abort with
  // "Expected XMM7 to be an integral type". A real arm target is always an
  // instruction start, so the first non-boundary entry marks the end of the table.
  uint64_t bnd_lo = 1, bnd_hi = 0;
  std::set<uint64_t> bnd;

  const std::set<uint64_t> &InstBoundaries(uint64_t lo, uint64_t hi) {
    if (lo == bnd_lo && hi == bnd_hi) {
      return bnd;
    }
    bnd.clear();
    bnd_lo = lo;
    bnd_hi = hi;
    std::string buf;
    remill::Instruction inst;
    for (uint64_t a = lo; a <= hi;) {
      buf.clear();
      for (uint64_t k = 0; k < 16 && a + k <= hi; k++) {
        auto it = memory.find(a + k);
        if (it == memory.end()) {
          break;
        }
        buf.push_back(static_cast<char>(it->second));
      }
      if (buf.empty()) {
        break;
      }
      inst.Reset();
      if (!arch->DecodeInstruction(a, buf, inst, arch->CreateInitialContext()) ||
          !inst.NumBytes()) {
        a++;  // undecodable (padding / data): resync a byte at a time
        continue;
      }
      bnd.insert(a);
      a += inst.NumBytes();
    }
    return bnd;
  }

  // M12.7: jump-table devirtualization for `br Xn` (a `match` lowered to a
  // PC-relative jump table). The arm targets are intra-fn instructions; the
  // lifted IR computes the target correctly but `br Xn` would otherwise become
  // the no-op __remill_jump. Provide every 4-byte-aligned address in the
  // lifted fn's byte range as a candidate; TraceLifter switches the computed
  // target PC over them. Jumps only (indirect CALLs go to other fns).
  void ForEachDevirtualizedTarget(
      const remill::Instruction &inst,
      std::function<void(uint64_t, remill::DevirtualizedTargetKind)> func)
      override {
    if (inst.category != remill::Instruction::kCategoryIndirectJump ||
        memory.empty() || inst.pc < 4) {
      return;
    }
    // x86/AMD64: the AArch64 pattern-detector below reads the bytes preceding
    // the indirect jump as 4-byte ARM words. On x86 they're variable-length x86
    // instructions: the ARM bit-checks FALSE-POSITIVE, the exact decode fails,
    // and the fallback WINDOW SWEEP (further down) then emits hundreds of
    // 4-byte-aligned addresses as switch targets — many land MID x86 instruction,
    // so the lifter decodes bogus instructions (e.g. resolve_font_size_slow's
    // cvtsi2ss arm opcode bytes `0f 2a c0` as CVTPI2PS) and aborts in
    // InstructionLifter ("Expected XMM to be an integral type [16 x i8] vs i64").
    // Handle the x86 compiler jump-table idiom explicitly and RETURN (never reach
    // the ARM path / sweep). LLVM lowers a dense `match` to:
    //   lea disp32(%rip),%Rb ; movslq (%Rb,%Ri,4),%Rt ; add %Rb,%Rt ; jmp *%Rt
    // The i32 offset table lives in .rodata (provided via --extra_data); table
    // base = (jmp_pc - 7) + disp32, target[i] = base + (i32)tbl[i]. Emit only the
    // in-function targets; the first off-function entry ends the table. No idiom /
    // no table => emit nothing => the caller falls back to __remill_jump (the
    // host indirect-dispatch path every other x86 indirect jump already uses).
    if (arch && (arch->IsAMD64() || arch->IsX86())) {
      auto rdb = [&](uint64_t a, uint8_t &v) -> bool {
        auto it = memory.find(a);
        if (it == memory.end()) return false;
        v = it->second;
        return true;
      };
      // 2026-08-14: this used to require the idiom at FIXED byte offsets
      // (lea@-14, movslq@-7, add@-3). Real LLVM output breaks that in two ways,
      // and 18 of the 23 jump tables in ONE function (build_compact_cache_with_
      // inheritance) failed to match — each silently becoming __remill_jump ->
      // missing_block, which RETURNS, so the whole `match` body was skipped and
      // struct fields were left unwritten:
      //   (a) the `lea` is HOISTED — the table base is materialised once into a
      //       callee-saved reg (r14/rbp) and reused by several tables, so there
      //       is no lea directly before the jmp at all;
      //   (b) `movslq` is 5 bytes, not 4, when the base reg is rbp/r13 (mod=01
      //       forces a disp8), which shifts every fixed offset.
      // So walk REAL instruction boundaries backwards instead of guessing sizes.
      uint64_t clo = inst.pc, chi = inst.pc, guard = 0;
      while (memory.count(clo - 1) && ++guard < (1u << 18)) clo--;
      guard = 0;
      while (memory.count(chi + 1) && ++guard < (1u << 18)) chi++;
      const std::set<uint64_t> &bounds = InstBoundaries(clo, chi);
      auto bit = bounds.find(inst.pc);
      bool ok = bit != bounds.end();
      uint8_t b;
      int rt = -1, rb = -1;  // jump-target reg, table-base reg
      uint64_t p_add = 0, p_mov = 0;
      // [FIX] The machine scheduler SINKS unrelated instructions between the
      // idiom's pieces (parse_css_color: four arm-common `xor %r,%r` zero-inits
      // sit between the `add` and the `jmp`), so "immediately preceding" match
      // offsets miss real tables — the third adjacency break in this matcher's
      // history (fixed offsets, hoisted lea, 5-byte movslq). Anchor on the
      // JMP'S OWN target register (which also stops a memory-indirect
      // `jmp *(%r)` from false-matching a nearby reg-reg add) and walk back
      // over real instruction boundaries, skipping an instruction only when a
      // full decode proves it never MENTIONS the live registers — mention, not
      // just write, so no operand-action subtleties are load-bearing. A decode
      // failure or a control-flow instruction aborts the walk: falling back to
      // the dispatcher is only a missed devirt, never a wrong table.
      auto mentions = [&](uint64_t p, int r1, int r2) -> bool {
        static const char *kAlias[16][5] = {
            {"RAX", "EAX", "AX", "AL", "AH"},
            {"RCX", "ECX", "CX", "CL", "CH"},
            {"RDX", "EDX", "DX", "DL", "DH"},
            {"RBX", "EBX", "BX", "BL", "BH"},
            {"RSP", "ESP", "SP", "SPL", nullptr},
            {"RBP", "EBP", "BP", "BPL", nullptr},
            {"RSI", "ESI", "SI", "SIL", nullptr},
            {"RDI", "EDI", "DI", "DIL", nullptr},
            {"R8", "R8D", "R8W", "R8B", nullptr},
            {"R9", "R9D", "R9W", "R9B", nullptr},
            {"R10", "R10D", "R10W", "R10B", nullptr},
            {"R11", "R11D", "R11W", "R11B", nullptr},
            {"R12", "R12D", "R12W", "R12B", nullptr},
            {"R13", "R13D", "R13W", "R13B", nullptr},
            {"R14", "R14D", "R14W", "R14B", nullptr},
            {"R15", "R15D", "R15W", "R15B", nullptr},
        };
        auto hits = [&](const std::string &n) -> bool {
          if (n.empty()) return true;  // unnamed register: assume interference
          for (int r : {r1, r2}) {
            if (r < 0 || r > 15) continue;
            for (int i = 0; i < 5 && kAlias[r][i]; i++) {
              if (n == kAlias[r][i]) return true;
            }
          }
          return false;
        };
        std::string buf;
        for (uint64_t k = 0; k < 16; k++) {
          uint8_t v;
          if (!rdb(p + k, v)) break;
          buf.push_back(static_cast<char>(v));
        }
        remill::Instruction di;
        if (!arch->DecodeInstruction(p, buf, di, arch->CreateInitialContext())) {
          return true;
        }
        if (di.IsControlFlow()) return true;
        for (const auto &op : di.operands) {
          switch (op.type) {
            case remill::Operand::kTypeRegister:
              if (hits(op.reg.name)) return true;
              break;
            case remill::Operand::kTypeShiftRegister:
              if (hits(op.shift_reg.reg.name)) return true;
              break;
            case remill::Operand::kTypeAddress:
              if (hits(op.addr.base_reg.name) || hits(op.addr.index_reg.name)) {
                return true;
              }
              break;
            case remill::Operand::kTypeImmediate:
              break;
            default:
              return true;  // expression operands: not worth reasoning about
          }
        }
        return false;
      };
      if (ok) {  // the jmp itself: [REX] FF /4 mod=11 → target register
        uint64_t p = inst.pc;
        uint8_t rex = 0;
        if (rdb(p, b) && (b & 0xF0) == 0x40) { rex = b; p++; }
        uint8_t modrm = 0;
        ok = rdb(p, b) && b == 0xFF && rdb(p + 1, modrm) &&
             (modrm & 0xF8) == 0xE0;  // mod=11, /4
        if (ok) rt = (modrm & 7) | ((rex & 1) << 3);
      }
      if (ok) {  // find `add %Rb,%Rt` (REX.W 01 /r, mod=11), ≤12 insns back
        ok = false;
        for (int back = 0; back < 12 && bit != bounds.begin(); back++) {
          --bit;
          uint64_t p = *bit;
          uint8_t rex = 0, modrm = 0;
          if (rdb(p, rex) && (rex & 0xF8) == 0x48 && rdb(p + 1, b) &&
              b == 0x01 && rdb(p + 2, modrm) && (modrm & 0xC0) == 0xC0 &&
              static_cast<int>((modrm & 7) | ((rex & 1) << 3)) == rt) {
            rb = ((modrm >> 3) & 7) | ((rex & 4) << 1);  // reg = table base
            p_add = p;
            ok = true;
            break;
          }
          if (mentions(p, rt, -1)) break;  // Rt no longer flows from the add
        }
      }
      if (ok) {  // find `movslq (%Rb,%Ri,4),%Rt` (REX.W 63 + SIB scale=4)
        ok = false;
        for (int back = 0; back < 12 && bit != bounds.begin(); back++) {
          --bit;
          uint64_t p = *bit;
          uint8_t rex = 0, modrm = 0, sib = 0;
          if (rdb(p, rex) && (rex & 0xF8) == 0x48 && rdb(p + 1, b) &&
              b == 0x63 && rdb(p + 2, modrm) && (modrm & 7) == 4 &&
              rdb(p + 3, sib) && ((sib >> 6) & 3) == 2 &&
              static_cast<int>(((modrm >> 3) & 7) | ((rex & 4) << 1)) == rt &&
              static_cast<int>((sib & 7) | ((rex & 1) << 3)) == rb) {
            p_mov = p;
            ok = true;
            break;
          }
          if (mentions(p, rt, rb)) break;  // a write here breaks the idiom
        }
      }
      (void) p_add;
      (void) p_mov;
      // Now find where %Rb was set: the nearest preceding
      // `lea %Rb,[rip+disp32]` (REX.W 8D /r, mod=00 rm=101, 7 bytes). Scanning
      // by boundary (not by byte) is what makes the hoisted-lea case work.
      int32_t disp = 0;
      uint64_t p_lea = 0;
      if (ok) {
        ok = false;
        for (int back = 0; back < 96 && bit != bounds.begin(); back++) {
          --bit;
          uint64_t p = *bit;
          uint8_t rex = 0, op = 0, modrm = 0;
          if (!rdb(p, rex) || (rex & 0xF8) != 0x48) continue;
          if (!rdb(p + 1, op) || op != 0x8D) continue;
          if (!rdb(p + 2, modrm) || (modrm & 0xC7) != 0x05) continue;
          if ((((modrm >> 3) & 7) | ((rex & 4) << 1)) != rb) continue;
          bool got = true;
          disp = 0;
          for (int i = 0; i < 4; i++) {
            uint8_t d;
            if (rdb(p + 3 + i, d)) disp |= static_cast<int32_t>(d) << (8 * i);
            else { got = false; break; }
          }
          if (!got) continue;
          p_lea = p;
          ok = true;
          break;
        }
      }
      {
        if (ok) {
          // rip-relative: base = address of the NEXT instruction + disp32.
          const uint64_t tbl_base =
              (p_lea + 7) + static_cast<uint64_t>(static_cast<int64_t>(disp));
          // Tables are emitted back-to-back in .rdata, so "target still inside
          // the function" does NOT bound this table — the next table's entries
          // also land in-function (just offset by the inter-table distance, i.e.
          // mid-instruction). Bound on instruction boundaries instead
          // (`bounds`, computed above for the backward instruction walk).
          std::vector<uint64_t> emitted;
          for (int i = 0; i < 1024; i++) {
            int32_t off = 0;
            bool got = true;
            for (int k = 0; k < 4; k++) {
              uint8_t e;
              if (rdb(tbl_base + static_cast<uint64_t>(i) * 4 + k, e))
                off |= static_cast<int32_t>(e) << (8 * k);
              else { got = false; break; }
            }
            if (!got) break;
            const uint64_t tgt =
                tbl_base + static_cast<uint64_t>(static_cast<int64_t>(off));
            if (tgt < clo || tgt > chi) break;  // first off-function entry ends the table
            // First entry that isn't an instruction start = we have read past
            // this table into the neighbouring one. Never emit it: a
            // mid-instruction target aborts the lifter (see InstBoundaries).
            if (!bounds.count(tgt)) break;
            bool dup = false;
            for (uint64_t e : emitted) if (e == tgt) { dup = true; break; }
            if (!dup) {
              emitted.push_back(tgt);
              func(tgt, remill::DevirtualizedTargetKind::kTraceLocal);
            }
            if (emitted.size() >= 256) break;
          }
        }
      }
      // [FIX 2026-08-17] Boundary-sweep FALLBACK for indirect jumps the idiom
      // matcher does NOT recognize. Returning nothing here lowers the jump to
      // __remill_jump, and even routed through the runtime dispatcher a
      // MID-FUNCTION target matches no case — the transfer silently vanishes.
      // Observed: pad_integral's padding/alignment `match` (a jump table only
      // reached when a WIDTH is specified) jumps to pad_integral+0x3AF; every
      // `{:>N}` format returned Err while all other families worked.
      //
      // Offering every real instruction boundary of the contiguous code block
      // as a kTraceLocal target makes TraceLifter emit its own PC switch over
      // them, so ANY computed intra-fn target (odd table shapes, cmov-selected
      // labels) lands on a lifted block — no runtime dispatcher involved. The
      // boundary set keeps mid-instruction addresses out (the earlier abort
      // class), and the existing 24 KiB contiguous-block cap bounds the cost;
      // idiom-matched tables above never reach this fallback.
      // Size gates: hundreds of mid-block entry points stress TraceLifter's
      // block splitting; on large functions (layout_formatting_context, 14 KiB;
      // unicode_bidi) the emitted IR failed the verifier ("Instruction does not
      // dominate all uses"). Small blocks lift cleanly (pad_integral: 1296 B,
      // 346 boundaries -> valid IR, 2 switches). Cap at 4 KiB / 1024 boundaries:
      // covers the fmt-family tables; a too-big unmatched jump falls back to the
      // dispatcher route, where the unk counter at least makes it visible.
      if (!ok && chi - clo <= 4096 && bounds.size() <= 1024) {
        for (uint64_t b : bounds) {
          if (b != inst.pc) {
            func(b, remill::DevirtualizedTargetKind::kTraceLocal);
          }
        }
      }
      return;
    }
    // Only devirt the COMPILER JUMP-TABLE pattern: `br Xn` immediately preceded
    // by `add Xn, Xn, Xm, lsl #2` (the table-target computation). Skip every
    // other indirect jump (e.g. a bytecode interpreter's fn-ptr `br` dispatch),
    // which would otherwise sweep a huge window and blow up the lifted IR.
    bool is_jumptable = false;
    for (int k = 1; k <= 5 && inst.pc >= static_cast<uint64_t>(4 * k); k++) {
      uint32_t w = 0;
      bool got = true;
      for (int i = 0; i < 4; i++) {
        auto it = memory.find(inst.pc - 4 * k + static_cast<uint64_t>(i));
        if (it == memory.end()) { got = false; break; }
        w |= static_cast<uint32_t>(it->second) << (8 * i);
      }
      // Two AArch64 jump-table dispatch forms:
      //   (a) `add Xd, Xn, Xm, lsl #2; br Xd`  — offset == index*4 directly.
      //   (b) `adr X; ldrsw Xt,[Xn,Xm,lsl #2]; add Xd,X,Xt; br Xd` — Rust's common
      //       PC-relative signed-offset table (lsl#2 is on the LDRSW, not the add).
      // Detect (b) via the table load `ldrsw Xt,[Xn,Xm,lsl #2]`
      // (bits[31:21]==0b10111000101==0x5C5, bits[15:10]==0b011110==0x1E: option=LSL,S=1).
      bool add_lsl2 = ((w >> 24) == 0x8Bu) && (((w >> 10) & 0x3Fu) == 2u);
      bool ldrsw_lsl2 = ((w >> 21) == 0x5C5u) && (((w >> 10) & 0x3Fu) == 0x1Eu);
      if (got && (add_lsl2 || ldrsw_lsl2)) {
        is_jumptable = true;
        break;
      }
    }
    if (!is_jumptable) {
      return;
    }
    // Skip devirt for VERY large fns (e.g. taffy grid track-sizing ~65 KB and the
    // TrueType hinting bytecode interpreter, which have many dispatch jump tables) —
    // sweeping them blows up the lifted IR, and they aren't on the bare-body layout
    // path. M12.7: cap on the CONTIGUOUS CODE BLOCK around inst.pc, NOT the full
    // memory-map span. The span includes far-away mirrored .rodata (the jump-table
    // byte-offset tables + string/panic constants) whose distance from the code is
    // irrelevant to function complexity — and it wrongly excluded calc_used_size
    // (its jump-table data sits far in .rodata), leaving calc's SizeMetric/box-sizing
    // match-tables as __remill_jump → a remill PC-dispatch loop (while{switch(PC)})
    // whose loop-carried phis mis-deliver the f32 width (body came out 0). The
    // contiguous code extent is the right proxy: ~6 KB for calc_used_size (devirt OK)
    // vs ~65 KB for grid track-sizing (still excluded). 24576 covers
    // layout_document/bfc/ifc (~20-23 KB) too.
    {
      uint64_t clo = inst.pc, chi = inst.pc, guard = 0;
      while (memory.count(clo - 1) && ++guard < (1u << 18)) { clo--; }
      guard = 0;
      while (memory.count(chi + 1) && ++guard < (1u << 18)) { chi++; }
      if (chi - clo > 24576) {
        return;
      }
    }
    // M12.7: decode the EXACT jump-table targets so the devirt emits ONLY the real arm
    // blocks — NOT the helper-return / continuation addresses a window sweep would add as
    // spurious switch cases (those create dispatch edges into call-return blocks where a
    // callee's f32 return isn't in State yet → calc_used_size's body width came out 0).
    // The .rodata offset table is provided to the lifter via --extra_data. Forms:
    //   adrp Xb,#pg ; add Xb,Xb,#off            -> Xb = table base
    //   adr  Xt,ARM                             -> ARM = arm-block base
    //   ldrb/ldrh Wd,[Xb,Xi{,lsl#k}] (or ldrsw) -> tbl[i] (1/2/4 bytes)
    //   add  Xt,Xt,Wd,lsl#2 (compact) / add Xt,Xt,Xd (ldrsw) ; br Xt
    // Fall back to a bounded window sweep if any field can't be decoded (no regression).
    auto read32 = [&](uint64_t a, uint32_t &w) -> bool {
      w = 0;
      for (int i = 0; i < 4; i++) {
        auto it = memory.find(a + static_cast<uint64_t>(i));
        if (it == memory.end()) return false;
        w |= static_cast<uint32_t>(it->second) << (8 * i);
      }
      return true;
    };
    {
      uint64_t arm_block = 0, tbl_base = 0;
      int elem = 0, ldr_base_reg = -1, idx_reg = -1, n_entries = -1;
      bool scaled4 = false;
      for (int k = 1; k <= 12 && inst.pc >= static_cast<uint64_t>(4 * k); k++) {
        uint32_t w;
        if (!read32(inst.pc - 4 * k, w)) continue;
        if (((w >> 24) & 0x9F) == 0x10 && arm_block == 0) {   // ADR -> arm block
          int64_t immlo = (w >> 29) & 3, immhi = (w >> 5) & 0x7FFFF;
          int64_t imm21 = (immhi << 2) | immlo;
          if (imm21 & (1LL << 20)) imm21 |= ~((1LL << 21) - 1);
          arm_block = (inst.pc - 4 * k) + static_cast<uint64_t>(imm21);
        } else if ((w >> 21) == 0x1C3) {            // LDRB (reg)
          elem = 1; scaled4 = true;
          ldr_base_reg = (w >> 5) & 0x1F; idx_reg = (w >> 16) & 0x1F;
        } else if ((w >> 21) == 0x3C3) {            // LDRH (reg)  [size=01]
          elem = 2; scaled4 = true;
          ldr_base_reg = (w >> 5) & 0x1F; idx_reg = (w >> 16) & 0x1F;
        } else if ((w >> 21) == 0x5C5) {            // LDRSW (reg)
          elem = 4; scaled4 = false;
          ldr_base_reg = (w >> 5) & 0x1F; idx_reg = (w >> 16) & 0x1F;
        }
      }
      // Bounds: cmp Xidx,#N / cmp Widx,#N (subs Xzr,Xidx,#imm) guards the table
      // index, so the table has exactly N entries. Read EXACTLY N (no over-read
      // of post-table arm code that happens to land in [arm_block,+8192]).
      if (idx_reg >= 0) {
        for (int k = 1; k <= 24 && inst.pc >= static_cast<uint64_t>(4 * k); k++) {
          uint32_t w;
          if (!read32(inst.pc - 4 * k, w)) continue;
          if (((w >> 24) == 0xF1 || (w >> 24) == 0x71) &&   // SUBS (imm), sf=1/0
              (w & 0x1F) == 0x1F &&                          // Rd == zr (cmp)
              static_cast<int>((w >> 5) & 0x1F) == idx_reg) {  // Rn == idx
            uint64_t imm = (w >> 10) & 0xFFF;
            if ((w >> 22) & 1) imm <<= 12;
            n_entries = static_cast<int>(imm);                // N (count, not max)
            break;
          }
        }
      }
      if (ldr_base_reg >= 0) {                       // wide scan: table base
        for (int k = 1; k <= 160 && inst.pc >= static_cast<uint64_t>(4 * k); k++) {
          uint32_t w;
          if (!read32(inst.pc - 4 * k, w)) continue;
          if ((w >> 24) == 0x91 &&
              static_cast<int>(w & 0x1F) == ldr_base_reg &&
              static_cast<int>((w >> 5) & 0x1F) == ldr_base_reg) {  // add Xb,Xb,#imm
            uint64_t imm = (w >> 10) & 0xFFF;
            if ((w >> 22) & 1) imm <<= 12;
            uint32_t aw;
            if (read32(inst.pc - 4 * k - 4, aw) && ((aw >> 24) & 0x9F) == 0x90 &&
                static_cast<int>(aw & 0x1F) == ldr_base_reg) {       // adrp Xb
              int64_t lo2 = (aw >> 29) & 3, hi2 = (aw >> 5) & 0x7FFFF, im = (hi2 << 2) | lo2;
              if (im & (1LL << 20)) im |= ~((1LL << 21) - 1);
              uint64_t apc = inst.pc - 4 * k - 4;
              tbl_base = ((apc & ~uint64_t(0xFFF)) + (static_cast<uint64_t>(im) << 12)) + imm;
              break;
            }
          }
        }
      }
      if (arm_block && tbl_base && elem > 0) {
        std::vector<uint64_t> targets;
        int limit = (n_entries > 0 && n_entries <= 256) ? n_entries : 256;
        for (int i = 0; i < limit; i++) {
          uint64_t off = 0; bool got = true;
          for (int b = 0; b < elem; b++) {
            auto it = memory.find(tbl_base + static_cast<uint64_t>(i * elem + b));
            if (it == memory.end()) { got = false; break; }
            off |= static_cast<uint64_t>(it->second) << (8 * b);
          }
          if (!got) break;
          uint64_t tgt = scaled4
              ? (arm_block + off * 4)
              : (arm_block + static_cast<uint64_t>(
                                static_cast<int64_t>(static_cast<int32_t>(off))));
          if (tgt < arm_block || tgt > arm_block + 8192 ||
              memory.find(tgt) == memory.end()) {
            break;  // past the end of the table
          }
          bool dup = false;
          for (uint64_t e : targets) { if (e == tgt) { dup = true; break; } }
          if (!dup) targets.push_back(tgt);
        }
        if (!targets.empty() && targets.size() <= 256) {
          for (uint64_t t : targets) {
            func(t, remill::DevirtualizedTargetKind::kTraceLocal);
          }
          return;  // exact decode succeeded
        }
      }
    }
    // Fallback window sweep (the table wasn't provided / decodable).
    const uint64_t mlo = memory.begin()->first & ~uint64_t(3);
    const uint64_t mhi = memory.rbegin()->first;
    const uint64_t lo =
        (inst.pc > mlo + 256) ? ((inst.pc - 256) & ~uint64_t(3)) : mlo;
    const uint64_t hi = (inst.pc + 2048 < mhi) ? (inst.pc + 2048) : mhi;
    for (uint64_t a = lo; a <= hi; a += 4) {
      func(a, remill::DevirtualizedTargetKind::kTraceLocal);
    }
  }

  // Called when we have lifted, i.e. defined the contents, of a new trace.
  // The derived class is expected to do something useful with this.
  void SetLiftedTraceDefinition(uint64_t addr,
                                llvm::Function *lifted_func) override {
    traces[addr] = lifted_func;
  }

  // Get a definition for a lifted trace.
  //
  // NOTE: This is permitted to return a function from an arbitrary module.
  llvm::Function *GetLiftedTraceDefinition(uint64_t addr) override {

    // The entry function needs to be lifted by the TraceLifter
    if (addr == entry) {
      return nullptr;
    }

    // The get_trace_decl in TraceLifter creates a declaration for us.
    // Instead of providing an implementation, we keep it extern.
    auto name = TraceName(addr);
    auto fn = module->getFunction(name);
    if (fn == nullptr) {
      fn = arch->DeclareLiftedFunction(name, module);
    }
    return fn;
  }

  // Get a declaration for a lifted trace. The idea here is that a derived
  // class might have additional global info available to them that lets
  // them declare traces ahead of time. In order to distinguish between
  // stuff we've lifted, and stuff we haven't lifted, we allow the lifter
  // to access "defined" vs. "declared" traces.
  //
  // NOTE: This is permitted to return a function from an arbitrary module.
  llvm::Function *GetLiftedTraceDeclaration(uint64_t addr) override {
    return remill::TraceManager::GetLiftedTraceDeclaration(addr);
  }

  // Try to read an executable byte of memory. Returns `true` of the byte
  // at address `addr` is executable and readable, and updates the byte
  // pointed to by `byte` with the read value.
  bool TryReadExecutableByte(uint64_t addr, uint8_t *byte) override {
    auto byte_it = memory.find(addr);
    if (byte_it != memory.end()) {
      if (byte != nullptr) {
        *byte = byte_it->second;
      }
      return true;
    } else {
      return false;
    }
  }
};

// Looks for calls to a function like `__remill_function_return`, and
// replace its state pointer with a null pointer so that the state
// pointer never escapes.
static void MuteStateEscape(llvm::Module *module, const char *func_name) {
  auto func = module->getFunction(func_name);
  if (!func) {
    return;
  }

  for (auto user : func->users()) {
    if (auto call_inst = llvm::dyn_cast<llvm::CallInst>(user)) {
      auto arg_op = call_inst->getArgOperand(remill::kStatePointerArgNum);
      call_inst->setArgOperand(remill::kStatePointerArgNum,
                               llvm::UndefValue::get(arg_op->getType()));
    }
  }
}

static void SetVersion(void) {
  std::stringstream ss;
  auto vs = remill::version::GetVersionString();
  if (0 == vs.size()) {
    vs = "unknown";
  }
  ss << vs << "\n";
  if (!remill::version::HasVersionData()) {
    ss << "No extended version information found!\n";
  } else {
    ss << "Commit Hash: " << remill::version::GetCommitHash() << "\n";
    ss << "Commit Date: " << remill::version::GetCommitDate() << "\n";
    ss << "Last commit by: " << remill::version::GetAuthorName() << " ["
       << remill::version::GetAuthorEmail() << "]\n";
    ss << "Commit Subject: [" << remill::version::GetCommitSubject() << "]\n";
    ss << "\n";
    if (remill::version::HasUncommittedChanges()) {
      ss << "Uncommitted changes were present during build.\n";
    } else {
      ss << "All changes were committed prior to building.\n";
    }
  }
  google::SetVersionString(ss.str());
}

struct Argument {
  bool is_memory = false;
  size_t size = 0;
  std::string reg;
  int64_t offset = 0;

  static int64_t parse_hex(const std::string &argument) {
    int64_t hex_value = 0;
    std::istringstream iss(argument);
    iss >> std::hex >> hex_value;
    return hex_value;
  }

  static Argument parse(const std::string &argument) {
    Argument out;
    auto mem_idx = argument.find('[');
    if (mem_idx != std::string::npos) {
      out.is_memory = true;
      if (mem_idx > 0) {
        out.size = parse_hex(argument.substr(0, mem_idx));
      } else {
        out.size = 0;
      }
      auto sign_idx = argument.find_first_of("+-");
      if (sign_idx == std::string::npos) {
        out.reg = argument.substr(mem_idx + 1, argument.size() - mem_idx - 2);
        out.offset = 0;
      } else {
        out.reg = argument.substr(mem_idx + 1, sign_idx - mem_idx - 1);
        out.offset = parse_hex(
            argument.substr(sign_idx, argument.size() - sign_idx - 1));
      }
    } else {
      out.reg = argument;
    }
    for (auto &ch : out.reg) {
      if (ch >= 'a' && ch <= 'z') {
        ch -= 'a' - 'A';
      }
    }
    return out;
  }

  void dump() {
    if (is_memory) {
      if (offset < 0) {
        printf("%zu:['%s'%ld]\n", size, reg.c_str(), offset);
      } else {
        printf("%zu:['%s'+%ld]\n", size, reg.c_str(), offset);
      }
    } else {
      printf("%s\n", reg.c_str());
    }
  }
};

// One function, lifted into its own semantics module.
//
// Split out of main so a batch run performs ONE process spawn for many
// functions instead of one each. A full lift shells out ~100k times and a
// wedged CreateProcess froze an entire run, so cutting spawn count is the
// point; the semantics load stays per-entry because the Arch caches types
// that belong to the module it was loaded with.
static int LiftOne() {
  // A fresh context/arch/semantics per entry. remill's Arch caches types
  // that belong to the module its semantics were loaded into, so neither the
  // Arch nor the module can be reused across entries - a second
  // LoadArchSemantics on the same Arch aborts the process. Batching therefore
  // saves the PROCESS SPAWN (and gflags/glog startup) per function, which is
  // what the ~100k-spawn hang is about, not the semantics parse.
  llvm::LLVMContext context;
  auto arch_owned = remill::Arch::Get(context, FLAGS_os, FLAGS_arch);
  if (!arch_owned) {
    std::cerr << "Cannot create arch" << std::endl;
    return EXIT_FAILURE;
  }
  const remill::Arch *arch = arch_owned.get();
  const uint64_t addr_mask = ~0ULL >> (64UL - arch->address_size);
  std::unique_ptr<llvm::Module> module(remill::LoadArchSemantics(arch));
  const auto mem_ptr_type = arch->MemoryPointerType();

  Memory memory = UnhexlifyInputBytes(addr_mask);
  // M12.7: merge extra regions (jump-table .rodata) into `memory`.
  if (!FLAGS_extra_data.empty()) {
    size_t pos = 0;
    while (pos < FLAGS_extra_data.size()) {
      size_t semi = FLAGS_extra_data.find(';', pos);
      std::string region = FLAGS_extra_data.substr(
          pos, semi == std::string::npos ? std::string::npos : semi - pos);
      pos = (semi == std::string::npos) ? FLAGS_extra_data.size() : semi + 1;
      size_t colon = region.find(':');
      if (colon == std::string::npos) continue;
      uint64_t base = std::strtoull(region.substr(0, colon).c_str(), nullptr, 16);
      const std::string hx = region.substr(colon + 1);
      for (size_t i = 0; i + 1 < hx.size(); i += 2) {
        char nb[] = {hx[i], hx[i + 1], '\0'};
        memory[base + i / 2] = static_cast<uint8_t>(std::strtoul(nb, nullptr, 16));
      }
    }
  }
  SimpleTraceManager manager(arch, module.get(), memory,
                             FLAGS_entry_address);
  if (!manager.TryReadExecutableByte(FLAGS_entry_address, nullptr)) {
    std::cerr << "No executable code at address 0x" << std::hex
              << FLAGS_entry_address << std::endl;
    return EXIT_FAILURE;
  }
  remill::IntrinsicTable intrinsics(module.get());


  auto inst_lifter = arch->DefaultLifter(intrinsics);

  remill::TraceLifter trace_lifter(arch, manager);

  // Lift all discoverable traces starting from `-entry_address` into
  // `module`.
  trace_lifter.Lift(FLAGS_entry_address);

  // Remove llvm.compiler.used to not preserve unused semantics
  auto compilerUsed = module->getGlobalVariable("llvm.compiler.used", true);
  if (compilerUsed != nullptr) {
    compilerUsed->eraseFromParent();
  }

  // Remove ISEL_ globals that contain pointers to the semantic functions
  std::vector<llvm::GlobalVariable *> erase;
  for (auto &G : module->globals()) {
    if (G.getName().find("ISEL_") == 0) {
      erase.push_back(&G);
    }
  }
  for (auto G : erase) {
    G->eraseFromParent();
  }

  // Remove function that keeps the references to unused intrinsics
  auto remillIntrinsics = module->getFunction("__remill_intrinsics");
  if (remillIntrinsics != nullptr) {
    remillIntrinsics->eraseFromParent();
  }

  // Remove the implementation of the __remill_sync_hyper_call from the bitcode, because
  // after inlining things get very confusing if this is actually called.
  // TODO: this should probably be removed
  auto hyperCall = module->getFunction("__remill_sync_hyper_call");
  if (hyperCall != nullptr) {
    // Take an owned copy of the name *before* hyperCall is destroyed —
    // hyperCall->getName() returns a StringRef into hyperCall's storage,
    // which is freed by eraseFromParent() below. Re-using the StringRef
    // after the erase yields zeroed memory and trips LLVM's "Null bytes
    // not allowed in names" assertion in setName().
    auto name = hyperCall->getName().str();
    auto ty = hyperCall->getFunctionType();
    auto newFn = module->getOrInsertFunction(name + "_", ty);
    hyperCall->replaceAllUsesWith(newFn.getCallee());
    hyperCall->eraseFromParent();
    newFn.getCallee()->setName(name);
  }

  // A lot of intrinsic functions are (incorrectly) marked as [[gnu::const]].
  // This causes problems where optimizer's assumptions are violated when an
  // implementation is provided. To work around this we remove these attributes
  // from the functions and from the call sites.
  // Another workaround is to first do a separate inline pass and then O3.
  // NOTE: This was fixed in https://github.com/lifting-bits/remill/commit/7f091d42
  for (auto &function : module->functions()) {
    if (function.getName().find("__remill_") != 0) {
      continue;
    }

    function.removeFnAttr(llvm::Attribute::ReadNone);
    for (auto &argument : function.args()) {
      argument.removeAttr(llvm::Attribute::ReadNone);
    }
    for (auto user : function.users()) {
      if (auto call = llvm::dyn_cast<llvm::CallInst>(user)) {
        call->removeFnAttr(llvm::Attribute::ReadNone);
      }
    }
  }

  // Dump the pre-optimization IR
  if (!FLAGS_ir_pre_out.empty()) {
    if (!remill::StoreModuleIRToFile(module.get(), FLAGS_ir_pre_out, true)) {
      LOG(ERROR) << "Could not save LLVM IR to " << FLAGS_ir_pre_out;
    }
  }

  // Optimize the module, but with a particular focus on only the functions
  // that we actually lifted.
  remill::OptimizationGuide guide = {};
  remill::OptimizeModule(arch, module.get(), manager.traces, guide);

  // Create a new module in which we will move all the lifted functions. Prepare
  // the module for code of this architecture, i.e. set the data layout, triple,
  // etc.
  llvm::Module dest_module("lifted_code", context);
  arch->PrepareModuleDataLayout(&dest_module);

  llvm::Function *entry_trace = nullptr;

  // Move the lifted code into a new module. This module will be much smaller
  // because it won't be bogged down with all of the semantics definitions.
  // This is a good JITing strategy: optimize the lifted code in the semantics
  // module, move it to a new module, instrument it there, then JIT compile it.
  for (auto &lifted_entry : manager.traces) {
    if (lifted_entry.first == FLAGS_entry_address) {
      entry_trace = lifted_entry.second;
    }
    remill::MoveFunctionIntoModule(lifted_entry.second, &dest_module);

    // If we are providing a prototype, then we'll be re-optimizing the new
    // module, and we want everything to get inlined.
    if (!FLAGS_signature.empty()) {
      lifted_entry.second->setLinkage(llvm::GlobalValue::InternalLinkage);
      lifted_entry.second->removeFnAttr(llvm::Attribute::NoInline);
      lifted_entry.second->addFnAttr(llvm::Attribute::InlineHint);
      lifted_entry.second->addFnAttr(llvm::Attribute::AlwaysInline);
    }
  }

  // We have a prototype, so go create a function that will call our entrypoint.
  if (!FLAGS_signature.empty()) {
    CHECK_NOTNULL(entry_trace);

    // Set the entry trace as internal so it can be removed during optimizations
    entry_trace->setLinkage(llvm::Function::InternalLinkage);

    std::string signature;
    for (auto ch : FLAGS_signature) {
      if (ch >= 'a' && ch <= 'z') {
        ch -= 'a' - 'A';
      }
      if (ch != ' ') {
        signature.push_back(ch);
      }
    }
    auto paren_idx = signature.find('(');
    CHECK(paren_idx != std::string::npos && signature.back() == ')')
        << "Invalid function signature";

    auto output_reg_name = signature.substr(0, paren_idx);
    if (output_reg_name == "void") {
      output_reg_name.clear();
    }
    std::vector<Argument> input_args;
    std::string temp;
    for (size_t i = paren_idx + 1; i < signature.size() - 1; i++) {
      auto ch = signature[i];
      if (ch == ',') {
        input_args.push_back(Argument::parse(temp));
        temp.clear();
      } else {
        temp.push_back(ch);
      }
    }
    if (!temp.empty()) {
      input_args.push_back(Argument::parse(temp));
    }

    // Use the registers to build a function prototype.
    llvm::SmallVector<llvm::Type *, 8> arg_types;
    for (auto &arg : input_args) {
      const auto input_reg = arch->RegisterByName(arg.reg);
      CHECK(input_reg != nullptr)
          << "Invalid register name '" << arg.reg << "' used in signature '"
          << FLAGS_signature << "'";

      if (arg.size == 0) {
        arg.size = input_reg->size;
      }
      auto arg_type = llvm::Type::getIntNTy(context, arg.size * 8);
      arg_types.push_back(arg_type);
    }

    auto return_type = llvm::Type::getVoidTy(context);
    if (!output_reg_name.empty()) {
      const auto output_reg = arch->RegisterByName(output_reg_name);
      CHECK(output_reg != nullptr)
          << "Invalid register name '" << output_reg_name << "'";
      return_type = output_reg->type;
    }
    const auto func_type =
        llvm::FunctionType::get(return_type, arg_types, false);
    const auto func =
        llvm::Function::Create(func_type, llvm::GlobalValue::ExternalLinkage,
                               "call_" + entry_trace->getName(), &dest_module);

    // HACK: This is a workaround for the issue with the DSEPass making false assumptions
    func->addFnAttr("disable-tail-calls", "true");

    // Get the program counter and stack pointer registers.
    const remill::Register *pc_reg =
        arch->RegisterByName(arch->ProgramCounterRegisterName());
    const remill::Register *sp_reg =
        arch->RegisterByName(arch->StackPointerRegisterName());

    CHECK(pc_reg != nullptr)
        << "Could not find the register in the state structure "
        << "associated with the program counter.";

    CHECK(sp_reg != nullptr)
        << "Could not find the register in the state structure "
        << "associated with the stack pointer.";

    // Store all of the function arguments (corresponding with specific registers)
    // into the stack-allocated `State` structure.
    auto entry = llvm::BasicBlock::Create(context, "", func);
    llvm::IRBuilder<> ir(entry);

    const auto state_type = arch->StateStructType();
    const auto state_ptr = ir.CreateAlloca(state_type);

    auto CreateSymbolicReg = [&](const remill::Register *reg,
                                 const std::string &name) {
      std::string symbol_name = "symbolic_" + name;
      auto symbolic_fn = dest_module.getOrInsertFunction(
          "__remill_" + symbol_name, llvm::FunctionType::get(reg->type, false));
      auto fn = llvm::dyn_cast<llvm::Function>(symbolic_fn.getCallee());

      // Allow the optimizer to delete calls if the result is not used
      fn->setDoesNotAccessMemory();
      fn->setDoesNotThrow();
      fn->addFnAttr(llvm::Attribute::WillReturn);

      auto call = ir.CreateCall(symbolic_fn, {}, symbol_name);
      const auto reg_ptr = reg->AddressOf(state_ptr, entry);
      ir.CreateStore(call, reg_ptr);
    };

    // Store symbolic values into general purpose registers
    if (FLAGS_symbolic_regs) {
      arch->ForEachRegister([&](const remill::Register *reg) {
        if (reg->parent == nullptr) {
          CreateSymbolicReg(reg, reg->name);
        }
      });
    }

    // Store the program counter into the state.
    const auto trace_pc =
        llvm::ConstantInt::get(pc_reg->type, FLAGS_entry_address, false);
    ir.SetInsertPoint(entry);
    ir.CreateStore(trace_pc, pc_reg->AddressOf(state_ptr, entry));

    // Set up symbolic globals
    CreateSymbolicReg(sp_reg, "STACK");
    auto gsbase_reg = arch->RegisterByName("GSBASE");
    if (gsbase_reg != nullptr) {
      CreateSymbolicReg(gsbase_reg, "GSBASE");
    }
    auto fsbase_reg = arch->RegisterByName("FSBASE");
    if (fsbase_reg != nullptr) {
      CreateSymbolicReg(fsbase_reg, "FSBASE");
    }

    llvm::Value *mem_ptr = llvm::UndefValue::get(mem_ptr_type);

    // Store the argument registers into the state
    auto args_it = func->arg_begin();
    for (auto &input_arg : input_args) {
      const auto reg = arch->RegisterByName(input_arg.reg);
      auto reg_ptr = reg->AddressOf(state_ptr, entry);
      auto &arg = *args_it++;

      ir.SetInsertPoint(entry);
      if (input_arg.is_memory) {
        arg.setName("arg_mem_" + input_arg.reg + "_" +
                    llvm::utohexstr(input_arg.offset));
        auto helper_name =
            "__remill_write_memory_" + std::to_string(input_arg.size * 8);
        auto orig_memory_helper = module->getFunction(helper_name);
        CHECK(orig_memory_helper != nullptr)
            << "Could not find memory helper for " << helper_name;
        auto memory_helper = dest_module.getOrInsertFunction(
            helper_name, orig_memory_helper->getFunctionType());
        auto reg_value = ir.CreateLoad(reg->type, reg_ptr);
        auto arg_ptr = ir.CreateAdd(
            reg_value, llvm::ConstantInt::get(reg->type, input_arg.offset));
        ir.CreateCall(memory_helper, {mem_ptr, arg_ptr, &arg});
      } else {
        arg.setName("arg_" + input_arg.reg);
        ir.CreateStore(&arg, reg_ptr);
      }
    }

    // Call the lifted function
    llvm::Value *trace_args[remill::kNumBlockArgs] = {};
    trace_args[remill::kStatePointerArgNum] = state_ptr;
    trace_args[remill::kMemoryPointerArgNum] = mem_ptr;
    trace_args[remill::kPCArgNum] = llvm::ConstantInt::get(
        llvm::IntegerType::get(context, arch->address_size),
        FLAGS_entry_address, false);

    mem_ptr = ir.CreateCall(entry_trace, trace_args);

    // Read and return the output register
    if (!output_reg_name.empty()) {
      const auto out_reg = arch->RegisterByName(output_reg_name);
      auto out_reg_ptr = out_reg->AddressOf(state_ptr, entry);
      ir.CreateRet(ir.CreateLoad(out_reg->type, out_reg_ptr));
    } else {
      ir.CreateRetVoid();
    }

    // NOTE: Doing this prevents the helpers implementation from working properly,
    // which is why this is disabled per default.
    if (FLAGS_mute_state_escape) {
      // We want the stack-allocated `State` to be subject to scalarization
      // and mem2reg, but to "encourage" that, we need to prevent the
      // `alloca`d `State` from escaping.
      MuteStateEscape(&dest_module, "__remill_error");
      MuteStateEscape(&dest_module, "__remill_function_call");
      MuteStateEscape(&dest_module, "__remill_function_return");
      MuteStateEscape(&dest_module, "__remill_jump");
      MuteStateEscape(&dest_module, "__remill_missing_block");
    }

    // Optimize the module to inline everything
    guide.slp_vectorize = true;
    guide.loop_vectorize = true;

    auto check = remill::VerifyModuleMsg(&dest_module);
    if (check) {
      llvm::errs() << "Verification error: " << *check;
      CHECK(false);
    }
    remill::OptimizeBareModule(&dest_module, guide);
  }

  int ret = EXIT_SUCCESS;

  if (!FLAGS_ir_out.empty()) {
    if (!remill::StoreModuleIRToFile(&dest_module, FLAGS_ir_out, true)) {
      LOG(ERROR) << "Could not save LLVM IR to " << FLAGS_ir_out;
      ret = EXIT_FAILURE;
    }
  }
  if (!FLAGS_bc_out.empty()) {
    if (!remill::StoreModuleToFile(&dest_module, FLAGS_bc_out, true)) {
      LOG(ERROR) << "Could not save LLVM bitcode to " << FLAGS_bc_out;
      ret = EXIT_FAILURE;
    }
  }

  return ret;
}

int main(int argc, char *argv[]) {
  SetVersion();
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);


  if (FLAGS_bytes.empty() && FLAGS_batch_manifest.empty()) {
    std::cerr << "Please specify a sequence of hex bytes to -bytes."
              << std::endl;
    return EXIT_FAILURE;
  } else if (!FLAGS_bytes.empty() && FLAGS_bytes.size() % 2) {
    std::cerr << "Please specify an even number of nibbles to -bytes."
              << std::endl;
    return EXIT_FAILURE;
  }

  if (FLAGS_arch.empty()) {
    std::cerr
        << "No architecture specified. Valid architectures: x86, amd64 (with or without "
           "`_avx` or `_avx512` appended), aarch64, aarch32"
        << std::endl;
    return EXIT_FAILURE;
  }

  if (FLAGS_address == (uint64_t) -1) {
    FLAGS_address = 0;
  }

  if (FLAGS_entry_address == (uint64_t) -1) {
    FLAGS_entry_address = FLAGS_address;
  }

  // Make sure `-address` and `-entry_address` are in-bounds for the target
  // architecture's address size.
  llvm::LLVMContext context;
  auto arch = remill::Arch::Get(
      context, FLAGS_os,
      FLAGS_arch);  // TODO: what happens with invalid arguments?
  const uint64_t addr_mask = ~0ULL >> (64UL - arch->address_size);
  if (FLAGS_address != (FLAGS_address & addr_mask)) {
    std::cerr << "Value " << std::hex << FLAGS_address
              << " passed to -address does not fit into 32-bits. Did mean"
              << " to specify a 64-bit architecture to -arch?" << std::endl;
    return EXIT_FAILURE;
  }

  if (FLAGS_entry_address != (FLAGS_entry_address & addr_mask)) {
    std::cerr << "Value " << std::hex << FLAGS_entry_address
              << " passed to -entry_address does not fit into 32-bits. Did mean"
              << " to specify a 64-bit architecture to -arch?" << std::endl;
    return EXIT_FAILURE;
  }

  if (FLAGS_batch_manifest.empty()) {
    return LiftOne();
  }

  // Batch: one line per function, "<entry_hex> <ir_out> <bytes_hex> [extra|-]".
  // A manifest rather than a command line because the single-shot path already
  // spills to a response file past 30k chars, and a batch is many times that.
  std::ifstream manifest(FLAGS_batch_manifest);
  if (!manifest) {
    std::cerr << "Cannot open batch manifest " << FLAGS_batch_manifest
              << std::endl;
    return EXIT_FAILURE;
  }
  std::string line;
  int failures = 0, count = 0;
  while (std::getline(manifest, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream fields(line);
    std::string entry_hex, ir_out, bytes_hex, extra;
    if (!(fields >> entry_hex >> ir_out >> bytes_hex)) continue;
    fields >> extra;
    FLAGS_address = std::strtoull(entry_hex.c_str(), nullptr, 16);
    FLAGS_entry_address = FLAGS_address;
    FLAGS_bytes = bytes_hex;
    FLAGS_ir_out = ir_out;
    FLAGS_bc_out = "";
    FLAGS_extra_data = (extra == "-" || extra.empty()) ? "" : extra;
    ++count;
    // One bad function must not cost the rest of the batch - the caller gets
    // the same per-function failure it would from a single-shot run.
    if (LiftOne() != EXIT_SUCCESS) {
      LOG(ERROR) << "batch: failed to lift 0x" << std::hex << FLAGS_address;
      ++failures;
    }
  }
  std::cerr << "batch: lifted " << (count - failures) << "/" << count
            << " function(s)" << std::endl;
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
