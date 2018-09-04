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

#include "dexanalyze_strings.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <queue>

#include "dex/class_accessor-inl.h"
#include "dex/code_item_accessors-inl.h"
#include "dex/dex_instruction-inl.h"

namespace art {
namespace dexanalyze {

// Tunable parameters.
static const size_t kMinPrefixLen = 1;
static const size_t kMaxPrefixLen = 255;
static const size_t kPrefixConstantCost = 4;
static const size_t kPrefixIndexCost = 2;

// Node value = (distance from root) * (occurrences - 1).
class MatchTrie {
 public:
  MatchTrie* Add(const std::string& str) {
    MatchTrie* node = this;
    size_t depth = 0u;
    for (uint8_t c : str) {
      ++depth;
      if (node->nodes_[c] == nullptr) {
        MatchTrie* new_node = new MatchTrie();
        node->nodes_[c].reset(new_node);
        new_node->parent_ = node;
        new_node->depth_ = depth;
        new_node->incoming_ = c;
        node = new_node;
      } else {
        node = node->nodes_[c].get();
      }
      ++node->count_;
    }
    return node;
  }

  // Returns the length of the longest prefix and if it's a leaf node.
  MatchTrie* LongestPrefix(const std::string& str) {
    MatchTrie* node = this;
    for (uint8_t c : str) {
      if (node->nodes_[c] == nullptr) {
        break;
      }
      node = node->nodes_[c].get();
    }
    return node;
  }

  bool IsLeaf() const {
    for (const std::unique_ptr<MatchTrie>& cur_node : nodes_) {
      if (cur_node != nullptr) {
        return false;
      }
    }
    return true;
  }

  int32_t Savings() const {
    int32_t cost = kPrefixConstantCost;
    int32_t first_used = 0u;
    if (chosen_suffix_count_ == 0u) {
      cost += depth_;
    }
    uint32_t extra_savings = 0u;
    for (MatchTrie* cur = parent_; cur != nullptr; cur = cur->parent_) {
      if (cur->chosen_) {
        first_used = cur->depth_;
        if (cur->chosen_suffix_count_ == 0u) {
          // First suffix for the chosen parent, remove the cost of the dictionary entry.
          extra_savings += first_used;
        }
        break;
      }
    }
    return count_ * (depth_ - first_used) - cost + extra_savings;
  }

  template <typename T, typename... Args, template <typename...> class Queue>
  T PopRealTop(Queue<T, Args...>& queue) {
    auto pair = queue.top();
    queue.pop();
    // Keep updating values until one sticks.
    while (pair.second->Savings() != pair.first) {
      pair.first = pair.second->Savings();
      queue.push(pair);
      pair = queue.top();
      queue.pop();
    }
    return pair;
  }

  std::vector<std::string> ExtractPrefixes(size_t max) {
    std::vector<std::string> ret;
    // Make priority queue and adaptively update it. Each node value is the savings from picking
    // it. Insert all of the interesting nodes in the queue (children != 1).
    std::priority_queue<std::pair<int32_t, MatchTrie*>> queue;
    // Add all of the nodes to the queue.
    std::vector<MatchTrie*> work(1, this);
    while (!work.empty()) {
      MatchTrie* elem = work.back();
      work.pop_back();
      size_t num_childs = 0u;
      for (const std::unique_ptr<MatchTrie>& child : elem->nodes_) {
        if (child != nullptr) {
          work.push_back(child.get());
          ++num_childs;
        }
      }
      if (num_childs > 1u || elem->value_ != 0u) {
        queue.emplace(elem->Savings(), elem);
      }
    }
    std::priority_queue<std::pair<int32_t, MatchTrie*>> prefixes;
    // The savings can only ever go down for a given node, never up.
    while (max != 0u && !queue.empty()) {
      std::pair<int32_t, MatchTrie*> pair = PopRealTop(queue);
      if (pair.second != this && pair.first > 0) {
        // Pick this node.
        uint32_t count = pair.second->count_;
        pair.second->chosen_ = true;
        for (MatchTrie* cur = pair.second->parent_; cur != this; cur = cur->parent_) {
          if (cur->chosen_) {
            break;
          }
          cur->count_ -= count;
        }
        for (MatchTrie* cur = pair.second->parent_; cur != this; cur = cur->parent_) {
          ++cur->chosen_suffix_count_;
        }
        prefixes.emplace(pair.first, pair.second);
        --max;
      } else {
        // Negative or no EV, just delete the node.
      }
    }
    while (!prefixes.empty()) {
      std::pair<int32_t, MatchTrie*> pair = PopRealTop(prefixes);
      if (pair.first <= 0) {
        continue;
      }
      ret.push_back(pair.second->GetString());
    }
    return ret;
  }

  std::string GetString() const {
    std::vector<uint8_t> chars;
    for (const MatchTrie* cur = this; cur->parent_ != nullptr; cur = cur->parent_) {
      chars.push_back(cur->incoming_);
    }
    return std::string(chars.rbegin(), chars.rend());
  }

  std::unique_ptr<MatchTrie> nodes_[256];
  MatchTrie* parent_ = nullptr;
  uint32_t count_ = 0u;
  uint32_t depth_ = 0u;
  int32_t savings_ = 0u;
  uint8_t incoming_ = 0u;
  // Value of the current node, non zero if the node is chosen.
  uint32_t value_ = 0u;
  // If the current node is chosen to be a used prefix.
  bool chosen_ = false;
  // If the current node is a prefix of a longer chosen prefix.
  uint32_t chosen_suffix_count_ = 0u;
};

void PrefixStrings::Builder::Build(const std::vector<std::string>& strings) {
  std::unique_ptr<MatchTrie> prefixe_trie(new MatchTrie());
  for (size_t i = 0; i < strings.size(); ++i) {
    size_t len = 0u;
    if (i > 0u) {
      CHECK_GT(strings[i], strings[i - 1]);
      len = std::max(len, PrefixLen(strings[i], strings[i - 1]));
    }
    if (i < strings.size() - 1) {
      len = std::max(len, PrefixLen(strings[i], strings[i + 1]));
    }
    len = std::min(len, kMaxPrefixLen);
    if (len >= kMinPrefixLen) {
      prefixe_trie->Add(strings[i].substr(0, len))->value_ = 1u;
    }
  }

  // Build prefixes.
  {
    static constexpr size_t kPrefixBits = 15;
    std::vector<std::string> prefixes(prefixe_trie->ExtractPrefixes(1 << kPrefixBits));
    // Add longest prefixes first so that subprefixes can share data.
    std::sort(prefixes.begin(), prefixes.end(), [](const std::string& a, const std::string& b) {
      return a.length() > b.length();
    });
    prefixe_trie.reset();
    prefixe_trie.reset(new MatchTrie());
    uint32_t prefix_idx = 0u;
    for (const std::string& str : prefixes) {
      uint32_t prefix_offset = 0u;
      MatchTrie* node = prefixe_trie->LongestPrefix(str);
      if (node != nullptr && node->depth_ == str.length() && node->value_ != 0u) {
        CHECK_EQ(node->GetString(), str);
        uint32_t existing_len = 0u;
        output_->dictionary_.GetOffset(node->value_, &prefix_offset, &existing_len);
        // Make sure to register the current node.
        prefixe_trie->Add(str)->value_ = prefix_idx;
      } else {
        auto add_str = [&](const std::string& s) {
          node = prefixe_trie->Add(s);
          node->value_ = prefix_idx;
          while (node != nullptr) {
            node->value_ = prefix_idx;
            node = node->parent_;
          }
        };
        static constexpr size_t kNumSubstrings = 1u;
        // Increasing kNumSubstrings provides savings since it enables common substrings and not
        // only prefixes to share data. The problem is that it's slow.
        for (size_t i = 0; i < std::min(str.length(), kNumSubstrings); ++i) {
          add_str(str.substr(i));
        }
        prefix_offset = output_->dictionary_.AddPrefixData(
            reinterpret_cast<const uint8_t*>(&str[0]),
            str.length());
      }
      // TODO: Validiate the prefix offset.
      CHECK_EQ(output_->dictionary_.AddOffset(prefix_offset, str.length()), prefix_idx);
      ++prefix_idx;
    }
  }

  // Add strings to the dictionary.
  for (const std::string& str : strings) {
    MatchTrie* node = prefixe_trie->LongestPrefix(str);
    uint32_t prefix_idx = 0u;
    uint32_t best_length = 0u;
    while (node != nullptr) {
      uint32_t offset = 0u;
      uint32_t length = 0u;
      output_->dictionary_.GetOffset(node->value_, &offset, &length);
      if (node->depth_ == length) {
        prefix_idx = node->value_;
        best_length = node->depth_;
        break;
        // Actually the prefix we want.
      }
      node = node->parent_;
    }
    output_->AddString(prefix_idx, str.substr(best_length));
  }
}

void AnalyzeStrings::ProcessDexFiles(const std::vector<std::unique_ptr<const DexFile>>& dex_files) {
  std::set<std::string> unique_strings;
  // Accumulate the strings.
  for (const std::unique_ptr<const DexFile>& dex_file : dex_files) {
    for (size_t i = 0; i < dex_file->NumStringIds(); ++i) {
      uint32_t length = 0;
      const char* data = dex_file->StringDataAndUtf16LengthByIdx(dex::StringIndex(i), &length);
      // Analyze if the string has any UTF16 chars.
      bool have_wide_char = false;
      const char* ptr = data;
      for (size_t j = 0; j < length; ++j) {
        have_wide_char = have_wide_char || GetUtf16FromUtf8(&ptr) >= 0x100;
      }
      if (have_wide_char) {
        wide_string_bytes_ += 2 * length;
      } else {
        ascii_string_bytes_ += length;
      }
      string_data_bytes_ += ptr - data;
      unique_strings.insert(data);
    }
  }
  // Unique strings only since we want to exclude savings from multi-dex duplication.
  ProcessStrings(std::vector<std::string>(unique_strings.begin(), unique_strings.end()));
}

void AnalyzeStrings::ProcessStrings(const std::vector<std::string>& strings) {
  // Calculate total shared prefix.
  size_t unique_string_data_bytes = 0u;
  size_t prefix_index_cost_ = 0u;
  for (size_t i = 0; i < strings.size(); ++i) {
    size_t best_len = 0;
    if (i > 0) {
      best_len = std::max(best_len, PrefixLen(strings[i], strings[i - 1]));
    }
    if (i < strings.size() - 1) {
      best_len = std::max(best_len, PrefixLen(strings[i], strings[i + 1]));
    }
    best_len = std::min(best_len, kMaxPrefixLen);
    if (best_len >= kMinPrefixLen) {
      total_shared_prefix_bytes_ += best_len;
    }
    prefix_index_cost_ += kPrefixIndexCost;
    unique_string_data_bytes += strings[i].length();
  }
  total_prefix_index_cost_ += prefix_index_cost_;
  total_unique_string_data_bytes_ += unique_string_data_bytes;

  PrefixStrings prefix_strings;
  {
    PrefixStrings::Builder prefix_builder(&prefix_strings);
    prefix_builder.Build(strings);
  }
  const size_t num_prefixes = prefix_strings.dictionary_.offsets_.size();
  total_num_prefixes_ += num_prefixes;
  total_prefix_table_ += num_prefixes * sizeof(prefix_strings.dictionary_.offsets_[0]);
  total_prefix_savings_ += unique_string_data_bytes - prefix_strings.chars_.size() +
    prefix_index_cost_;
  total_prefix_dict_ += prefix_strings.dictionary_.prefix_data_.size();
}

void AnalyzeStrings::Dump(std::ostream& os, uint64_t total_size) const {
  os << "Total string data bytes " << Percent(string_data_bytes_, total_size) << "\n";
  os << "Total unique string data bytes "
     << Percent(total_unique_string_data_bytes_, total_size) << "\n";
  os << "UTF-16 string data bytes " << Percent(wide_string_bytes_, total_size) << "\n";
  os << "ASCII string data bytes " << Percent(ascii_string_bytes_, total_size) << "\n";

  // Prefix based strings.
  os << "Total shared prefix bytes " << Percent(total_shared_prefix_bytes_, total_size) << "\n";
  os << "Prefix dictionary cost " << Percent(total_prefix_dict_, total_size) << "\n";
  os << "Prefix table cost " << Percent(total_prefix_table_, total_size) << "\n";
  os << "Prefix index cost " << Percent(total_prefix_index_cost_, total_size) << "\n";
  int64_t net_savings = total_prefix_savings_;
  net_savings -= total_prefix_dict_;
  net_savings -= total_prefix_table_;
  net_savings -= total_prefix_index_cost_;
  os << "Prefix dictionary elements " << total_num_prefixes_ << "\n";
  os << "Prefix base savings " << Percent(total_prefix_savings_, total_size) << "\n";
  os << "Prefix net savings " << Percent(net_savings, total_size) << "\n";
  os << "Strings using prefix "
     << Percent(strings_used_prefixed_, total_prefix_index_cost_ / kPrefixIndexCost) << "\n";
  os << "Short strings " << Percent(short_strings_, short_strings_ + long_strings_) << "\n";
  if (verbose_level_ >= VerboseLevel::kEverything) {
    std::vector<std::pair<std::string, size_t>> pairs;  // (prefixes_.begin(), prefixes_.end());
    // Sort lexicographically.
    std::sort(pairs.begin(), pairs.end());
    for (const auto& pair : pairs) {
      os << pair.first << " : " << pair.second << "\n";
    }
  }
}

}  // namespace dexanalyze
}  // namespace art
