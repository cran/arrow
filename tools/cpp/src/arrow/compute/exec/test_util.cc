// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "arrow/compute/exec/test_util.h"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "arrow/compute/api_vector.h"
#include "arrow/compute/exec.h"
#include "arrow/compute/exec/exec_plan.h"
#include "arrow/compute/exec/util.h"
#include "arrow/datum.h"
#include "arrow/record_batch.h"
#include "arrow/table.h"
#include "arrow/testing/gtest_util.h"
#include "arrow/testing/random.h"
#include "arrow/type.h"
#include "arrow/util/async_generator.h"
#include "arrow/util/iterator.h"
#include "arrow/util/logging.h"
#include "arrow/util/optional.h"
#include "arrow/util/vector.h"

namespace arrow {

using internal::Executor;

namespace compute {
namespace {

struct DummyNode : ExecNode {
  DummyNode(ExecPlan* plan, NodeVector inputs, int num_outputs,
            StartProducingFunc start_producing, StopProducingFunc stop_producing)
      : ExecNode(plan, std::move(inputs), {}, dummy_schema(), num_outputs),
        start_producing_(std::move(start_producing)),
        stop_producing_(std::move(stop_producing)) {
    input_labels_.resize(inputs_.size());
    for (size_t i = 0; i < input_labels_.size(); ++i) {
      input_labels_[i] = std::to_string(i);
    }
  }

  const char* kind_name() const override { return "Dummy"; }

  void InputReceived(ExecNode* input, ExecBatch batch) override {}

  void ErrorReceived(ExecNode* input, Status error) override {}

  void InputFinished(ExecNode* input, int total_batches) override {}

  Status StartProducing() override {
    if (start_producing_) {
      RETURN_NOT_OK(start_producing_(this));
    }
    started_ = true;
    return Status::OK();
  }

  void PauseProducing(ExecNode* output) override {
    ASSERT_GE(num_outputs(), 0) << "Sink nodes should not experience backpressure";
    AssertIsOutput(output);
  }

  void ResumeProducing(ExecNode* output) override {
    ASSERT_GE(num_outputs(), 0) << "Sink nodes should not experience backpressure";
    AssertIsOutput(output);
  }

  void StopProducing(ExecNode* output) override {
    EXPECT_GE(num_outputs(), 0) << "Sink nodes should not experience backpressure";
    AssertIsOutput(output);
  }

  void StopProducing() override {
    if (started_) {
      for (const auto& input : inputs_) {
        input->StopProducing(this);
      }
      if (stop_producing_) {
        stop_producing_(this);
      }
    }
  }

  Future<> finished() override { return Future<>::MakeFinished(); }

 private:
  void AssertIsOutput(ExecNode* output) {
    auto it = std::find(outputs_.begin(), outputs_.end(), output);
    ASSERT_NE(it, outputs_.end());
  }

  std::shared_ptr<Schema> dummy_schema() const {
    return schema({field("dummy", null())});
  }

  StartProducingFunc start_producing_;
  StopProducingFunc stop_producing_;
  std::unordered_set<ExecNode*> requested_stop_;
  bool started_ = false;
};

}  // namespace

ExecNode* MakeDummyNode(ExecPlan* plan, std::string label, std::vector<ExecNode*> inputs,
                        int num_outputs, StartProducingFunc start_producing,
                        StopProducingFunc stop_producing) {
  auto node =
      plan->EmplaceNode<DummyNode>(plan, std::move(inputs), num_outputs,
                                   std::move(start_producing), std::move(stop_producing));
  if (!label.empty()) {
    node->SetLabel(std::move(label));
  }
  return node;
}

ExecBatch ExecBatchFromJSON(const std::vector<ValueDescr>& descrs,
                            util::string_view json) {
  auto fields = ::arrow::internal::MapVector(
      [](const ValueDescr& descr) { return field("", descr.type); }, descrs);

  ExecBatch batch{*RecordBatchFromJSON(schema(std::move(fields)), json)};

  auto value_it = batch.values.begin();
  for (const auto& descr : descrs) {
    if (descr.shape == ValueDescr::SCALAR) {
      if (batch.length == 0) {
        *value_it = MakeNullScalar(value_it->type());
      } else {
        *value_it = value_it->make_array()->GetScalar(0).ValueOrDie();
      }
    }
    ++value_it;
  }

  return batch;
}

Future<std::vector<ExecBatch>> StartAndCollect(
    ExecPlan* plan, AsyncGenerator<util::optional<ExecBatch>> gen) {
  RETURN_NOT_OK(plan->Validate());
  RETURN_NOT_OK(plan->StartProducing());

  auto collected_fut = CollectAsyncGenerator(gen);

  return AllComplete({plan->finished(), Future<>(collected_fut)})
      .Then([collected_fut]() -> Result<std::vector<ExecBatch>> {
        ARROW_ASSIGN_OR_RAISE(auto collected, collected_fut.result());
        return ::arrow::internal::MapVector(
            [](util::optional<ExecBatch> batch) { return std::move(*batch); },
            std::move(collected));
      });
}

BatchesWithSchema MakeBasicBatches() {
  BatchesWithSchema out;
  out.batches = {
      ExecBatchFromJSON({int32(), boolean()}, "[[null, true], [4, false]]"),
      ExecBatchFromJSON({int32(), boolean()}, "[[5, null], [6, false], [7, false]]")};
  out.schema = schema({field("i32", int32()), field("bool", boolean())});
  return out;
}

BatchesWithSchema MakeRandomBatches(const std::shared_ptr<Schema>& schema,
                                    int num_batches, int batch_size) {
  BatchesWithSchema out;

  random::RandomArrayGenerator rng(42);
  out.batches.resize(num_batches);

  for (int i = 0; i < num_batches; ++i) {
    out.batches[i] = ExecBatch(*rng.BatchOf(schema->fields(), batch_size));
    // add a tag scalar to ensure the batches are unique
    out.batches[i].values.emplace_back(i);
  }

  out.schema = schema;
  return out;
}

Result<std::shared_ptr<Table>> SortTableOnAllFields(const std::shared_ptr<Table>& tab) {
  std::vector<SortKey> sort_keys;
  for (auto&& f : tab->schema()->fields()) {
    sort_keys.emplace_back(f->name());
  }
  ARROW_ASSIGN_OR_RAISE(auto sort_ids, SortIndices(tab, SortOptions(sort_keys)));
  ARROW_ASSIGN_OR_RAISE(auto tab_sorted, Take(tab, sort_ids));
  return tab_sorted.table();
}

void AssertTablesEqual(const std::shared_ptr<Table>& exp,
                       const std::shared_ptr<Table>& act) {
  ASSERT_EQ(exp->num_columns(), act->num_columns());
  if (exp->num_rows() == 0) {
    ASSERT_EQ(exp->num_rows(), act->num_rows());
  } else {
    ASSERT_OK_AND_ASSIGN(auto exp_sorted, SortTableOnAllFields(exp));
    ASSERT_OK_AND_ASSIGN(auto act_sorted, SortTableOnAllFields(act));

    AssertTablesEqual(*exp_sorted, *act_sorted,
                      /*same_chunk_layout=*/false, /*flatten=*/true);
  }
}

void AssertExecBatchesEqual(const std::shared_ptr<Schema>& schema,
                            const std::vector<ExecBatch>& exp,
                            const std::vector<ExecBatch>& act) {
  ASSERT_OK_AND_ASSIGN(auto exp_tab, TableFromExecBatches(schema, exp));
  ASSERT_OK_AND_ASSIGN(auto act_tab, TableFromExecBatches(schema, act));
  AssertTablesEqual(exp_tab, act_tab);
}

}  // namespace compute
}  // namespace arrow
