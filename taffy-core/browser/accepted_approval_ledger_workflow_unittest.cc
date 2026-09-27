// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/accepted_approval_ledger.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "taffy/browser/core_state_binding_registry.h"
#include "testing/gtest/include/gtest/gtest.h"

// Which workflows this gate admits, which is deliberately not a question about
// one workflow any more (decision 0057).
//
// The submission gate used to require template `kBuildSourceTable` and the
// provider route identifier `no_model_required` by name, which made a gate
// about a person's consent into the product's list of buildable tasks and
// refused every task the assistant runtime is being built for. The replacement
// is structural but exact: template-specific source cardinality, bounded
// discovery, and a provider identifier that agrees with the accepted route.
// The core repeats the same shape when it decodes the reducer seed; neither
// process borrows the other's verdict.
//
// These sit in their own file beside the allowlist rule's for the reason that
// one gives: they are one rule, and a reader checking what this gate names
// should not have to walk a shared fixture in another file to find out what a
// refusal was a refusal of.

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 3u;
constexpr uint64_t kNow = 10'000u;
constexpr uint64_t kNowUtc = 1'800'000'000'000u;
constexpr char kProfileId[] = "profile-1";
constexpr char kBrowserSessionId[] = "browser-session-1";
constexpr char kTaskId[] = "task-1";
constexpr char kTabId[] = "tab-1";
constexpr char kOrigin[] = "https://example.test";

// A core with no task bindings yet, which is what a brand-new task is
// submitted against. `state_sequence` must advance past zero or the registry
// registers nothing and every case below fails for an unrelated reason.
void RegisterEmptyState(CoreStateBindingRegistry* registry) {
  auto state = mojom::CoreStateBrowserBindings::New();
  state->service_generation = kGeneration;
  state->state_sequence = 1u;
  ASSERT_EQ(registry->Replace(std::move(state)),
            mojom::PendingApprovalRegistrationStatus::kRegistered);
}

mojom::CoreServiceCommandPtr StartTask() {
  auto command = mojom::CoreServiceCommand::New();
  command->operation = mojom::OperationEnvelope::New(
      "start-operation", kGeneration, 0u, 25'000u, "start-idempotency");
  command->kind = mojom::CoreServiceCommandKind::kStartTask;
  command->start_task = mojom::StartTaskCommand::New();
  command->start_task->task_id = kTaskId;
  command->start_task->browser_profile_id = kProfileId;
  command->start_task->browser_session_id = kBrowserSessionId;
  command->start_task->template_id = mojom::TaskTemplateId::kBuildSourceTable;
  command->start_task->provider_route_id = "no_model_required";
  command->start_task->tool_allowlist = {"browser.dom.read"};
  command->start_task->initial_consent_receipt_id = "initial-receipt-1";
  command->start_task->consent_preview = mojom::TaskConsentPreview::New();
  command->start_task->consent_preview->sources.push_back(
      mojom::TaskConsentSource::New("source-1", kTabId, kOrigin, std::nullopt));
  command->start_task->consent_preview->provider_route =
      mojom::TaskProviderRoute::kNoModelRequired;
  return command;
}

AuthoritySubmissionStage Stage(AcceptedApprovalLedger* ledger,
                               const mojom::CoreServiceCommand& command) {
  CoreStateBindingRegistry registry;
  RegisterEmptyState(&registry);
  return ledger->StageSubmittedCommand(command, kProfileId, kBrowserSessionId,
                                       registry, kNow, kNowUtc);
}

TEST(AcceptedApprovalLedgerTest, ATemplateBesideTheM3WorkflowIsStaged) {
  // The change this gate exists to allow. Which templates a build offers is
  // decided by the interface that offers them and by the core that agrees to
  // run them; a person consenting to one of the others has consented to
  // something this gate has nothing to say about.
  AcceptedApprovalLedger ledger;
  auto command = StartTask();
  command->start_task->template_id = mojom::TaskTemplateId::kCompareProducts;
  command->start_task->provider_route_id = "direct_user_key";
  command->start_task->consent_preview->provider_route =
      mojom::TaskProviderRoute::kDirectUserKey;
  command->start_task->consent_preview->sources.push_back(
      mojom::TaskConsentSource::New("source-2", "tab-2",
                                    "https://second.test", std::nullopt));

  EXPECT_EQ(Stage(&ledger, *command), AuthoritySubmissionStage::kStaged);
  EXPECT_EQ(ledger.staged_consent_count_for_testing(), 1u);
}

TEST(AcceptedApprovalLedgerTest, ATemplateOutsideTheEnumerationIsRefused) {
  // "Any template" is not the rule; "a template this contract defines" is. The
  // value below is what an in-process caller can hand a decoded struct, and
  // reading an unvalidated integer as a template would be reading it as
  // consent.
  AcceptedApprovalLedger ledger;
  auto command = StartTask();
  command->start_task->template_id = static_cast<mojom::TaskTemplateId>(97);

  EXPECT_EQ(Stage(&ledger, *command), AuthoritySubmissionStage::kInvalid);
  EXPECT_EQ(ledger.staged_consent_count_for_testing(), 0u);
}

TEST(AcceptedApprovalLedgerTest, AProviderRouteMismatchIsNeverStaged) {
  AcceptedApprovalLedger ledger;
  auto command = StartTask();
  command->start_task->provider_route_id = "managed_service";

  EXPECT_EQ(Stage(&ledger, *command), AuthoritySubmissionStage::kInvalid);
  EXPECT_EQ(ledger.staged_consent_count_for_testing(), 0u);
}

TEST(AcceptedApprovalLedgerTest, DirectUserKeyStartStagesSourceAuthority) {
  AcceptedApprovalLedger ledger;
  auto command = StartTask();
  command->start_task->consent_preview->provider_route =
      mojom::TaskProviderRoute::kDirectUserKey;
  command->start_task->provider_route_id = "direct_user_key";

  EXPECT_EQ(Stage(&ledger, *command), AuthoritySubmissionStage::kStaged);
  EXPECT_EQ(ledger.staged_consent_count_for_testing(), 1u);
}

TEST(AcceptedApprovalLedgerTest,
     ManagedProviderRouteStagesOnlyItsExactAcceptedShape) {
  AcceptedApprovalLedger ledger;
  auto command = StartTask();
  command->start_task->consent_preview->provider_route =
      mojom::TaskProviderRoute::kManagedService;
  command->start_task->provider_route_id = "managed_service";

  EXPECT_EQ(Stage(&ledger, *command), AuthoritySubmissionStage::kStaged);
  EXPECT_EQ(ledger.staged_consent_count_for_testing(), 1u);

  AcceptedApprovalLedger mismatch;
  command->start_task->provider_route_id = "direct_user_key";
  EXPECT_EQ(Stage(&mismatch, *command), AuthoritySubmissionStage::kInvalid);
  EXPECT_EQ(mismatch.staged_consent_count_for_testing(), 0u);
}

TEST(AcceptedApprovalLedgerTest,
     WebErrandStagesZeroOrOneSourceWithBoundedDiscovery) {
  AcceptedApprovalLedger zero;
  auto command = StartTask();
  command->start_task->template_id = mojom::TaskTemplateId::kWebErrand;
  command->start_task->provider_route_id = "managed_service";
  command->start_task->consent_preview->provider_route =
      mojom::TaskProviderRoute::kManagedService;
  command->start_task->consent_preview->sources.clear();
  command->start_task->consent_preview->source_discovery_enabled = true;
  command->start_task->consent_preview->new_source_cap = 1u;
  EXPECT_EQ(Stage(&zero, *command), AuthoritySubmissionStage::kStaged);

  AcceptedApprovalLedger over_cap;
  command->start_task->consent_preview->new_source_cap = 9u;
  EXPECT_EQ(Stage(&over_cap, *command), AuthoritySubmissionStage::kInvalid);

  AcceptedApprovalLedger no_model;
  command->start_task->consent_preview->new_source_cap = 1u;
  command->start_task->consent_preview->provider_route =
      mojom::TaskProviderRoute::kNoModelRequired;
  command->start_task->provider_route_id = "no_model_required";
  EXPECT_EQ(Stage(&no_model, *command), AuthoritySubmissionStage::kInvalid);
}

TEST(AcceptedApprovalLedgerTest, AnAbsentProviderRouteIsRefused) {
  AcceptedApprovalLedger ledger;
  auto command = StartTask();
  command->start_task->provider_route_id = std::nullopt;

  EXPECT_EQ(Stage(&ledger, *command), AuthoritySubmissionStage::kInvalid);
  EXPECT_EQ(ledger.staged_consent_count_for_testing(), 0u);
}

TEST(AcceptedApprovalLedgerTest, AnEmptyProviderRouteIsRefused) {
  // Empty is not "no route chosen" here — the field is optional and absence
  // already spells that. An empty identifier is a malformed one.
  AcceptedApprovalLedger ledger;
  auto command = StartTask();
  command->start_task->provider_route_id = "";

  EXPECT_EQ(Stage(&ledger, *command), AuthoritySubmissionStage::kInvalid);
}

TEST(AcceptedApprovalLedgerTest, LocalErrandConsentRequiresSelectedSavedVersion) {
  auto command = StartTask();
  command->start_task->template_id = mojom::TaskTemplateId::kWebErrand;
  AcceptedApprovalLedger ordinary;
  EXPECT_EQ(Stage(&ordinary, *command), AuthoritySubmissionStage::kInvalid);
  command->start_task->skill_version_id = "recorded-flow@1";
  AcceptedApprovalLedger selected;
  EXPECT_EQ(Stage(&selected, *command), AuthoritySubmissionStage::kStaged);
  command->start_task->consent_preview->source_discovery_enabled = true;
  command->start_task->consent_preview->new_source_cap = 1u;
  AcceptedApprovalLedger widened;
  EXPECT_EQ(Stage(&widened, *command), AuthoritySubmissionStage::kInvalid);
}

}  // namespace
}  // namespace taffy
