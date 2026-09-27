// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/task_benchmark_page_evidence.h"

#include <optional>
#include <string>
#include <utility>

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {
namespace {

TaskBenchmarkScenario::RequiredFact PageFact(std::string fixture,
                                             std::string field) {
  TaskBenchmarkScenario::RequiredFact fact;
  fact.id = "fact-1";
  fact.evidence = "page-field";
  fact.source_fixture = std::move(fixture);
  fact.source_field = std::move(field);
  fact.tolerance = "substance";
  return fact;
}

GraphPayload OneTextSegment(std::string text) {
  GraphPayload graph;
  GraphPayloadNode node;
  node.texts.push_back(std::move(text));
  graph.nodes.push_back(std::move(node));
  return graph;
}

TEST(TaskBenchmarkPageEvidenceTest, ProvesOnlyExactNormalizedGraphText) {
  const auto fact = PageFact("static-article", "headline");
  const auto evidence = ExtractTaskBenchmarkPageFieldEvidence(
      fact, "static-article", /*graph_complete=*/true,
      OneTextSegment("  HOW desk lighting\nAFFECTS focus  "));
  ASSERT_TRUE(evidence);
  EXPECT_EQ("a2af26dd3b2f2f1f5be92aca388dd2dc4536323cd806c1cc694dd3424991aaed",
            evidence->expected_value_sha256);

  GraphPayload split;
  split.nodes.push_back(GraphPayloadNode());
  split.nodes.back().texts.push_back("How desk lighting");
  split.nodes.push_back(GraphPayloadNode());
  split.nodes.back().texts.push_back("affects focus");
  EXPECT_FALSE(ExtractTaskBenchmarkPageFieldEvidence(
      fact, "static-article", /*graph_complete=*/true, split));
}

TEST(TaskBenchmarkPageEvidenceTest, AbsentValueNeedsCompleteExplicitProof) {
  const auto fact = PageFact("comparison-source-b", "warranty");
  GraphPayload explicit_absence = OneTextSegment(
      "This seller does not publish a warranty term. The value is absent.");
  EXPECT_FALSE(ExtractTaskBenchmarkPageFieldEvidence(
      fact, "comparison-source-b", /*graph_complete=*/false, explicit_absence));
  EXPECT_TRUE(ExtractTaskBenchmarkPageFieldEvidence(
      fact, "comparison-source-b", /*graph_complete=*/true, explicit_absence));
  explicit_absence.nodes.front().text_withheld = true;
  EXPECT_FALSE(ExtractTaskBenchmarkPageFieldEvidence(
      fact, "comparison-source-b", /*graph_complete=*/true, explicit_absence));
  EXPECT_FALSE(ExtractTaskBenchmarkPageFieldEvidence(
      fact, "comparison-source-b", /*graph_complete=*/true,
      OneTextSegment("Warranty details")));
}

TEST(TaskBenchmarkPageEvidenceTest, RefusesAnotherFixtureOrEvidenceKind) {
  auto fact = PageFact("static-article", "author");
  const GraphPayload graph = OneTextSegment("Priya Raman");
  EXPECT_FALSE(ExtractTaskBenchmarkPageFieldEvidence(
      fact, "static-product", /*graph_complete=*/true, graph));
  fact.evidence = "result-artifact";
  EXPECT_FALSE(ExtractTaskBenchmarkPageFieldEvidence(
      fact, "static-article", /*graph_complete=*/true, graph));
}

}  // namespace
}  // namespace taffy::test
