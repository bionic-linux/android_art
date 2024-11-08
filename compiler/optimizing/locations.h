/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_COMPILER_OPTIMIZING_LOCATIONS_H_
#define ART_COMPILER_OPTIMIZING_LOCATIONS_H_

#include "base/arena_containers.h"
#include "base/arena_object.h"
#include "base/bit_field.h"
#include "base/bit_utils.h"
#include "base/bit_vector.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/value_object.h"
#include "runtime_globals.h"

namespace art HIDDEN {

class HConstant;
class HInstruction;
class Location;

std::ostream& operator<<(std::ostream& os, const Location& location);

/**
 * A Location is an abstraction over the potential location
 * of an instruction. It could be in register or stack.
 */
class Location : public ValueObject {
 public:
  enum OutputOverlap {
    // The liveness of the output overlaps the liveness of one or
    // several input(s); the register allocator cannot reuse an
    // input's location for the output's location.
    kOutputOverlap,
    // The liveness of the output does not overlap the liveness of any
    // input; the register allocator is allowed to reuse an input's
    // location for the output's location.
    kNoOutputOverlap
  };

  enum Kind {
    kInvalid = 0,
    kConstant = 1,
    kStackSlot = 2,        // 32bit stack slot.
    kDoubleStackSlot = 3,  // 64bit stack slot.

    kRegister = 4,  // Core register.

    // We do not use the value 5 because it conflicts with kLocationConstantMask.
    kDoNotUse5 = 5,

    kFpuRegister = 6,  // Float register.

    kRegisterPair = 7,  // Long register.

    kFpuRegisterPair = 8,  // Double register.

    // We do not use the value 9 because it conflicts with kLocationConstantMask.
    kDoNotUse9 = 9,

    kVecRegister = 10,  // Vector register.

    kSIMDStackSlot = 11,  // 128bit stack slot. TODO: generalize with encoded #bytes?

    // Unallocated location represents a location that is not fixed and can be
    // allocated by a register allocator.  Each unallocated location has
    // a policy that specifies what kind of location is suitable. Payload
    // contains register allocation policy.
    kUnallocated = 12,
  };

  constexpr Location() : ValueObject(), value_(kInvalid) {
    // Verify that non-constant location kinds do not interfere with kConstant.
    static_assert((kInvalid & kLocationConstantMask) != kConstant, "TagError");
    static_assert((kUnallocated & kLocationConstantMask) != kConstant, "TagError");
    static_assert((kStackSlot & kLocationConstantMask) != kConstant, "TagError");
    static_assert((kDoubleStackSlot & kLocationConstantMask) != kConstant, "TagError");
    static_assert((kSIMDStackSlot & kLocationConstantMask) != kConstant, "TagError");
    static_assert((kRegister & kLocationConstantMask) != kConstant, "TagError");
    static_assert((kFpuRegister & kLocationConstantMask) != kConstant, "TagError");
    static_assert((kRegisterPair & kLocationConstantMask) != kConstant, "TagError");
    static_assert((kFpuRegisterPair & kLocationConstantMask) != kConstant, "TagError");
    static_assert((kConstant & kLocationConstantMask) == kConstant, "TagError");
    static_assert((kVecRegister & kLocationConstantMask) != kConstant, "TagError");

    DCHECK(!IsValid());
  }

  constexpr Location(const Location& other) = default;

  Location& operator=(const Location& other) = default;

  bool IsConstant() const {
    return (value_ & kLocationConstantMask) == kConstant;
  }

  static Location ConstantLocation(HInstruction* constant) {
    DCHECK(constant != nullptr);
    if (kIsDebugBuild) {
      // Call out-of-line helper to avoid circular dependency with `nodes.h`.
      DCheckInstructionIsConstant(constant);
    }
    return Location(kConstant | reinterpret_cast<uintptr_t>(constant));
  }

  HConstant* GetConstant() const {
    DCHECK(IsConstant());
    return reinterpret_cast<HConstant*>(value_ & ~kLocationConstantMask);
  }

  bool IsValid() const {
    return value_ != kInvalid;
  }

  bool IsInvalid() const {
    return !IsValid();
  }

  // Empty location. Used if there the location should be ignored.
  static constexpr Location NoLocation() {
    return Location();
  }

  // Register locations.
  static constexpr Location RegisterLocation(int reg) {
    return Location(kRegister, reg);
  }

  static constexpr Location FpuRegisterLocation(int reg) {
    return Location(kFpuRegister, reg);
  }

  static Location FpuRegisterLocation(int reg, int vec_len) {
    return Location(kFpuRegister, reg, vec_len);
  }

  // TODO: Implement this when we enable to architecures with exclusive Vec register
  static Location VecRegisterLocation([[maybe_unused]] int reg, [[maybe_unused]] int vec_len) {
    UNREACHABLE();
  }

  static constexpr Location RegisterPairLocation(int low, int high) {
    return Location(kRegisterPair, low << 16 | high);
  }

  static constexpr Location FpuRegisterPairLocation(int low, int high) {
    return Location(kFpuRegisterPair, low << 16 | high);
  }

  bool IsRegister() const {
    return GetKind() == kRegister;
  }

  bool IsFpuRegister() const {
    return GetKind() == kFpuRegister;
  }

  bool IsVecRegister() const {
    return (GetKind() == kVecRegister) || (IsFpuRegister() && GetVecLen() > 0);
  }

  bool IsRegisterPair() const {
    return GetKind() == kRegisterPair;
  }

  bool IsFpuRegisterPair() const {
    return GetKind() == kFpuRegisterPair;
  }

  bool IsRegisterKind() const {
    return IsRegister() || IsFpuRegister() || IsRegisterPair() || IsFpuRegisterPair();
  }

  int reg() const {
    DCHECK(IsRegister() || IsFpuRegister());
    return GetPayload();
  }

  int low() const {
    DCHECK(IsPair());
    return GetPayload() >> 16;
  }

  int high() const {
    DCHECK(IsPair());
    return GetPayload() & 0xFFFF;
  }

  template <typename T>
  T AsRegister() const {
    DCHECK(IsRegister());
    return static_cast<T>(reg());
  }

  template <typename T>
  T AsFpuRegister() const {
    DCHECK(IsFpuRegister());
    return static_cast<T>(reg());
  }

  template <typename T, bool kHasOverlappingFPVecRegisters = false>
  T AsVectorRegister() const {
    if (kHasOverlappingFPVecRegisters) {
      DCHECK(IsFpuRegister());
      return T(reg(), GetVecLen());
    } else {
      DCHECK(!IsFpuRegister());
      DCHECK(IsVecRegister());
      return static_cast<T>(reg());
    }
  }

  template <typename T>
  T AsFPVectorRegister() const {
    return AsVectorRegister<T, true>();
  }

  template <typename T>
  T AsRegisterPairLow() const {
    DCHECK(IsRegisterPair());
    return static_cast<T>(low());
  }

  template <typename T>
  T AsRegisterPairHigh() const {
    DCHECK(IsRegisterPair());
    return static_cast<T>(high());
  }

  template <typename T>
  T AsFpuRegisterPairLow() const {
    DCHECK(IsFpuRegisterPair());
    return static_cast<T>(low());
  }

  template <typename T>
  T AsFpuRegisterPairHigh() const {
    DCHECK(IsFpuRegisterPair());
    return static_cast<T>(high());
  }

  bool IsPair() const {
    return IsRegisterPair() || IsFpuRegisterPair();
  }

  Location ToLow() const {
    if (IsRegisterPair()) {
      return Location::RegisterLocation(low());
    } else if (IsFpuRegisterPair()) {
      return Location::FpuRegisterLocation(low());
    } else {
      DCHECK(IsDoubleStackSlot());
      return Location::StackSlot(GetStackIndex());
    }
  }

  Location ToHigh() const {
    if (IsRegisterPair()) {
      return Location::RegisterLocation(high());
    } else if (IsFpuRegisterPair()) {
      return Location::FpuRegisterLocation(high());
    } else {
      DCHECK(IsDoubleStackSlot());
      return Location::StackSlot(GetHighStackIndex(4));
    }
  }

  static uintptr_t EncodeStackIndex(intptr_t stack_index) {
    DCHECK(-kStackIndexBias <= stack_index);
    DCHECK(stack_index < kStackIndexBias);
    return static_cast<uintptr_t>(kStackIndexBias + stack_index);
  }

  static Location StackSlot(intptr_t stack_index) {
    uintptr_t payload = EncodeStackIndex(stack_index);
    Location loc(kStackSlot, payload);
    // Ensure that sign is preserved.
    DCHECK_EQ(loc.GetStackIndex(), stack_index);
    return loc;
  }

  bool IsStackSlot() const {
    return GetKind() == kStackSlot;
  }

  static Location DoubleStackSlot(intptr_t stack_index) {
    uintptr_t payload = EncodeStackIndex(stack_index);
    Location loc(kDoubleStackSlot, payload);
    // Ensure that sign is preserved.
    DCHECK_EQ(loc.GetStackIndex(), stack_index);
    return loc;
  }

  bool IsDoubleStackSlot() const {
    return GetKind() == kDoubleStackSlot;
  }

  static Location SIMDStackSlot(intptr_t stack_index, size_t num_of_slots = 0) {
    uintptr_t payload = EncodeStackIndex(stack_index);
    Location loc(kSIMDStackSlot, payload, num_of_slots * kVRegSize);
    // Ensure that sign is preserved.
    DCHECK_EQ(loc.GetStackIndex(), stack_index);
    return loc;
  }

  bool IsSIMDStackSlot() const {
    return GetKind() == kSIMDStackSlot;
  }

  static Location StackSlotByNumOfSlots(size_t num_of_slots, int spill_slot) {
    DCHECK_NE(num_of_slots, 0u);
    switch (num_of_slots) {
      case 1u:
        return Location::StackSlot(spill_slot);
      case 2u:
        return Location::DoubleStackSlot(spill_slot);
      default:
        // Assume all other stack slot sizes correspond to SIMD slot size.
        return Location::SIMDStackSlot(spill_slot, num_of_slots);
    }
  }

  intptr_t GetStackIndex() const {
    DCHECK(IsStackSlot() || IsDoubleStackSlot() || IsSIMDStackSlot());
    // Decode stack index manually to preserve sign.
    return GetPayload() - kStackIndexBias;
  }

  intptr_t GetHighStackIndex(uintptr_t word_size) const {
    DCHECK(IsDoubleStackSlot());
    // Decode stack index manually to preserve sign.
    return GetPayload() - kStackIndexBias + word_size;
  }

  Kind GetKind() const {
    return IsConstant() ? kConstant : KindField::Decode(value_);
  }

  bool Equals(Location other) const {
    // Handle the case of overlapping FP vector registers
    if (IsFpuRegister() && other.IsFpuRegister()) {
      return reg() == other.reg();
    } else {
      return value_ == other.value_;
    }
  }

  bool Contains(Location other) const {
    if (Equals(other)) {
      return true;
    } else if (IsPair() || IsDoubleStackSlot()) {
      return ToLow().Equals(other) || ToHigh().Equals(other);
    }
    return false;
  }

  bool OverlapsWith(Location other) const {
    // Only check the overlapping case that can happen with our register allocation algorithm.
    bool overlap = Contains(other) || other.Contains(*this);
    if (kIsDebugBuild && !overlap) {
      // Note: These are also overlapping cases. But we are not able to handle them in
      // ParallelMoveResolverWithSwap. Make sure that we do not meet such case with our compiler.
      if ((IsPair() && other.IsPair()) || (IsDoubleStackSlot() && other.IsDoubleStackSlot())) {
        DCHECK(!Contains(other.ToLow()));
        DCHECK(!Contains(other.ToHigh()));
      }
    }
    return overlap;
  }

  const char* DebugString() const {
    switch (GetKind()) {
      case kInvalid: return "I";
      case kRegister: return "R";
      case kStackSlot: return "S";
      case kDoubleStackSlot: return "DS";
      case kSIMDStackSlot: return "SIMD";
      case kUnallocated: return "U";
      case kConstant: return "C";
      case kFpuRegister: return "F";
      case kRegisterPair: return "RP";
      case kFpuRegisterPair: return "FP";
      case kVecRegister:
        return "V";
      case kDoNotUse5:  // fall-through
      case kDoNotUse9:
        LOG(FATAL) << "Should not use this location kind";
    }
    UNREACHABLE();
  }

  // Unallocated locations.
  enum Policy {
    kAny,
    kRequiresRegister,
    kRequiresFpuRegister,
    kSameAsFirstInput,
  };

  bool IsUnallocated() const {
    return GetKind() == kUnallocated;
  }

  static Location UnallocatedLocation(Policy policy) {
    return Location(kUnallocated, PolicyField::Encode(policy));
  }

  // Any free register is suitable to replace this unallocated location.
  static Location Any() {
    return UnallocatedLocation(kAny);
  }

  static Location RequiresRegister() {
    return UnallocatedLocation(kRequiresRegister);
  }

  static Location RequiresFpuRegister() {
    return UnallocatedLocation(kRequiresFpuRegister);
  }

  static Location RegisterOrConstant(HInstruction* instruction);
  static Location RegisterOrInt32Constant(HInstruction* instruction);
  static Location ByteRegisterOrConstant(int reg, HInstruction* instruction);
  static Location FpuRegisterOrConstant(HInstruction* instruction);
  static Location FpuRegisterOrInt32Constant(HInstruction* instruction);

  // The location of the first input to the instruction will be
  // used to replace this unallocated location.
  static Location SameAsFirstInput() {
    return UnallocatedLocation(kSameAsFirstInput);
  }

  Policy GetPolicy() const {
    DCHECK(IsUnallocated());
    return PolicyField::Decode(GetPayload());
  }

  bool RequiresRegisterKind() const {
    return GetPolicy() == kRequiresRegister || GetPolicy() == kRequiresFpuRegister;
  }

  uintptr_t GetEncoding() const {
    return GetPayload();
  }

  size_t GetVecLen() const {
    uint8_t decodedVecLen = GetVecLenAsPowerOf2();
    return (decodedVecLen > 0) ? (1 << decodedVecLen) : 0;
  }

  uint8_t GetVecLenAsPowerOf2() const {
    DCHECK(IsFpuRegister() || IsVecRegister() || IsSIMDStackSlot());
    return VecLenField::Decode(value_);
  }

 private:
  // Number of bits required to encode Kind value.
  static constexpr uint32_t kBitsForKind = 4;
  static constexpr uint32_t kBitsForVecLen = 4;
  static constexpr uint32_t kBitsForPayload = kBitsPerIntPtrT - (kBitsForKind + kBitsForVecLen);
  static constexpr uintptr_t kLocationConstantMask = 0x3;

  explicit Location(uintptr_t value) : value_(value) {}

  constexpr Location(Kind kind, uintptr_t payload, size_t vec_len = 0)
      : value_(KindField::Encode(kind) | VecLenField::Encode(0) | PayloadField::Encode(payload)) {
    if (vec_len > 0 && (kind == kFpuRegister || kind == kVecRegister || kind == kSIMDStackSlot)) {
      size_t vec_len_as_pow_of_2 = CTZ(vec_len);
      DCHECK_LE(vec_len_as_pow_of_2, 15U) << "Insufficient bits to represent vector length";
      value_ |= VecLenField::Encode(vec_len_as_pow_of_2);
    } else {
      DCHECK_EQ(vec_len, 0U) << "Invalid vector length on Location of kind - " << DebugString();
    }
  }

  uintptr_t GetPayload() const {
    return PayloadField::Decode(value_);
  }

  static void DCheckInstructionIsConstant(HInstruction* instruction);

  using KindField = BitField<Kind, 0, kBitsForKind>;
  using VecLenField = BitField<size_t, kBitsForKind, kBitsForVecLen>;
  using PayloadField = BitField<uintptr_t, kBitsForKind + kBitsForVecLen, kBitsForPayload>;

  // Layout for kUnallocated locations payload.
  using PolicyField = BitField<Policy, 0, 3>;

  // Layout for stack slots.
  static const intptr_t kStackIndexBias =
      static_cast<intptr_t>(1) << (kBitsForPayload - 1);

  // Location either contains kind and payload fields or a tagged handle for
  // a constant locations. Values of enumeration Kind are selected in such a
  // way that none of them can be interpreted as a kConstant tag.
  uintptr_t value_;
};
std::ostream& operator<<(std::ostream& os, Location::Kind rhs);
std::ostream& operator<<(std::ostream& os, Location::Policy rhs);

class RegisterSet : public ValueObject {
 public:
  static RegisterSet Empty() { return RegisterSet(); }
  static RegisterSet AllFpu() { return RegisterSet(0, -1); }

  void Add(Location loc) {
    if (loc.IsRegister()) {
      core_registers_ |= (1 << loc.reg());
    } else if (loc.IsFpuRegister()) {
      floating_point_registers_ |= (1 << loc.reg());
      DCHECK_IMPLIES(!has_overlapping_fp_vec_registers_, vector_registers_ == 0U)
          << "All FP Vec registers must either be overlapping/non-overlapping";
      if (loc.IsVecRegister()) {
        vector_registers_ |= (1 << loc.reg());
        DCHECK(vector_length_as_pow_of_2_ == 0 ||
               vector_length_as_pow_of_2_ == loc.GetVecLenAsPowerOf2())
            << "Unexpected vector length " << (1 << loc.GetVecLenAsPowerOf2());
        vector_length_as_pow_of_2_ = loc.GetVecLenAsPowerOf2();
        has_overlapping_fp_vec_registers_ = true;
      }
    } else {
      DCHECK(loc.IsVecRegister());
      DCHECK(!has_overlapping_fp_vec_registers_);
      DCHECK(vector_length_as_pow_of_2_ == 0 ||
             vector_length_as_pow_of_2_ == loc.GetVecLenAsPowerOf2())
          << "Unexpected vector length " << (1 << loc.GetVecLenAsPowerOf2());
      vector_length_as_pow_of_2_ = loc.GetVecLenAsPowerOf2();
      vector_registers_ |= (1 << loc.reg());
    }
  }

  void Remove(Location loc) {
    if (loc.IsRegister()) {
      core_registers_ &= ~(1 << loc.reg());
    } else if (loc.IsFpuRegister()) {
      floating_point_registers_ &= ~(1 << loc.reg());
      if (has_overlapping_fp_vec_registers_) {
        vector_registers_ &= ~(1 << loc.reg());
      }
    } else {
      DCHECK(loc.IsVecRegister()) << loc;
      DCHECK(!has_overlapping_fp_vec_registers_);
      vector_registers_ &= ~(1 << loc.reg());
    }
  }

  bool ContainsCoreRegister(uint32_t id) const {
    return Contains(core_registers_, id);
  }

  bool ContainsFloatingPointRegister(uint32_t id) const {
    return Contains(floating_point_registers_, id);
  }

  bool ContainsVectorRegister(uint32_t id) const { return Contains(vector_registers_, id); }

  static bool Contains(uint32_t register_set, uint32_t reg) {
    return (register_set & (1 << reg)) != 0;
  }

  bool OverlapsRegisters(Location out) {
    DCHECK(out.IsRegisterKind());
    switch (out.GetKind()) {
      case Location::Kind::kRegister:
        return ContainsCoreRegister(out.reg());
      case Location::Kind::kFpuRegister:
        return ContainsFloatingPointRegister(out.reg());
      case Location::Kind::kRegisterPair:
        return ContainsCoreRegister(out.low()) || ContainsCoreRegister(out.high());
      case Location::Kind::kFpuRegisterPair:
        return ContainsFloatingPointRegister(out.low()) ||
               ContainsFloatingPointRegister(out.high());
      default:
        return false;
    }
  }

  size_t GetNumberOfRegisters() const {
    size_t total = POPCOUNT(core_registers_) + POPCOUNT(floating_point_registers_);
    return has_overlapping_fp_vec_registers_ ? total : (total + GetNumberOfVectorRegisters());
  }

  size_t GetNumberOfVectorRegisters() const { return POPCOUNT(vector_registers_); }

  uint32_t GetCoreRegisters() const {
    return core_registers_;
  }

  uint32_t GetFloatingPointRegisters() const {
    return floating_point_registers_;
  }

  uint32_t GetVectorRegisters() const { return vector_registers_; }

  Location VecRegAsLocation(uint32_t reg_id) const {
    if (ContainsVectorRegister(reg_id)) {
      size_t vec_len = (vector_length_as_pow_of_2_ > 0) ? (1 << vector_length_as_pow_of_2_) : 0;
      return (has_overlapping_fp_vec_registers_) ? Location::FpuRegisterLocation(reg_id, vec_len) :
                                                   Location::VecRegisterLocation(reg_id, vec_len);
    }
    return Location::NoLocation();
  }

 private:
  RegisterSet()
      : core_registers_(0),
        floating_point_registers_(0),
        vector_registers_(0),
        vector_length_as_pow_of_2_(0),
        has_overlapping_fp_vec_registers_(false) {}
  RegisterSet(uint32_t core, uint32_t fp)
      : core_registers_(core),
        floating_point_registers_(fp),
        vector_registers_(0),
        vector_length_as_pow_of_2_(0),
        has_overlapping_fp_vec_registers_(false) {}
  RegisterSet(uint32_t core,
              uint32_t fp,
              uint32_t vecreg,
              uint8_t vec_length_as_pow_of_2,
              bool FPVecRegOverlap)
      : core_registers_(core),
        floating_point_registers_(fp),
        vector_registers_(vecreg),
        vector_length_as_pow_of_2_(vec_length_as_pow_of_2),
        has_overlapping_fp_vec_registers_(FPVecRegOverlap) {}

  uint32_t core_registers_;
  uint32_t floating_point_registers_;
  // TODO: Vector registers require vector length info as well, although not for all archs
  //  Storing vector length needs atleast 4 bits/reg => 16 bytes per RegisterSet/location summary
  //  For now we simplify by just assuming vector length to be fixed
  uint32_t vector_registers_;
  uint8_t vector_length_as_pow_of_2_;
  bool has_overlapping_fp_vec_registers_;
};

static constexpr bool kIntrinsified = true;

/**
 * The code generator computes LocationSummary for each instruction so that
 * the instruction itself knows what code to generate: where to find the inputs
 * and where to place the result.
 *
 * The intent is to have the code for generating the instruction independent of
 * register allocation. A register allocator just has to provide a LocationSummary.
 */
class LocationSummary : public ArenaObject<kArenaAllocLocationSummary> {
 public:
  enum CallKind {
    kNoCall,
    kCallOnMainAndSlowPath,
    kCallOnSlowPath,
    kCallOnMainOnly
  };

  explicit LocationSummary(HInstruction* instruction,
                           CallKind call_kind = kNoCall,
                           bool intrinsified = false);

  void SetInAt(uint32_t at, Location location) {
    inputs_[at] = location;
  }

  Location InAt(uint32_t at) const {
    return inputs_[at];
  }

  size_t GetInputCount() const {
    return inputs_.size();
  }

  // Set the output location.  Argument `overlaps` tells whether the
  // output overlaps any of the inputs (if so, it cannot share the
  // same register as one of the inputs); it is set to
  // `Location::kOutputOverlap` by default for safety.
  void SetOut(Location location, Location::OutputOverlap overlaps = Location::kOutputOverlap) {
    DCHECK(output_.IsInvalid());
    output_overlaps_ = overlaps;
    output_ = location;
  }

  void UpdateOut(Location location) {
    // There are two reasons for updating an output:
    // 1) Parameters, where we only know the exact stack slot after
    //    doing full register allocation.
    // 2) Unallocated location.
    DCHECK(output_.IsStackSlot() || output_.IsDoubleStackSlot() || output_.IsUnallocated());
    output_ = location;
  }

  void AddTemp(Location location) {
    temps_.push_back(location);
  }

  void AddRegisterTemps(size_t count) {
    for (size_t i = 0; i < count; ++i) {
      AddTemp(Location::RequiresRegister());
    }
  }

  Location GetTemp(uint32_t at) const {
    return temps_[at];
  }

  void SetTempAt(uint32_t at, Location location) {
    DCHECK(temps_[at].IsUnallocated() || temps_[at].IsInvalid());
    temps_[at] = location;
  }

  size_t GetTempCount() const {
    return temps_.size();
  }

  bool HasTemps() const { return !temps_.empty(); }

  Location Out() const { return output_; }

  bool CanCall() const {
    return call_kind_ != kNoCall;
  }

  bool WillCall() const {
    return call_kind_ == kCallOnMainOnly || call_kind_ == kCallOnMainAndSlowPath;
  }

  bool CallsOnSlowPath() const {
    return OnlyCallsOnSlowPath() || CallsOnMainAndSlowPath();
  }

  bool OnlyCallsOnSlowPath() const {
    return call_kind_ == kCallOnSlowPath;
  }

  bool NeedsSuspendCheckEntry() const {
    // Slow path calls do not need a SuspendCheck at method entry since they go into the runtime,
    // which we expect to either do a suspend check or return quickly.
    return WillCall();
  }

  bool CallsOnMainAndSlowPath() const {
    return call_kind_ == kCallOnMainAndSlowPath;
  }

  bool NeedsSafepoint() const {
    return CanCall();
  }

  void SetCustomSlowPathCallerSaves(const RegisterSet& caller_saves) {
    DCHECK(OnlyCallsOnSlowPath());
    has_custom_slow_path_calling_convention_ = true;
    custom_slow_path_caller_saves_ = caller_saves;
  }

  bool HasCustomSlowPathCallingConvention() const {
    return has_custom_slow_path_calling_convention_;
  }

  const RegisterSet& GetCustomSlowPathCallerSaves() const {
    DCHECK(HasCustomSlowPathCallingConvention());
    return custom_slow_path_caller_saves_;
  }

  void SetStackBit(uint32_t index) {
    stack_mask_->SetBit(index);
  }

  void ClearStackBit(uint32_t index) {
    stack_mask_->ClearBit(index);
  }

  void SetRegisterBit(uint32_t reg_id) {
    register_mask_ |= (1 << reg_id);
  }

  uint32_t GetRegisterMask() const {
    return register_mask_;
  }

  bool RegisterContainsObject(uint32_t reg_id) {
    return RegisterSet::Contains(register_mask_, reg_id);
  }

  void AddLiveRegister(Location location) {
    live_registers_.Add(location);
  }

  BitVector* GetStackMask() const {
    return stack_mask_;
  }

  RegisterSet* GetLiveRegisters() {
    return &live_registers_;
  }

  size_t GetNumberOfLiveRegisters() const {
    return live_registers_.GetNumberOfRegisters();
  }

  size_t GetNumLiveVectorRegisters() const { return live_registers_.GetNumberOfVectorRegisters(); }

  bool OutputUsesSameAs(uint32_t input_index) const {
    return (input_index == 0)
        && output_.IsUnallocated()
        && (output_.GetPolicy() == Location::kSameAsFirstInput);
  }

  bool IsFixedInput(uint32_t input_index) const {
    Location input = inputs_[input_index];
    return input.IsRegister()
        || input.IsFpuRegister()
        || input.IsPair()
        || input.IsStackSlot()
        || input.IsDoubleStackSlot();
  }

  bool OutputCanOverlapWithInputs() const {
    return output_overlaps_ == Location::kOutputOverlap;
  }

  bool Intrinsified() const {
    return intrinsified_;
  }

  Location LiveFPVecRegAsLocation(int reg_id) const {
    if (live_registers_.ContainsVectorRegister(reg_id)) {
      return LiveVecRegAsLocation(reg_id);
    } else if (live_registers_.ContainsFloatingPointRegister(reg_id)) {
      return Location::FpuRegisterLocation(reg_id);
    } else {
      return Location::NoLocation();
    }
  }

  Location LiveVecRegAsLocation(int reg_id) const {
    return live_registers_.VecRegAsLocation(reg_id);
  }

 private:
  LocationSummary(HInstruction* instruction,
                  CallKind call_kind,
                  bool intrinsified,
                  ArenaAllocator* allocator);

  ArenaVector<Location> inputs_;
  ArenaVector<Location> temps_;
  const CallKind call_kind_;
  // Whether these are locations for an intrinsified call.
  const bool intrinsified_;
  // Whether the slow path has default or custom calling convention.
  bool has_custom_slow_path_calling_convention_;
  // Whether the output overlaps with any of the inputs. If it overlaps, then it cannot
  // share the same register as the inputs.
  Location::OutputOverlap output_overlaps_;
  Location output_;

  // Mask of objects that live in the stack.
  BitVector* stack_mask_;

  // Mask of objects that live in register.
  uint32_t register_mask_;

  // Registers that are in use at this position.
  RegisterSet live_registers_;

  // Custom slow path caller saves. Valid only if indicated by slow_path_calling_convention_.
  RegisterSet custom_slow_path_caller_saves_;

  ART_FRIEND_TEST(RegisterAllocatorTest, ExpectedInRegisterHint);
  ART_FRIEND_TEST(RegisterAllocatorTest, SameAsFirstInputHint);
  DISALLOW_COPY_AND_ASSIGN(LocationSummary);
};

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_LOCATIONS_H_
