// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_backup.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core.h"
#include "taffy/services/core/rust_core_command_conversions.h"
#include "taffy/services/core/service_bridge.rs.h"
#include "taffy/services/core/service_bridge_backup_ffi.rs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace bridge = core_bridge;
namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 9u;
constexpr uint64_t kNow = 100u;

std::vector<uint8_t> Digest(uint8_t value) {
  return std::vector<uint8_t>(32u, value);
}

mojom::OperationEnvelopePtr Operation(const std::string& id) {
  return mojom::OperationEnvelope::New(id, kGeneration, 0u, kNow + 1u,
                                       id + "-once");
}

mojom::CoreBootstrapPtr Bootstrap() {
  auto bootstrap = mojom::CoreBootstrap::New();
  bootstrap->service_generation = kGeneration;
  bootstrap->generation_capability_entropy.resize(32u);
  for (size_t index = 0u;
       index < bootstrap->generation_capability_entropy.size(); ++index) {
    bootstrap->generation_capability_entropy[index] =
        static_cast<uint8_t>(index + 1u);
  }
  bootstrap->browser_profile_id = "source-profile";
  bootstrap->browser_session_id = "browser-session-1";
  bootstrap->available_account_methods = {
      mojom::AccountAuthMethod::kGoogle, mojom::AccountAuthMethod::kEmailLink,
      mojom::AccountAuthMethod::kGithub, mojom::AccountAuthMethod::kFacebook};
  return bootstrap;
}

mojom::BackupRecordDescriptorPtr Descriptor(mojom::BackupRecordKind kind,
                                            const std::string& stable_id,
                                            uint64_t revision,
                                            uint64_t plaintext_bytes,
                                            uint8_t digest_byte) {
  auto descriptor = mojom::BackupRecordDescriptor::New();
  descriptor->kind = kind;
  descriptor->stable_id = stable_id;
  descriptor->revision = revision;
  descriptor->schema_version = 1u;
  descriptor->state = mojom::BackupRecordState::kActive;
  descriptor->plaintext_bytes = plaintext_bytes;
  descriptor->plaintext_sha256.assign(32u, digest_byte);
  return descriptor;
}

mojom::BackupManifestPrepareRequestPtr PrepareRequest() {
  auto request = mojom::BackupManifestPrepareRequest::New();
  request->operation = Operation("prepare");
  request->backup_id = "backup-1";
  request->source_installation_id = "installation-1";
  request->created_at_utc = "2026-09-05T00:00:00Z";
  request->selection = {mojom::BackupRecordKind::kMemoryRecord,
                        mojom::BackupRecordKind::kSavedWorkspace};
  request->records.push_back(Descriptor(mojom::BackupRecordKind::kMemoryRecord,
                                        "memory-1", 3u, 5u, 3u));
  request->records.push_back(Descriptor(
      mojom::BackupRecordKind::kSavedWorkspace, "workspace-1", 2u, 7u, 2u));
  return request;
}

mojom::BackupManifestPrepareResultPtr PrepareThroughBridge() {
  mojom::BackupManifestPrepareRequestPtr request = PrepareRequest();
  std::optional<bridge::BridgeBackupManifestPrepareRequest> projected =
      core_service_internal::ToBridgeBackupManifestPrepareRequest(*request);
  if (!projected) {
    return nullptr;
  }
  return core_service_internal::ToMojoBackupManifestPrepareResult(
      bridge::PrepareBackupManifest(std::move(*projected), kGeneration, kNow));
}

mojom::BackupRestorePlanRequestPtr RestoreRequest(
    const std::vector<uint8_t>& manifest,
    uint64_t second_record_bytes) {
  auto request = mojom::BackupRestorePlanRequest::New();
  request->operation = Operation("restore");
  request->manifest_plaintext = manifest;
  request->staged_records.push_back(
      mojom::StagedBackupRecord::New(7u, Digest(2u)));
  request->staged_records.push_back(
      mojom::StagedBackupRecord::New(second_record_bytes, Digest(3u)));
  request->target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile, "target-profile");
  return request;
}

mojom::BackupRestorePlanResultPtr PlanThroughBridge(
    mojom::BackupRestorePlanRequestPtr request) {
  RustCore core;
  CoreInitializationBatch initialized = core.Initialize(Bootstrap());
  if (!initialized.result ||
      initialized.result->status != mojom::InitializationStatus::kReady) {
    return nullptr;
  }
  return core.PlanBackupRestore(std::move(request), kGeneration, kNow);
}

TEST(RustCoreBackupTest, CanonicalPlanCrossesMojoCxxAndRustBoundaries) {
  mojom::BackupManifestPrepareResultPtr prepared = PrepareThroughBridge();
  ASSERT_TRUE(prepared);
  ASSERT_EQ(mojom::BackupPlanningStatus::kSucceeded, prepared->status);
  EXPECT_EQ(std::vector<uint32_t>({1u, 0u}), prepared->source_order);
  EXPECT_EQ(12u, prepared->payload_plaintext_bytes);
  EXPECT_EQ(2u, prepared->expected_sealed_chunks);
  ASSERT_FALSE(prepared->manifest_plaintext.empty());

  std::optional<bridge::BridgeBackupOperation> inspect_operation =
      core_service_internal::ToBridgeBackupOperation(*Operation("inspect"));
  ASSERT_TRUE(inspect_operation);
  const rust::Slice<const uint8_t> manifest(
      prepared->manifest_plaintext.data(), prepared->manifest_plaintext.size());
  mojom::BackupManifestInspectResultPtr inspected =
      core_service_internal::ToMojoBackupManifestInspectResult(
          bridge::InspectBackupManifest(std::move(*inspect_operation), manifest,
                                        kGeneration, kNow));
  ASSERT_TRUE(inspected);
  ASSERT_EQ(mojom::BackupPlanningStatus::kSucceeded, inspected->status);
  EXPECT_EQ("backup-1", inspected->backup_id);
  EXPECT_EQ("installation-1", inspected->source_installation_id);
  ASSERT_EQ(2u, inspected->selection.size());
  EXPECT_EQ(mojom::BackupRecordKind::kSavedWorkspace, inspected->selection[0]);
  EXPECT_EQ(mojom::BackupRecordKind::kMemoryRecord, inspected->selection[1]);
  ASSERT_EQ(2u, inspected->records.size());
  EXPECT_EQ(7u, inspected->records[0]->plaintext_bytes);
  EXPECT_EQ(5u, inspected->records[1]->plaintext_bytes);

  mojom::BackupRestorePlanResultPtr planned = PlanThroughBridge(
      RestoreRequest(prepared->manifest_plaintext, /*second_record_bytes=*/5u));
  ASSERT_TRUE(planned);
  ASSERT_EQ(mojom::BackupPlanningStatus::kSucceeded, planned->status);
  ASSERT_EQ(2u, planned->entries.size());
  EXPECT_EQ(mojom::BackupRestoreAction::kStageCreate,
            planned->entries[0]->action);
  EXPECT_EQ(1u, planned->entries[0]->schema_version);
  EXPECT_EQ(mojom::BackupRecordState::kActive, planned->entries[0]->state);
  EXPECT_EQ(7u, planned->entries[0]->plaintext_bytes);
  EXPECT_EQ(Digest(2u), planned->entries[0]->plaintext_sha256);
  EXPECT_EQ(mojom::BackupRestoreAction::kStageCreate,
            planned->entries[1]->action);
  EXPECT_FALSE(planned->has_conflicts);
  EXPECT_NE(Digest(0u), planned->confirmation_sha256);
}

TEST(RustCoreBackupTest, WrongStagedRangeCannotReturnAnyRestoreAction) {
  mojom::BackupManifestPrepareResultPtr prepared = PrepareThroughBridge();
  ASSERT_TRUE(prepared);
  mojom::BackupRestorePlanResultPtr refused = PlanThroughBridge(
      RestoreRequest(prepared->manifest_plaintext, /*second_record_bytes=*/6u));
  ASSERT_TRUE(refused);
  EXPECT_EQ(mojom::BackupPlanningStatus::kStagedPayloadMismatch,
            refused->status);
  EXPECT_TRUE(refused->backup_id.empty());
  EXPECT_TRUE(refused->entries.empty());
  EXPECT_FALSE(refused->has_conflicts);
  EXPECT_EQ(Digest(0u), refused->snapshot_sha256);
  EXPECT_EQ(Digest(0u), refused->confirmation_sha256);
}

TEST(RustCoreBackupTest, InputBoundsAreCheckedBeforeBridgeAllocation) {
  mojom::BackupManifestPrepareRequestPtr request = PrepareRequest();
  request->selection.push_back(mojom::BackupRecordKind::kMemoryRecord);
  EXPECT_FALSE(
      core_service_internal::ToBridgeBackupManifestPrepareRequest(*request));

  request = PrepareRequest();
  request->records.clear();
  for (uint64_t index = 0u; index < 129u; ++index) {
    request->records.push_back(
        Descriptor(mojom::BackupRecordKind::kMemoryRecord,
                   "memory-" + std::to_string(index), index + 1u,
                   mojom::kMaxBackupRecordBytes, 4u));
  }
  EXPECT_FALSE(
      core_service_internal::ToBridgeBackupManifestPrepareRequest(*request));

  request = PrepareRequest();
  request->records.front()->plaintext_sha256.pop_back();
  EXPECT_FALSE(
      core_service_internal::ToBridgeBackupManifestPrepareRequest(*request));

  mojom::BackupManifestPrepareResultPtr prepared = PrepareThroughBridge();
  ASSERT_TRUE(prepared);
  mojom::BackupRestorePlanRequestPtr restore =
      RestoreRequest(prepared->manifest_plaintext, /*second_record_bytes=*/5u);
  restore->staged_records.front()->plaintext_sha256.pop_back();
  EXPECT_FALSE(
      core_service_internal::ToBridgeBackupRestorePlanRequest(*restore));
}

TEST(RustCoreBackupTest, MalformedRustPermutationCannotCrossIntoMojo) {
  mojom::BackupManifestPrepareRequestPtr request = PrepareRequest();
  std::optional<bridge::BridgeBackupManifestPrepareRequest> projected =
      core_service_internal::ToBridgeBackupManifestPrepareRequest(*request);
  ASSERT_TRUE(projected);
  bridge::BridgeBackupManifestPrepareResult result =
      bridge::PrepareBackupManifest(std::move(*projected), kGeneration, kNow);
  ASSERT_EQ(2u, result.source_order.size());
  result.source_order[1] = result.source_order[0];
  EXPECT_FALSE(core_service_internal::ToMojoBackupManifestPrepareResult(
      std::move(result)));
}

TEST(RustCoreBackupTest, FailureWithUnknownTargetCannotCrossIntoMojo) {
  bridge::BridgeBackupRestorePlanResult result;
  result.operation.operation_id = "restore";
  result.operation.service_generation = kGeneration;
  result.operation.deadline_monotonic_ms = kNow + 1u;
  result.operation.idempotency_key = "restore-once";
  result.status =
      static_cast<uint8_t>(mojom::BackupPlanningStatus::kInvalidRequest);
  result.target_kind = 0xffu;
  EXPECT_FALSE(
      core_service_internal::ToMojoBackupRestorePlanResult(std::move(result)));
}

TEST(RustCoreBackupTest, MalformedRestoreDescriptorCannotCrossIntoMojo) {
  mojom::BackupManifestPrepareResultPtr prepared = PrepareThroughBridge();
  ASSERT_TRUE(prepared);
  mojom::BackupRestorePlanRequestPtr request =
      RestoreRequest(prepared->manifest_plaintext, /*second_record_bytes=*/5u);
  mojom::CoreBootstrapPtr bootstrap = Bootstrap();
  std::optional<bridge::BridgeBootstrap> projected_bootstrap =
      core_service_internal::ToBridgeBootstrap(*bootstrap);
  ASSERT_TRUE(projected_bootstrap);
  rust::Box<bridge::ServiceBridge> runtime =
      bridge::CreateServiceBridge(std::move(*projected_bootstrap));
  ASSERT_EQ(bridge::BridgeInitializationStatus::Ready,
            bridge::Initialization(*runtime).status);
  std::optional<bridge::BridgeBackupRestorePlanRequest> projected =
      core_service_internal::ToBridgeBackupRestorePlanRequest(*request);
  ASSERT_TRUE(projected);
  const rust::Slice<const uint8_t> manifest(request->manifest_plaintext.data(),
                                            request->manifest_plaintext.size());
  bridge::BridgeBackupRestorePlanResult result = bridge::PlanBackupRestore(
      *runtime, std::move(*projected), manifest, kNow);
  ASSERT_FALSE(result.entries.empty());
  result.entries.front().schema_version = 0u;
  EXPECT_FALSE(
      core_service_internal::ToMojoBackupRestorePlanResult(std::move(result)));
}

}  // namespace
}  // namespace taffy
