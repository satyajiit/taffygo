// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core.h"

#include <utility>
#include <vector>

#include "base/strings/stringprintf.h"
#include "taffy/contracts/core-api/generated/cpp/core_api_enums.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::CoreBootstrapPtr ValidBootstrap(uint64_t generation) {
  auto bootstrap = mojom::CoreBootstrap::New();
  bootstrap->service_generation = generation;
  bootstrap->generation_capability_entropy.resize(32u);
  for (size_t index = 0; index < 32u; ++index) {
    bootstrap->generation_capability_entropy[index] =
        static_cast<uint8_t>(index + 1u);
  }
  bootstrap->browser_profile_id = "profile-1";
  bootstrap->browser_session_id = "browser-session-1";
  bootstrap->available_account_methods = {
      mojom::AccountAuthMethod::kGoogle, mojom::AccountAuthMethod::kEmailLink,
      mojom::AccountAuthMethod::kGithub, mojom::AccountAuthMethod::kFacebook};
  return bootstrap;
}

mojom::CoreBootstrapPtr BootstrapWithOversizedDurableLibrary(
    uint64_t generation) {
  auto bootstrap = ValidBootstrap(generation);
  bootstrap->library_revision = 17u;
  for (size_t index = 0; index < 17u; ++index) {
    auto entry = mojom::LibraryEntryRecord::New();
    entry->entry_id = base::StringPrintf("%032zx", index + 1u);
    entry->revision = 1u;
    entry->collection_id = base::StringPrintf("%032zx", index + 2'001u);
    entry->collection_name = "Durable collection";
    entry->source_workspace_id =
        base::StringPrintf("%032zx", index + 3'001u);
    entry->source_workspace_revision = 1u;
    entry->source_fact_id = base::StringPrintf("%032zx", index + 4'001u);
    entry->field = "durable field";
    entry->original_value.assign(16'384u, 'x');
    entry->kind = mojom::LibraryFactKind::kFromPage;
    auto source = mojom::LibrarySourceRecord::New();
    source->source_id = base::StringPrintf("%032zx", index + 5'001u);
    source->title = "Durable source";
    source->host = "source.example";
    source->observed_at_epoch_ms = 1u;
    entry->sources.push_back(std::move(source));
    entry->captured_at_epoch_ms = 1u;
    entry->last_checked_epoch_ms = 1u;
    bootstrap->library_entries.push_back(std::move(entry));
  }
  return bootstrap;
}

mojom::CoreServiceCommandPtr GithubStartAuth(uint64_t generation,
                                             uint64_t now_millis) {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New();
  command->operation->operation_id = "account-operation-1";
  command->operation->service_generation = generation;
  command->operation->deadline_monotonic_ms = now_millis + 60'000u;
  command->operation->idempotency_key = "account-key-1";
  command->kind = mojom::CoreServiceCommandKind::kStartAuth;
  command->start_auth = mojom::StartAuthCommand::New();
  command->start_auth->flow_id = "flow-1";
  command->start_auth->method = mojom::AccountAuthMethod::kGithub;
  command->start_auth->redirect_binding_id = "primary-auth-callback";
  command->start_auth->scopes = {mojom::AccountScope::kOpenId,
                                 mojom::AccountScope::kEmail};
  command->start_auth->issued_at_monotonic_ms = now_millis;
  return command;
}

mojom::CoreServiceCommandPtr AssistantConfigurationCommand(
    uint64_t generation,
    uint64_t now_millis,
    std::string operation_id,
    uint64_t expected_revision,
    std::vector<mojom::AssistantAbility> disabled_abilities = {
        mojom::AssistantAbility::kForm}) {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New();
  command->operation->operation_id = std::move(operation_id);
  command->operation->service_generation = generation;
  command->operation->deadline_monotonic_ms = now_millis + 60'000u;
  command->operation->idempotency_key =
      command->operation->operation_id + "-key";
  command->kind = mojom::CoreServiceCommandKind::kSetAssistantConfiguration;
  command->set_assistant_configuration =
      mojom::SetAssistantConfigurationCommand::New();
  command->set_assistant_configuration->expected_revision = expected_revision;
  command->set_assistant_configuration->disabled_abilities =
      std::move(disabled_abilities);
  command->set_assistant_configuration->preset =
      mojom::PersonalityPreset::kQuickShopper;
  command->set_assistant_configuration->pace = 2u;
  command->set_assistant_configuration->length = 0u;
  command->set_assistant_configuration->check_in = 1u;
  return command;
}

mojom::EffectResultPtr SuccessfulStorageResult(
    const mojom::EffectEnvelope &effect) {
  auto result = mojom::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  result->status = mojom::EffectStatus::kCompleted;
  result->kind = mojom::EffectKind::kStorageCommit;
  result->storage = mojom::StorageEffectResult::New();
  result->storage->committed_revision =
      effect.storage_commit->resulting_revision;
  return result;
}

TEST(RustCoreTest, MissingBootstrapFailsClosed) {
  RustCore core;

  CoreInitializationBatch initialized = core.Initialize(nullptr);

  ASSERT_TRUE(initialized.result);
  EXPECT_EQ(mojom::InitializationStatus::kInvalidBootstrap,
            initialized.result->status);
  EXPECT_EQ(0u, initialized.result->accepted_generation);
  EXPECT_TRUE(initialized.states.empty());
}

TEST(RustCoreTest, MissingTypedCommandIsInvalid) {
  RustCore core;

  CoreResponseBatch batch = core.Submit(nullptr, /*now_monotonic_ms=*/1u);

  ASSERT_TRUE(batch.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kInvalidCommand, batch.admission->status);
  EXPECT_TRUE(batch.effects.empty());
  EXPECT_TRUE(batch.states.empty());
}

TEST(RustCoreTest, PolicyWithoutRuntimeReturnsNoGrant) {
  RustCore core;
  auto request = mojom::PolicyEvaluationRequest::New();

  mojom::PolicyEvaluationResultPtr result =
      core.EvaluatePolicy(std::move(request));

  EXPECT_FALSE(result);
}

TEST(RustCoreTest, StartAuthEmitsTypedEffectAndSequencedState) {
  constexpr uint64_t kGeneration = 7u;
  constexpr uint64_t kNowMillis = 10'000u;
  RustCore core;
  CoreInitializationBatch initialized =
      core.Initialize(ValidBootstrap(kGeneration));
  ASSERT_TRUE(initialized.result);
  ASSERT_EQ(mojom::InitializationStatus::kReady, initialized.result->status);
  ASSERT_EQ(1u, initialized.states.size());
  ASSERT_TRUE(initialized.states.front().state);
  ASSERT_TRUE(initialized.states.front().browser_bindings);
  EXPECT_EQ(kGeneration, initialized.states.front().state->service_generation);
  EXPECT_EQ(1u, initialized.states.front().state->sequence);
  EXPECT_EQ(core_api::wire::kStatePayloadSchemaVersion,
            initialized.states.front().state->core_status_schema_version);
  EXPECT_FALSE(initialized.states.front().state->payload.empty());
  EXPECT_TRUE(
      initialized.states.front().browser_bindings->task_revisions.empty());
  EXPECT_TRUE(
      initialized.states.front().browser_bindings->pending_approvals.empty());

  CoreResponseBatch batch =
      core.Submit(GithubStartAuth(kGeneration, kNowMillis), kNowMillis);

  ASSERT_TRUE(batch.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted, batch.admission->status);
  ASSERT_EQ(1u, batch.effects.size());
  const mojom::EffectEnvelope &effect = *batch.effects.front();
  EXPECT_EQ(mojom::EffectKind::kSecureStore, effect.kind);
  ASSERT_TRUE(effect.secure_store);
  EXPECT_EQ(mojom::SecureStoreOperation::kGenerateEntropy,
            effect.secure_store->operation_kind);
  ASSERT_TRUE(effect.secure_store->generate_entropy);
  EXPECT_EQ("flow-1", effect.secure_store->generate_entropy->flow_id);
  EXPECT_GT(effect.secure_store->generate_entropy->byte_count, 0u);
  EXPECT_FALSE(effect.secure_store->write_transient);
  EXPECT_FALSE(effect.secure_store->delete_handle);
  ASSERT_EQ(1u, batch.states.size());
  ASSERT_TRUE(batch.states.front().state);
  ASSERT_TRUE(batch.states.front().browser_bindings);
  EXPECT_EQ(kGeneration, batch.states.front().state->service_generation);
  EXPECT_EQ(2u, batch.states.front().state->sequence);
  EXPECT_EQ(2u, batch.states.front().browser_bindings->state_sequence);
}

TEST(RustCoreTest, OversizedDurableStatusPublishesRecoveryAndKeepsRuntime) {
  constexpr uint64_t kGeneration = 71u;
  constexpr uint64_t kNowMillis = 10'000u;
  RustCore core;

  CoreInitializationBatch initialized = core.Initialize(
      BootstrapWithOversizedDurableLibrary(kGeneration));
  ASSERT_TRUE(initialized.result);
  ASSERT_EQ(mojom::InitializationStatus::kReady, initialized.result->status);
  ASSERT_EQ(1u, initialized.states.size());
  ASSERT_TRUE(initialized.states.front().state);
  EXPECT_FALSE(initialized.states.front().state->payload.empty());
  EXPECT_LE(initialized.states.front().state->payload.size(), 262'144u);

  CoreResponseBatch next =
      core.Submit(GithubStartAuth(kGeneration, kNowMillis), kNowMillis);
  ASSERT_TRUE(next.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted, next.admission->status);
  ASSERT_EQ(1u, next.states.size());
  ASSERT_TRUE(next.states.front().state);
  EXPECT_EQ(2u, next.states.front().state->sequence);
}

TEST(RustCoreTest,
     AssistantConfigurationPublishesOnlyAfterItsDurableCompletion) {
  constexpr uint64_t kGeneration = 8u;
  constexpr uint64_t kNowMillis = 20'000u;
  RustCore core;
  CoreInitializationBatch initialized =
      core.Initialize(ValidBootstrap(kGeneration));
  ASSERT_TRUE(initialized.result);
  ASSERT_EQ(mojom::InitializationStatus::kReady, initialized.result->status);

  CoreResponseBatch submitted = core.Submit(
      AssistantConfigurationCommand(kGeneration, kNowMillis,
                                    "configuration-operation-1", 0u),
      kNowMillis);
  ASSERT_TRUE(submitted.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted,
            submitted.admission->status);
  ASSERT_EQ(1u, submitted.effects.size());
  EXPECT_TRUE(submitted.states.empty());
  const mojom::EffectEnvelope &effect = *submitted.effects.front();
  ASSERT_TRUE(effect.storage_commit);
  EXPECT_EQ(mojom::StorageOperation::kSetAssistantConfiguration,
            effect.storage_commit->operation_kind);
  EXPECT_EQ(0u, effect.storage_commit->expected_revision);
  EXPECT_EQ(1u, effect.storage_commit->resulting_revision);

  CoreResponseBatch committed = core.DeliverEffectResult(
      SuccessfulStorageResult(effect), kNowMillis + 1u);
  ASSERT_TRUE(committed.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted, committed.admission->status);
  ASSERT_EQ(1u, committed.states.size());
  ASSERT_TRUE(committed.states.front().state);
  EXPECT_EQ(2u, committed.states.front().state->sequence);

  CoreResponseBatch stale = core.Submit(
      AssistantConfigurationCommand(kGeneration, kNowMillis + 2u,
                                    "configuration-operation-stale", 0u),
      kNowMillis + 2u);
  ASSERT_TRUE(stale.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kStaleRevision,
            stale.admission->status);
  EXPECT_TRUE(stale.effects.empty());
  EXPECT_TRUE(stale.states.empty());
}

TEST(RustCoreTest, AssistantConfigurationRacesHaveOnePublishedWinner) {
  constexpr uint64_t kGeneration = 9u;
  constexpr uint64_t kNowMillis = 30'000u;
  RustCore core;
  CoreInitializationBatch initialized =
      core.Initialize(ValidBootstrap(kGeneration));
  ASSERT_TRUE(initialized.result);
  ASSERT_EQ(mojom::InitializationStatus::kReady, initialized.result->status);

  CoreResponseBatch first = core.Submit(
      AssistantConfigurationCommand(kGeneration, kNowMillis,
                                    "configuration-race-1", 0u),
      kNowMillis);
  CoreResponseBatch second = core.Submit(
      AssistantConfigurationCommand(
          kGeneration, kNowMillis, "configuration-race-2", 0u,
          {mojom::AssistantAbility::kDownloads}),
      kNowMillis);
  ASSERT_EQ(1u, first.effects.size());
  ASSERT_EQ(1u, second.effects.size());

  CoreResponseBatch winner = core.DeliverEffectResult(
      SuccessfulStorageResult(*second.effects.front()), kNowMillis + 1u);
  ASSERT_TRUE(winner.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted, winner.admission->status);
  ASSERT_EQ(1u, winner.states.size());

  mojom::EffectResultPtr loser =
      SuccessfulStorageResult(*first.effects.front());
  loser->status = mojom::EffectStatus::kUnavailable;
  loser->storage->committed_revision = 0u;
  CoreResponseBatch refused =
      core.DeliverEffectResult(std::move(loser), kNowMillis + 2u);
  ASSERT_TRUE(refused.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kStaleRevision,
            refused.admission->status);
  EXPECT_TRUE(refused.states.empty());
}

TEST(RustCoreTest, AssistantConfigurationRestoresAcrossCoreRecreation) {
  constexpr uint64_t kGeneration = 10u;
  constexpr uint64_t kNowMillis = 40'000u;
  auto bootstrap = ValidBootstrap(kGeneration);
  bootstrap->assistant_configuration = mojom::AssistantConfiguration::New();
  bootstrap->assistant_configuration->revision = 11u;
  bootstrap->assistant_configuration->disabled_abilities = {
      mojom::AssistantAbility::kPdf};
  bootstrap->assistant_configuration->preset =
      mojom::PersonalityPreset::kTripPlanner;
  bootstrap->assistant_configuration->pace = 1u;
  bootstrap->assistant_configuration->length = 2u;
  bootstrap->assistant_configuration->check_in = 1u;
  RustCore recreated;
  CoreInitializationBatch initialized =
      recreated.Initialize(std::move(bootstrap));
  ASSERT_TRUE(initialized.result);
  ASSERT_EQ(mojom::InitializationStatus::kReady, initialized.result->status);
  ASSERT_EQ(1u, initialized.states.size());

  CoreResponseBatch submitted = recreated.Submit(
      AssistantConfigurationCommand(kGeneration, kNowMillis,
                                    "configuration-after-restart", 11u),
      kNowMillis);
  ASSERT_TRUE(submitted.admission);
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted,
            submitted.admission->status);
  ASSERT_EQ(1u, submitted.effects.size());
  EXPECT_EQ(12u,
            submitted.effects.front()->storage_commit->resulting_revision);
}

} // namespace
} // namespace taffy
