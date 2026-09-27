// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_page_export.h"

#include <stdint.h>

#include <optional>
#include <string>
#include <utility>

#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;
namespace wire = core_service::wire;

namespace {

bridge::BridgePageExportOperation ToOperation(
    mojom::OperationEnvelope& input) {
  bridge::BridgePageExportOperation output;
  output.operation_id = std::move(input.operation_id);
  output.service_generation = input.service_generation;
  output.task_revision = input.task_revision;
  output.deadline_monotonic_ms = input.deadline_monotonic_ms;
  output.idempotency_key = std::move(input.idempotency_key);
  return output;
}

bool IsBoundedNonEmpty(const std::string& value, uint64_t maximum) {
  return !value.empty() && value.size() <= maximum;
}

bool IsBoundedCommand(const mojom::PageSnapshotExportCommand& input) {
  const mojom::OperationEnvelope& operation = *input.operation;
  const mojom::ObservationEffectResult& observation = *input.observation;
  return IsBoundedNonEmpty(operation.operation_id,
                           mojom::kMaxIdentifierBytes) &&
         IsBoundedNonEmpty(operation.idempotency_key,
                           mojom::kMaxIdempotencyKeyBytes) &&
         IsBoundedNonEmpty(input.expected_tab_id,
                           mojom::kMaxIdentifierBytes) &&
         IsBoundedNonEmpty(input.expected_frame_id,
                           mojom::kMaxIdentifierBytes) &&
         IsBoundedNonEmpty(input.expected_page_epoch,
                           mojom::kMaxIdentifierBytes) &&
         IsBoundedNonEmpty(input.expected_origin,
                           mojom::kMaxNormalizedOriginBytes) &&
         IsBoundedNonEmpty(observation.schema_version,
                           mojom::kMaxIdentifierBytes) &&
         IsBoundedNonEmpty(observation.tab_id, mojom::kMaxIdentifierBytes) &&
         IsBoundedNonEmpty(observation.frame_id,
                           mojom::kMaxIdentifierBytes) &&
         IsBoundedNonEmpty(observation.page_epoch,
                           mojom::kMaxIdentifierBytes) &&
         IsBoundedNonEmpty(observation.origin,
                           mojom::kMaxNormalizedOriginBytes) &&
         input.max_bytes > 0u &&
         input.max_bytes <= mojom::kMaxPageSnapshotExportBytes &&
         input.captured_at_epoch_ms > 0u &&
         !observation.graph_payload.empty() &&
         observation.graph_payload.size() <=
             mojom::kMaxTaskObservationTotalBytes &&
         observation.total_bytes == observation.graph_payload.size() &&
         observation.node_count <= mojom::kMaxTaskObservationNodes;
}

bool IsSuccessfulShape(const bridge::BridgePageExportResult& input,
                       mojom::PageSnapshotExportStatus status,
                       mojom::PageSnapshotExportFormat format) {
  if (status != mojom::PageSnapshotExportStatus::kExported) {
    return input.tab_id.empty() && input.frame_id.empty() &&
           input.page_epoch.empty() && input.graph_revision == 0u &&
           input.origin.empty() && input.mime_type.empty() &&
           input.suggested_file_name.empty() && input.content.empty() &&
           input.node_count == 0u && input.redacted_field_count == 0u &&
           input.suppressed_secret_value_count == 0u &&
           input.withheld_field_count == 0u &&
           input.captured_at_epoch_ms == 0u &&
           !input.source_query_withheld &&
           !input.source_fragment_withheld && !input.secure_context;
  }
  const bool markdown = format == mojom::PageSnapshotExportFormat::kMarkdown;
  return !input.tab_id.empty() && !input.frame_id.empty() &&
         !input.page_epoch.empty() && input.graph_revision != 0u &&
         !input.origin.empty() && !input.content.empty() &&
         input.captured_at_epoch_ms > 0u &&
         input.content.size() <= mojom::kMaxPageSnapshotExportBytes &&
         input.mime_type == (markdown ? "text/markdown" : "application/json") &&
         input.suggested_file_name ==
             (markdown ? "taffy-page-snapshot.md" : "taffy-page-snapshot.json");
}

}  // namespace

std::optional<bridge::BridgePageExportCommand> ToBridgePageExportCommand(
    mojom::PageSnapshotExportCommand& input) {
  if (!input.operation || !input.observation || input.observation->media) {
    return std::nullopt;
  }
  if (!IsBoundedCommand(input)) {
    return std::nullopt;
  }
  bridge::BridgePageExportCommand output;
  output.operation = ToOperation(*input.operation);
  output.format = static_cast<uint8_t>(input.format);
  output.expected_tab_id = std::move(input.expected_tab_id);
  output.expected_frame_id = std::move(input.expected_frame_id);
  output.expected_page_epoch = std::move(input.expected_page_epoch);
  output.expected_graph_revision = input.expected_graph_revision;
  output.expected_origin = std::move(input.expected_origin);
  output.max_bytes = input.max_bytes;
  output.captured_at_epoch_ms = input.captured_at_epoch_ms;
  output.source_query_withheld = input.source_query_withheld;
  output.source_fragment_withheld = input.source_fragment_withheld;
  mojom::ObservationEffectResult& observation = *input.observation;
  output.observation.status = static_cast<uint8_t>(observation.status);
  output.observation.schema_version = std::move(observation.schema_version);
  output.observation.tab_id = std::move(observation.tab_id);
  output.observation.frame_id = std::move(observation.frame_id);
  output.observation.page_epoch = std::move(observation.page_epoch);
  output.observation.graph_revision = observation.graph_revision;
  output.observation.origin = std::move(observation.origin);
  output.observation.is_potentially_trustworthy =
      observation.is_potentially_trustworthy;
  output.observation.private_profile = observation.private_profile;
  output.observation.node_count = observation.node_count;
  output.observation.total_bytes = observation.total_bytes;
  output.observation.truncated = observation.truncated;
  output.observation.may_change_answer = observation.may_change_answer;
  output.observation.redacted_field_count = observation.redacted_field_count;
  output.observation.suppressed_secret_value_count =
      observation.suppressed_secret_value_count;
  output.observation.sensitive_zone_count = observation.sensitive_zone_count;
  output.observation.policy_filtered_frame_count =
      observation.policy_filtered_frame_count;
  output.observation.highest_sensitivity =
      static_cast<uint8_t>(observation.highest_sensitivity);
  output.observation.graph_encoding =
      static_cast<uint8_t>(observation.graph_encoding);
  return output;
}

mojom::PageSnapshotExportResultPtr ToMojoPageExportResult(
    bridge::BridgePageExportResult input) {
  const std::optional<mojom::PageSnapshotExportStatus> status =
      wire::PageSnapshotExportStatusFromWire(input.status);
  const std::optional<mojom::PageSnapshotExportFormat> format =
      wire::PageSnapshotExportFormatFromWire(input.format);
  if (!status || !format || !IsSuccessfulShape(input, *status, *format)) {
    return nullptr;
  }
  auto output = mojom::PageSnapshotExportResult::New();
  output->operation = mojom::OperationEnvelope::New(
      std::string(input.operation.operation_id),
      input.operation.service_generation, input.operation.task_revision,
      input.operation.deadline_monotonic_ms,
      std::string(input.operation.idempotency_key));
  output->status = *status;
  output->format = *format;
  output->tab_id = std::string(input.tab_id);
  output->frame_id = std::string(input.frame_id);
  output->page_epoch = std::string(input.page_epoch);
  output->graph_revision = input.graph_revision;
  output->origin = std::string(input.origin);
  output->mime_type = std::string(input.mime_type);
  output->suggested_file_name = std::string(input.suggested_file_name);
  output->content.assign(input.content.begin(), input.content.end());
  output->node_count = input.node_count;
  output->redacted_field_count = input.redacted_field_count;
  output->suppressed_secret_value_count = input.suppressed_secret_value_count;
  output->withheld_field_count = input.withheld_field_count;
  output->captured_at_epoch_ms = input.captured_at_epoch_ms;
  output->source_query_withheld = input.source_query_withheld;
  output->source_fragment_withheld = input.source_fragment_withheld;
  output->secure_context = input.secure_context;
  return output;
}

}  // namespace taffy::core_service_internal
