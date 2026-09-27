// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "taffy/browser/accepted_approval_ledger.h"
#include "taffy/browser/accepted_approval_ledger_test_support.h"
#include "taffy/browser/core_state_binding_registry.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

using accepted_approval_ledger_test::kBrowserSessionId;
using accepted_approval_ledger_test::kCommittedRevision;
using accepted_approval_ledger_test::kGeneration;
using accepted_approval_ledger_test::kInitialTaskRevision;
using accepted_approval_ledger_test::kNow;
using accepted_approval_ledger_test::kNowUtc;
using accepted_approval_ledger_test::kOrigin;
using accepted_approval_ledger_test::kProfileId;
using accepted_approval_ledger_test::kRestartGeneration;
using accepted_approval_ledger_test::kTabId;
using accepted_approval_ledger_test::kTaskId;
using accepted_approval_ledger_test::AcceptStart;
using accepted_approval_ledger_test::CommitStart;
using accepted_approval_ledger_test::DurableErrand;
using accepted_approval_ledger_test::ErrandStart;
using accepted_approval_ledger_test::Register;
using accepted_approval_ledger_test::RestampGeneration;
using accepted_approval_ledger_test::RetainErrandStart;
using accepted_approval_ledger_test::State;

TEST(AcceptedApprovalLedgerDiscoveryTest,
     PreCreationSnapshotCannotEraseCompletedStartConsent) {
  CoreStateBindingRegistry registry;
  Register(&registry, State(std::nullopt));
  AcceptedApprovalLedger ledger;
  auto command = ErrandStart(true);
  ASSERT_EQ(
      ledger.StageSubmittedCommand(*command, kProfileId, kBrowserSessionId,
                                   registry, kNow, kNowUtc),
      AuthoritySubmissionStage::kStaged);
  CommitStart(&ledger, *command);

  // Storage and state publication travel on independent pipes. A full state
  // created before this task can therefore arrive after its commit terminal;
  // absence from that state is not evidence against a revision it predates.
  auto pre_creation = State(std::nullopt);
  pre_creation->state_sequence = 2u;
  Register(&registry, pre_creation.Clone());
  ledger.Reconcile(registry, kGeneration, kNow);
  ledger.RehydrateDurableAuthority(
      *pre_creation, registry, kGeneration, kBrowserSessionId, kNow, kNowUtc,
      base::BindRepeating(
          [](const mojom::TaskConsentSource&) { return IssuedSourceLiveness::kLive; }));
  EXPECT_EQ(1u, ledger.staged_consent_count_for_testing());
  EXPECT_EQ(0u, ledger.accepted_consent_count_for_testing());

  auto operation = mojom::OperationEnvelope::New(
      "policy", kGeneration, kInitialTaskRevision, 25'000u, "policy-key");
  EXPECT_FALSE(ledger.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                             kOrigin, kGeneration));

  auto committed = DurableErrand(true, 4u);
  committed->state_sequence = 3u;
  committed->task_revisions.front()->task_revision = kInitialTaskRevision;
  committed->accepted_task_consents.front()->current_task_revision =
      kInitialTaskRevision;
  Register(&registry, committed.Clone());
  ledger.Reconcile(registry, kGeneration, kNow);
  ledger.RehydrateDurableAuthority(
      *committed, registry, kGeneration, kBrowserSessionId, kNow, kNowUtc,
      base::BindRepeating(
          [](const mojom::TaskConsentSource&) { return IssuedSourceLiveness::kLive; }));

  EXPECT_EQ(0u, ledger.staged_consent_count_for_testing());
  EXPECT_EQ(1u, ledger.accepted_consent_count_for_testing());
  EXPECT_TRUE(ledger.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                            kOrigin, kGeneration));
}

TEST(AcceptedApprovalLedgerDiscoveryTest,
     ZeroSourceErrandStagesNoGenericPageAuthority) {
  CoreStateBindingRegistry registry;
  Register(&registry, State(std::nullopt));
  AcceptedApprovalLedger ledger;
  auto command = ErrandStart(false);
  AcceptStart(&ledger, &registry, *command);
  auto operation = mojom::OperationEnvelope::New(
      "policy", kGeneration, kInitialTaskRevision, 25'000u, "policy-key");

  EXPECT_FALSE(ledger.IsTaskSourceAuthorized(kTaskId, "any-tab", *operation,
                                             "https://any.test", kGeneration));
  EXPECT_TRUE(ledger.HasTaskSourceDiscoveryBootstrapAuthority(
      kTaskId, *operation, kGeneration, kBrowserSessionId, 4u));
  EXPECT_FALSE(ledger.HasTaskSourceDiscoveryBootstrapAuthority(
      kTaskId, *operation, kGeneration, "stale-session", 4u));
  EXPECT_FALSE(ledger.HasTaskSourceDiscoveryBootstrapAuthority(
      kTaskId, *operation, kGeneration, kBrowserSessionId, 3u));
  auto candidate =
      mojom::TaskConsentSource::New("discovered-source", "discovered-tab",
                                    "https://discovered.test", std::nullopt);
  EXPECT_EQ(ledger.ValidateDiscoveredTaskSource(
                kTaskId, *operation, *candidate, kGeneration,
                base::BindRepeating(
                    [](const mojom::TaskConsentSource&) { return true; })),
            TaskSourceDiscoveryVerdict::kAdmitted);
  EXPECT_EQ(ledger.ValidateDiscoveredTaskSource(
                kTaskId, *operation, *candidate, kGeneration,
                base::BindRepeating(
                    [](const mojom::TaskConsentSource&) { return false; })),
            TaskSourceDiscoveryVerdict::kRefused);
  EXPECT_FALSE(
      ledger.IsTaskSourceAuthorized(kTaskId, candidate->tab_id, *operation,
                                    candidate->normalized_origin, kGeneration));

  operation->service_generation = kGeneration + 1u;
  EXPECT_FALSE(ledger.HasTaskSourceDiscoveryBootstrapAuthority(
      kTaskId, *operation, kGeneration, kBrowserSessionId, 4u));
  EXPECT_EQ(ledger.ValidateDiscoveredTaskSource(
                kTaskId, *operation, *candidate, kGeneration,
                base::BindRepeating(
                    [](const mojom::TaskConsentSource&) { return true; })),
            TaskSourceDiscoveryVerdict::kRefused);
}

TEST(AcceptedApprovalLedgerDiscoveryTest,
     OneSourceErrandAdmitsOnlyANewBrowserIssuedBinding) {
  CoreStateBindingRegistry registry;
  Register(&registry, State(std::nullopt));
  AcceptedApprovalLedger ledger;
  auto command = ErrandStart(true);
  AcceptStart(&ledger, &registry, *command);
  auto operation = mojom::OperationEnvelope::New(
      "policy", kGeneration, kInitialTaskRevision, 25'000u, "policy-key");
  const auto live = base::BindRepeating(
      [](const mojom::TaskConsentSource&) { return true; });

  // Holding the page the person asked from does not end discovery. The
  // consent sheet granted both, and what bounds the second is its own cap:
  // this record still has all four, so the authority stands, and a count that
  // is not the record's own is refused exactly as it was before (decision
  // 0224).
  EXPECT_TRUE(ledger.HasTaskSourceDiscoveryBootstrapAuthority(
      kTaskId, *operation, kGeneration, kBrowserSessionId, 4u));
  EXPECT_FALSE(ledger.HasTaskSourceDiscoveryBootstrapAuthority(
      kTaskId, *operation, kGeneration, kBrowserSessionId, 3u));
  EXPECT_FALSE(ledger.HasTaskSourceDiscoveryBootstrapAuthority(
      kTaskId, *operation, kGeneration, "stale-session", 4u));
  EXPECT_FALSE(ledger.HasTaskSourceDiscoveryBootstrapAuthority(
      kTaskId, *operation, kGeneration, kBrowserSessionId, 0u));
  auto exact =
      mojom::TaskConsentSource::New("source-1", kTabId, kOrigin, std::nullopt);
  EXPECT_EQ(ledger.ValidateDiscoveredTaskSource(kTaskId, *operation, *exact,
                                                kGeneration, live),
            TaskSourceDiscoveryVerdict::kAlreadyBound);
  auto replacement = mojom::TaskConsentSource::New(
      "replacement-source", kTabId, "https://replacement.test", std::nullopt);
  EXPECT_EQ(ledger.ValidateDiscoveredTaskSource(
                kTaskId, *operation, *replacement, kGeneration, live),
            TaskSourceDiscoveryVerdict::kAdmitted);
  auto aliased = mojom::TaskConsentSource::New(
      "source-1", kTabId, "https://replacement.test", std::nullopt);
  EXPECT_EQ(ledger.ValidateDiscoveredTaskSource(kTaskId, *operation, *aliased,
                                                kGeneration, live),
            TaskSourceDiscoveryVerdict::kRefused);
  EXPECT_TRUE(ledger.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                            kOrigin, kGeneration));
  EXPECT_FALSE(ledger.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                             replacement->normalized_origin,
                                             kGeneration));
}

TEST(AcceptedApprovalLedgerDiscoveryTest,
     MultiPageConsentBindsEachExactTabAndNoOtherDocument) {
  CoreStateBindingRegistry registry;
  Register(&registry, State(std::nullopt));
  AcceptedApprovalLedger ledger;
  auto command = ErrandStart(true);
  command->start_task->template_id = mojom::TaskTemplateId::kCompareProducts;
  command->start_task->consent_preview->source_discovery_enabled = false;
  command->start_task->consent_preview->new_source_cap = 0u;
  command->start_task->consent_preview->sources.push_back(
      mojom::TaskConsentSource::New("source-2", "tab-2", "https://second.test",
                                    std::nullopt));
  AcceptStart(&ledger, &registry, *command);

  auto operation = mojom::OperationEnvelope::New(
      "policy", kGeneration, kInitialTaskRevision, 25'000u, "policy-key");
  EXPECT_TRUE(ledger.IsTaskSourceAuthorized(kTaskId, kTabId, *operation,
                                            kOrigin, kGeneration));
  EXPECT_TRUE(ledger.IsTaskSourceAuthorized(
      kTaskId, "tab-2", *operation, "https://second.test", kGeneration));
  EXPECT_FALSE(ledger.IsTaskSourceAuthorized(
      kTaskId, kTabId, *operation, "https://second.test", kGeneration));
  EXPECT_FALSE(ledger.IsTaskSourceAuthorized(kTaskId, "tab-2", *operation,
                                             kOrigin, kGeneration));
  EXPECT_FALSE(ledger.IsTaskSourceAuthorized(
      kTaskId, "tab-3", *operation, "https://third.test", kGeneration));
}

TEST(AcceptedApprovalLedgerDiscoveryTest,
     DurableAppendIsTheOnlyEventThatInstallsDiscoveredAuthority) {
  AcceptedApprovalLedger ledger;
  RetainErrandStart(&ledger, false);

  CoreStateBindingRegistry registry;
  auto errand = DurableErrand(false, 4u);
  RestampGeneration(errand.get(), kRestartGeneration);
  Register(&registry, errand.Clone());
  auto operation = mojom::OperationEnvelope::New(
      "policy", kRestartGeneration, kCommittedRevision, 25'000u, "policy-key");
  ledger.RehydrateDurableAuthority(
      *errand, registry, kRestartGeneration, kBrowserSessionId, kNow, kNowUtc,
      base::BindRepeating(
          [](const mojom::TaskConsentSource&) { return IssuedSourceLiveness::kGone; }));
  EXPECT_EQ(ledger.accepted_consent_count_for_testing(), 1u);
  EXPECT_TRUE(ledger.HasTaskSourceDiscoveryBootstrapAuthority(
      kTaskId, *operation, kRestartGeneration, kBrowserSessionId, 4u));

  auto published = errand.Clone();
  published->state_sequence = 2u;
  auto& preview = published->accepted_task_consents.front()->consent_preview;
  preview->sources.push_back(
      mojom::TaskConsentSource::New("discovered-source", "discovered-tab",
                                    "https://discovered.test", std::nullopt));
  preview->new_source_cap = 3u;
  Register(&registry, published.Clone());
  ledger.RehydrateDurableAuthority(
      *published, registry, kRestartGeneration, kBrowserSessionId, kNow,
      kNowUtc, base::BindRepeating([](const mojom::TaskConsentSource& source) {
        return source.source_id == "discovered-source" &&
                       source.tab_id == "discovered-tab" &&
                       source.normalized_origin == "https://discovered.test"
                   ? IssuedSourceLiveness::kLive
                   : IssuedSourceLiveness::kGone;
      }));
  EXPECT_TRUE(ledger.IsTaskSourceAuthorized(
      kTaskId, "discovered-tab", *operation, "https://discovered.test",
      kRestartGeneration));
  auto exact =
      mojom::TaskConsentSource::New("discovered-source", "discovered-tab",
                                    "https://discovered.test", std::nullopt);
  EXPECT_EQ(ledger.ValidateDiscoveredTaskSource(
                kTaskId, *operation, *exact, kRestartGeneration,
                base::BindRepeating(
                    [](const mojom::TaskConsentSource&) { return true; })),
            TaskSourceDiscoveryVerdict::kAlreadyBound);
}

TEST(AcceptedApprovalLedgerDiscoveryTest,
     DiscoveryAppendMustSpendExactlyOneCapPerAddedSource) {
  for (const uint32_t remaining_cap : {4u, 3u, 1u}) {
    SCOPED_TRACE(remaining_cap);
    AcceptedApprovalLedger ledger;
    RetainErrandStart(&ledger, false);
    auto state = DurableErrand(false, remaining_cap);
    auto& sources =
        state->accepted_task_consents.front()->consent_preview->sources;
    sources.push_back(mojom::TaskConsentSource::New(
        "discovered-1", "discovered-tab-1", "https://one.test", std::nullopt));
    sources.push_back(mojom::TaskConsentSource::New(
        "discovered-2", "discovered-tab-2", "https://two.test", std::nullopt));
    RestampGeneration(state.get(), kRestartGeneration);
    CoreStateBindingRegistry registry;
    Register(&registry, state.Clone());
    ledger.RehydrateDurableAuthority(
        *state, registry, kRestartGeneration, kBrowserSessionId, kNow, kNowUtc,
        base::BindRepeating(
            [](const mojom::TaskConsentSource&) { return IssuedSourceLiveness::kLive; }));
    EXPECT_EQ(ledger.accepted_consent_count_for_testing(), 0u);
  }
}

TEST(AcceptedApprovalLedgerDiscoveryTest,
     DeadOrUnknownDiscoveredSourceCannotExtendAuthority) {
  AcceptedApprovalLedger ledger;
  RetainErrandStart(&ledger, false);
  auto state = DurableErrand(false, 3u);
  state->accepted_task_consents.front()->consent_preview->sources.push_back(
      mojom::TaskConsentSource::New("unknown-source", "unknown-tab",
                                    "https://unknown.test", std::nullopt));
  RestampGeneration(state.get(), kRestartGeneration);
  CoreStateBindingRegistry registry;
  Register(&registry, state.Clone());
  ledger.RehydrateDurableAuthority(
      *state, registry, kRestartGeneration, kBrowserSessionId, kNow, kNowUtc,
      base::BindRepeating(
          [](const mojom::TaskConsentSource&) { return IssuedSourceLiveness::kGone; }));
  EXPECT_EQ(ledger.accepted_consent_count_for_testing(), 0u);
}

TEST(AcceptedApprovalLedgerDiscoveryTest,
     ExistingSourceMutationCannotRestampAuthority) {
  AcceptedApprovalLedger ledger;
  RetainErrandStart(&ledger, true);
  auto state = DurableErrand(true, 3u);
  auto& preview = state->accepted_task_consents.front()->consent_preview;
  preview->sources.front()->normalized_origin = "https://mutated.test";
  preview->sources.push_back(
      mojom::TaskConsentSource::New("discovered-source", "discovered-tab",
                                    "https://discovered.test", std::nullopt));
  RestampGeneration(state.get(), kRestartGeneration);
  CoreStateBindingRegistry registry;
  Register(&registry, state.Clone());
  ledger.RehydrateDurableAuthority(
      *state, registry, kRestartGeneration, kBrowserSessionId, kNow, kNowUtc,
      base::BindRepeating(
          [](const mojom::TaskConsentSource&) { return IssuedSourceLiveness::kLive; }));
  EXPECT_EQ(ledger.accepted_consent_count_for_testing(), 0u);
}


// Decision 0183's other half, and the half that keeps it from being a way to
// widen anything: tolerating a document with no site is a refusal deferred on
// a source the ledger already holds, never permission to accept one it does
// not. A binding naming a source this record has never carried is refused on
// that document exactly as it is on a dead one, which is what the test above
// says for a dead one.
//
// The case where the record *is* held through such a document is
// `TaskSourceSelectionRegistryErrandTest.ErrandConsentSurvivesAnAddressThatDoesNotResolve`,
// and it lives there rather than here because it drives a real committed error
// page through the real registry instead of asserting against a fake answer.
TEST(AcceptedApprovalLedgerDiscoveryTest,
     ADocumentWithNoSiteCannotIntroduceANewSource) {
  AcceptedApprovalLedger ledger;
  RetainErrandStart(&ledger, false);
  auto state = DurableErrand(true, 3u);
  state->accepted_task_consents.front()->consent_preview->sources.push_back(
      mojom::TaskConsentSource::New("unknown-source", "unknown-tab",
                                    "https://unknown.test", std::nullopt));
  RestampGeneration(state.get(), kRestartGeneration);
  CoreStateBindingRegistry registry;
  Register(&registry, state.Clone());
  ledger.RehydrateDurableAuthority(
      *state, registry, kRestartGeneration, kBrowserSessionId, kNow, kNowUtc,
      base::BindRepeating([](const mojom::TaskConsentSource&) {
        return IssuedSourceLiveness::kTabHasNoDocumentOfItsOwn;
      }));
  EXPECT_EQ(ledger.accepted_consent_count_for_testing(), 0u);
}

}  // namespace
}  // namespace taffy
