/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

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

#include "xla/translate/hlo_to_mhlo/hlo_module_importer.h"

#include <iterator>
#include <memory>
#include <vector>

#include "xla/hlo/ir/hlo_computation.h"
#include "xla/hlo/ir/hlo_instruction.h"
#include "xla/hlo/ir/hlo_module.h"
#include "xla/layout_util.h"
#include "xla/mlir_hlo/mhlo/IR/hlo_ops.h"
#include "xla/permutation_util.h"
#include "xla/translate/hlo_to_mhlo/attribute_importer.h"
#include "xla/translate/hlo_to_mhlo/hlo_function_importer.h"
#include "xla/xla.pb.h"

namespace xla {

HloModuleImporter::HloModuleImporter(mlir::ModuleOp module,
                                     bool import_all_computation)
    : import_all_computation_(import_all_computation),
      symbol_table_(module),
      builder_(module.getContext()) {
  module.getContext()->loadDialect<mlir::arith::ArithDialect>();
  module.getContext()->loadDialect<mlir::func::FuncDialect>();
  module.getContext()->loadDialect<mlir::mhlo::MhloDialect>();
}

Status HloModuleImporter::Import(const xla::HloModule& hlo_module) {
  auto module = llvm::cast<mlir::ModuleOp>(symbol_table_.getOp());
  module.setName(hlo_module.name());
  module->setAttr("mhlo.cross_program_prefetches",
                  ConvertCrossProgramPrefetches(
                      hlo_module.CrossProgramPrefetches(), &builder_));

  if (!import_all_computation_)
    // Only import the entry computation, any reachable one will be imported
    // unless turned into a region operation.
    return HloFunctionImporter::ImportAsFunc(*hlo_module.entry_computation(),
                                             symbol_table_, &function_map_,
                                             &builder_,
                                             /*is_main*/ true)
        .status();

  auto* module_entry_computation = hlo_module.entry_computation();
  for (const auto* computation : hlo_module.computations())
    TF_RETURN_IF_ERROR(HloFunctionImporter::ImportAsFunc(
                           *computation, symbol_table_, &function_map_,
                           &builder_,
                           /*is_main*/ computation == module_entry_computation)
                           .status());

  return OkStatus();
}

Status HloModuleImporter::Import(const xla::HloModuleProto& module_proto) {
  xla::DebugOptions debug_options;
  TF_ASSIGN_OR_RETURN(
      auto module_config,
      xla::HloModule::CreateModuleConfigFromProto(module_proto, debug_options));
  TF_ASSIGN_OR_RETURN(auto module, xla::HloModule::CreateFromProto(
                                       module_proto, module_config));

  return Import(*module);
}

}  // namespace xla
