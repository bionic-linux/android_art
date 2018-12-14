/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef ART_LIBARTBASE_BASE_HIDDENAPI_FLAGS_H_
#define ART_LIBARTBASE_BASE_HIDDENAPI_FLAGS_H_

#include "sdk_version.h"

#include "android-base/logging.h"

namespace art {
namespace hiddenapi {

/*
 * This class represents the information whether a field/method is in
 * public API (whitelist) or if it isn't, apps targeting which SDK
 * versions are allowed to access it.
 */
class ApiList {
 private:
  using IntValueType = uint32_t;

  enum class Value : IntValueType {
    // Values independent of target SDK version of app
    kWhitelist =     0,
    kGreylist =      1,
    kBlacklist =     2,

    // Values dependent on target SDK version of app. Put these last as
    // their list will be extended in future releases.
    // The max release code implicitly includes all maintenance releases,
    // e.g. GreylistMaxO is accessible to targetSdkVersion <= 27 (O_MR1).
    kGreylistMaxO =  3,
    kGreylistMaxP =  4,

    // Special values
    kInvalid =       static_cast<uint32_t>(-1),
    kMinValue =      kWhitelist,
    kMaxValue =      kGreylistMaxP,
  };

  enum class DomainApi : IntValueType {
    kCorePlatformApi = 0,

    kMinValue =        kCorePlatformApi,
    kMaxValue =        kCorePlatformApi,
  };

  static constexpr const char* kApiListNames[] = {
    "whitelist",
    "greylist",
    "blacklist",
    "greylist-max-o",
    "greylist-max-p",
  };

  static constexpr SdkVersion kMaxSdkVersions[] {
    /* whitelist */ SdkVersion::kMax,
    /* greylist */ SdkVersion::kMax,
    /* blacklist */ SdkVersion::kMin,
    /* greylist-max-o */ SdkVersion::kO_MR1,
    /* greylist-max-p */ SdkVersion::kP,
  };

  static constexpr const char* kDomainApiNames[] = {
    "core-platform-api",
  };

  static constexpr uint32_t GetDomainApiBitMask(DomainApi domain_api) {
    return 1u << static_cast<IntValueType>(domain_api);
  }

  explicit ApiList(Value value) : value_(value), domain_apis_(0u) {}
  explicit ApiList(DomainApi domain_api)
      : value_(Value::kInvalid), domain_apis_(GetDomainApiBitMask(domain_api)) {}
  ApiList(Value value, uint32_t domain_apis) : value_(value), domain_apis_(domain_apis) {}

  bool HasValue() const { return value_ != Value::kInvalid; }
  bool Hasdomain_apiFlags() const { return domain_apis_ != 0; }

  Value value_;
  uint32_t domain_apis_;

  static constexpr size_t kValueBitSize = 3;
  static constexpr uint32_t kValueBitMask = (1u << kValueBitSize) - 1;

 public:
  ApiList() : ApiList(Value::kInvalid) {}
  static ApiList Empty() { return ApiList(); }

  static ApiList Whitelist() { return ApiList(Value::kWhitelist); }
  static ApiList Greylist() { return ApiList(Value::kGreylist); }
  static ApiList Blacklist() { return ApiList(Value::kBlacklist); }
  static ApiList GreylistMaxO() { return ApiList(Value::kGreylistMaxO); }
  static ApiList GreylistMaxP() { return ApiList(Value::kGreylistMaxP); }

  static ApiList CorePlatformApi() { return ApiList(DomainApi::kCorePlatformApi); }

  // Decodes ApiList from dex hiddenapi flags.
  static ApiList FromDexFlags(uint32_t dex_flags) {
    uint32_t dex_api_list = dex_flags & kValueBitMask;
    uint32_t dex_domain_apis = dex_flags >> kValueBitSize;

    if (dex_api_list < static_cast<IntValueType>(Value::kMinValue) ||
        dex_api_list > static_cast<IntValueType>(Value::kMaxValue) ||
        dex_domain_apis > GetDomainApiBitMask(DomainApi::kMaxValue)) {
      return Empty();
    }

    return ApiList(static_cast<Value>(dex_api_list), dex_domain_apis);
  }

  uint32_t ToDexFlags() const {
    CHECK(IsValid());
    return static_cast<uint32_t>(static_cast<IntValueType>(value_)) |
           (domain_apis_ << kValueBitSize);
  }

  // Decodes ApiList from its integer value.
  static ApiList FromIntValue(IntValueType int_value) {
    CHECK_GE(int_value, static_cast<IntValueType>(Value::kMinValue));
    CHECK_LE(int_value, static_cast<IntValueType>(Value::kMaxValue));
    return ApiList(static_cast<Value>(int_value));
  }

  // Returns the ApiList with a given name.
  static ApiList FromName(const std::string& str) {
    for (IntValueType i = static_cast<IntValueType>(Value::kMinValue);
         i <= static_cast<IntValueType>(Value::kMaxValue);
         i++) {
      if (str == kApiListNames[i]) {
        return ApiList(static_cast<Value>(i));
      }
    }
    for (IntValueType i = static_cast<IntValueType>(DomainApi::kMinValue);
         i <= static_cast<IntValueType>(DomainApi::kMaxValue);
         i++) {
      if (str == kDomainApiNames[i]) {
        return ApiList(static_cast<DomainApi>(i));
      }
    }
    return Empty();
  }

  bool operator==(const ApiList other) const {
    return (value_ == other.value_) && (domain_apis_ == other.domain_apis_);
  }

  bool operator!=(const ApiList other) const { return !(*this == other); }

  const ApiList& operator|=(const ApiList other) {
    if (other.HasValue()) {
      if (HasValue()) {
        CHECK(value_ == other.value_) << "Value conflict. Cannot assign two different values";
      } else {
        value_ = other.value_;
      }
    }
    domain_apis_ |= other.domain_apis_;
    return *this;
  }

  // Returns true whether the configuration is valid for runtime use.
  bool IsValid() const { return HasValue(); }

  // Returns true when no ApiList is specified and no domain_api flags either.
  bool IsEmpty() const { return !HasValue() && !Hasdomain_apiFlags(); }

  // Returns true if all flags set by `other` are also set in `this`.
  bool Contains(const ApiList& other) const {
    return (!other.HasValue() || (value_ == other.value_)) &&
           ((other.domain_apis_ & domain_apis_) == other.domain_apis_);
  }

  IntValueType GetIntValue() const {
    DCHECK(IsValid());
    return static_cast<IntValueType>(value_);
  }

  void Dump(std::ostream& os) const {
    bool is_first = true;

    if (HasValue()) {
      os << kApiListNames[static_cast<IntValueType>(value_)];
      is_first = false;
    }

    if (Hasdomain_apiFlags()) {
      for (IntValueType i = static_cast<IntValueType>(DomainApi::kMinValue);
           i <= static_cast<IntValueType>(DomainApi::kMaxValue);
           i++) {
        if ((domain_apis_ & GetDomainApiBitMask(static_cast<DomainApi>(i))) != 0) {
          if (is_first) {
            is_first = false;
          } else {
            os << ",";
          }
          os << kDomainApiNames[i];
        }
      }
    }
  }

  SdkVersion GetMaxAllowedSdkVersion() const { return kMaxSdkVersions[GetIntValue()]; }

  static constexpr size_t kValueCount = static_cast<size_t>(Value::kMaxValue) + 1;
  static constexpr size_t kDomainApiCount = static_cast<size_t>(DomainApi::kMaxValue) + 1;

  static_assert((1u << kValueBitSize) >= kValueCount, "Not enough bits to store Value");
};

inline std::ostream& operator<<(std::ostream& os, ApiList value) {
  value.Dump(os);
  return os;
}

inline bool AreValidDexFlags(uint32_t dex_flags) {
  return ApiList::FromDexFlags(dex_flags).IsValid();
}

}  // namespace hiddenapi
}  // namespace art


#endif  // ART_LIBARTBASE_BASE_HIDDENAPI_FLAGS_H_
