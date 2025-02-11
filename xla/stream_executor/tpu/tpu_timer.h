/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

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

#ifndef TENSORFLOW_COMPILER_XLA_STREAM_EXECUTOR_TPU_TPU_TIMER_H_
#define TENSORFLOW_COMPILER_XLA_STREAM_EXECUTOR_TPU_TPU_TIMER_H_

#include "xla/stream_executor/stream_executor_internal.h"
#include "xla/stream_executor/tpu/tpu_api.h"
#include "xla/stream_executor/tpu/tpu_executor_c_api.h"
#include "tsl/platform/types.h"

namespace tensorflow {

class TpuTimer : public ::stream_executor::internal::TimerInterface {
 public:
  explicit TpuTimer(SE_Timer* timer) : timer_(timer) {}
  ~TpuTimer() override {
    tensorflow::tpu::ExecutorApiFn()->TpuTimer_FreeFn(timer_);
  }
  uint64_t Microseconds() const override {
    return tensorflow::tpu::ExecutorApiFn()->TpuTimer_MicrosecondsFn(timer_);
  }
  uint64_t Nanoseconds() const override {
    return tensorflow::tpu::ExecutorApiFn()->TpuTimer_NanosecondsFn(timer_);
  }

 private:
  SE_Timer* timer_;
};

}  // namespace tensorflow

#endif  // TENSORFLOW_COMPILER_XLA_STREAM_EXECUTOR_TPU_TPU_TIMER_H_
