/* Copyright 2022 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef TENSORFLOW_COMPILER_XLA_SERVICE_GPU_RUNTIME_COLLECTIVES_H_
#define TENSORFLOW_COMPILER_XLA_SERVICE_GPU_RUNTIME_COLLECTIVES_H_

#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "xla/runtime/custom_call_registry.h"
#include "xla/stream_executor/event.h"
#include "xla/stream_executor/stream.h"

namespace xla {
namespace gpu {

// Support for running async collective operations communicating via events.
// Registers XLA Gpu runtime collective custom calls.
void RegisterCollectiveCustomCalls(runtime::DirectCustomCallRegistry& registry);

class CollectivesSupport {
 public:
  // Maybe block host after the first call to the collective operation with the
  // given uid, to ensure that all devices have allocated the required buffers
  // for their communicators before allowing any device to continue enqueuing
  // operations. Otherwise, the allocations can cause deadlock in the CUDA
  // driver.
  //
  // This basically ports workaround from cr/435058849 to Xla runtime (see
  // details in the b/215649390).
  Status MaybeBlockAfterFirstRun(int32_t uid, int32_t device_ordinal,
                                 se::Stream* stream);

 private:
  mutable absl::Mutex mutex_;

  // Store if a particular collective operation was executed at least once. We
  // rely on unique `uid` assigned to each collective operation by the lowering
  // pass.
  absl::flat_hash_map<int64_t, bool> executed_ ABSL_GUARDED_BY(mutex_);
};

// Support for running async collective operations communicating via events.
class AsyncCollectivesSupport {
 public:
  explicit AsyncCollectivesSupport(se::Stream* async_comm_stream);

  StatusOr<se::Event> PopEvent(int32_t uid, int32_t device_ordinal);
  Status PushEvent(int32_t uid, int32_t device_ordinal, se::Event done_event);

  se::Stream* async_comm_stream() const { return async_comm_stream_; }

 private:
  mutable absl::Mutex mutex_;

  se::Stream* async_comm_stream_;

  // Store done events for the AllReduceDone to wait on.
  absl::node_hash_map<int64_t, se::Event> done_events_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace gpu
}  // namespace xla

#endif  // TENSORFLOW_COMPILER_XLA_SERVICE_GPU_RUNTIME_COLLECTIVES_H_
