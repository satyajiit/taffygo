// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "taffy/browser/accepted_approval_ledger.h"
#include "taffy/browser/core_state_binding_registry.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 3u;
constexpr uint64_t kActionRevision = 8u;
constexpr uint64_t kNow = 10'000u;
constexpr uint64_t kNowUtc = 1'800'000'000'000u;
constexpr char kOrigin[] = "https://search.test";
constexpr char kLanding[] = "https://official.test";

class AcceptedApprovalLedgerSourceReplacementTest : public testing::Test {
 protected:
  void SetUp() override {
    auto empty = mojom::CoreStateBrowserBindings::New();
    empty->service_generation = kGeneration;
    empty->state_sequence = 1u;
    Register(std::move(empty));
    auto start = mojom::CoreServiceCommand::New();
    start->operation = Operation(0u);
    start->kind = mojom::CoreServiceCommandKind::kStartTask;
    start->start_task = mojom::StartTaskCommand::New();
    auto& task = *start->start_task;
    task.task_id = "task-1";
    task.browser_profile_id = "profile-1";
    task.browser_session_id = "session-1";
    task.template_id = mojom::TaskTemplateId::kWebErrand;
    task.provider_route_id = "direct_user_key";
    task.tool_allowlist = {"browser.search"};
    task.initial_consent_receipt_id = "receipt-1";
    task.consent_preview = Preview(4u);
    SetLive(*task.consent_preview->sources.front());
    ASSERT_EQ(ledger_.StageSubmittedCommand(*start, "profile-1", "session-1",
                                            registry_, kNow, kNowUtc),
              AuthoritySubmissionStage::kStaged);
    Commit(*start, 3u);
    Publish(Durable(kActionRevision, 4u));
    ASSERT_TRUE(Authorized(kOrigin));
  }

  mojom::OperationEnvelopePtr Operation(uint64_t revision = kActionRevision,
                                        uint64_t generation = kGeneration) {
    const auto id = "operation-" + std::to_string(++next_operation_);
    return mojom::OperationEnvelope::New(id, generation, revision, 25'000u, id);
  }

  mojom::TaskConsentPreviewPtr Preview(uint32_t cap) {
    auto preview = mojom::TaskConsentPreview::New();
    preview->sources.push_back(mojom::TaskConsentSource::New(
        "source-b", "tab-1", kOrigin, std::nullopt));
    preview->source_discovery_enabled = true;
    preview->new_source_cap = cap;
    preview->provider_route = mojom::TaskProviderRoute::kDirectUserKey;
    return preview;
  }

  mojom::CoreStateBrowserBindingsPtr
  Durable(uint64_t revision, uint32_t cap, uint64_t generation = kGeneration) {
    auto state = mojom::CoreStateBrowserBindings::New();
    state->service_generation = generation;
    state->state_sequence = ++next_sequence_;
    state->task_revisions.push_back(mojom::TaskRevisionBinding::New(
        "task-1", generation, revision, std::vector<mojom::TaskControlKind>()));
    auto consent = mojom::AcceptedTaskConsentBinding::New();
    consent->task_id = "task-1";
    consent->service_generation = generation;
    consent->current_task_revision = revision;
    consent->accepted_revision = 3u;
    consent->browser_session_id = "session-1";
    consent->receipt_id = "receipt-1";
    consent->consent_preview = Preview(cap);
    state->accepted_task_consents.push_back(std::move(consent));
    return state;
  }

  mojom::TaskConsentSourcePtr Replacement() {
    return mojom::TaskConsentSource::New("source-z", "tab-1", kLanding,
                                         "https://official.test/start");
  }

  void SetLive(const mojom::TaskConsentSource& source) {
    live_sources_.insert_or_assign(source.tab_id, source.Clone());
  }

  auto LiveValidator() {
    return base::BindRepeating(
        [](const base::flat_map<std::string, mojom::TaskConsentSourcePtr>* live,
           const mojom::TaskConsentSource& source) {
          const auto held = live->find(source.tab_id);
          return held != live->end() && held->second->Equals(source);
        },
        base::Unretained(&live_sources_));
  }

  // The same fixture answer in the shape durable rehydration now asks for.
  // These tests are about source replacement, so a source this fixture has not
  // been told is live is `kGone` — a tab with no document of its own is
  // decision 0183's subject and has its own tests.
  auto LivenessValidator() {
    return base::BindRepeating(
        [](const base::flat_map<std::string, mojom::TaskConsentSourcePtr>* live,
           const mojom::TaskConsentSource& source) {
          const auto held = live->find(source.tab_id);
          return held != live->end() && held->second->Equals(source)
                     ? IssuedSourceLiveness::kLive
                     : IssuedSourceLiveness::kGone;
        },
        base::Unretained(&live_sources_));
  }

  TaskSourceDiscoveryVerdict StageReplacement() {
    auto source = Replacement();
    SetLive(*source);
    return ledger_.ValidateDiscoveredTaskSource("task-1", *Operation(), *source,
                                                kGeneration, LiveValidator());
  }

  void Register(mojom::CoreStateBrowserBindingsPtr state) {
    ASSERT_EQ(mojom::PendingApprovalRegistrationStatus::kRegistered,
              registry_.Replace(std::move(state)));
  }

  void Publish(mojom::CoreStateBrowserBindingsPtr state) {
    Register(state.Clone());
    ledger_.Reconcile(registry_, state->service_generation, kNow);
    ledger_.RehydrateDurableAuthority(*state, registry_,
                                      state->service_generation, "session-1",
                                      kNow, kNowUtc, LivenessValidator());
  }

  bool Authorized(const std::string& origin,
                  uint64_t generation = kGeneration) {
    return ledger_.IsTaskSourceAuthorized(
        "task-1", "tab-1", *Operation(30u, generation), origin, generation);
  }

  void Commit(const mojom::CoreServiceCommand& command, uint64_t revision) {
    auto effect = mojom::EffectEnvelope::New();
    effect->operation = command.operation.Clone();
    effect->operation->task_revision = revision;
    effect->effect_id = command.operation->operation_id;
    effect->kind = mojom::EffectKind::kStorageCommit;
    effect->retry_class = mojom::RetryClass::kIdempotent;
    effect->storage_commit = mojom::StorageCommitEffect::New();
    effect->storage_commit->operation_kind =
        mojom::StorageOperation::kAppendTaskCommit;
    effect->storage_commit->task_id = "task-1";
    effect->storage_commit->expected_revision =
        command.operation->task_revision;
    effect->storage_commit->resulting_revision = revision;
    ASSERT_EQ(AuthorityStorageBinding::kBound,
              ledger_.BindStorageCommit(*effect, kNow));
    auto result = mojom::EffectResult::New();
    result->operation = effect->operation.Clone();
    result->effect_id = effect->effect_id;
    result->kind = effect->kind;
    result->status = mojom::EffectStatus::kCompleted;
    result->storage = mojom::StorageEffectResult::New(revision);
    ASSERT_EQ(AuthorityStorageCompletion::kCommitted,
              ledger_.RecordStorageCompletion(*result));
  }

  AcceptedApprovalLedger ledger_;
  CoreStateBindingRegistry registry_;
  base::flat_map<std::string, mojom::TaskConsentSourcePtr> live_sources_;
  uint64_t next_sequence_ = 1u;
  uint64_t next_operation_ = 0u;
};

TEST_F(AcceptedApprovalLedgerSourceReplacementTest,
       VerifiedReplacementNeedsNewerPublicationAndSpendsOneCap) {
  ASSERT_EQ(TaskSourceDiscoveryVerdict::kAdmitted, StageReplacement());
  EXPECT_FALSE(Authorized(kLanding));
  EXPECT_TRUE(Authorized(kOrigin));
  auto state = Durable(9u, 3u);
  state->accepted_task_consents.front()->consent_preview->sources.front() =
      Replacement();
  auto replay = state.Clone();
  Publish(std::move(state));
  EXPECT_TRUE(Authorized(kLanding));
  EXPECT_FALSE(Authorized(kOrigin));
  replay->state_sequence = ++next_sequence_;
  Publish(std::move(replay));
  EXPECT_TRUE(Authorized(kLanding));
  EXPECT_EQ(1u, ledger_.accepted_consent_count_for_testing());
}

TEST_F(AcceptedApprovalLedgerSourceReplacementTest,
       SameRevisionReplacementCannotBecomeAuthority) {
  ASSERT_EQ(TaskSourceDiscoveryVerdict::kAdmitted, StageReplacement());
  auto state = Durable(kActionRevision, 3u);
  state->accepted_task_consents.front()->consent_preview->sources.front() =
      Replacement();
  Publish(std::move(state));
  EXPECT_FALSE(Authorized(kLanding));
}

TEST_F(AcceptedApprovalLedgerSourceReplacementTest,
       LiveIssuedTupleWithoutCompletedActionProofCannotReplace) {
  SetLive(*Replacement());
  auto state = Durable(9u, 3u);
  state->accepted_task_consents.front()->consent_preview->sources.front() =
      Replacement();
  Publish(std::move(state));
  EXPECT_FALSE(Authorized(kLanding));
}

TEST_F(AcceptedApprovalLedgerSourceReplacementTest,
       CandidateCannotBeReboundToAnotherActionOrSource) {
  auto source = Replacement();
  SetLive(*source);
  const auto action = Operation();
  ASSERT_EQ(TaskSourceDiscoveryVerdict::kAdmitted,
            ledger_.ValidateDiscoveredTaskSource("task-1", *action, *source,
                                                 kGeneration, LiveValidator()));
  EXPECT_EQ(TaskSourceDiscoveryVerdict::kAdmitted,
            ledger_.ValidateDiscoveredTaskSource("task-1", *action, *source,
                                                 kGeneration, LiveValidator()));
  EXPECT_EQ(TaskSourceDiscoveryVerdict::kRefused,
            ledger_.ValidateDiscoveredTaskSource(
                "task-1", *Operation(), *source, kGeneration, LiveValidator()));
  source->source_id = "forged-source";
  SetLive(*source);
  EXPECT_EQ(TaskSourceDiscoveryVerdict::kRefused,
            ledger_.ValidateDiscoveredTaskSource("task-1", *action, *source,
                                                 kGeneration, LiveValidator()));
}

class AcceptedApprovalLedgerSourceMutationTest
    : public AcceptedApprovalLedgerSourceReplacementTest,
      public testing::WithParamInterface<int> {};

TEST_P(AcceptedApprovalLedgerSourceMutationTest,
       ForgedTupleWrongTabAndCapMismatchCannotConsumeCandidate) {
  ASSERT_EQ(TaskSourceDiscoveryVerdict::kAdmitted, StageReplacement());
  auto state = Durable(9u, 3u);
  auto& preview = state->accepted_task_consents.front()->consent_preview;
  preview->sources.front() = Replacement();
  auto& source = *preview->sources.front();
  switch (GetParam()) {
    case 0:
      source.source_id = "forged-source";
      break;
    case 1:
      source.normalized_origin = "https://forged.test";
      break;
    case 2:
      source.canonical_locator = "https://official.test/other";
      break;
    case 3:
      source.tab_id = "other-tab";
      break;
    case 4:
      preview->new_source_cap = 4u;
      break;
    case 5:
      preview->new_source_cap = 2u;
      break;
  }
  // Even another currently issued browser tuple cannot change the completed
  // action's exact candidate or compensate for incorrect budget accounting.
  SetLive(source);
  Publish(std::move(state));
  EXPECT_EQ(0u, ledger_.accepted_consent_count_for_testing());
}

INSTANTIATE_TEST_SUITE_P(ReplacementMutations,
                         AcceptedApprovalLedgerSourceMutationTest,
                         testing::Values(0, 1, 2, 3, 4, 5));

TEST_F(AcceptedApprovalLedgerSourceReplacementTest,
       RustSortedAppendPreservesBothConsentedTabsAndSpendsOneCap) {
  auto source = mojom::TaskConsentSource::New("source-a", "tab-2", kLanding,
                                              "https://official.test/start");
  SetLive(*source);
  ASSERT_EQ(TaskSourceDiscoveryVerdict::kAdmitted,
            ledger_.ValidateDiscoveredTaskSource(
                "task-1", *Operation(), *source, kGeneration, LiveValidator()));
  EXPECT_FALSE(ledger_.IsTaskSourceAuthorized("task-1", "tab-2", *Operation(),
                                              kLanding, kGeneration));
  auto state = Durable(9u, 3u);
  auto& sources =
      state->accepted_task_consents.front()->consent_preview->sources;
  sources.insert(sources.begin(), std::move(source));
  auto refund = state.Clone();
  Publish(std::move(state));
  EXPECT_TRUE(Authorized(kOrigin));
  EXPECT_TRUE(ledger_.IsTaskSourceAuthorized("task-1", "tab-2", *Operation(9u),
                                             kLanding, kGeneration));
  refund->state_sequence = ++next_sequence_;
  refund->task_revisions.front()->task_revision = 10u;
  refund->accepted_task_consents.front()->current_task_revision = 10u;
  refund->accepted_task_consents.front()->consent_preview->new_source_cap = 4u;
  Publish(std::move(refund));
  EXPECT_EQ(0u, ledger_.accepted_consent_count_for_testing());
}

TEST_F(AcceptedApprovalLedgerSourceReplacementTest,
       RustSortedReplacementPreservesOtherConsentedTabs) {
  auto multiple = Durable(kActionRevision, 2u);
  auto& sources =
      multiple->accepted_task_consents.front()->consent_preview->sources;
  sources.push_back(mojom::TaskConsentSource::New(
      "source-m", "tab-2", "https://second.test", std::nullopt));
  sources.push_back(mojom::TaskConsentSource::New(
      "source-t", "tab-3", "https://third.test", std::nullopt));
  SetLive(*sources[1]);
  SetLive(*sources[2]);
  auto sorted = multiple.Clone();
  Publish(std::move(multiple));
  ASSERT_EQ(TaskSourceDiscoveryVerdict::kAdmitted, StageReplacement());
  sorted->state_sequence = ++next_sequence_;
  sorted->task_revisions.front()->task_revision = 9u;
  auto& consent = *sorted->accepted_task_consents.front();
  consent.current_task_revision = 9u;
  consent.consent_preview->new_source_cap = 1u;
  auto& replaced = consent.consent_preview->sources;
  replaced.erase(replaced.begin());
  replaced.push_back(Replacement());
  Publish(std::move(sorted));
  EXPECT_TRUE(Authorized(kLanding));
  EXPECT_TRUE(ledger_.IsTaskSourceAuthorized(
      "task-1", "tab-2", *Operation(9u), "https://second.test", kGeneration));
  EXPECT_TRUE(ledger_.IsTaskSourceAuthorized(
      "task-1", "tab-3", *Operation(9u), "https://third.test", kGeneration));
}

TEST_F(AcceptedApprovalLedgerSourceReplacementTest,
       LaterUnchangedPublicationRetiresUnusedCandidate) {
  ASSERT_EQ(TaskSourceDiscoveryVerdict::kAdmitted, StageReplacement());
  SetLive(*Preview(4u)->sources.front());
  Publish(Durable(9u, 4u));
  ASSERT_TRUE(Authorized(kOrigin));
  SetLive(*Replacement());
  auto state = Durable(10u, 3u);
  state->accepted_task_consents.front()->consent_preview->sources.front() =
      Replacement();
  Publish(std::move(state));
  EXPECT_FALSE(Authorized(kLanding));
}

TEST_F(AcceptedApprovalLedgerSourceReplacementTest,
       DeadCandidateCannotBecomeAuthority) {
  ASSERT_EQ(TaskSourceDiscoveryVerdict::kAdmitted, StageReplacement());
  live_sources_.clear();
  auto state = Durable(9u, 3u);
  state->accepted_task_consents.front()->consent_preview->sources.front() =
      Replacement();
  Publish(std::move(state));
  EXPECT_FALSE(Authorized(kLanding));
}

TEST_F(AcceptedApprovalLedgerSourceReplacementTest,
       GenerationLossDiscardsUnpublishedReplacementProof) {
  ASSERT_EQ(TaskSourceDiscoveryVerdict::kAdmitted, StageReplacement());
  ledger_.ResetForServiceGenerationLoss();
  auto state = Durable(9u, 3u, kGeneration + 1u);
  state->accepted_task_consents.front()->consent_preview->sources.front() =
      Replacement();
  Publish(std::move(state));
  EXPECT_FALSE(Authorized(kLanding, kGeneration + 1u));
}

TEST_F(AcceptedApprovalLedgerSourceReplacementTest,
       PauseAndExactResumeDoNotResurrectUnpublishedReplacement) {
  ASSERT_EQ(TaskSourceDiscoveryVerdict::kAdmitted, StageReplacement());
  auto pause = Durable(9u, 4u);
  pause->accepted_task_consents.clear();
  pause->task_settlements.push_back(mojom::TaskSettlementBinding::New(
      "task-1", kGeneration, 9u, mojom::TaskSettlementKind::kPause));
  Publish(std::move(pause));
  ASSERT_EQ(1u, ledger_.suspended_consent_count_for_testing());
  auto paused = Durable(10u, 4u);
  paused->accepted_task_consents.clear();
  paused->task_revisions.front()->allowed_controls = {
      mojom::TaskControlKind::kResume, mojom::TaskControlKind::kStop};
  Publish(std::move(paused));
  auto resume = mojom::CoreServiceCommand::New();
  resume->operation = Operation(10u);
  resume->kind = mojom::CoreServiceCommandKind::kResumeTask;
  resume->resume_task = mojom::ResumeTaskCommand::New("task-1", "trace-resume");
  ASSERT_EQ(AuthoritySubmissionStage::kStaged,
            ledger_.StageSubmittedCommand(*resume, "profile-1", "session-1",
                                          registry_, kNow, kNowUtc));
  Commit(*resume, 11u);
  auto resumed = Durable(11u, 3u);
  resumed->accepted_task_consents.front()->consent_preview->sources.front() =
      Replacement();
  Publish(std::move(resumed));
  EXPECT_FALSE(Authorized(kLanding));
  EXPECT_EQ(0u, ledger_.accepted_consent_count_for_testing());
}

}  // namespace
}  // namespace taffy
