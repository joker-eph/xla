/* Copyright 2021 The TensorFlow Authors. All Rights Reserved.

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

#include "xla/service/collectives_schedule_linearizer.h"

#include "xla/debug_options_flags.h"
#include "xla/hlo/ir/hlo_computation.h"
#include "xla/hlo/ir/hlo_instruction.h"
#include "xla/hlo/ir/hlo_module.h"
#include "xla/hlo/ir/hlo_opcode.h"
#include "xla/literal.h"
#include "xla/service/hlo_matchers.h"
#include "xla/service/hlo_runner.h"
#include "xla/service/pattern_matcher.h"
#include "xla/shape_util.h"
#include "xla/test.h"
#include "xla/test_helpers.h"
#include "xla/tests/hlo_test_base.h"
#include "xla/xla_data.pb.h"
#include "tsl/platform/test_benchmark.h"

namespace xla {
namespace {

namespace m = match;

int64_t CountControlEdges(const HloComputation& computation) {
  int64_t count = 0;
  for (const auto& instruction : computation.instructions()) {
    count += instruction->control_successors().size();
  }
  return count;
}

class CollectivesScheduleLinearizerTest : public HloTestBase {
 protected:
  void InsertCollectivesSchedule(HloModule* module) {
    CollectivesScheduleLinearizer collectives_schedule_linearizer;
    ASSERT_IS_OK(collectives_schedule_linearizer.Run(module).status());
  }
};

TEST_F(CollectivesScheduleLinearizerTest, FixOrdering) {
  absl::string_view hlo_string = R"(
HloModule module

sum {
  a = f32[] parameter(0)
  b = f32[] parameter(1)
  ROOT out = f32[] add(a, b)
}

ENTRY entry {
  p0 = f32[100] parameter(0), parameter_replication={false}
  p1 = f32[100] parameter(1), parameter_replication={false}
  c1 = f32[100] all-reduce(p0), replica_groups={}, to_apply=sum
  c2 = f32[100] all-reduce(p1), replica_groups={}, to_apply=sum
  ROOT out = f32[100] add(c1, c2)
}

  )";
  TF_ASSERT_OK_AND_ASSIGN(std::unique_ptr<HloModule> module,
                          ParseAndReturnVerifiedModule(hlo_string));
  InsertCollectivesSchedule(module.get());
  EXPECT_EQ(CountControlEdges(*module->entry_computation()), 1);
  HloInstruction *c1 = nullptr, *c2 = nullptr;
  for (HloInstruction* instr : module->entry_computation()->instructions()) {
    if (Match(instr, m::AllReduce(m::Parameter(0)))) {
      c1 = instr;
    }
    if (Match(instr, m::AllReduce(m::Parameter(1)))) {
      c2 = instr;
    }
  }
  EXPECT_TRUE(c1 != nullptr && c2 != nullptr);
  EXPECT_TRUE(absl::c_linear_search(c2->control_predecessors(), c1));
}

TEST_F(CollectivesScheduleLinearizerTest, NoFixRequired) {
  absl::string_view hlo_string = R"(
HloModule module

sum {
  a = f32[] parameter(0)
  b = f32[] parameter(1)
  ROOT out = f32[] add(a, b)
}

ENTRY entry {
  p0 = f32[100] parameter(0), parameter_replication={false}
  p1 = f32[100] parameter(1), parameter_replication={false}
  c1 = f32[100] all-reduce(p0), replica_groups={}, to_apply=sum
  c2 = f32[100] all-reduce(p1), replica_groups={}, to_apply=sum, control-predecessors={c1}
  ROOT out = f32[100] add(c1, c2)
}

  )";
  TF_ASSERT_OK_AND_ASSIGN(std::unique_ptr<HloModule> module,
                          ParseAndReturnVerifiedModule(hlo_string));
  InsertCollectivesSchedule(module.get());
  EXPECT_EQ(CountControlEdges(*module->entry_computation()), 1);
}

TEST_F(CollectivesScheduleLinearizerTest, DependentCollectives) {
  absl::string_view hlo_string = R"(
HloModule module

sum {
  a = f32[] parameter(0)
  b = f32[] parameter(1)
  ROOT out = f32[] add(a, b)
}

ENTRY entry {
  p0 = f32[100] parameter(0), parameter_replication={false}
  p1 = f32[100] parameter(1), parameter_replication={false}
  c1 = f32[100] all-reduce(p0), replica_groups={}, to_apply=sum
  c2 = f32[100] all-reduce(c1), replica_groups={}, to_apply=sum
  ROOT out = f32[100] add(c1, c2)
}

  )";
  TF_ASSERT_OK_AND_ASSIGN(std::unique_ptr<HloModule> module,
                          ParseAndReturnVerifiedModule(hlo_string));
  InsertCollectivesSchedule(module.get());
  EXPECT_EQ(CountControlEdges(*module->entry_computation()), 0);
}

TEST_F(CollectivesScheduleLinearizerTest, NonPostorder) {
  absl::string_view hlo_string = R"(
HloModule module

sum {
  a = f32[] parameter(0)
  b = f32[] parameter(1)
  ROOT out = f32[] add(a, b)
}

ENTRY entry {
  p0 = f32[100] parameter(0), parameter_replication={false}
  p1 = f32[100] parameter(1), parameter_replication={false}
  c1 = f32[100] all-reduce(p0), replica_groups={}, to_apply=sum
  c2 = f32[100] all-reduce(p1), replica_groups={}, to_apply=sum
  c3 = f32[100] all-reduce(p1), replica_groups={}, to_apply=sum
  t = f32[100] add(c1, c2)
  ROOT out = f32[100] add(t, c3)
}
  )";
  TF_ASSERT_OK_AND_ASSIGN(std::unique_ptr<HloModule> module,
                          ParseAndReturnVerifiedModule(hlo_string));
  ASSERT_IS_OK(
      module->entry_computation()
          ->GetInstructionWithName("c3")
          ->AddControlDependencyTo(
              module->entry_computation()->GetInstructionWithName("c1")));
  InsertCollectivesSchedule(module.get());
  EXPECT_EQ(CountControlEdges(*module->entry_computation()), 2);
}

}  // namespace
}  // namespace xla
