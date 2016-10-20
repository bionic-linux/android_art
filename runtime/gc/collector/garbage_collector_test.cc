/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "common_runtime_test.h"

#include "gc_type.h"
#include "handle_scope-inl.h"
#include "mirror/class.h"
#include "scoped_thread_state_change.h"

namespace art {
namespace gc {
namespace collector {

class GarbageCollectorTest : public CommonRuntimeTest {
 public:
  static bool IsGcStress() {
    return Runtime::Current()->GetHeap()->gc_stress_mode_;
  }

  GarbageCollector* FindCollector() {
    gc::Heap* heap = Runtime::Current()->GetHeap();
    for (size_t type = 0; type < kGcTypeMax; ++type) {
      GarbageCollector* gc = heap->FindCollectorByGcType(static_cast<GcType>(type));
      if (gc != nullptr) {
        return gc;
      }
    }
    return nullptr;
  }
};

TEST_F(GarbageCollectorTest, IsMarked) {
  ScopedObjectAccess soa(Thread::Current());
  VariableSizedHandleScope hs(soa.Self());
  Runtime* runtime = Runtime::Current();
  ClassLinker* class_linker = runtime->GetClassLinker();
  gc::Heap* heap = runtime->GetHeap();
  if (heap->CurrentCollectorType() == kCollectorTypeCC) {
    // CC only supports calling IsMarked while the GC is running.
    return;
  }
  Handle<mirror::Class> klass = hs.NewHandle(class_linker->FindSystemClass(soa.Self(),
                                                                           "Ljava/lang/Object;"));
  // Perform GC so that new allocations are in the allocation stack and don't trigger GC.
  heap->CollectGarbage(true);
  std::vector<Handle<mirror::Object>> handles;
  GarbageCollector* gc = FindCollector();
  static constexpr size_t kNumObjects = 100;
  for (size_t i = 0; i < kNumObjects; ++i) {
    ObjPtr<mirror::Object> obj = klass->AllocObject(soa.Self());
    handles.push_back(hs.NewHandle(obj));
    if (heap->CurrentCollectorType() == kCollectorTypeCMS) {
      if (!IsGcStress()) {
        // CMS should have not have IsMarked return true since the allocation is only in the
        // allocation stack.
        ASSERT_EQ(gc->IsMarked(obj.Ptr()), nullptr);
      }
    } else {
      ASSERT_OBJ_PTR_EQ(gc->IsMarked(obj.Ptr()), obj);
    }
  }

  // Now check is marked or newly allocated.
  for (Handle<mirror::Object> h : handles) {
    ASSERT_EQ(gc->IsMarkedOrNewlyAllocated(h.Get()), h.Get());
  }
}

}  // namespace collector
}  // namespace gc
}  // namespace art
