// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

api::PageSnapshotExportResultPtr ExportResult(
    api::PageSnapshotExportAvailability availability) {
  auto result = api::PageSnapshotExportResult::New();
  result->availability = availability;
  return result;
}

bool SameDocument(const DirectObservationContext& live,
                  const CorePageObservationIdentity& expected) {
  return live.tab_id == expected.tab_id && live.frame_id == expected.frame_id &&
         live.page_epoch == expected.page_epoch &&
         live.origin == expected.origin;
}

bool SameOperation(const service::OperationEnvelope& actual,
                   const service::OperationEnvelope& expected) {
  return actual.operation_id == expected.operation_id &&
         actual.service_generation == expected.service_generation &&
         actual.task_revision == expected.task_revision &&
         actual.deadline_monotonic_ms == expected.deadline_monotonic_ms &&
         actual.idempotency_key == expected.idempotency_key;
}

bool IsContentFree(const service::PageSnapshotExportResult& result) {
  return result.tab_id.empty() && result.frame_id.empty() &&
         result.page_epoch.empty() && result.graph_revision == 0u &&
         result.origin.empty() && result.mime_type.empty() &&
         result.suggested_file_name.empty() && result.content.empty() &&
         result.node_count == 0u && result.redacted_field_count == 0u &&
         result.suppressed_secret_value_count == 0u &&
         result.withheld_field_count == 0u &&
         result.captured_at_epoch_ms == 0u &&
         !result.source_query_withheld && !result.source_fragment_withheld &&
         !result.secure_context;
}

bool HasExpectedContentType(const service::PageSnapshotExportResult& result) {
  switch (result.format) {
    case service::PageSnapshotExportFormat::kMarkdown:
      return result.mime_type == "text/markdown" &&
             result.suggested_file_name == "taffy-page-snapshot.md";
    case service::PageSnapshotExportFormat::kCanonicalJson:
      return result.mime_type == "application/json" &&
             result.suggested_file_name == "taffy-page-snapshot.json";
  }
}

service::PageSnapshotExportFormat ServiceFormat(
    api::PageSnapshotExportFormat format) {
  return format == api::PageSnapshotExportFormat::kMarkdown
             ? service::PageSnapshotExportFormat::kMarkdown
             : service::PageSnapshotExportFormat::kCanonicalJson;
}

api::PageSnapshotExportAvailability ApiStatus(
    service::PageSnapshotExportStatus status) {
  switch (status) {
    case service::PageSnapshotExportStatus::kExported:
      return api::PageSnapshotExportAvailability::kAvailable;
    case service::PageSnapshotExportStatus::kStalePage:
      return api::PageSnapshotExportAvailability::kStaleDocument;
    case service::PageSnapshotExportStatus::kPrivateProfile:
      return api::PageSnapshotExportAvailability::kPrivateProfile;
    case service::PageSnapshotExportStatus::kIncomplete:
      return api::PageSnapshotExportAvailability::kIncomplete;
    case service::PageSnapshotExportStatus::kOversize:
      return api::PageSnapshotExportAvailability::kOversize;
    case service::PageSnapshotExportStatus::kCancelled:
      return api::PageSnapshotExportAvailability::kCancelled;
    case service::PageSnapshotExportStatus::kReplayConflict:
      return api::PageSnapshotExportAvailability::kReplayConflict;
    case service::PageSnapshotExportStatus::kMalformed:
    case service::PageSnapshotExportStatus::kUnavailable:
      return api::PageSnapshotExportAvailability::kInvalidResponse;
  }
}

}  // namespace

void CoreServiceManager::OnPageExportObservationCompleted(
    std::string operation_id,
    ActorLeaseId lease_id,
    service::EffectResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  actor_leases_.Release(lease_id);
  auto pending = pending_page_exports_.find(operation_id);
  if (pending == pending_page_exports_.end()) {
    return;
  }
  if (!result || result->status != service::EffectStatus::kCompleted ||
      !result->observation || !pending->second.web_contents) {
    FinishPageExport(
        operation_id,
        ExportResult(
            api::PageSnapshotExportAvailability::kDocumentUnavailable));
    return;
  }
  TaffyPageIntelligenceHost* host = TaffyPageIntelligenceHost::FromWebContents(
      pending->second.web_contents.get());
  const std::optional<DirectObservationContext> live =
      host ? host->BuildDirectObservationContext() : std::nullopt;
  const service::ObservationEffectResult& observed = *result->observation;
  if (!live || !SameDocument(*live, pending->second.identity) ||
      live->graph_revision != observed.graph_revision ||
      (pending->second.identity.graph_revision != 0u &&
       pending->second.identity.graph_revision != observed.graph_revision)) {
    FinishPageExport(
        operation_id,
        ExportResult(api::PageSnapshotExportAvailability::kStaleDocument));
    return;
  }
  const GURL committed_url = pending->second.web_contents->GetLastCommittedURL();
  const url::Origin committed_origin = url::Origin::Create(committed_url);
  const int64_t captured_at_epoch_ms =
      base::Time::Now().InMillisecondsSinceUnixEpoch();
  if (!committed_url.is_valid() || committed_origin.opaque() ||
      committed_origin.Serialize() != live->origin ||
      captured_at_epoch_ms <= 0) {
    FinishPageExport(
        operation_id,
        ExportResult(api::PageSnapshotExportAvailability::kStaleDocument));
    return;
  }
  pending->second.captured_at_epoch_ms =
      static_cast<uint64_t>(captured_at_epoch_ms);
  pending->second.source_query_withheld = committed_url.has_query();
  pending->second.source_fragment_withheld = committed_url.has_ref();
  pending->second.secure_context = observed.is_potentially_trustworthy;
  auto command = service::PageSnapshotExportCommand::New();
  command->operation = pending->second.operation.Clone();
  command->format = ServiceFormat(pending->second.format);
  command->expected_tab_id = live->tab_id;
  command->expected_frame_id = live->frame_id;
  command->expected_page_epoch = live->page_epoch;
  command->expected_graph_revision = live->graph_revision;
  command->expected_origin = live->origin;
  command->max_bytes =
      static_cast<uint32_t>(service::kMaxPageSnapshotExportBytes);
  command->observation = std::move(result->observation);
  command->captured_at_epoch_ms = pending->second.captured_at_epoch_ms;
  command->source_query_withheld = pending->second.source_query_withheld;
  command->source_fragment_withheld =
      pending->second.source_fragment_withheld;
  session_->ExportPageSnapshot(
      std::move(command),
      base::BindOnce(&CoreServiceManager::OnPageSnapshotExported,
                     weak_factory_.GetWeakPtr(), operation_id));
}

void CoreServiceManager::OnPageSnapshotExported(
    std::string operation_id,
    service::PageSnapshotExportResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto pending = pending_page_exports_.find(operation_id);
  if (pending == pending_page_exports_.end()) {
    return;
  }
  if (!result || !result->operation || !pending->second.operation ||
      !SameOperation(*result->operation, *pending->second.operation) ||
      result->operation->service_generation != service_generation_ ||
      result->format != ServiceFormat(pending->second.format) ||
      (result->status != service::PageSnapshotExportStatus::kExported &&
       !IsContentFree(*result))) {
    FinishPageExport(
        operation_id,
        ExportResult(api::PageSnapshotExportAvailability::kInvalidResponse));
    return;
  }
  const api::PageSnapshotExportAvailability status = ApiStatus(result->status);
  auto response = ExportResult(status);
  if (status == api::PageSnapshotExportAvailability::kAvailable) {
    TaffyPageIntelligenceHost* host =
        pending->second.web_contents
            ? TaffyPageIntelligenceHost::FromWebContents(
                  pending->second.web_contents.get())
            : nullptr;
    const std::optional<DirectObservationContext> live =
        host ? host->BuildDirectObservationContext() : std::nullopt;
    if (!live || !SameDocument(*live, pending->second.identity) ||
        result->tab_id != live->tab_id || result->frame_id != live->frame_id ||
        result->page_epoch != live->page_epoch ||
        result->origin != live->origin ||
        live->graph_revision != result->graph_revision ||
        result->captured_at_epoch_ms == 0u ||
        result->source_query_withheld !=
            pending->second.source_query_withheld ||
        result->source_fragment_withheld !=
            pending->second.source_fragment_withheld ||
        result->secure_context != pending->second.secure_context ||
        result->content.empty() ||
        result->content.size() > api::kMaxPageSnapshotExportBytes ||
        !HasExpectedContentType(*result)) {
      response =
          ExportResult(api::PageSnapshotExportAvailability::kStaleDocument);
    } else {
      response->snapshot_export = api::PageSnapshotExportView::New(
          pending->second.request_id, pending->second.document_id,
          result->graph_revision,
          std::move(result->origin), pending->second.format,
          std::move(result->mime_type), std::move(result->suggested_file_name),
          std::move(result->content), result->node_count,
          result->redacted_field_count, result->suppressed_secret_value_count,
          result->withheld_field_count, result->captured_at_epoch_ms,
          result->source_query_withheld, result->source_fragment_withheld,
          result->secure_context);
    }
  }
  FinishPageExport(operation_id, std::move(response));
}

bool CoreServiceManager::CancelPageSnapshotExport(
    const std::string& request_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto active = active_page_export_requests_.find(request_id);
  if (active == active_page_export_requests_.end()) {
    return false;
  }
  auto pending = pending_page_exports_.find(active->second);
  CHECK(pending != pending_page_exports_.end());
  const std::string operation_id = pending->first;
  if (!pending->second.effect_id.empty()) {
    page_observation_broker_->CancelEffect(pending->second.effect_id,
                                           service_generation_);
  }
  if (session_.is_bound() && pending->second.operation) {
    session_->CancelPageSnapshotExport(pending->second.operation.Clone(),
                                       base::BindOnce([](bool) {}));
  }
  FinishPageExport(
      operation_id,
      ExportResult(api::PageSnapshotExportAvailability::kCancelled));
  return true;
}

void CoreServiceManager::FinishPageExport(
    const std::string& operation_id,
    api::PageSnapshotExportResultPtr result) {
  auto pending = pending_page_exports_.find(operation_id);
  if (pending == pending_page_exports_.end()) {
    return;
  }
  auto active = active_page_export_requests_.find(pending->second.request_id);
  CHECK(active != active_page_export_requests_.end());
  CHECK(active->second == operation_id);
  CoreServicePageExportCallback callback = std::move(pending->second.callback);
  active_page_export_requests_.erase(active);
  pending_page_exports_.erase(pending);
  RefreshIdleTeardown();
  std::move(callback).Run(std::move(result));
}

void CoreServiceManager::ResolvePendingPageExportsUnavailable() {
  auto pending = std::move(pending_page_exports_);
  pending_page_exports_.clear();
  active_page_export_requests_.clear();
  RefreshIdleTeardown();
  for (auto& [operation_id, value] : pending) {
    static_cast<void>(operation_id);
    std::move(value.callback)
        .Run(ExportResult(
            api::PageSnapshotExportAvailability::kCoreUnavailable));
  }
}

}  // namespace taffy
