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

namespace {

DEF_SEM(DoNOP) {
  return memory;
}

}  // namespace

DEF_ISEL(NOP) = DoNOP;
DEF_ISEL(HINT_1) = DoNOP;
DEF_ISEL(HINT_2) = DoNOP;
DEF_ISEL(HINT_3) = DoNOP;
DEF_ISEL(NOP_HI_SYSTEM) = DoNOP;
// M12.7 (azul web): CLREX clears the local exclusive monitor — a no-op in the
// single-threaded wasm lift. Appears in std's ldxrb/stxrb retry loops under
// -Z build-std -C target-feature=-lse. Decoder: Arch.cpp.
DEF_ISEL(CLREX_BN_SYSTEM) = DoNOP;
