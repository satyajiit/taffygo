// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/benchmark/task_benchmark_model_script.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {
namespace {

TEST(TaskBenchmarkModelScriptTest,
     FindsOneStableLinkOnlyInsideDecodedModelContent) {
  EXPECT_EQ(
      17u,
      FindStableLinkHandleForTesting(
          R"json({"messages":[{"content":"Source 1:\n[17] link Stable link — never mutates — can activate\n"}]})json"));
  EXPECT_FALSE(FindStableLinkHandleForTesting(
      R"({"messages":[{"content":"[4] link \"something else\""}]})"));
  EXPECT_FALSE(FindStableLinkHandleForTesting("not-json"));
}

TEST(TaskBenchmarkModelScriptTest, AmbiguousStableHandlesFailClosed) {
  EXPECT_FALSE(FindStableLinkHandleForTesting(
      R"({"messages":[{"content":"[3] Stable link — never mutates\n[4] Stable link — never mutates"}]})"));
}

}  // namespace
}  // namespace taffy::test
