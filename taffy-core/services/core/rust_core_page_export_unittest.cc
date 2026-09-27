// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_page_export.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace bridge = core_bridge;
namespace mojom = core_service::mojom;

bridge::BridgePageExportResult ExportResult() {
  bridge::BridgePageExportResult result;
  result.operation.operation_id = "page-export-1";
  result.operation.service_generation = 3u;
  result.operation.deadline_monotonic_ms = 10'000u;
  result.operation.idempotency_key = "page-export-1";
  result.status =
      static_cast<uint8_t>(mojom::PageSnapshotExportStatus::kExported);
  result.format =
      static_cast<uint8_t>(mojom::PageSnapshotExportFormat::kMarkdown);
  result.tab_id = "tab-1";
  result.frame_id = "frame-1";
  result.page_epoch = "epoch-1";
  result.graph_revision = 7u;
  result.origin = "https://example.test";
  result.mime_type = "text/markdown";
  result.suggested_file_name = "taffy-page-snapshot.md";
  result.content = {'#', ' ', 'P', 'a', 'g', 'e', '\n'};
  result.node_count = 2u;
  result.redacted_field_count = 1u;
  result.withheld_field_count = 1u;
  result.captured_at_epoch_ms = 1'725'000'000'123u;
  result.source_query_withheld = true;
  result.source_fragment_withheld = true;
  result.secure_context = true;
  return result;
}

mojom::PageSnapshotExportCommandPtr ExportCommandWithGraph(
    size_t graph_bytes) {
  auto command = mojom::PageSnapshotExportCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "page-export-1", 3u, 0u, 10'000u, "page-export-1");
  command->expected_tab_id = "tab-1";
  command->expected_frame_id = "frame-1";
  command->expected_page_epoch = "epoch-1";
  command->expected_graph_revision = 7u;
  command->expected_origin = "https://example.test";
  command->max_bytes = mojom::kMaxPageSnapshotExportBytes;
  command->captured_at_epoch_ms = 1'725'000'000'123u;
  command->source_query_withheld = true;
  command->source_fragment_withheld = true;
  command->observation = mojom::ObservationEffectResult::New();
  command->observation->schema_version = "0.12";
  command->observation->tab_id = "tab-1";
  command->observation->frame_id = "frame-1";
  command->observation->page_epoch = "epoch-1";
  command->observation->origin = "https://example.test";
  command->observation->node_count = 1u;
  command->observation->total_bytes = static_cast<uint32_t>(graph_bytes);
  command->observation->graph_payload.assign(graph_bytes, 0u);
  return command;
}

TEST(RustCorePageExportTest, ExactRustBytesCrossWithoutSemanticProjection) {
  const mojom::PageSnapshotExportResultPtr result =
      core_service_internal::ToMojoPageExportResult(ExportResult());

  ASSERT_TRUE(result);
  EXPECT_EQ(mojom::PageSnapshotExportStatus::kExported, result->status);
  EXPECT_EQ("https://example.test", result->origin);
  EXPECT_EQ((std::vector<uint8_t>{'#', ' ', 'P', 'a', 'g', 'e', '\n'}),
            result->content);
  EXPECT_EQ(1u, result->withheld_field_count);
  EXPECT_EQ(1'725'000'000'123u, result->captured_at_epoch_ms);
  EXPECT_TRUE(result->source_query_withheld);
  EXPECT_TRUE(result->source_fragment_withheld);
  EXPECT_TRUE(result->secure_context);
}

TEST(RustCorePageExportTest, FailureTerminalMustRemainContentFree) {
  bridge::BridgePageExportResult result = ExportResult();
  result.status =
      static_cast<uint8_t>(mojom::PageSnapshotExportStatus::kStalePage);
  EXPECT_FALSE(
      core_service_internal::ToMojoPageExportResult(std::move(result)));

  result = {};
  result.operation.operation_id = "page-export-1";
  result.operation.service_generation = 3u;
  result.operation.idempotency_key = "page-export-1";
  result.status =
      static_cast<uint8_t>(mojom::PageSnapshotExportStatus::kStalePage);
  result.format =
      static_cast<uint8_t>(mojom::PageSnapshotExportFormat::kMarkdown);
  const mojom::PageSnapshotExportResultPtr projected =
      core_service_internal::ToMojoPageExportResult(std::move(result));
  ASSERT_TRUE(projected);
  EXPECT_TRUE(projected->content.empty());
  EXPECT_TRUE(projected->origin.empty());
}

TEST(RustCorePageExportTest, MissingCommandIdentityIsRefused) {
  auto command = mojom::PageSnapshotExportCommand::New();
  command->observation = mojom::ObservationEffectResult::New();
  EXPECT_FALSE(core_service_internal::ToBridgePageExportCommand(*command));
}

TEST(RustCorePageExportTest, ProjectionLeavesGraphInItsMojoOwner) {
  auto command = ExportCommandWithGraph(4u);
  const uint8_t* const graph_allocation =
      command->observation->graph_payload.data();

  const std::optional<bridge::BridgePageExportCommand> projected =
      core_service_internal::ToBridgePageExportCommand(*command);

  ASSERT_TRUE(projected);
  EXPECT_EQ(4u, command->observation->graph_payload.size());
  EXPECT_EQ(graph_allocation, command->observation->graph_payload.data());
}

TEST(RustCorePageExportTest, OversizedGraphIsRefusedBeforeBridgeBorrow) {
  auto command = ExportCommandWithGraph(
      mojom::kMaxTaskObservationTotalBytes + 1u);

  EXPECT_FALSE(core_service_internal::ToBridgePageExportCommand(*command));
}

TEST(RustCorePageExportTest, GraphByteCountMismatchIsRefusedBeforeBridgeBorrow) {
  auto command = ExportCommandWithGraph(4u);
  command->observation->total_bytes = 5u;

  EXPECT_FALSE(core_service_internal::ToBridgePageExportCommand(*command));
}

}  // namespace
}  // namespace taffy
