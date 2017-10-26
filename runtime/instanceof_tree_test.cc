/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "instanceof_tree.h"

#include "gtest/gtest.h"
#include "android-base/logging.h"

namespace art {

constexpr size_t BitString::kBitSizeAtPosition[BitString::kCapacity];
constexpr size_t BitString::kCapacity;

};  // namespace art

using namespace art;

// These helper functions are only used by the test,
// so they are not in the main BitString class.
std::string Stringify(BitString bit_string) {
  std::stringstream ss;
  ss << bit_string;
  return ss.str();
}

BitChar MakeBitChar(size_t idx, size_t val) {
  return BitChar(val, BitString::MaybeGetBitLengthAtPosition(idx));
}

BitChar MakeBitChar(size_t val) {
  return BitChar(val, MinimumBitsToStore(val));
}

BitString MakeBitString(std::initializer_list<size_t> values = {}) {
  CHECK_GE(BitString::kCapacity, values.size());

  BitString bs{};  // NOLINT

  size_t i = 0;
  for (size_t val : values) {
    bs.SetAt(i, MakeBitChar(i, val));
    ++i;
  }

  return bs;
}

template <typename T>
size_t AsUint(const T& value) {
  size_t uint_value = 0;
  memcpy(&uint_value, &value, sizeof(value));
  return uint_value;
}

// Make max bistring, e.g. BitString[4095,7,255] for {12,3,8}
template <size_t kCount=BitString::kCapacity>
BitString MakeBitStringMax() {
  BitString bs{};

  for (size_t i = 0; i < kCount; ++i) {
    bs.SetAt(i,
             MakeBitChar(i, MaxInt<BitChar::StorageType>(BitString::kBitSizeAtPosition[i])));
  }

  return bs;
}

BitString SetBitCharAt(BitString bit_string, size_t i, size_t val) {
  BitString bs = bit_string;
  bs.SetAt(i, MakeBitChar(i, val));
  return bs;
}

struct MockClass {
  MockClass(MockClass* parent, size_t x = 0, size_t y = 0) {
    parent_ = parent;
    memset(&instance_of_and_status_, 0u, sizeof(instance_of_and_status_));

    // Start the numbering at '1' to match the bitstring numbering.
    // A bitstring numbering never starts at '0' which just means 'no value'.
    x_ = 1;
    if (parent_ != nullptr) {
      if (parent_->GetMaxChild() != nullptr) {
        x_ = parent_->GetMaxChild()->x_ + 1u;
      }

      parent_->children_.push_back(this);
      if (parent_->path_to_root_ != "") {
        path_to_root_ = parent->path_to_root_ + ",";
      }
      path_to_root_ += std::to_string(x_);
    } else {
      path_to_root_ = "";  // root has no path.
    }
    y_ = y;
    UNUSED(x);
  }

  MockClass() : MockClass(nullptr) {
  }

  ///////////////////////////////////////////////////////////////
  // Implementation of the InstanceOfTreeBase static interface.
  ///////////////////////////////////////////////////////////////
  MockClass* GetSuperClass() const {
    return parent_;
  }

  bool HasSuperClass() const {
    return GetSuperClass() != nullptr;
  }

  size_t Depth() const {
    if (parent_ == nullptr) {
      return 0u;
    } else {
      return parent_->Depth() + 1u;
    }
  }

  ///////////////////////////////////////////////////////////////
  // Convenience functions to make the testing easier
  ///////////////////////////////////////////////////////////////

  size_t GetNumberOfChildren() const {
    return children_.size();
  }

  MockClass* GetParent() const {
    return parent_;
  }

  MockClass* GetMaxChild() const {
    if (GetNumberOfChildren() > 0u) {
      return GetChild(GetNumberOfChildren() - 1);
    }
    return nullptr;
  }

  MockClass* GetChild(size_t idx) const {
    if (idx >= GetNumberOfChildren()) {
      return nullptr;
    }
    return children_[idx];
  }

  // Traverse the sibling at "X" at each level.
  // Once we get to level==depth, return yourself.
  MockClass* FindChildAt(size_t x, size_t depth) {
    if (Depth() == depth) {
      return this;
    } else if (GetNumberOfChildren() > 0) {
      return GetChild(x)->FindChildAt(x, depth);
    }
    return nullptr;
  }

  template <typename T>
  MockClass* Visit(T visitor, bool recursive=true) {
    if (!visitor(this)) {
      return this;
    }

    if (!recursive) {
      return this;
    }

    for (MockClass* child : children_) {
      MockClass* visit_res = child->Visit(visitor);
      if (visit_res != nullptr) {
        return visit_res;
      }
    }

    return nullptr;
  }

  size_t GetX() const {
    return x_;
  }

  bool SlowIsInstanceOf(const MockClass* target) const {
    DCHECK(target != nullptr);
    const MockClass* kls = this;
    while (kls != nullptr) {
      if (kls == target) {
        return true;
      }
      kls = kls->GetSuperClass();
    }

    return false;
  }

  std::string ToDotGraph() const {
    std::stringstream ss;
    ss << std::endl;
    ss << "digraph MockClass {" << std::endl;
    ss << "    node [fontname=\"Arial\"];" << std::endl;
    ToDotGraphImpl(ss);
    ss << "}" << std::endl;
    return ss.str();
  }

  void ToDotGraphImpl(std::ostream& os) const {
    for (MockClass* child : children_) {
      os << "    '" << path_to_root_ << "' -> '" << child->path_to_root_ << "';" << std::endl;
      child->ToDotGraphImpl(os);
    }
  }

  std::vector<MockClass*> children_;
  MockClass* parent_;
  InstanceOfAndStatusNew instance_of_and_status_;
  size_t x_;
  size_t y_;
  std::string path_to_root_;
};

std::ostream& operator<<(std::ostream& os, const MockClass& kls) {
  InstanceOfData iod = kls.instance_of_and_status_.instance_of_;
  os << "MClass{D:" << kls.Depth() << ",W:" << kls.x_
     << ", OF:"
     << (iod.overflow_ ? "true" : "false")
     << ", bitstring: " << iod.bitstring_
     << ", mock_path: " << kls.path_to_root_
     << "}";
  return os;
}

struct MockInstanceOfTree : public InstanceOfTreeBase<MockClass*, MockInstanceOfTree> {
  static InstanceOfAndStatusNew ReadField(MockClass* klass)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return klass->instance_of_and_status_;
  }

  static void WriteField(MockClass* klass, const InstanceOfAndStatusNew& new_ios)
      REQUIRES(Locks::bitstring_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    klass->instance_of_and_status_ = new_ios;
  }

  InstanceOf::State GetState() const
      REQUIRES(Locks::bitstring_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    return GetInstanceOf().GetState();
  }

  MockClass& GetClass() const {
    return *klass_;
  }
};

std::ostream& operator<<(std::ostream& os, const MockInstanceOfTree& tree) {
  return os << tree.GetClass();
}

struct MockScopedLockBitstring {
  MockScopedLockBitstring() ACQUIRE(*Locks::bitstring_lock_) {}
  ~MockScopedLockBitstring() RELEASE(*Locks::bitstring_lock_) {}
};

struct MockScopedLockMutator {
  MockScopedLockMutator() ACQUIRE_SHARED(*Locks::mutator_lock_) {}
  ~MockScopedLockMutator() RELEASE_SHARED(*Locks::mutator_lock_) {}
};

struct InstanceOfTreeTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    android::base::InitLogging(/*argv*/nullptr);

    CreateRootedTree(BitString::kCapacity + 2u, BitString::kCapacity + 2u);
  }

  virtual void TearDown() {
  }

  void CreateRootedTree(size_t width, size_t height) {
    all_classes_.clear();
    root_ = CreateClassFor(/*parent*/nullptr, /*x*/0, /*y*/0);
    CreateTreeFor(root_, /*width*/width, /*depth*/height);
  }

  MockClass* CreateClassFor(MockClass* parent, size_t x, size_t y) {
    MockClass* kls = new MockClass(parent, x, y);
    all_classes_.push_back(std::unique_ptr<MockClass>(kls));
    return kls;
  }

  void CreateTreeFor(MockClass* parent, size_t width, size_t levels) {
    DCHECK(parent != nullptr);
    if (levels == 0) {
      return;
    }

    for (size_t i = 0; i < width; ++i) {
      MockClass* child = CreateClassFor(parent, i, parent->y_ + 1);
      CreateTreeFor(child, width, levels - 1);
    }
  }

  MockClass* root_ = nullptr;
  std::vector<std::unique_ptr<MockClass>> all_classes_;
};

TEST_F(InstanceOfTreeTest, LookupAllChildren) {
  MockScopedLockBitstring lock_a;
  MockScopedLockMutator lock_b;
  using IOTree = MockInstanceOfTree;

  root_->Visit([&](MockClass* kls) {
    MockScopedLockBitstring lock_a;
    MockScopedLockMutator lock_b;

    EXPECT_EQ(InstanceOf::kUninitialized, IOTree::Lookup(kls).GetState());
    return true;  // Keep visiting.
  });
}

TEST_F(InstanceOfTreeTest, LookupRoot) {
  MockScopedLockBitstring lock_a;
  MockScopedLockMutator lock_b;
  using IOTree = MockInstanceOfTree;

  IOTree root = IOTree::Lookup(root_);
  EXPECT_EQ(InstanceOf::kAssigned, root.EnsureInitialized());

  EXPECT_EQ(InstanceOf::kInstanceOf, root.IsInstanceOf(root)) << root;
}

TEST_F(InstanceOfTreeTest, EnsureInitializedFirstLevel) {
  MockScopedLockBitstring lock_a;
  MockScopedLockMutator lock_b;
  using IOTree = MockInstanceOfTree;

  IOTree root = IOTree::Lookup(root_);
  EXPECT_EQ(InstanceOf::kAssigned, root.EnsureInitialized());

  ASSERT_LT(0u, root_->GetNumberOfChildren());

  // Initialize root's children only.
  for (size_t i = 0; i < root_->GetNumberOfChildren(); ++i) {
    MockClass* child = root_->GetChild(i);
    IOTree child_tree = IOTree::Lookup(child);
    // Before: all unknown.
    EXPECT_EQ(InstanceOf::kUnknownInstanceOf, root.IsInstanceOf(child_tree)) << child_tree;
    EXPECT_EQ(InstanceOf::kUnknownInstanceOf, child_tree.IsInstanceOf(root)) << child_tree;
    // Transition.
    EXPECT_EQ(InstanceOf::kInitialized, child_tree.EnsureInitialized());
    // After: "src instanceof target" known, but "target instanceof src" unknown.
    EXPECT_EQ(InstanceOf::kInstanceOf, child_tree.IsInstanceOf(root)) << child_tree;
    EXPECT_EQ(InstanceOf::kUnknownInstanceOf, root.IsInstanceOf(child_tree)) << child_tree;
  }
}

TEST_F(InstanceOfTreeTest, EnsureAssignedFirstLevel) {
  MockScopedLockBitstring lock_a;
  MockScopedLockMutator lock_b;
  using IOTree = MockInstanceOfTree;

  IOTree root = IOTree::Lookup(root_);
  EXPECT_EQ(InstanceOf::kAssigned, root.EnsureInitialized());

  ASSERT_LT(0u, root_->GetNumberOfChildren());

  // Initialize root's children only.
  for (size_t i = 0; i < root_->GetNumberOfChildren(); ++i) {
    MockClass* child = root_->GetChild(i);
    IOTree child_tree = IOTree::Lookup(child);
    // Before: all unknown.
    EXPECT_EQ(InstanceOf::kUnknownInstanceOf, root.IsInstanceOf(child_tree)) << child_tree;
    EXPECT_EQ(InstanceOf::kUnknownInstanceOf, child_tree.IsInstanceOf(root)) << child_tree;
    // Transition.
    EXPECT_EQ(InstanceOf::kAssigned, child_tree.EnsureAssigned());
    // After: "src instanceof target" known, and "target instanceof src" known.
    EXPECT_EQ(InstanceOf::kInstanceOf, child_tree.IsInstanceOf(root)) << child_tree;
    EXPECT_EQ(InstanceOf::kNotInstanceOf, root.IsInstanceOf(child_tree)) << child_tree;
  }
}

TEST_F(InstanceOfTreeTest, EnsureInitializedSecondLevelWithPreassign) {
  MockScopedLockBitstring lock_a;
  MockScopedLockMutator lock_b;
  using IOTree = MockInstanceOfTree;

  IOTree root = IOTree::Lookup(root_);
  EXPECT_EQ(InstanceOf::kAssigned, root.EnsureInitialized());

  ASSERT_LT(0u, root_->GetNumberOfChildren());

  // Initialize root's children.
  for (size_t i = 0; i < root_->GetNumberOfChildren(); ++i) {
    MockClass* child = root_->GetChild(i);
    IOTree child_tree = IOTree::Lookup(child);

    ASSERT_EQ(1u, child->Depth());

    EXPECT_EQ(InstanceOf::kInitialized, child_tree.EnsureInitialized()) << *child;
    EXPECT_EQ(InstanceOf::kAssigned, child_tree.EnsureAssigned()) << *child << ", root:" << *root_;
    for (size_t j = 0; j < child->GetNumberOfChildren(); ++j) {
      MockClass* child2 = child->GetChild(j);
      ASSERT_EQ(2u, child2->Depth());
      IOTree child2_tree = IOTree::Lookup(child2);

      // Before: all unknown.
      EXPECT_EQ(InstanceOf::kUnknownInstanceOf, root.IsInstanceOf(child2_tree)) << child2_tree;
      EXPECT_EQ(InstanceOf::kUnknownInstanceOf, child_tree.IsInstanceOf(child2_tree)) << child2_tree;
      EXPECT_EQ(InstanceOf::kUnknownInstanceOf, child2_tree.IsInstanceOf(root)) << child2_tree;
      EXPECT_EQ(InstanceOf::kUnknownInstanceOf, child2_tree.IsInstanceOf(child_tree)) << child2_tree;

      EXPECT_EQ(InstanceOf::kUninitialized, child2_tree.GetState()) << *child2;
      EXPECT_EQ(InstanceOf::kInitialized, child2_tree.EnsureInitialized()) << *child2;

      // After: src=child2_tree is known, otherwise unknown.
      EXPECT_EQ(InstanceOf::kUnknownInstanceOf, root.IsInstanceOf(child2_tree)) << child2_tree;
      EXPECT_EQ(InstanceOf::kUnknownInstanceOf, child_tree.IsInstanceOf(child2_tree)) << child2_tree;
      EXPECT_EQ(InstanceOf::kInstanceOf, child2_tree.IsInstanceOf(root)) << child2_tree;
      EXPECT_EQ(InstanceOf::kInstanceOf, child2_tree.IsInstanceOf(child_tree)) << child2_tree;
    }

    // The child is "assigned" as a side-effect of initializing sub-children.
    EXPECT_EQ(InstanceOf::kAssigned, child_tree.GetState());
  }
}

TEST_F(InstanceOfTreeTest, EnsureInitializedSecondLevelDontPreassign) {
  MockScopedLockBitstring lock_a;
  MockScopedLockMutator lock_b;
  using IOTree = MockInstanceOfTree;

  IOTree root = IOTree::Lookup(root_);
  EXPECT_EQ(InstanceOf::kAssigned, root.EnsureInitialized());

  ASSERT_LT(0u, root_->GetNumberOfChildren());

  // Initialize root's children only.
  for (size_t i = 0; i < root_->GetNumberOfChildren(); ++i) {
    MockClass* child = root_->GetChild(i);
    IOTree child_tree = IOTree::Lookup(child);

    ASSERT_EQ(1u, child->Depth());

    for (size_t j = 0; j < child->GetNumberOfChildren(); ++j) {
      MockClass* child2 = child->GetChild(j);
      ASSERT_EQ(2u, child2->Depth());
      IOTree child2_tree = IOTree::Lookup(child2);
      // Before: all unknown.
      EXPECT_EQ(InstanceOf::kUnknownInstanceOf, root.IsInstanceOf(child2_tree)) << child2_tree;
      EXPECT_EQ(InstanceOf::kUnknownInstanceOf, child_tree.IsInstanceOf(child2_tree)) << child2_tree;
      EXPECT_EQ(InstanceOf::kUnknownInstanceOf, child2_tree.IsInstanceOf(root)) << child2_tree;
      EXPECT_EQ(InstanceOf::kUnknownInstanceOf, child2_tree.IsInstanceOf(child_tree)) << child2_tree;
      // Transition.
      EXPECT_EQ(InstanceOf::kUninitialized, child2_tree.GetState()) << *child2;
      EXPECT_EQ(InstanceOf::kInitialized, child2_tree.EnsureInitialized()) << *child2;
      // After: src=child2_tree is known, otherwise unknown.
      EXPECT_EQ(InstanceOf::kUnknownInstanceOf, root.IsInstanceOf(child2_tree)) << child2_tree;
      EXPECT_EQ(InstanceOf::kUnknownInstanceOf, child_tree.IsInstanceOf(child2_tree)) << child2_tree;
      EXPECT_EQ(InstanceOf::kInstanceOf, child2_tree.IsInstanceOf(root)) << child2_tree;
      EXPECT_EQ(InstanceOf::kInstanceOf, child2_tree.IsInstanceOf(child_tree)) << child2_tree;
    }

    // The child is "assigned" as a side-effect of initializing sub-children.
    EXPECT_EQ(InstanceOf::kAssigned, child_tree.GetState());
  }
}

void ApplyTransition(MockInstanceOfTree io_tree, InstanceOf::State transition, InstanceOf::State expected) {
  MockScopedLockBitstring lock_a;
  MockScopedLockMutator lock_b;

  EXPECT_EQ(InstanceOf::kUninitialized, io_tree.GetState()) << io_tree.GetClass();

  if (transition == InstanceOf::kUninitialized) {
    EXPECT_EQ(expected, io_tree.ForceUninitialize()) << io_tree.GetClass();
  } else if (transition == InstanceOf::kInitialized) {
    EXPECT_EQ(expected, io_tree.EnsureInitialized()) << io_tree.GetClass();
  } else if (transition == InstanceOf::kAssigned) {
    EXPECT_EQ(expected, io_tree.EnsureAssigned()) << io_tree.GetClass();
  }
}

enum MockInstanceOfTransition {
  kNone,
  kUninitialized,
  kInitialized,
  kAssigned,
};

std::ostream& operator<<(std::ostream& os, const MockInstanceOfTransition& transition) {
  if (transition == MockInstanceOfTransition::kUninitialized) {
    os << "kUninitialized";
  } else if (transition == MockInstanceOfTransition::kInitialized) {
    os << "kInitialized";
  } else if (transition == MockInstanceOfTransition::kAssigned) {
    os << "kAssigned";
  } else {
    os << "kNone";
  }
  return os;
}

InstanceOf::State ApplyTransition(MockInstanceOfTree io_tree, MockInstanceOfTransition transition) {
  MockScopedLockBitstring lock_a;
  MockScopedLockMutator lock_b;

  if (transition ==  MockInstanceOfTransition::kUninitialized) {
    return io_tree.ForceUninitialize();
  } else if (transition == MockInstanceOfTransition::kInitialized) {
    return io_tree.EnsureInitialized();
  } else if (transition == MockInstanceOfTransition::kAssigned) {
    return io_tree.EnsureAssigned();
  }

  return io_tree.GetState();
}

enum {
  kBeforeTransition = 0,
  kAfterTransition = 1,
  kAfterChildren = 2,
};

const char* StringifyTransition(int x) {
  if (x == kBeforeTransition) {
    return "kBeforeTransition";
  } else if (x == kAfterTransition) {
    return "kAfterTransition";
  } else if (x == kAfterChildren) {
    return "kAfterChildren";
  }

  return "<<Unknown>>";
}

struct TransitionHistory {
  void Record(int transition_label, MockClass* kls) {
    ss_ << "<<<" << StringifyTransition(transition_label) << ">>>";
    ss_ << "{Self}: " << *kls;

    if (kls->HasSuperClass()) {
      ss_ << "{Parent}: " << *(kls->GetSuperClass());
    }
    ss_ << "================== ";
  }

  friend std::ostream& operator<<(std::ostream& os, const TransitionHistory& t) {
    os << t.ss_.str();
    return os;
  }

  std::stringstream ss_;
};

template <typename T, typename T2>
void EnsureStateChangedTestRecursiveGeneric(MockClass* klass, size_t cur_depth, size_t total_depth, T2 transition_func, T expect_checks) {
  MockScopedLockBitstring lock_a;
  MockScopedLockMutator lock_b;
  using IOTree = MockInstanceOfTree;

  IOTree io_tree = IOTree::Lookup(klass);
  MockInstanceOfTransition requested_transition = transition_func(klass);

  // FIXME: need to print before(self, parent) and after(self, parent)
  // to make any sense of what's going on.

  auto do_expect_checks = [&](int transition_label, TransitionHistory& transition_details) {
    MockScopedLockBitstring lock_a;
    MockScopedLockMutator lock_b;

    transition_details.Record(transition_label, klass);

    SCOPED_TRACE(transition_details);
    ASSERT_EQ(cur_depth, klass->Depth());

    ASSERT_NO_FATAL_FAILURE(expect_checks(klass,
                                          transition_label,
                                          io_tree.GetState(),
                                          requested_transition));
  };

  TransitionHistory transition_history;
  do_expect_checks(kBeforeTransition, transition_history);
  InstanceOf::State state = ApplyTransition(io_tree, requested_transition);
  UNUSED(state);
  do_expect_checks(kAfterTransition, transition_history);

  if (total_depth == cur_depth) {
    return;
  }

  // Initialize root's children only.
  for (size_t i = 0; i < klass->GetNumberOfChildren(); ++i) {
    MockClass* child = klass->GetChild(i);
    EnsureStateChangedTestRecursiveGeneric(child, cur_depth + 1u, total_depth, transition_func, expect_checks);
  }

  do_expect_checks(kAfterChildren, transition_history);
}

void EnsureStateChangedTestRecursive(MockClass* klass, size_t cur_depth, size_t total_depth, std::vector<std::pair<InstanceOf::State, InstanceOf::State>> transitions) {
  MockScopedLockBitstring lock_a;
  MockScopedLockMutator lock_b;
  using IOTree = MockInstanceOfTree;

  ASSERT_EQ(cur_depth, klass->Depth());
  ApplyTransition(IOTree::Lookup(klass), transitions[cur_depth].first, transitions[cur_depth].second);

  if (total_depth == cur_depth + 1) {
    return;
  }

  // Initialize root's children only.
  for (size_t i = 0; i < klass->GetNumberOfChildren(); ++i) {
    MockClass* child = klass->GetChild(i);
    EnsureStateChangedTestRecursive(child, cur_depth + 1u, total_depth, transitions);
  }
}

void EnsureStateChangedTest(MockClass* root, size_t depth, std::vector<std::pair<InstanceOf::State, InstanceOf::State>> transitions) {
  ASSERT_EQ(depth, transitions.size());

  EnsureStateChangedTestRecursive(root, /*cur_depth*/0u, depth, transitions);
}

TEST_F(InstanceOfTreeTest, EnsureInitialized_NoOverflow) {
  auto transitions = [](MockClass* kls) {
    UNUSED(kls);
    return MockInstanceOfTransition::kInitialized;
  };

  constexpr size_t kMaxDepthForThisTest = BitString::kCapacity;
  auto expected = [=](MockClass* kls, int expect_when, InstanceOf::State actual_state, MockInstanceOfTransition transition) {
    if (expect_when == kBeforeTransition) {
      EXPECT_EQ(InstanceOf::kUninitialized, actual_state);
      return;
    }

    if (expect_when == kAfterTransition) {
      // After explicit transition has been completed.
      switch (kls->Depth()) {
      case 0:
        if (transition >= MockInstanceOfTransition::kInitialized) {
          EXPECT_EQ(InstanceOf::kAssigned, actual_state);
        }
        break;
      default:
        if (transition >= MockInstanceOfTransition::kInitialized) {
          if (transition == MockInstanceOfTransition::kInitialized) {
            EXPECT_EQ(InstanceOf::kInitialized, actual_state);
          } else if (transition == MockInstanceOfTransition::kAssigned) {
            EXPECT_EQ(InstanceOf::kAssigned, actual_state);
          }
        }
        break;
      }
    }

    if (expect_when == kAfterChildren) {
      if (transition >= MockInstanceOfTransition::kInitialized) {
        ASSERT_NE(kls->Depth(), kMaxDepthForThisTest);
        EXPECT_EQ(InstanceOf::kAssigned, actual_state);
      }
    }
  };

  // Initialize every level 0-3.
  // Intermediate levels become "assigned", max levels become initialized.
  EnsureStateChangedTestRecursiveGeneric(root_, 0u, kMaxDepthForThisTest, transitions, expected);

  auto transitions_uninitialize = [](MockClass* kls) {
    UNUSED(kls);
    return MockInstanceOfTransition::kUninitialized;
  };

  auto expected_uninitialize = [](MockClass* kls, int expect_when, InstanceOf::State actual_state, MockInstanceOfTransition transition) {
    UNUSED(kls);
    UNUSED(transition);
    if (expect_when >= kAfterTransition) {
      EXPECT_EQ(InstanceOf::kUninitialized, actual_state);
    }
  };

  // Uninitialize the entire tree after it was assigned.
  EnsureStateChangedTestRecursiveGeneric(root_, 0u, kMaxDepthForThisTest, transitions_uninitialize, expected_uninitialize);
}

TEST_F(InstanceOfTreeTest, EnsureAssigned_TooDeep) {
  auto transitions = [](MockClass* kls) {
    UNUSED(kls);
    return MockInstanceOfTransition::kAssigned;
  };

  constexpr size_t kMaxDepthForThisTest = BitString::kCapacity + 1u;
  auto expected = [=](MockClass* kls, int expect_when, InstanceOf::State actual_state, MockInstanceOfTransition transition) {
    UNUSED(transition);
    if (expect_when == kAfterTransition) {
      if (kls->Depth() > BitString::kCapacity) {
        EXPECT_EQ(InstanceOf::kOverflowed, actual_state);
      }
    }
  };

  // Assign every level 0-4.
  // We cannot assign 4th level, so it will overflow instead.
  EnsureStateChangedTestRecursiveGeneric(root_, 0u, kMaxDepthForThisTest, transitions, expected);
}

TEST_F(InstanceOfTreeTest, EnsureAssigned_TooDeep_OfTooDeep) {
  auto transitions = [](MockClass* kls) {
    UNUSED(kls);
    return MockInstanceOfTransition::kAssigned;
  };

  constexpr size_t kMaxDepthForThisTest = BitString::kCapacity + 2u;
  auto expected = [=](MockClass* kls, int expect_when, InstanceOf::State actual_state, MockInstanceOfTransition transition) {
    UNUSED(transition);
    if (expect_when == kAfterTransition) {
      if (kls->Depth() > BitString::kCapacity) {
        EXPECT_EQ(InstanceOf::kOverflowed, actual_state);
      }
    }
  };

  // Assign every level 0-5.
  // We cannot assign 4th level, so it will overflow instead.
  // In addition, level 5th cannot be assigned (parent is overflowed), so it will also fail.
  EnsureStateChangedTestRecursiveGeneric(root_, 0u, kMaxDepthForThisTest, transitions, expected);
}

constexpr size_t MaxWidthCutOff(size_t depth) {
  if (depth == 0) {
    return 1;
  }
  return MaxInt<size_t>(BitString::kBitSizeAtPosition[depth - 1]);
}

// Either itself is too wide, or any of the parents were too wide.
bool IsTooWide(MockClass* kls) {
  if (kls == nullptr || kls->Depth() == 0u) {
    // Root is never too wide.
    return false;
  } else {
    if (kls->GetX() >= MaxWidthCutOff(kls->Depth())) {
      return true;
    }
  }
  return IsTooWide(kls->GetParent());
};

// Either itself is too deep, or any of the parents were too deep.
bool IsTooDeep(MockClass* kls) {
  if (kls == nullptr || kls->Depth() == 0u) {
    // Root is never too deep.
    return false;
  } else {
    if (kls->Depth() > BitString::kCapacity) {
      return true;
    }
  }
  return false;
};

TEST_F(InstanceOfTreeTest, EnsureInitialized_TooWide) {
  auto transitions = [](MockClass* kls) {
    UNUSED(kls);
    return MockInstanceOfTransition::kAssigned;
  };

  // Pick the 2nd level because has the most narrow # of bits.
  constexpr size_t kTargetDepth = 2;
  constexpr size_t kMaxWidthCutOff = MaxWidthCutOff(kTargetDepth);

  constexpr size_t kMaxDepthForThisTest = std::numeric_limits<size_t>::max();
  auto expected = [=](MockClass* kls, int expect_when, InstanceOf::State actual_state, MockInstanceOfTransition transition) {
    UNUSED(transition);
    // Note: purposefuly ignore the too-deep children in the premade tree.
    if (expect_when == kAfterTransition && kls->Depth() <= BitString::kCapacity) {
      if (IsTooWide(kls)) {
        EXPECT_EQ(InstanceOf::kOverflowed, actual_state);
      } else {
        EXPECT_EQ(InstanceOf::kAssigned, actual_state);
      }
    }
  };

  {
    // Create too-wide siblings at the kTargetDepth level.
    MockClass* child = root_->FindChildAt(/*x*/0, kTargetDepth - 1u);
    CreateTreeFor(child, kMaxWidthCutOff*2, /*depth*/1);
    ASSERT_LE(kMaxWidthCutOff*2, child->GetNumberOfChildren());
    ASSERT_TRUE(IsTooWide(child->GetMaxChild())) << *(child->GetMaxChild());
    // Leave the rest of the tree as the default.
  }

  // Try to assign every level
  // It will fail once it gets to the "too wide" siblings and cause overflows.
  EnsureStateChangedTestRecursiveGeneric(root_, 0u, kMaxDepthForThisTest, transitions, expected);
}

TEST_F(InstanceOfTreeTest, EnsureInitialized_TooWide_TooWide) {
  auto transitions = [](MockClass* kls) {
    UNUSED(kls);
    return MockInstanceOfTransition::kAssigned;
  };

  // Pick the 2nd level because has the most narrow # of bits.
  constexpr size_t kTargetDepth = 2;
  constexpr size_t kMaxWidthCutOff = MaxWidthCutOff(kTargetDepth);
  constexpr size_t kMaxWidthCutOffSub = MaxWidthCutOff(kTargetDepth+1u);

  constexpr size_t kMaxDepthForThisTest = std::numeric_limits<size_t>::max();
  auto expected = [=](MockClass* kls, int expect_when, InstanceOf::State actual_state, MockInstanceOfTransition transition) {
    UNUSED(transition);
    // Note: purposefuly ignore the too-deep children in the premade tree.
    if (expect_when == kAfterTransition && kls->Depth() <= BitString::kCapacity) {
      if (IsTooWide(kls)) {
        EXPECT_EQ(InstanceOf::kOverflowed, actual_state);
      } else {
        EXPECT_EQ(InstanceOf::kAssigned, actual_state);
      }
    }
  };

  {
    // Create too-wide siblings at the kTargetDepth level.
    MockClass* child = root_->FindChildAt(/*x*/0, kTargetDepth - 1);
    CreateTreeFor(child, kMaxWidthCutOff*2, /*depth*/1);
    ASSERT_LE(kMaxWidthCutOff*2, child->GetNumberOfChildren()) << *child;
    ASSERT_TRUE(IsTooWide(child->GetMaxChild())) << *(child->GetMaxChild());
    // Leave the rest of the tree as the default.

    // Create too-wide children for a too-wide parent.
    MockClass* child_subchild = child->FindChildAt(/*x*/0, kTargetDepth);
    CreateTreeFor(child_subchild, kMaxWidthCutOffSub*2, /*depth*/1);
    ASSERT_LE(kMaxWidthCutOffSub*2, child_subchild->GetNumberOfChildren()) << *child_subchild;
    ASSERT_TRUE(IsTooWide(child_subchild->GetMaxChild())) << *(child_subchild->GetMaxChild());
  }

  // Try to assign every level
  // It will fail once it gets to the "too wide" siblings and cause overflows.
  // Furthermore, assigning any subtree whose ancestor is too wide will also fail.
  EnsureStateChangedTestRecursiveGeneric(root_, 0u, kMaxDepthForThisTest, transitions, expected);
}

void EnsureInstanceOfCorrect(MockClass* a, MockClass* b) {
  MockScopedLockBitstring lock_a;
  MockScopedLockMutator lock_b;
  using IOTree = MockInstanceOfTree;

  auto IsAssigned = [](IOTree& tree) {
    MockScopedLockBitstring lock_a;
    MockScopedLockMutator lock_b;
    // This assumes that MockClass is always called with EnsureAssigned.
    EXPECT_NE(InstanceOf::kInitialized, tree.GetState());
    EXPECT_NE(InstanceOf::kUninitialized, tree.GetState());
    // Use our own test checks, so we are actually testing different logic than the impl.
    return !(IsTooDeep(&tree.GetClass()) || IsTooWide(&tree.GetClass()));
  };

  IOTree src_tree = IOTree::Lookup(a);
  IOTree target_tree = IOTree::Lookup(b);

  SCOPED_TRACE("class A");
  SCOPED_TRACE(*a);
  SCOPED_TRACE("class B");
  SCOPED_TRACE(*b);

  InstanceOf::Result slow_result =
      a->SlowIsInstanceOf(b) ? InstanceOf::kInstanceOf : InstanceOf::kNotInstanceOf;
  InstanceOf::Result fast_result = src_tree.IsInstanceOf(target_tree);

  // Target must be Assigned for this check to succeed.
  // Source is either Overflowed | Assigned (in this case).

  if (IsAssigned(src_tree) && IsAssigned(target_tree)) {
    ASSERT_EQ(slow_result, fast_result);
  } else if (IsAssigned(src_tree)) {
    // A is assigned. B is >= initialized.
    ASSERT_EQ(InstanceOf::kUnknownInstanceOf, fast_result);
  } else if (IsAssigned(target_tree)) {
    // B is assigned. A is >= initialized.
    ASSERT_EQ(slow_result, fast_result);
  } else {
    // Neither A,B are assigned.
    ASSERT_EQ(InstanceOf::kUnknownInstanceOf, fast_result);
  }

  // Use asserts,  not expects to immediately fail.
  // Otherwise the entire tree (very large) could potentially be broken.
}

void EnsureInstanceOfRecursive(MockClass* kls_root) {
  MockScopedLockMutator mutator_lock_fake_;

  auto visit_func = [&](MockClass* kls) {
    kls->Visit([&](MockClass* inner_class) {

      EnsureInstanceOfCorrect(kls, inner_class);
      EnsureInstanceOfCorrect(inner_class, kls);

      if (::testing::Test::HasFatalFailure()) {
        return false;
      }

      return true;  // Keep visiting.
    });

    if (::testing::Test::HasFatalFailure()) {
        return false;
    }

    return true;  // Keep visiting.
  };

  ASSERT_NO_FATAL_FAILURE(kls_root->Visit(visit_func));
}

TEST_F(InstanceOfTreeTest, EnsureInitialized_TooWide_TooDeep) {
  auto transitions = [](MockClass* kls) {
    UNUSED(kls);
    return MockInstanceOfTransition::kAssigned;
  };

  // Pick the 2nd level because has the most narrow # of bits.
  constexpr size_t kTargetDepth = 2;
  constexpr size_t kTooDeepTargetDepth = BitString::kCapacity + 1;
  constexpr size_t kMaxWidthCutOff = MaxWidthCutOff(kTargetDepth);

  constexpr size_t kMaxDepthForThisTest = std::numeric_limits<size_t>::max();
  auto expected = [=](MockClass* kls, int expect_when, InstanceOf::State actual_state, MockInstanceOfTransition transition) {
    UNUSED(transition);
    if (expect_when == kAfterTransition) {
      if (IsTooDeep(kls)) {
        EXPECT_EQ(InstanceOf::kOverflowed, actual_state);
      } else if (IsTooWide(kls)) {
        EXPECT_EQ(InstanceOf::kOverflowed, actual_state);
      } else {
        EXPECT_EQ(InstanceOf::kAssigned, actual_state);
      }
    }
  };

  {
    // Create too-wide siblings at the kTargetDepth level.
    MockClass* child = root_->FindChildAt(/*x*/0, kTargetDepth - 1u);
    CreateTreeFor(child, kMaxWidthCutOff*2, /*depth*/1);
    ASSERT_LE(kMaxWidthCutOff*2, child->GetNumberOfChildren());
    ASSERT_TRUE(IsTooWide(child->GetMaxChild())) << *(child->GetMaxChild());
    // Leave the rest of the tree as the default.

    // Create too-deep children for a too-wide parent.
    MockClass* child_subchild = child->GetMaxChild();
    ASSERT_TRUE(child_subchild != nullptr);
    ASSERT_EQ(0u, child_subchild->GetNumberOfChildren()) << *child_subchild;
    CreateTreeFor(child_subchild, /*width*/1, /*levels*/kTooDeepTargetDepth);
    MockClass* too_deep_child = child_subchild->FindChildAt(0, kTooDeepTargetDepth + 2);
    ASSERT_TRUE(too_deep_child != nullptr) << child_subchild->ToDotGraph();
    ASSERT_TRUE(IsTooWide(too_deep_child)) << *(too_deep_child);
    ASSERT_TRUE(IsTooDeep(too_deep_child)) << *(too_deep_child);
  }

  // Try to assign every level
  // It will fail once it gets to the "too wide" siblings and cause overflows.
  EnsureStateChangedTestRecursiveGeneric(root_, 0u, kMaxDepthForThisTest, transitions, expected);

  // Check every class against every class for "x instanceof y".
  EnsureInstanceOfRecursive(root_);
}
