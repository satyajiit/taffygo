// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "taffy/services/core/rust_core.h"
#include "taffy/services/core/rust_core_bridge_handle.h"
#include "taffy/services/core/rust_core_page_export.h"
#include "taffy/services/core/service_bridge.rs.h"

namespace taffy {

namespace bridge = core_bridge;
namespace mojom = core_service::mojom;

mojom::PageSnapshotExportResultPtr RustCore::ExportPageSnapshot(
    mojom::PageSnapshotExportCommandPtr command,
    uint64_t now_monotonic_ms) {
  if (!bridge_ || !command || !command->operation) {
    return nullptr;
  }
  const std::string expected_operation_id = command->operation->operation_id;
  const uint64_t expected_generation = command->operation->service_generation;
  const std::string expected_idempotency_key =
      command->operation->idempotency_key;
  std::optional<bridge::BridgePageExportCommand> projected =
      core_service_internal::ToBridgePageExportCommand(*command);
  if (!projected) {
    return nullptr;
  }
  const std::vector<uint8_t>& graph_payload =
      command->observation->graph_payload;
  // The CXX call is synchronous and its Rust view cannot outlive this call.
  // Keep the Mojo command alive so the largest input crosses once, borrowed;
  // the generation-local replay cache retains only its digest.
  const rust::Slice<const uint8_t> borrowed_graph(graph_payload.data(),
                                                  graph_payload.size());
  mojom::PageSnapshotExportResultPtr result =
      core_service_internal::ToMojoPageExportResult(bridge::ExportPageSnapshot(
          *bridge_->runtime(), std::move(*projected), borrowed_graph,
          now_monotonic_ms));
  if (!result || !result->operation ||
      result->operation->operation_id != expected_operation_id ||
      result->operation->service_generation != expected_generation ||
      result->operation->idempotency_key != expected_idempotency_key) {
    return nullptr;
  }
  return result;
}

bool RustCore::CancelPageSnapshotExport(const std::string& operation_id,
                                        const std::string& idempotency_key) {
  return bridge_ && bridge::CancelPageSnapshotExport(
                        *bridge_->runtime(), operation_id, idempotency_key);
}

}  // namespace taffy
