// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "taffy/renderer/adapters/adapter.h"
#include "taffy/renderer/test/endpoint_test_harness.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {
namespace {

class AdapterBudgetTest : public EndpointTestHarness {};

TEST_F(AdapterBudgetTest,
       HostileTableRelationshipsStayInsideTheSnapshotBudget) {
  std::string html = "<html><body><table><thead><tr>";
  constexpr int kColumns = 48;
  constexpr int kRows = 48;
  for (int column = 0; column < kColumns; ++column) {
    html += "<th scope=\"col\"></th>";
  }
  html += "</tr></thead><tbody>";
  for (int row = 0; row < kRows; ++row) {
    html += "<tr>";
    for (int column = 0; column < kColumns; ++column) {
      html += "<td></td>";
    }
    html += "</tr>";
  }
  html += "</tbody></table></body></html>";
  LoadAndBind(html);

  mojom::SnapshotRequestPtr request = DocumentRequest();
  request->adapters.clear();
  auto dom = mojom::AdapterRequirement::New();
  dom->adapter = mojom::AdapterKind::kDom;
  dom->requirement = mojom::AdapterRequirementLevel::kOptional;
  request->adapters.push_back(std::move(dom));
  constexpr uint32_t kTotalBudget = 32 * 1024;
  request->max_total_bytes = kTotalBudget;

  mojom::SnapshotResultPtr result = Snapshot(std::move(request));
  ASSERT_TRUE(result && result->snapshot);
  EXPECT_EQ(result->code, mojom::ObservationResultCode::kIncomplete);
  EXPECT_TRUE(result->snapshot->truncation->truncated);
  EXPECT_NE(std::ranges::find(result->snapshot->truncation->budgets_reached,
                              mojom::BudgetKind::kMaxTotalBytes),
            result->snapshot->truncation->budgets_reached.end());

  size_t header_edges = 0;
  for (const mojom::SemanticEdgePtr& edge : result->snapshot->edges) {
    if (edge->relationship == mojom::RelationshipKind::kColumnHeaderFor ||
        edge->relationship == mojom::RelationshipKind::kRowHeaderFor) {
      ++header_edges;
    }
  }
  const size_t minimum_edge_bytes =
      BudgetLedger::ConservativeEdgeBytes(SemanticEdge());
  EXPECT_LE(header_edges, kTotalBudget / minimum_edge_bytes)
      << "table relationship work escaped the total snapshot-byte budget";
}

TEST_F(AdapterBudgetTest, WideDomFanoutIsBoundedBeforeTheWorkQueueGrows) {
  std::string html = "<html><body>";
  for (int i = 0; i < 256; ++i) {
    html += "<p>item</p>";
  }
  html += "</body></html>";
  LoadAndBind(html);

  mojom::SnapshotRequestPtr request = DocumentRequest();
  request->adapters.clear();
  auto dom = mojom::AdapterRequirement::New();
  dom->adapter = mojom::AdapterKind::kDom;
  dom->requirement = mojom::AdapterRequirementLevel::kOptional;
  request->adapters.push_back(std::move(dom));
  request->max_nodes = 8;

  mojom::SnapshotResultPtr result = Snapshot(std::move(request));
  ASSERT_TRUE(result && result->snapshot);
  EXPECT_EQ(result->code, mojom::ObservationResultCode::kIncomplete);
  EXPECT_LE(result->snapshot->nodes.size(), 8u);
  EXPECT_TRUE(std::ranges::any_of(AllStringsIn(*result), [](const auto& text) {
    return text.find("dom-child-fanout-bound") != std::string::npos;
  }));
}

mojom::SnapshotRequestPtr AccessibilityOnlyRequest(
    mojom::SnapshotRequestPtr request) {
  request->adapters.clear();
  auto accessibility = mojom::AdapterRequirement::New();
  accessibility->adapter = mojom::AdapterKind::kAccessibility;
  accessibility->requirement = mojom::AdapterRequirementLevel::kOptional;
  request->adapters.push_back(std::move(accessibility));
  return request;
}

// Where in the snapshot's node list a node with this exact name sits, or the
// list's size when none does.
size_t PositionOfName(const mojom::PageSnapshot& snapshot,
                      std::string_view name) {
  for (size_t i = 0; i < snapshot.nodes.size(); ++i) {
    if (snapshot.nodes[i]->name == name) {
      return i;
    }
  }
  return snapshot.nodes.size();
}

TEST_F(AdapterBudgetTest, TheAccessibilityWalkReadsThePageTopToBottom) {
  LoadAndBind(
      "<html><body><button>Top of the page</button><p>between</p>"
      "<footer><button>Bottom of the page</button></footer></body></html>");

  mojom::SnapshotResultPtr result =
      Snapshot(AccessibilityOnlyRequest(DocumentRequest()));
  ASSERT_TRUE(result && result->snapshot);
  const size_t top = PositionOfName(*result->snapshot, "Top of the page");
  const size_t bottom = PositionOfName(*result->snapshot, "Bottom of the page");
  ASSERT_LT(top, result->snapshot->nodes.size());
  ASSERT_LT(bottom, result->snapshot->nodes.size());
  EXPECT_LT(top, bottom) << "the page arrived bottom to top";
}

TEST_F(AdapterBudgetTest, AnAccessibilityFanoutBoundKeepsTheTopOfThePage) {
  std::string html = "<html><body>";
  for (int i = 0; i < 256; ++i) {
    html += "<button>item-" + std::to_string(i) + "</button>";
  }
  html += "</body></html>";
  LoadAndBind(html);

  mojom::SnapshotRequestPtr request =
      AccessibilityOnlyRequest(DocumentRequest());
  request->max_nodes = 8;
  mojom::SnapshotResultPtr result = Snapshot(std::move(request));
  ASSERT_TRUE(result && result->snapshot);
  EXPECT_TRUE(std::ranges::any_of(AllStringsIn(*result), [](const auto& text) {
    return text.find("accessibility-child-fanout-bound") != std::string::npos;
  }));
  EXPECT_LT(PositionOfName(*result->snapshot, "item-0"),
            result->snapshot->nodes.size())
      << "the bound dropped the first control on the page";
  EXPECT_EQ(PositionOfName(*result->snapshot, "item-255"),
            result->snapshot->nodes.size());
}

TEST_F(AdapterBudgetTest, StructuredClaimConstructionStopsAtTheNodeCeiling) {
  std::string html = "<html><body><script type=\"application/ld+json\">[";
  for (int i = 0; i < 64; ++i) {
    if (i > 0) {
      html += ',';
    }
    html +=
        "{\"@type\":\"Product\",\"name\":\"item-" + std::to_string(i) + "\"}";
  }
  html += "]</script></body></html>";
  LoadAndBind(html);

  mojom::SnapshotRequestPtr request = DocumentRequest();
  request->adapters.clear();
  auto metadata = mojom::AdapterRequirement::New();
  metadata->adapter = mojom::AdapterKind::kMetadata;
  metadata->requirement = mojom::AdapterRequirementLevel::kOptional;
  request->adapters.push_back(std::move(metadata));
  request->max_nodes = 8;

  mojom::SnapshotResultPtr result = Snapshot(std::move(request));
  ASSERT_TRUE(result && result->snapshot);
  EXPECT_LE(result->snapshot->nodes.size(), 8u);
  EXPECT_TRUE(std::ranges::any_of(AllStringsIn(*result), [](const auto& text) {
    return text.find("structured-claim-count-bound") != std::string::npos;
  }));
}

}  // namespace
}  // namespace taffy::test
