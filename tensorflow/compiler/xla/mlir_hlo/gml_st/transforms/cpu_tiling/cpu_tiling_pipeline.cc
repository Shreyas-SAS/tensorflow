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

#include <algorithm>
#include <functional>

#include "gml_st/transforms/passes.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Func/Transforms/Passes.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"

namespace mlir {
namespace gml_st {

GmlStCPUTilingOptions getDefaultCPUPipelineOptions(StringRef cpuName,
                                                   int64_t statsDetailLevel) {
  GmlStCPUTilingOptions opts;
  opts.vectorSize = 8;
  opts.reduction1DTileSize = 32;
  opts.reduction2DTileSizes = {4, 4};
  opts.matmulTileSizes = {};
  opts.lowerToMmt4d = false;
  opts.enableFusionClusters = false;
  opts.enableFusionClusterOutlining = false;
  opts.cpuName = cpuName;
  opts.statsDetailLevel = statsDetailLevel;
  opts.fuseDegenerateReshapes = false;
  return opts;
}

void addCPUTilingPipeline(OpPassManager& pm,
                          const GmlStCPUTilingOptions& options) {
  using func::FuncOp;

  pm.addNestedPass<FuncOp>(createCollectStatsPass(options.statsDetailLevel));
  pm.addNestedPass<FuncOp>(createScalarizationPass(false));

  if (options.enableFusionClusters) {
    pm.addNestedPass<FuncOp>(createFusionPlanningForCpuPass());
  }

  // Outline and deduplicate fusion clusters.
  if (options.enableFusionClusterOutlining) {
    pm.addPass(createFusionOutliningPass());
    pm.addPass(func::createDuplicateFunctionEliminationPass());
    pm.addPass(createCSEPass());
  }

  if (options.lowerToMmt4d) pm.addNestedPass<FuncOp>(createPackMatmulPass());

  pm.addNestedPass<FuncOp>(createTransformConvForCpuPass());
  pm.addNestedPass<FuncOp>(createTransformScatterForCpuPass());
  pm.addNestedPass<FuncOp>(createTransformReduceForCpuPass(
      options.vectorSize, options.reduction1DTileSize,
      options.reduction2DTileSizes));
  pm.addNestedPass<FuncOp>(
      createTransformDotForCpuPass(options.matmulTileSizes));
  pm.addNestedPass<FuncOp>(createTransformMmt4DForCpuPass());
  pm.addNestedPass<FuncOp>(createTransformPackForCpuPass());
  pm.addNestedPass<FuncOp>(createTransformElementwiseForCpuPass(
      options.vectorSize, options.fuseDegenerateReshapes));

  pm.addNestedPass<FuncOp>(createInlineFusionClustersPass());

  pm.addPass(createCSEPass());
  pm.addPass(createCanonicalizerPass());

  pm.addNestedPass<FuncOp>(createRewriteForallOpPass());
  pm.addNestedPass<FuncOp>(createComposeExtractInsertSlicePass());
  pm.addNestedPass<FuncOp>(createVectorizeForCPUPass());

  // Tile remaining ops by size one and scalarize what we can.
  pm.addNestedPass<FuncOp>(createTileByOnePass());
  pm.addNestedPass<FuncOp>(createScalarizationPass());

  pm.addPass(createCanonicalizerPass());
}

void addDefaultCPUTilingPipeline(OpPassManager& pm, StringRef cpuName,
                                 int64_t statsDetailLevel) {
  addCPUTilingPipeline(pm,
                       getDefaultCPUPipelineOptions(cpuName, statsDetailLevel));
}

}  // namespace gml_st
}  // namespace mlir
